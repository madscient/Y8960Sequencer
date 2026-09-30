// y8960gui - Y8960 シーケンスデータのプレイヤー（GUI）。
//
// 画面の文字は英語。Dear ImGui の既定のフォントが日本語の字を持たないためで、
// 日本語にするならフォントを同梱することになる（doc/plan.md）。
//
// 操作は Windows Media Player に倣う ―― 右のプレイリスト、シャッフルとリピート、
// 前・再生/一時停止・次、同じキーボードの近道。

#include "block.h"
#include "engine.h"
#include "levelmeter.h"
#include "pcmfile.h"
#include "playlist.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // WIN32 の実行ファイルに入口を用意する
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr float    kFadeSeconds   = 5.0f;
constexpr float    kPlaylistWidth = 300.0f;

const char* const kDeviceNames[y8960::kDeviceCount] = {
    "SSGS", "OPLLEX 1", "OPLLEX 2", "OPL2EX 1", "OPL2EX 2", "DCSG 1", "DCSG 2", "SCC",
    "OPL3", "OPM", "OPNA", "OPNB",
};

const char* const kTickNames[] = {"VDP 60Hz", "VDP 50Hz", "MSX-TIMER 100Hz", "MSX-TIMER 200Hz"};

// 開いたもの・落としたものをどう扱うか。
struct PendingAdd {
    std::vector<std::string> paths;
    bool replace = true;   // プレイリストを入れ替える。false なら後ろに足す
    bool play    = true;   // 入れ替えたら鳴らす。足したときは、止まっていれば鳴らす
};

struct App {
    SDL_Window*           window = nullptr;
    y8960::PlaybackEngine engine;
    y8960::SequenceBlock  block;
    y8960::PcmFile        pcm;
    bool        haveBlock = false;
    bool        havePcm   = false;
    std::string sequenceName;
    std::string pcmName;
    std::string status = "Open or drop sequence files or a folder.";
    int         loops  = 1;          // 曲ごとの繰り返し回数。0 は終わらない
    int         tick   = 3;          // kTickNames の索引
    bool        showAllChannels = false;
    y8960::LevelMeter meter;
    y8960::MuteState  mutes;
    std::array<float, y8960::kDeviceCount> gainDb{};

    y8960::Playlist playlist;
    int   selected     = y8960::Playlist::kNone;
    bool  showPlaylist = true;
    // いまの曲がフェードで終わったら次へ進む。止めたときや繰り返し 0 では下りている。
    bool  autoAdvance  = false;

    // ウィンドウに落とされたもの。DROP_BEGIN から DROP_COMPLETE までを1回として扱う。
    std::vector<std::string> dropped;
    bool   dropOnPlaylist = false;
    ImVec2 paneMin{}, paneMax{};     // 前のフレームのプレイリストの枠
    bool   paneShown = false;

