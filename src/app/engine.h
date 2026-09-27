#pragma once
// 音声出力と再生の制御。CLI と GUI が共有する。
// コアは SDL を知らないので、SDL に触るのはこの層だけ。

#include "activity.h"
#include "block.h"
#include "chips.h"
#include "device.h"
#include "fader.h"
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
    // withAudio が false なら音声デバイスは開かず、renderOffline で音を取り出す。
    bool open(uint32_t sampleRate, TickRate rate, std::string& error, bool withAudio = true);

    // 音声デバイスを開かなかったときに、音を取り出す。
    void renderOffline(float* left, float* right, uint32_t frames);

    // ブロックごとの音量とレベルメーター。
    void  setGain(Device device, float gain) { chips_.setGain(device, gain); }
    float takeLevel(Device device) { return chips_.takeLevel(device); }
    // チャンネルごとの発音の様子。画面のスレッドから読んでよい。
    const ChannelActivity& activity() const { return activity_; }

    // 黙らせるトラックのビット（bit n がトラック n）。読み込み直しても続く。
    void setTrackMutes(uint16_t mask);

    // 鳴らすものを差し替える。演奏中なら止まる。
    void load(const SequenceBlock& block, const PcmFile* pcm);

    void play(uint8_t repeat);
    // passes 周を終えたら fadeSeconds 秒でフェードアウトし、落ちきったら止まる。
    // passes が 0 なら終わらない。
    void playThenFade(uint8_t passes, float fadeSeconds);
    // playThenFade で鳴らしている曲の周の数を変える。フェードが始まっていたら変えない。
    void setFadeAfter(uint8_t passes);
    void stop();

    // playThenFade の曲がフェードで終わったら、一度だけ true を返す。
    // load や stop で止まったときは返さない。
    bool takeFadeEnd();
    bool fading();
    uint32_t passes();          // いまの曲が終えた周の数

    // 音声デバイスを止め、再生の位置をそこで保つ。
    void setPaused(bool paused);
    bool paused() const { return paused_; }

    bool playing();
    TickRate tickRate() const { return rate_; }
    void setTickRate(TickRate rate);   // 演奏中は変えない

    // 音声のスレッドから呼ばれる。ほかから呼ばないこと。
    void render(float* interleaved, int frames);

private:
    void rebuildPlayer();
    void applyMutes();          // mutex_ を持って呼ぶこと
    void startSequence(uint8_t repeat);   // 同じく
    void afterRender(float* left, float* right, uint32_t frames);   // 同じく

    uint16_t muteMask_ = 0;
    uint32_t fadeAfter_   = 0;   // この周の数を終えたらフェードを始める。0 は始めない
    uint32_t fadeSamples_ = 0;
    Fader    fader_;
    bool     fadeMode_ = false;  // playThenFade で鳴らしている
    bool     fadeEnd_  = false;
    bool     paused_ = false;

    std::mutex  mutex_;
    Y8960Chips  chips_;
    std::unique_ptr<DeviceSet> devices_;
    std::unique_ptr<Sequencer> sequencer_;
    std::unique_ptr<Player>    player_;
    SDL_AudioStream* stream_ = nullptr;

    ChannelActivity activity_;
    uint32_t sampleRate_ = 48000;
    TickRate rate_       = TickRate::Hz200;
    bool     loaded_     = false;
    std::array<AdpcmVoiceFile, kAdpcmFiles> directory_{};
    SequenceBlock block_;
    std::vector<float> left_;
    std::vector<float> right_;
};

} // namespace y8960
