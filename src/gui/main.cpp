// y8960gui - Y8960 シーケンスデータのプレイヤー（GUI）。
//
// 画面の文字は英語。Dear ImGui の既定のフォントが日本語の字を持たないためで、
// 日本語にするならフォントを同梱することになる（doc/plan.md）。

#include "block.h"
#include "engine.h"
#include "levelmeter.h"
#include "pcmfile.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // WIN32 の実行ファイルに入口を用意する
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;

const char* const kDeviceNames[y8960::kDeviceCount] = {
    "SSGS", "OPLLEX 1", "OPLLEX 2", "OPL2EX 1", "OPL2EX 2", "DCSG 1", "DCSG 2", "SCC",
};

const char* const kTickNames[] = {"VDP 60Hz", "VDP 50Hz", "MSX-TIMER 100Hz", "MSX-TIMER 200Hz"};

struct App {
    y8960::PlaybackEngine engine;
    y8960::SequenceBlock  block;
    y8960::PcmFile        pcm;
    bool        haveBlock = false;
    bool        havePcm   = false;
    std::string sequenceName;
    std::string pcmName;
    std::string status = "Open a sequence file.";
    int         repeat = 1;
    int         tick   = 3;          // kTickNames の索引
    bool        showAllChannels = false;
    y8960::LevelMeter meter;

    // ファイル選択の答えは別のスレッドから来ることがあるので、いったん置く。
    std::mutex  pending;
    std::string pendingSequence;
    std::string pendingPcm;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

std::string baseName(const std::string& path) {
    const size_t cut = path.find_last_of("/\\");
    return (cut == std::string::npos) ? path : path.substr(cut + 1);
}

void loadSequence(App& app, const std::string& path) {
    std::vector<uint8_t> raw;
    if (!readFile(path, raw)) {
        app.status = "Cannot read " + baseName(path);
        return;
    }
    std::string error;
    if (!y8960::loadBlock(raw, app.block, error)) {
        app.status = baseName(path) + ": " + error;
        return;
    }
    app.haveBlock = true;
    app.sequenceName = baseName(path);
    app.engine.load(app.block, app.havePcm ? &app.pcm : nullptr);
    int tracks = 0;
    for (const auto& t : app.block.tracks) tracks += t.assigned ? 1 : 0;
    app.status = app.sequenceName + ": " + std::to_string(tracks) + " tracks";
}

void loadPcm(App& app, const std::string& path) {
    std::vector<uint8_t> raw;
    if (!readFile(path, raw)) {
        app.status = "Cannot read " + baseName(path);
        return;
    }
    std::string error;
    if (!y8960::parsePcmFile(raw, app.pcm, error)) {
        app.status = baseName(path) + ": " + error;
        return;
    }
    app.havePcm = true;
    app.pcmName = baseName(path);
    if (app.haveBlock) app.engine.load(app.block, &app.pcm);
    app.status = app.pcmName + " loaded";
}

void SDLCALL chosenSequence(void* userdata, const char* const* files, int /*filter*/) {
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    std::lock_guard<std::mutex> lock(app->pending);
    app->pendingSequence = files[0];
}

void SDLCALL chosenPcm(void* userdata, const char* const* files, int /*filter*/) {
    if (!files || !files[0]) return;
    auto* app = static_cast<App*>(userdata);
    std::lock_guard<std::mutex> lock(app->pending);
    app->pendingPcm = files[0];
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
        if (t.channel == y8960::kChannelRhythm)      ImGui::TextUnformatted("rhythm");
        else if (t.channel == y8960::kChannelAdpcm &&
                 (t.device == y8960::Device::OPL2EX1 || t.device == y8960::Device::OPL2EX2))
            ImGui::TextUnformatted("ADPCM");
        else ImGui::Text("%u", t.channel);
        ImGui::TableNextColumn(); ImGui::Text("%zu", t.events.size());
    }
    ImGui::EndTable();
}

} // namespace

// y8960gui [シーケンスファイル] [--adpcm <Y8PC ファイル>]
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
    SDL_Window* window = SDL_CreateWindow("Y8960 Player", 720, 520, SDL_WINDOW_RESIZABLE);
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
    std::string error;
    if (!app.engine.open(kSampleRate, static_cast<y8960::TickRate>(app.tick), error)) {
        app.status = "No sound: " + error;
    }

    const SDL_DialogFileFilter anyFile[] = {{"All files", "*"}};

    if (!startPcm.empty())      loadPcm(app, startPcm);
    if (!startSequence.empty()) loadSequence(app, startSequence);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) running = false;
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
                std::lock_guard<std::mutex> lock(app.pending);
                app.pendingSequence = event.drop.data;
            }
        }

        {
            std::lock_guard<std::mutex> lock(app.pending);
            if (!app.pendingSequence.empty()) {
                const std::string path = app.pendingSequence;
                app.pendingSequence.clear();
                loadSequence(app, path);
            }
            if (!app.pendingPcm.empty()) {
                const std::string path = app.pendingPcm;
                app.pendingPcm.clear();
                loadPcm(app, path);
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

        if (ImGui::Button("Open sequence...")) {
            SDL_ShowOpenFileDialog(chosenSequence, &app, window, anyFile, 1, nullptr, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Open ADPCM (Y8PC)...")) {
            SDL_ShowOpenFileDialog(chosenPcm, &app, window, anyFile, 1, nullptr, false);
        }
        ImGui::TextUnformatted(app.sequenceName.empty() ? "(no sequence)" : app.sequenceName.c_str());
        if (app.havePcm) {
            ImGui::SameLine();
            ImGui::Text("+ %s", app.pcmName.c_str());
        }

        ImGui::Separator();
        const bool playing = app.engine.playing();
        ImGui::BeginDisabled(!app.haveBlock);
        if (ImGui::Button(playing ? "Restart" : "Play")) {
            app.engine.play(static_cast<uint8_t>(app.repeat));
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!playing);
        if (ImGui::Button("Stop")) app.engine.stop();
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("Repeat (0 = endless)", &app.repeat);
        if (app.repeat < 0)   app.repeat = 0;
        if (app.repeat > 255) app.repeat = 255;

        ImGui::BeginDisabled(playing);
        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("Tick rate", &app.tick, kTickNames, IM_ARRAYSIZE(kTickNames))) {
            app.engine.setTickRate(static_cast<y8960::TickRate>(app.tick));
            if (app.haveBlock) app.engine.load(app.block, app.havePcm ? &app.pcm : nullptr);
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextUnformatted(playing ? "Playing" : "Stopped");
        ImGui::TextUnformatted(app.status.c_str());

        ImGui::Checkbox("All channels", &app.showAllChannels);
        app.meter.update(app.engine, app.block, app.haveBlock, app.showAllChannels,
                         static_cast<float>(ImGui::GetTime()));
        app.meter.draw();

        ImGui::Separator();
        drawTracks(app);

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
