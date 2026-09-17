// SSGS（YMZ705 の SSG 互換部）。Y8960BasicExtension の src/dev/ssg.asm の写し。
//
// 2つの SSG が1つのポート対を分け合う。レジスタ番号が 20h 未満なら第1セット、
// 20h 以上なら第2セット。チャンネル 0-2 が第1セット、3-5 が第2セット。
//
// キーはトーンのスイッチではなくレベル。スイッチを切ってもチャンネルは切れず、
// レベルが直流として出てしまうので、消すのはレベルのほう。

#include "dev_internal.h"
#include "pitch.h"

namespace y8960 {

namespace {

constexpr uint8_t kSsg2Base   = 0x20;
constexpr uint8_t kChPerSet    = 3;
constexpr uint8_t kChannels    = 6;
constexpr uint8_t kRegToneL    = 0x00;
constexpr uint8_t kRegNoise    = 0x06;
constexpr uint8_t kRegEnable   = 0x07;
constexpr uint8_t kRegVol      = 0x08;
constexpr uint8_t kRegEnvPerL  = 0x0B;
constexpr uint8_t kRegEnvPerH  = 0x0C;
constexpr uint8_t kRegShape    = 0x0D;
constexpr uint8_t kRegs        = 0x0E;   // 00h-0Dh が音のレジスタ
constexpr uint8_t kRegPan      = 0x10;
constexpr uint8_t kPanCentre   = 8;
constexpr uint8_t kNoiseOff    = 0x38;   // トーンは通し、ノイズは切る
constexpr uint8_t kNoiseShift  = 3;
constexpr uint8_t kVolEnv      = 0x10;
constexpr uint8_t kVolMax      = 15;
constexpr uint8_t kSavKey      = 0x80;

class SsgsDevice final : public SoundDevice {
public:
    explicit SsgsDevice(ChipBus& bus) : bus_(bus) {}

    void reset() override {
        for (uint8_t ch = 0; ch < kChannels; ++ch) {
            const uint8_t sub = subChannel(ch);
            write(ch, static_cast<uint8_t>(kRegToneL + sub * 2), 0);
            write(ch, static_cast<uint8_t>(kRegToneL + sub * 2 + 1), 0);
            write(ch, static_cast<uint8_t>(kRegVol + sub), 0);
            level_[ch] = 0;
            // チップは左端で立ち上がるので、何も言わないパートが片側に寄らないよう
            // 中央に置く。
            write(ch, static_cast<uint8_t>(kRegPan + sub), kPanCentre);
        }
        for (uint8_t set = 0; set < 2; ++set) {
            const uint8_t ch = static_cast<uint8_t>(set * kChPerSet);
            write(ch, kRegNoise, 0);
            write(ch, kRegEnvPerL, 0);
            write(ch, kRegEnvPerH, 0);
            write(ch, kRegShape, 0);
            write(ch, kRegEnable, kNoiseOff);
            enable_[set] = kNoiseOff;
        }
    }

    void keyOn(uint8_t ch) override {
        if (ch >= kChannels) return;
        level_[ch] = static_cast<uint8_t>(level_[ch] | kSavKey);
        writeLevel(ch);
    }

    void keyOff(uint8_t ch) override {
        if (ch >= kChannels) return;
        level_[ch] = static_cast<uint8_t>(level_[ch] & ~kSavKey);
        writeLevel(ch);
    }

    void setVolume(uint8_t ch, uint8_t loudness) override {
        if (ch >= kChannels) return;
        const uint8_t v = static_cast<uint8_t>((loudness >> 3) & kVolMax);
        level_[ch] = static_cast<uint8_t>((level_[ch] & ~kVolMax) | v);
        writeLevel(ch);
    }

    void setPitch(uint8_t ch, uint8_t note, int16_t bend) override {
        if (ch >= kChannels) return;
        const DivPitch p = divPitch(Device::SSGS, note, bend);
        const uint8_t sub = subChannel(ch);
        write(ch, static_cast<uint8_t>(kRegToneL + sub * 2), static_cast<uint8_t>(p.divisor & 0xFF));
        write(ch, static_cast<uint8_t>(kRegToneL + sub * 2 + 1), static_cast<uint8_t>(p.divisor >> 8));
    }