    // ファイル選択の答えは別のスレッドから来ることがあるので、いったん置く。
    std::mutex              pending;
    std::vector<PendingAdd> pendingAdds;
    std::string             pendingPcm;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

// 落とされたファイルのうち ADPCM サンプルファイルは、プレイリストではなく ADPCM として読む。
bool isPcmFile(const std::string& path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    char head[4] = {};
    return f.read(head, sizeof head) && std::memcmp(head, "Y8PC", sizeof head) == 0;
}

bool loadSequence(App& app, const std::string& path) {
    std::vector<uint8_t> raw;
    if (!readFile(path, raw)) {
        app.status = "Cannot read " + y8960::fileNameOf(path);
        return false;
    }
    std::string error;
    if (!y8960::loadBlock(raw, app.block, error)) {
        app.status = y8960::fileNameOf(path) + ": " + error;
        return false;
    }
    app.haveBlock = true;
    app.sequenceName = y8960::fileNameOf(path);
    app.engine.setTrackMutes(app.mutes.trackMask(app.block));
    app.engine.load(app.block, app.havePcm ? &app.pcm : nullptr);
    int tracks = 0;
    for (const auto& t : app.block.tracks) tracks += t.assigned ? 1 : 0;
    app.status = app.sequenceName + ": " + std::to_string(tracks) + " tracks";
    return true;
}

void loadPcm(App& app, const std::string& path) {
    std::vector<uint8_t> raw;
    if (!readFile(path, raw)) {
        app.status = "Cannot read " + y8960::fileNameOf(path);
        return;
    }
    std::string error;
    if (!y8960::parsePcmFile(raw, app.pcm, error)) {
        app.status = y8960::fileNameOf(path) + ": " + error;
        return;
    }
    app.havePcm = true;
    app.pcmName = y8960::fileNameOf(path);
    // 読み込み直しで演奏は止まる。フェードの終わりではないので、次へは進まない。
    if (app.haveBlock) app.engine.load(app.block, &app.pcm);
    app.autoAdvance = false;
    app.status = app.pcmName + " loaded";
}

void setTitle(App& app, const std::string& song) {
    const std::string title = song.empty() ? std::string("Y8960 Player") : song + " - Y8960 Player";
    SDL_SetWindowTitle(app.window, title.c_str());
}

// 曲を読んで鳴らす。読めない曲や、鳴らすトラックが無い曲は印を付けて false。
bool startItem(App& app, int index) {
    y8960::PlaylistItem& item = app.playlist[static_cast<size_t>(index)];
    app.playlist.setCurrent(index);
    app.selected = index;
    app.engine.setPaused(false);
    if (!loadSequence(app, item.path)) {
        item.broken = true;
        return false;
    }
    app.engine.playThenFade(static_cast<uint8_t>(app.loops), kFadeSeconds);
    if (!app.engine.playing()) {
        item.broken = true;
        app.status = item.name + ": nothing to play";
        return false;
    }
    item.broken = false;
    app.autoAdvance = app.loops > 0;
    setTitle(app, item.name);
    return true;
}

// index から鳴らす。鳴らせなければ、再生の順で次（backward なら前）を試す。
void playFrom(App& app, int index, bool backward = false) {
    app.autoAdvance = false;
    for (size_t tries = 0; index != y8960::Playlist::kNone && tries < app.playlist.size(); ++tries) {
        if (startItem(app, index)) return;
        index = backward ? app.playlist.previous() : app.playlist.next();
    }
    if (index != y8960::Playlist::kNone) app.status = "No playable sequence in the playlist";
    setTitle(app, "");
}

void stopPlayback(App& app) {
    app.autoAdvance = false;
    app.engine.setPaused(false);
    app.engine.stop();
}

// 止まっているときの再生は、選んでいる曲、なければいまの曲、なければ順の頭から。
void playOrPause(App& app) {
    if (app.engine.playing()) {
        app.engine.setPaused(!app.engine.paused());
        return;
    }
    int index = app.selected;
    if (index == y8960::Playlist::kNone) index = app.playlist.current();
    if (index == y8960::Playlist::kNone) index = app.playlist.next();
    playFrom(app, index);
}

void playNext(App& app) {
    const int index = app.playlist.next();
    if (index != y8960::Playlist::kNone) playFrom(app, index);
}

void playPrevious(App& app) {
    const int index = app.playlist.previous();
    if (index != y8960::Playlist::kNone) playFrom(app, index, true);
}

// フォルダはその直下の .sq に開く。ADPCM サンプルファイルは ADPCM として読む。
void addPaths(App& app, const PendingAdd& add) {
    std::vector<std::string> sequences;
    for (const std::string& path : add.paths) {
        if (y8960::isDirectory(path)) {
            const std::vector<std::string> found = y8960::sequenceFilesIn(path);
            if (found.empty()) app.status = "No .sq files in " + y8960::fileNameOf(path);
            sequences.insert(sequences.end(), found.begin(), found.end());
        } else if (isPcmFile(path)) {
            loadPcm(app, path);
        } else {
            sequences.push_back(path);
        }
    }
    if (sequences.empty()) return;

    if (add.replace) {
        stopPlayback(app);
        app.playlist.clear();
        app.selected = y8960::Playlist::kNone;
    }
    const int first = static_cast<int>(app.playlist.size());
    for (const std::string& path : sequences) app.playlist.add(path);
    app.status = std::to_string(sequences.size()) + (sequences.size() == 1 ? " file" : " files") +
                 (add.replace ? " opened" : " added");
    if (!add.play) return;
    // 入れ替えたときはシャッフルを混ぜ直し、再生の順の頭から鳴らす。
    if (add.replace) {
        if (app.playlist.shuffle()) app.playlist.setShuffle(true);
        playFrom(app, app.playlist.next());
    } else if (!app.engine.playing()) {
        playFrom(app, first);
    }
}

void SDLCALL chosenFiles(void* userdata, const char* const* files, bool replace) {
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    PendingAdd add;
    for (const char* const* f = files; *f; ++f) add.paths.emplace_back(*f);
    add.replace = replace;
    add.play    = replace;
    std::lock_guard<std::mutex> lock(app->pending);
    app->pendingAdds.push_back(std::move(add));
}

void SDLCALL chosenOpen(void* userdata, const char* const* files, int /*filter*/) {
    chosenFiles(userdata, files, true);
}

void SDLCALL chosenAdd(void* userdata, const char* const* files, int /*filter*/) {
    chosenFiles(userdata, files, false);
}

void SDLCALL chosenPcm(void* userdata, const char* const* files, int /*filter*/) {
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    std::lock_guard<std::mutex> lock(app->pending);
    app->pendingPcm = files[0];
}

// 押されているあいだ色の付くボタン。
bool toggleButton(const char* label, bool on) {
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    const bool clicked = ImGui::Button(label);
    if (on) ImGui::PopStyleColor();
    return clicked;
}

void tooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

void drawTracks(const App& app) {
    if (!app.haveBlock) return;
    if (!ImGui::BeginTable("tracks", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) return;
    ImGui::TableSetupColumn("Track");
    ImGui::TableSetupColumn("Device");
    ImGui::TableSetupColumn("Channel");
    ImGui::TableSetupColumn("Bytes");
    ImGui::TableHeadersRow();
    for (int i = 0; i < y8960::kTrackCount; ++i) {
        const y8960::TrackData& t = app.block.tracks[static_cast<size_t>(i)];
        if (!t.assigned) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("%d", i);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(kDeviceNames[static_cast<int>(t.device)]);
        ImGui::TableNextColumn();
        if (y8960::isRhythmChannel(t.device, t.channel)) {
            ImGui::TextUnformatted(t.device == y8960::Device::OPNB ? "ADPCM-A" : "rhythm");
        } else if (y8960::isAdpcmChannel(t.device, t.channel)) {
            ImGui::TextUnformatted("ADPCM");
        } else {
            ImGui::Text("%u", t.channel);
        }
        ImGui::TableNextColumn(); ImGui::Text("%zu", t.events.size());
    }
    ImGui::EndTable();
}

void drawTransport(App& app) {
    const bool playing = app.engine.playing();
    const bool paused  = app.engine.paused();
    const bool haveList = !app.playlist.empty();

    if (toggleButton("Shuffle", app.playlist.shuffle())) app.playlist.setShuffle(!app.playlist.shuffle());
    tooltip("Shuffle the playlist (Ctrl+H)");
    ImGui::SameLine();
    if (toggleButton("Repeat", app.playlist.repeat())) app.playlist.setRepeat(!app.playlist.repeat());
    tooltip("Repeat the playlist (Ctrl+T)");

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::BeginDisabled(!playing);
    if (ImGui::Button("Stop")) stopPlayback(app);
    ImGui::EndDisabled();
    tooltip("Stop (Ctrl+S)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!haveList);
    if (ImGui::Button("|<")) playPrevious(app);
    tooltip("Previous (Ctrl+B)");
    ImGui::SameLine();
    if (ImGui::Button((playing && !paused) ? "Pause" : "Play", ImVec2(60.0f, 0.0f))) playOrPause(app);
    tooltip("Play / Pause (Ctrl+P)");
    ImGui::SameLine();
    if (ImGui::Button(">|")) playNext(app);
    tooltip("Next (Ctrl+F)");
    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Loops", &app.loops)) {
        if (app.loops < 0)   app.loops = 0;
        if (app.loops > 255) app.loops = 255;
        app.engine.setFadeAfter(static_cast<uint8_t>(app.loops));
        if (playing) app.autoAdvance = app.loops > 0;
    }
    tooltip("Times each song plays before it fades out and the next one starts.\n"
            "0 plays the song endlessly and does not advance.");

    ImGui::BeginDisabled(playing);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Tick rate", &app.tick, kTickNames, IM_ARRAYSIZE(kTickNames))) {
        app.engine.setTickRate(static_cast<y8960::TickRate>(app.tick));
        if (app.haveBlock) app.engine.load(app.block, app.havePcm ? &app.pcm : nullptr);
    }
    ImGui::EndDisabled();

    // WMP と同じキーボードの近道。
    const ImGuiInputFlags route = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, route) && haveList) playOrPause(app);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, route) && playing) stopPlayback(app);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, route) && haveList) playNext(app);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_B, route) && haveList) playPrevious(app);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_H, route)) app.playlist.setShuffle(!app.playlist.shuffle());
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_T, route)) app.playlist.setRepeat(!app.playlist.repeat());
}

