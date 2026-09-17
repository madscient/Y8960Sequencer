// DCSG（SN76489）。Y8960BasicExtension の src/dev/dcsg.asm の写し。
//
// チップにキーは無い。消すのは減衰を最大にすることなので、次のキーオンで戻る
// レベルはこちらが覚える。ポートは1つで、コマンドバイトが行き先を言う。

#include "dev_internal.h"
#include "pitch.h"

namespace y8960 {

namespace {

constexpr uint8_t kChannels     = 4;   // 0-2 が矩形波、3 がノイズ
constexpr uint8_t kNoiseCh      = 3;
constexpr uint8_t kToneCh       = 2;   // ノイズレート 3 が追う相手
constexpr uint8_t kLatch        = 0x80;
constexpr uint8_t kVolumeBit    = 0x10;
constexpr uint8_t kChShift      = 5;
constexpr uint8_t kRegs         = 8;
constexpr uint8_t kNoiseWhite   = 0x04;
constexpr uint8_t kNoiseRates   = 4;
constexpr uint8_t kNoiseFollow  = 3;
constexpr uint8_t kAttOff       = 15;
constexpr uint8_t kVolMax       = 15;

constexpr uint8_t kSavKey    = 0x80;
constexpr uint8_t kSavFollow = 0x40;
constexpr uint8_t kSavLevel  = 0x0F;

class DcsgDevice final : public SoundDevice {
public:
    DcsgDevice(ChipBus& bus, Device which) : bus_(bus), which_(which) {}

    void reset() override {
        for (uint8_t ch = 0; ch < kChannels; ++ch) {
            state_[ch] = 0;               // キーは上、レベル無し、追随しない
            writeAttenuation(ch);
            if (ch < kNoiseCh) writeTone(ch, 0);
        }
        // 白色ノイズのいちばん速いレート。
        out(static_cast<uint8_t>(kLatch | (kNoiseCh << kChShift) | kNoiseWhite));
    }

    void keyOn(uint8_t ch) override {
        if (ch >= kChannels) return;
        state_[ch] = static_cast<uint8_t>(state_[ch] | kSavKey);
        writeAttenuation(ch);
    }

    void keyOff(uint8_t ch) override {
        if (ch >= kChannels) return;
        state_[ch] = static_cast<uint8_t>(state_[ch] & ~kSavKey);
        writeAttenuation(ch);
    }

    void setVolume(uint8_t ch, uint8_t loudness) override {
        if (ch >= kChannels) return;
        const uint8_t v = static_cast<uint8_t>((loudness >> 3) & kVolMax);
        state_[ch] = static_cast<uint8_t>((state_[ch] & ~kSavLevel) | v);
        writeAttenuation(ch);
    }

    // ノイズチャンネルに分周値は無い。レートが「チャンネル2に追随」のときだけ、
    // 音符はチャンネル2の分周値へ行く ―― ノイズがそこからしか読めないため。
    void setPitch(uint8_t ch, uint8_t note, int16_t bend) override {
        if (ch >= kChannels) return;
        uint8_t target = ch;
        if (ch == kNoiseCh) {
            if (!(state_[ch] & kSavFollow)) return;
            target = kToneCh;
        }
        writeTone(target, divPitch(which_, note, bend).divisor);
    }

    void setVoice(uint8_t ch, uint8_t number) override {
        if (ch != kNoiseCh || number >= kNoiseRates) return;
        state_[ch] = static_cast<uint8_t>(state_[ch] & ~kSavFollow);
        if (number == kNoiseFollow) state_[ch] = static_cast<uint8_t>(state_[ch] | kSavFollow);
        // 白色か周期かは Y のもの。いまチップが持っているビットを残す。
        const uint8_t white = static_cast<uint8_t>(shadow_[kNoiseCh * 2] & kNoiseWhite);
        out(static_cast<uint8_t>(kLatch | (kNoiseCh << kChShift) | white | number));
    }

    bool regRead(uint8_t reg, uint8_t& value) override {
        if (reg >= kRegs) return false;
        value = shadow_[reg];
        return true;
    }

    bool regWrite(uint8_t reg, uint8_t value) override {
        if (reg >= kRegs) return false;
        out(static_cast<uint8_t>(kLatch | (reg << 4) | (value & 0x0F)));
        ySave(reg, value);
        return true;
    }

private:
    // コマンドバイトのうち、レジスタを名指すものだけを控える。
    void out(uint8_t byte) {
        if (byte & kLatch) shadow_[(byte >> 4) & (kRegs - 1)] = static_cast<uint8_t>(byte & 0x0F);
        bus_.write(which_, 0, byte);
    }

    void writeAttenuation(uint8_t ch) {
        const uint8_t sav = state_[ch];
        const uint8_t att = (sav & kSavKey) ? static_cast<uint8_t>(kAttOff - (sav & kSavLevel))
                                            : kAttOff;
        out(static_cast<uint8_t>(kLatch | kVolumeBit | (ch << kChShift) | att));
    }

    // 下位4ビットはラッチバイトに乗り、残り6ビットは行き先を言わないバイトで続く。
    void writeTone(uint8_t ch, uint16_t divisor) {
        out(static_cast<uint8_t>(kLatch | (ch << kChShift) | (divisor & 0x0F)));
        bus_.write(which_, 0, static_cast<uint8_t>((divisor >> 4) & 0x3F));
    }

    void ySave(uint8_t reg, uint8_t value) {
        if (reg == kNoiseCh * 2) {                    // ノイズ制御
            uint8_t s = static_cast<uint8_t>(state_[kNoiseCh] & ~kSavFollow);
            if ((value & (kNoiseRates - 1)) == kNoiseFollow) s = static_cast<uint8_t>(s | kSavFollow);
            state_[kNoiseCh] = s;
            return;
        }
        if (!(reg & 1)) return;                       // 分周値の下位4ビット
        const uint8_t ch = static_cast<uint8_t>(reg >> 1);
        const uint8_t level = static_cast<uint8_t>(kAttOff - (value & kSavLevel));
        state_[ch] = static_cast<uint8_t>((state_[ch] & ~kSavLevel) | level);
    }

    ChipBus& bus_;
    Device   which_;
    std::array<uint8_t, kRegs> shadow_{};
    std::array<uint8_t, kChannels> state_{};
};

} // namespace

std::unique_ptr<SoundDevice> makeDcsgDevice(ChipBus& bus, Device which) {
    return std::make_unique<DcsgDevice>(bus, which);
}

} // namespace y8960
