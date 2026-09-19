// y8960player <シーケンスファイル> [--adpcm <ADPCM サンプルファイル>] [--tick <0-2>]
//             [--repeat <0-255>] [--mute <指定>]... [--wav <出力ファイル>]

#include "block.h"
#include "engine.h"
#include "mute.h"
#include "pcmfile.h"
#include "wavwriter.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
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

// `--tick` の値は `MINIT` の分解能と同じ。省略時は 2。
const y8960::TickRate kTickRates[] = {
    y8960::TickRate::Vdp60, y8960::TickRate::Hz100, y8960::TickRate::Hz200,
};

struct Options {
    fs::path sequence;
    std::optional<fs::path> adpcm;
    y8960::TickRate tick = y8960::TickRate::Hz200;
    std::vector<y8960::MuteSpec> mutes;
    std::optional<fs::path> wav;
    uint8_t repeat = 1;          // `MSTART` のリピート回数と同じ。0 は無限
};

// WAV に書き出すとき、曲が終わってから余韻を書く長さの上限と、無音とみなす振幅。
constexpr double kTailMaxSeconds  = 5.0;
constexpr double kSilentSeconds   = 0.5;
// 16bit の 1 LSB までは無音とみなす。DSAemuEngine の SCC は、キーオフのあとも
// 1 LSB ほどの一定値を出し続けるため（0f5c786 で測った）。
constexpr float  kSilentAmplitude = 1.5f / 32768.0f;
// 終わらない曲の保険。
constexpr double kWavMaxSeconds   = 30.0 * 60.0;