void drawPlaylist(App& app, const SDL_DialogFileFilter* filters, int filterCount) {
    ImGui::BeginChild("playlist", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    app.paneMin = ImGui::GetWindowPos();
    app.paneMax = ImVec2(app.paneMin.x + ImGui::GetWindowSize().x, app.paneMin.y + ImGui::GetWindowSize().y);

    ImGui::Text("Playlist (%zu)", app.playlist.size());
    if (ImGui::Button("Add files...")) {
        SDL_ShowOpenFileDialog(chosenAdd, &app, app.window, filters, filterCount, nullptr, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add folder...")) {
        SDL_ShowOpenFolderDialog(chosenAdd, &app, app.window, nullptr, false);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(app.playlist.empty());
    if (ImGui::Button("Clear")) {
        stopPlayback(app);
        app.playlist.clear();
        app.selected = y8960::Playlist::kNone;
        setTitle(app, "");
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    ImGui::BeginChild("items");
    if (app.playlist.empty()) {
        ImGui::TextDisabled("Drop files or a folder here.");
    }
    // 表を書き終えてから変える。書いている途中で添字をずらさない。
    int playIndex = y8960::Playlist::kNone;
    int removeIndex = y8960::Playlist::kNone;
    int moveFrom = y8960::Playlist::kNone, moveTo = y8960::Playlist::kNone;
    const ImVec4 currentColor(0.45f, 0.78f, 1.0f, 1.0f);
    for (size_t i = 0; i < app.playlist.size(); ++i) {
        const int index = static_cast<int>(i);
        const y8960::PlaylistItem& item = app.playlist[i];
        const bool isCurrent = index == app.playlist.current();
        ImGui::PushID(index);
        if (isCurrent)        ImGui::PushStyleColor(ImGuiCol_Text, currentColor);
        else if (item.broken) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const std::string label = std::to_string(i + 1) + ". " + item.name;
        if (ImGui::Selectable(label.c_str(), app.selected == index, ImGuiSelectableFlags_AllowDoubleClick)) {
            app.selected = index;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) playIndex = index;
        }
        if (isCurrent || item.broken) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("%s%s", item.path.c_str(), item.broken ? "\n(cannot play)" : "");
        }

        // 行を引きずって並べ替える。
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("Y8960_ITEM", &index, sizeof index);
            ImGui::TextUnformatted(item.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("Y8960_ITEM")) {
                moveFrom = *static_cast<const int*>(p->Data);
                moveTo = index;
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem()) {
            app.selected = index;
            if (ImGui::MenuItem("Play")) playIndex = index;
            if (ImGui::MenuItem("Remove", "Del")) removeIndex = index;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (ImGui::IsWindowFocused() && app.selected != y8960::Playlist::kNone) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) removeIndex = app.selected;
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            playIndex = app.selected;
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();

    if (moveFrom != y8960::Playlist::kNone) {
        app.playlist.move(static_cast<size_t>(moveFrom), static_cast<size_t>(moveTo));
        app.selected = moveTo;
    } else if (removeIndex != y8960::Playlist::kNone) {
        // 鳴っている曲を消しても演奏は続き、終われば消した曲の次へ進む。
        app.playlist.remove(static_cast<size_t>(removeIndex));
        if (app.selected >= static_cast<int>(app.playlist.size())) app.selected = static_cast<int>(app.playlist.size()) - 1;
    } else if (playIndex != y8960::Playlist::kNone) {
        playFrom(app, playIndex);
    }
}

} // namespace

// y8960gui [シーケンスファイルかフォルダ] [--adpcm <Y8PC ファイル>]
int main(int argc, char** argv) {
    std::string startSequence;
    std::string startPcm;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--adpcm" && i + 1 < argc) startPcm = argv[++i];
        else if (startSequence.empty() && !arg.empty() && arg[0] != '-') startSequence = arg;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Y8960 Player", 1040, 600, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!window || !renderer) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // エンジンは SDL より先に畳む。SDL_Quit のあとで音声ストリームを壊すと、
    // 解放済みのものに触ることになる。
    {
    App app;
    app.window = window;
    std::string error;
    if (!app.engine.open(kSampleRate, static_cast<y8960::TickRate>(app.tick), error)) {
        app.status = "No sound: " + error;
    }

    const SDL_DialogFileFilter sequenceFiles[] = {{"Sequence files (*.sq)", "sq"}, {"All files", "*"}};
    const SDL_DialogFileFilter anyFile[] = {{"All files", "*"}};

    if (!startPcm.empty()) loadPcm(app, startPcm);
    if (!startSequence.empty()) {
        PendingAdd add;
        add.paths.push_back(startSequence);
        add.play = false;
        addPaths(app, add);
        if (!app.playlist.empty()) {
            app.playlist.setCurrent(0);
            app.selected = 0;
            loadSequence(app, app.playlist[0].path);
        }
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) running = false;
            if (event.type == SDL_EVENT_DROP_BEGIN) {
                app.dropped.clear();
            }
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
                // 落とした位置がプレイリストの上なら足し、ほかなら入れ替える（WMP と同じ）。
                if (app.dropped.empty()) {
                    app.dropOnPlaylist = app.paneShown &&
                        event.drop.x >= app.paneMin.x && event.drop.x < app.paneMax.x &&
                        event.drop.y >= app.paneMin.y && event.drop.y < app.paneMax.y;
                }
                app.dropped.emplace_back(event.drop.data);
            }
            if (event.type == SDL_EVENT_DROP_COMPLETE && !app.dropped.empty()) {
                PendingAdd add;
                add.paths.swap(app.dropped);
                add.replace = !app.dropOnPlaylist;
                add.play    = true;
                std::lock_guard<std::mutex> lock(app.pending);
                app.pendingAdds.push_back(std::move(add));
            }
        }

        std::vector<PendingAdd> adds;
        std::string pcmPath;
        {
            std::lock_guard<std::mutex> lock(app.pending);
            adds.swap(app.pendingAdds);
            pcmPath.swap(app.pendingPcm);
        }
        if (!pcmPath.empty()) loadPcm(app, pcmPath);
        for (const PendingAdd& add : adds) addPaths(app, add);

        if (app.engine.takeFadeEnd() && app.autoAdvance) {
            const int next = app.playlist.next();
            if (next != y8960::Playlist::kNone) {
                playFrom(app, next);
            } else {
                app.autoAdvance = false;
                setTitle(app, "");
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("main", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginChild("left", ImVec2(app.showPlaylist ? -(kPlaylistWidth + spacing) : 0.0f, 0.0f));

        if (ImGui::Button("Open...")) {
            SDL_ShowOpenFileDialog(chosenOpen, &app, window, sequenceFiles, 2, nullptr, true);
        }
        tooltip("Open sequence files as a new playlist (Ctrl+O)");
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
            SDL_ShowOpenFileDialog(chosenOpen, &app, window, sequenceFiles, 2, nullptr, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Open folder...")) {
            SDL_ShowOpenFolderDialog(chosenOpen, &app, window, nullptr, false);
        }
        tooltip("Open the .sq files in a folder as a new playlist");
        ImGui::SameLine();
        if (ImGui::Button("Open ADPCM (Y8PC)...")) {
            SDL_ShowOpenFileDialog(chosenPcm, &app, window, anyFile, 1, nullptr, false);
        }
        ImGui::SameLine();
        const char* paneLabel = app.showPlaylist ? "Hide playlist" : "Show playlist";
        const float paneButton = ImGui::CalcTextSize(paneLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - paneButton));
        if (ImGui::Button(paneLabel)) app.showPlaylist = !app.showPlaylist;

        ImGui::TextUnformatted(app.sequenceName.empty() ? "(no sequence)" : app.sequenceName.c_str());
        if (app.havePcm) {
            ImGui::SameLine();
            ImGui::Text("+ %s", app.pcmName.c_str());
        }

        ImGui::Separator();
        drawTransport(app);

        ImGui::Separator();
        const bool playing = app.engine.playing();
        if (!playing) {
            ImGui::TextUnformatted("Stopped");
        } else {
            const uint32_t pass = app.engine.passes() + 1;
            const char* state = app.engine.paused() ? "Paused" : app.engine.fading() ? "Fading out" : "Playing";
            if (app.loops > 0) ImGui::Text("%s  -  loop %u / %d", state, pass, app.loops);
            else               ImGui::Text("%s  -  loop %u (endless)", state, pass);
        }
        ImGui::TextUnformatted(app.status.c_str());

        ImGui::Checkbox("All channels", &app.showAllChannels);
        ImGui::SameLine();
        bool mutesChanged = false;
        if (ImGui::Button("Unmute all")) {
            app.mutes = y8960::MuteState{};
            mutesChanged = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(click a bar to mute its channel)");
        app.meter.update(app.engine, app.block, app.haveBlock, app.showAllChannels,
                         static_cast<float>(ImGui::GetTime()));
        if (app.meter.draw(app.mutes, app.gainDb)) mutesChanged = true;
        if (mutesChanged && app.haveBlock) app.engine.setTrackMutes(app.mutes.trackMask(app.block));
        for (size_t d = 0; d < y8960::kDeviceCount; ++d) {
            app.engine.setGain(static_cast<y8960::Device>(d), y8960::gainFromDb(app.gainDb[d]));
        }

        ImGui::Separator();
        drawTracks(app);
        ImGui::EndChild();

        app.paneShown = app.showPlaylist;
        if (app.showPlaylist) {
            ImGui::SameLine();
            drawPlaylist(app, sequenceFiles, 2);
        }

        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
