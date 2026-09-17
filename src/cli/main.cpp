// y8960player <シーケンスファイル> [--adpcm <ADPCM サンプルファイル>]

#include "block.h"
#include "engine.h"
#include "pcmfile.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kSampleRate = 48000;

// ブロックの外にある値。引数で選べるようにするのはこれから（doc/plan.md）。
constexpr y8960::TickRate kTickRate = y8960::TickRate::Hz200;
constexpr uint8_t         kRepeat   = 1;

struct Options {
    fs::path sequence;
    std::optional<fs::path> adpcm;
};

void printUsage() {
    std::fputs("使い方: y8960player <シーケンスファイル> [--adpcm <ADPCM サンプルファイル>]\n", stderr);
}

std::vector<fs::path> commandLine(int argc, char** argv) {
    std::vector<fs::path> args;
#if defined(_WIN32)
    // main の argv は ANSI コードページに落とされていて、日本語のパスが化ける。
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 1; i < n; ++i) args.emplace_back(w[i]);
    LocalFree(w);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return args;
}

bool parseOptions(const std::vector<fs::path>& args, Options& opt) {
    bool haveSequence = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string a = args[i].u8string();
        if (a == "--adpcm") {
            if (i + 1 >= args.size()) {
                std::fputs("--adpcm の後にファイル名がありません\n", stderr);
                return false;
            }
            opt.adpcm = args[++i];
        } else if (a == "--help" || a == "-h") {
            return false;
        } else if (a.size() >= 2 && a[0] == '-' && a[1] == '-') {
            std::fprintf(stderr, "知らないオプションです: %s\n", a.c_str());
            return false;
        } else if (!haveSequence) {
            opt.sequence = args[i];
            haveSequence = true;
        } else {
            std::fprintf(stderr, "シーケンスファイルは1つだけ指定できます: %s\n", a.c_str());
            return false;
        }
    }
    if (!haveSequence) {
        std::fputs("シーケンスファイルを指定してください\n", stderr);
        return false;
    }
    return true;
}

bool readFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    Options opt;
    if (!parseOptions(commandLine(argc, argv), opt)) {
        printUsage();
        return 1;
    }

    std::vector<uint8_t> file;
    if (!readFile(opt.sequence, file)) {
        std::fprintf(stderr, "%s: 読めません\n", opt.sequence.u8string().c_str());
        return 1;
    }
    y8960::SequenceBlock block;
    std::string error;
    if (!y8960::loadBlock(file, block, error)) {
        std::fprintf(stderr, "%s: %s\n", opt.sequence.u8string().c_str(), error.c_str());
        return 1;
    }

    for (const std::string& w : block.warnings) {
        std::fprintf(stderr, "%s: %s（読み飛ばしました）\n", opt.sequence.u8string().c_str(), w.c_str());
    }

    y8960::PcmFile pcm;
    bool havePcm = false;
    if (opt.adpcm) {
        std::vector<uint8_t> raw;
        if (!readFile(*opt.adpcm, raw)) {
            std::fprintf(stderr, "%s: 読めません\n", opt.adpcm->u8string().c_str());
            return 1;
        }
        if (!y8960::parsePcmFile(raw, pcm, error)) {
            std::fprintf(stderr, "%s: %s\n", opt.adpcm->u8string().c_str(), error.c_str());
            return 1;
        }
        havePcm = true;
    }
    const auto directory = y8960::resolveAdpcmDirectory(block, havePcm ? &pcm : nullptr);

    int assigned = 0;
    for (const auto& t : block.tracks) assigned += t.assigned ? 1 : 0;
    int voiceFiles = 0;
    for (const auto& v : directory) voiceFiles += v.present ? 1 : 0;
    std::printf("%s: 版 %u、トラック %d 本、ボイスファイル %d 個\n",
                opt.sequence.u8string().c_str(), static_cast<unsigned>(block.version),
                assigned, voiceFiles);

    {
        y8960::PlaybackEngine engine;
        engine.setTickRate(kTickRate);
        if (!engine.open(kSampleRate, error)) {
            std::fprintf(stderr, "音を出せません: %s\n", error.c_str());
            return 1;
        }
        engine.load(block, havePcm ? &pcm : nullptr);
        engine.play(kRepeat);
        while (engine.playing()) SDL_Delay(20);
        SDL_Delay(200);   // 作り置きを鳴らし終えるまで
    }
    SDL_Quit();
    return 0;
}
