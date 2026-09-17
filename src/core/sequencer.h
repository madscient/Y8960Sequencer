#pragma once
// シーケンサ。Y8960BasicExtension の src/resident/seq.asm・seqtrk.asm・
// seqloop.asm・seqctl.asm を写したもの。
//
// シーケンスは4本あり、割り込みごとに自分の accumulator に自分の増分を足す。
// だから1つの割り込みで、4本が別々のテンポで進む。

#include "block.h"
#include "device.h"
#include "player.h"
#include "timing.h"

#include <array>
#include <cstdint>

namespace y8960 {

constexpr int kSequenceCount = 4;
constexpr int kLoopDepth     = 4;
constexpr int kMarkCount     = 8;

constexpr uint8_t kNoNote      = 0xFF;
constexpr uint8_t kDefaultOct  = 4;
constexpr uint8_t kDefaultVol  = 8 * 8 + 7;   // MML の V8
constexpr uint8_t kDefaultVoice = 0;
constexpr uint8_t kQuantMax    = 8;
constexpr uint8_t kMixerMax    = 127;

class Sequencer final : public InterruptHandler {
public:
    Sequencer(DeviceSet& devices, TickRate rate);

    // ブロックをシーケンスに割り当てる。演奏中のものには渡さないこと。
    void load(int sequence, const SequenceBlock& block);
    // repeat が 0 なら終わらない。
    void start(int sequence, uint8_t repeat);
    void stop(int sequence);

    // MTUNE。1/64 半音。
    void setTune(int16_t steps) { tune_ = steps; }

    void interrupt() override;
    bool finished() const override;

private:
    struct Track {
        bool     assigned = false;
        Device   device   = Device::SSGS;
        uint8_t  channel  = 0;
        const std::vector<uint8_t>* events = nullptr;

        size_t   ptr   = 0;
        uint16_t wait  = 0;
        uint16_t gate  = 0;
        uint8_t  oct   = kDefaultOct;
        uint8_t  vol   = kDefaultVol;
        uint8_t  voice = kDefaultVoice;
        uint8_t  quant = kQuantMax;
        uint8_t  rhythmAccent = 0;
        uint8_t  loopSp = 0;
        std::array<uint8_t, kLoopDepth> loop{};
        std::array<uint8_t, kMarkCount> mark{};
        uint8_t  note  = kNoNote;
        int16_t  bend  = 0;
        uint16_t porta = 0;     // 8000h いま鳴っている高さから、8001h タイ
        int32_t  glide = 0;     // 符号付き 16.8
        int32_t  gstep = 0;     // 符号付き 16.8
    };

    enum class State : uint8_t { Idle, Playing, Paused };

    struct Sequence {
        State    state   = State::Idle;
        uint8_t  repeat  = 1;
        bool     mute    = false;
        uint8_t  volume  = kMixerMax;
        uint8_t  current = kMixerMax;
        uint8_t  incWhole = 0;
        uint16_t incFrac  = 0;
        uint16_t acc      = 0;
        uint16_t active   = 0;   // まだ走っているトラックのビット
        bool     first    = false;
        const SequenceBlock* block = nullptr;
        std::array<Track, kTrackCount> tracks{};
    };

    void step(Sequence& s);
    void tracks(Sequence& s);
    void passEnd(Sequence& s);
    void rewind(Sequence& s);
    void markClear(Sequence& s);
    void seqOff(Sequence& s);
    uint16_t buildMask(const Sequence& s) const;

    void trackStep(Sequence& s, Track& t, int index);
    bool fetch(Sequence& s, Track& t, int index);   // true = この tick は終わり
    bool event(Sequence& s, Track& t, int index, uint8_t op);

    // トラックのバイト列を読む。末尾を超えたら終端として扱う。
    uint8_t readByte(Track& t);
    uint16_t readLength(Track& t);
    uint16_t readWord(Track& t);

    bool note(Sequence& s, Track& t, int index, uint8_t number);
    void setGate(Track& t);
    bool setGlide(Track& t, uint8_t number);   // true = 前の音から続ける
    void computeStep(Track& t);
    void glideStep(Track& t);
    void repitch(Track& t);
    int16_t bendOffset(const Track& t) const;
    void keyOff(Track& t);
    void volumeOut(Sequence& s, Track& t);
    void jump(Track& t, int16_t distance);
    bool markHit(Track& t, uint8_t ordinal, uint8_t at);
    bool daCapo(Sequence& s, Track& t);
    void endTrack(Sequence& s, Track& t, int index);

    SoundDevice& device(const Track& t) { return devices_[t.device]; }

    DeviceSet& devices_;
    TickRate   rate_;
    int16_t    tune_ = 0;
    std::array<Sequence, kSequenceCount> sequences_{};
};

} // namespace y8960
