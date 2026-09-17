#pragma once
// 音声出力と再生の制御。CLI と GUI が共有する。
// コアは SDL を知らないので、SDL に触るのはこの層だけ。

#include "block.h"
#include "chips.h"
#include "device.h"
#include "pcmfile.h"
#include "player.h"
#include "sequencer.h"
#include "timing.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace y8960 {

class PlaybackEngine {
public:
    PlaybackEngine();
    ~PlaybackEngine();
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    // エミュレータを実行ファイルのフォルダから読み、音声デバイスを開く。
    bool open(uint32_t sampleRate, std::string& error);

    // 鳴らすものを差し替える。演奏中なら止まる。
    void load(const SequenceBlock& block, const PcmFile* pcm);

    void play(uint8_t repeat);
    void stop();

    bool playing();
    TickRate tickRate() const { return rate_; }
    void setTickRate(TickRate rate);   // 演奏中は変えない

    // 音声のスレッドから呼ばれる。ほかから呼ばないこと。
    void render(float* interleaved, int frames);

private:
    void rebuildPlayer();

    std::mutex  mutex_;
    Y8960Chips  chips_;
    std::unique_ptr<DeviceSet> devices_;
    std::unique_ptr<Sequencer> sequencer_;
    std::unique_ptr<Player>    player_;
    SDL_AudioStream* stream_ = nullptr;

    uint32_t sampleRate_ = 48000;
    TickRate rate_       = TickRate::Hz200;
    bool     loaded_     = false;
    std::array<AdpcmVoiceFile, kAdpcmFiles> directory_{};
    SequenceBlock block_;
    std::vector<float> left_;
    std::vector<float> right_;
};

} // namespace y8960