    // @n はミキサの2つのスイッチとエンベロープの選択を1バイトに詰めたもの。
    //   bit4-0 レベルレジスタそのまま（bit4 でエンベロープに渡す）
    //   bit5   トーンのスイッチ、チップと同じ極性（0 が鳴る）
    //   bit6   ノイズのスイッチ、逆（1 が鳴る）
    void setVoice(uint8_t ch, uint8_t number) override {
        if (ch >= kChannels) return;
        if (number & kVolEnv) {
            level_[ch] = static_cast<uint8_t>((level_[ch] & kSavKey) |
                                              (number & (kVolEnv | kVolMax)));
        } else {
            level_[ch] = static_cast<uint8_t>(level_[ch] & ~kVolEnv);
        }
        writeLevel(ch);

        const uint8_t set  = setOf(ch);
        const uint8_t tone = static_cast<uint8_t>(1u << subChannel(ch));
        const uint8_t noise = static_cast<uint8_t>(tone << kNoiseShift);
        uint8_t en = static_cast<uint8_t>(enable_[set] & ~tone);      // トーンは鳴る
        if (number & 0x20) en = static_cast<uint8_t>(en | tone);      // @n が切った
        en = static_cast<uint8_t>(en | noise);                        // ノイズは鳴らない
        if (number & 0x40) en = static_cast<uint8_t>(en & ~noise);    // @n が入れた
        enable_[set] = en;
        write(ch, kRegEnable, en);
    }

    void ssgEnv(SsgEnv kind, uint8_t ch, uint8_t value) override {
        if (ch >= kChannels) return;
        switch (kind) {
        case SsgEnv::Shape:      write(ch, kRegShape, value); break;
        case SsgEnv::PeriodLow:  write(ch, kRegEnvPerL, value); break;
        case SsgEnv::PeriodHigh: write(ch, kRegEnvPerH, value); break;
        case SsgEnv::Pan:        write(ch, static_cast<uint8_t>(kRegPan + subChannel(ch)), value); break;
        }
    }

    bool regRead(uint8_t reg, uint8_t& value) override {
        if (!validReg(reg)) return false;
        value = shadow_[reg];
        return true;
    }

    bool regWrite(uint8_t reg, uint8_t value) override {
        if (!validReg(reg)) return false;
        writeAbs(reg, value);
        ySave(reg, value);
        return true;
    }

private:
    static uint8_t subChannel(uint8_t ch) { return static_cast<uint8_t>(ch % kChPerSet); }
    static uint8_t setOf(uint8_t ch)      { return static_cast<uint8_t>(ch / kChPerSet); }

    static bool validReg(uint8_t reg) {
        if (reg < kRegs) return true;
        return reg >= kSsg2Base && reg < kSsg2Base + kRegs;
    }

    void write(uint8_t ch, uint8_t reg, uint8_t value) {
        writeAbs(static_cast<uint8_t>(reg + (setOf(ch) ? kSsg2Base : 0)), value);
    }

    void writeAbs(uint8_t reg, uint8_t value) {
        shadow_[reg] = value;
        bus_.write(Device::SSGS, reg, value);
    }

    // キーが上がっていればレベルは 0。エンベロープのスイッチも一緒に落ちる。
    void writeLevel(uint8_t ch) {
        const uint8_t sav = level_[ch];
        const uint8_t out = (sav & kSavKey) ? static_cast<uint8_t>(sav & (kVolEnv | kVolMax)) : 0;
        write(ch, static_cast<uint8_t>(kRegVol + subChannel(ch)), out);
    }

    // Y の書き込みを、ドライバがバイトを組み立てる控えにも入れる。キーは演奏側のもの。
    void ySave(uint8_t reg, uint8_t value) {
        uint8_t base = 0;
        uint8_t r = reg;
        if (r >= kSsg2Base) {
            r = static_cast<uint8_t>(r - kSsg2Base);
            base = kChPerSet;
        }
        if (r == kRegEnable) {
            enable_[base ? 1 : 0] = value;
            return;
        }
        if (r < kRegVol || r >= kRegVol + kChPerSet) return;
        const uint8_t ch = static_cast<uint8_t>(base + (r - kRegVol));
        level_[ch] = static_cast<uint8_t>((level_[ch] & kSavKey) | (value & (kVolEnv | kVolMax)));
    }

    ChipBus& bus_;
    std::array<uint8_t, 0x40> shadow_{};
    std::array<uint8_t, kChannels> level_{};
    std::array<uint8_t, 2> enable_{};
};

} // namespace

std::unique_ptr<SoundDevice> makeSsgsDevice(ChipBus& bus) {
    return std::make_unique<SsgsDevice>(bus);
}

} // namespace y8960