void printUsage() {
    std::fputs("使い方: y8960player <シーケンスファイル> [--adpcm <ADPCM サンプルファイル>]"
               " [--tick <0-2>] [--repeat <0-255>] [--mute <指定>]... [--wav <出力ファイル>]\n"
               "  --tick は演奏を進める割り込みの周期。0 が約60Hz、1 が約100Hz、"
               "2 が約200Hz。省略時は 2\n"
               "  --repeat は曲を鳴らす回数。0 は終わらない。省略時は 1\n"
               "  --mute は黙らせるもの。繰り返して指定できる\n"
               "    <チップ>[,<CH番号>]  チップは SSGS OPLLEX1 OPLLEX2 OPL2EX1 OPL2EX2"
               " DCSG1 DCSG2 SCC\n"
               "    T<番号>              トラック 0-15\n"
               "    <A-P>                トラック 0-15 を英字で\n"
               "    頭に ! を付けると、指定したもの以外を黙らせる\n"
               "  --wav は鳴らす代わりに WAV に書き出す（48000Hz、16bit、ステレオ）。"
               "--repeat 0 とは一緒に使えない\n", stderr);
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
        } else if (a == "--tick") {
            if (i + 1 >= args.size()) {
                std::fputs("--tick の後に値がありません\n", stderr);
                return false;
            }
            const std::string v = args[++i].u8string();
            if (v.size() != 1 || v[0] < '0' || v[0] > '2') {
                std::fprintf(stderr, "--tick の値は 0 から 2 です: %s\n", v.c_str());
                return false;
            }
            opt.tick = kTickRates[v[0] - '0'];
        } else if (a == "--repeat") {
            if (i + 1 >= args.size()) {
                std::fputs("--repeat の後に値がありません\n", stderr);
                return false;
            }
            const std::string v = args[++i].u8string();
            const bool digits = !v.empty() && v.size() <= 3 &&
                                std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; });
            if (!digits || std::stoi(v) > 255) {
                std::fprintf(stderr, "--repeat の値は 0 から 255 です: %s\n", v.c_str());
                return false;
            }
            opt.repeat = static_cast<uint8_t>(std::stoi(v));
        } else if (a == "--mute") {
            if (i + 1 >= args.size()) {
                std::fputs("--mute の後に指定がありません\n", stderr);
                return false;
            }
            y8960::MuteSpec spec;
            std::string why;
            if (!y8960::parseMuteSpec(args[++i].u8string(), spec, why)) {
                std::fprintf(stderr, "--mute: %s\n", why.c_str());
                return false;
            }
            opt.mutes.push_back(spec);
        } else if (a == "--wav") {
            if (i + 1 >= args.size()) {
                std::fputs("--wav の後にファイル名がありません\n", stderr);
                return false;
            }
            opt.wav = args[++i];
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
    if (opt.wav && opt.repeat == 0) {
        std::fputs("--wav と --repeat 0 は一緒に使えません（終わらない曲は書き出せない）\n", stderr);
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

// 鳴らす代わりに WAV へ書く。音声デバイスは使わない。
// 曲が終わったあとも、音が消えるまで（最長 kTailMaxSeconds）書く ―― 最後の音の
// 余韻を切らないため。
int exportWav(const Options& opt, const y8960::SequenceBlock& block, const y8960::PcmFile* pcm) {
    std::string error;
    y8960::PlaybackEngine engine;
    if (!engine.open(kSampleRate, opt.tick, error, false)) {
        std::fprintf(stderr, "エミュレータを開けません: %s\n", error.c_str());
        return 1;
    }
    engine.setTrackMutes(y8960::resolveMutes(opt.mutes, block));
    engine.load(block, pcm);

    y8960::WavWriter wav;
    if (!wav.open(*opt.wav, kSampleRate, error)) {
        std::fprintf(stderr, "%s: %s\n", opt.wav->u8string().c_str(), error.c_str());
        return 1;
    }

    constexpr uint32_t kChunk = 1024;
    std::vector<float> left(kChunk), right(kChunk);
    const uint64_t maxFrames    = static_cast<uint64_t>(kWavMaxSeconds * kSampleRate);
    const uint64_t tailMax      = static_cast<uint64_t>(kTailMaxSeconds * kSampleRate);
    const uint64_t silentNeeded = static_cast<uint64_t>(kSilentSeconds * kSampleRate);
    uint64_t tail = 0;
    uint64_t silentRun = 0;
    bool cut = false;

    engine.play(opt.repeat);
    for (;;) {
        const bool playing = engine.playing();
        engine.renderOffline(left.data(), right.data(), kChunk);
        wav.write(left.data(), right.data(), kChunk);

        if (wav.frames() >= maxFrames) {
            cut = true;
            break;
        }
        if (playing) continue;
        float peak = 0.0f;
        for (uint32_t i = 0; i < kChunk; ++i) {
            peak = std::max(peak, std::max(std::fabs(left[i]), std::fabs(right[i])));
        }
        silentRun = (peak < kSilentAmplitude) ? silentRun + kChunk : 0;
        tail += kChunk;
        if (silentRun >= silentNeeded || tail >= tailMax) break;
    }

    if (!wav.close(error)) {
        std::fprintf(stderr, "%s: %s\n", opt.wav->u8string().c_str(), error.c_str());
        return 1;
    }
    std::printf("%s: %.2f 秒\n", opt.wav->u8string().c_str(),
                static_cast<double>(wav.frames()) / kSampleRate);
    if (cut) {
        std::fprintf(stderr, "%.0f 分で打ち切りました（曲が終わりません）\n", kWavMaxSeconds / 60.0);
    }
    return 0;
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

    if (opt.wav) return exportWav(opt, block, havePcm ? &pcm : nullptr);

    {
        y8960::PlaybackEngine engine;
        if (!engine.open(kSampleRate, opt.tick, error)) {
            std::fprintf(stderr, "音を出せません: %s\n", error.c_str());
            return 1;
        }
        engine.setTrackMutes(y8960::resolveMutes(opt.mutes, block));
        engine.load(block, havePcm ? &pcm : nullptr);
        engine.play(opt.repeat);
        while (engine.playing()) SDL_Delay(20);
        SDL_Delay(200);   // 作り置きを鳴らし終えるまで
    }
    SDL_Quit();
    return 0;
}
