// SSGS（YMZ705 の SSG 互換部）。Y8960BasicExtension の src/dev/ssg.asm の写し。
// OPNA・OPNB の SSG 部も同じレジスタを持つので、このドライバで鳴らす（ROM に無い）。
//
// SSGS は2つの SSG が1つのポート対を分け合う。レジスタ番号が 20h 未満なら第1セット、
// 20h 以上なら第2セット。チャンネル 0-2 が第1セット、3-5 が第2セット。
// OPNA・OPNB の SSG は1セットで、チャンネル 6-8 がその A-C。
//
// キーはトーンのスイッチではなくレベル。スイッチを切ってもチャンネルは切れず、
// レベルが直流として出てしまうので、消すのはレベルのほう。

#include "dev_internal.h"
#include "pitch.h"

namespace y8960 {

namespace {

constexpr uint8_t kSsg2Base   = 0x20;
constexpr uint8_t kChPerSet    = 3;
constexpr uint8_t kMaxLocal    = 6;
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

class SsgDevice final : public SoundDevice, private EnvelopeSink {
public:
    // first はデバイスのチャンネル番号の先頭、sets は SSG の数。pan は SSGS だけが持つ。
    SsgDevice(ChipBus& bus, Device which, uint8_t first, uint8_t sets, bool pan, SoftEnvelope& envelope)
        : bus_(bus), which_(which), first_(first),
          channels_(static_cast<uint8_t>(sets * kChPerSet)), pan_(pan), env_(envelope) {
        env_.attach(which_, this);
    }

    void reset() override {
        for (uint8_t local = 0; local < channels_; ++local) {
            const uint8_t sub = subChannel(local);
            write(local, static_cast<uint8_t>(kRegToneL + sub * 2), 0);
            write(local, static_cast<uint8_t>(kRegToneL + sub * 2 + 1), 0);
            write(local, static_cast<uint8_t>(kRegVol + sub), 0);
            level_[local] = 0;
            // チップは左端で立ち上がるので、何も言わないパートが片側に寄らないよう
            // 中央に置く。
            if (pan_) write(local, static_cast<uint8_t>(kRegPan + sub), kPanCentre);
        }
        for (uint8_t set = 0; set * kChPerSet < channels_; ++set) {
            const uint8_t local = static_cast<uint8_t>(set * kChPerSet);
            write(local, kRegNoise, 0);
            write(local, kRegEnvPerL, 0);
            write(local, kRegEnvPerH, 0);
            write(local, kRegShape, 0);
            write(local, kRegEnable, kNoiseOff);
            enable_[set] = kNoiseOff;
        }
        env_.resetDevice(which_);
    }

    void keyOn(uint8_t ch) override {
        uint8_t local;
        if (!toLocal(ch, local)) return;
        env_.keyOn(which_, ch);
        level_[local] = static_cast<uint8_t>(level_[local] | kSavKey);
        writeLevel(local);
    }

    void keyOff(uint8_t ch) override {
        uint8_t local;
        if (!toLocal(ch, local)) return;
        env_.keyOff(which_, ch);
        level_[local] = static_cast<uint8_t>(level_[local] & ~kSavKey);
        writeLevel(local);
    }

    void setVolume(uint8_t ch, uint8_t loudness) override {
        uint8_t local;
        if (!toLocal(ch, local)) return;
        const uint8_t v = static_cast<uint8_t>((loudness >> 3) & kVolMax);
        env_.setV(which_, ch, v);
        level_[local] = static_cast<uint8_t>((level_[local] & ~kVolMax) | v);
        writeLevel(local);
    }

    void setPitch(uint8_t ch, uint8_t note, int16_t bend) override {
        uint8_t local;
        if (!toLocal(ch, local)) return;
        const uint16_t divisor = period(note, bend);
        const uint8_t sub = subChannel(local);
        write(local, static_cast<uint8_t>(kRegToneL + sub * 2), static_cast<uint8_t>(divisor & 0xFF));
        write(local, static_cast<uint8_t>(kRegToneL + sub * 2 + 1), static_cast<uint8_t>(divisor >> 8));
    }

    // @n はミキサの2つのスイッチとエンベロープの選択を1バイトに詰めたもの。
    //   bit4-0 レベルレジスタそのまま（bit4 でエンベロープに渡す）
    //   bit5   トーンのスイッチ、チップと同じ極性（0 が鳴る）
    //   bit6   ノイズのスイッチ、逆（1 が鳴る）
    void setVoice(uint8_t ch, uint8_t number) override {
        uint8_t local;
        if (!toLocal(ch, local)) return;
        if (number & kVolEnv) {
            env_.none(which_, ch);                  // ハードウェアのほうが後から来た
            level_[local] = static_cast<uint8_t>((level_[local] & kSavKey) |
                                                 (number & (kVolEnv | kVolMax)));
        } else {
            level_[local] = static_cast<uint8_t>(level_[local] & ~kVolEnv);
        }
        writeLevel(local);

        const uint8_t set  = setOf(local);
        const uint8_t tone = static_cast<uint8_t>(1u << subChannel(local));
        const uint8_t noise = static_cast<uint8_t>(tone << kNoiseShift);
        uint8_t en = static_cast<uint8_t>(enable_[set] & ~tone);      // トーンは鳴る
        if (number & 0x20) en = static_cast<uint8_t>(en | tone);      // @n が切った
        en = static_cast<uint8_t>(en | noise);                        // ノイズは鳴らない
        if (number & 0x40) en = static_cast<uint8_t>(en & ~noise);    // @n が入れた
        enable_[set] = en;
        write(local, kRegEnable, en);
    }

    // SSGS の定位レジスタは `87` と同じ目盛り（0 が左端、8 が中央、15 が右端）。
    void setPan(uint8_t ch, uint8_t value) override {
        uint8_t local;
        if (!pan_ || !toLocal(ch, local)) return;
        write(local, static_cast<uint8_t>(kRegPan + subChannel(local)), value);
    }

    void ssgEnv(SsgEnv kind, uint8_t ch, uint8_t value) override {
        uint8_t local;
        if (!toLocal(ch, local)) return;
        switch (kind) {
        case SsgEnv::Shape:      write(local, kRegShape, value); break;
        case SsgEnv::PeriodLow:  write(local, kRegEnvPerL, value); break;
        case SsgEnv::PeriodHigh: write(local, kRegEnvPerH, value); break;
        }
    }

    bool regRead(uint8_t port, uint8_t reg, uint8_t& value) override {
        if (port != 0 || !validReg(reg)) return false;
        value = shadow_[reg];
        return true;
    }

    bool regWrite(uint8_t port, uint8_t reg, uint8_t value) override {
        if (port != 0 || !validReg(reg)) return false;
        writeAbs(reg, value);
        ySave(reg, value);
        return true;
    }

private:
    static uint8_t subChannel(uint8_t local) { return static_cast<uint8_t>(local % kChPerSet); }
    static uint8_t setOf(uint8_t local)      { return static_cast<uint8_t>(local / kChPerSet); }

    bool toLocal(uint8_t ch, uint8_t& local) const {
        if (ch < first_ || ch >= first_ + channels_) return false;
        local = static_cast<uint8_t>(ch - first_);
        return true;
    }

    bool validReg(uint8_t reg) const {
        if (reg < kRegs) return true;
        return channels_ > kChPerSet && reg >= kSsg2Base && reg < kSsg2Base + kRegs;
    }

    uint16_t period(uint8_t note, int16_t bend) const {
        switch (which_) {
        case Device::OPNA: return opnSsgPeriod(kClockOpna, note, bend);
        case Device::OPNB: return opnSsgPeriod(kClockOpnb, note, bend);
        default:           return divPitch(which_, note, bend).divisor;
        }
    }

    void write(uint8_t local, uint8_t reg, uint8_t value) {
        writeAbs(static_cast<uint8_t>(reg + (setOf(local) ? kSsg2Base : 0)), value);
    }

    void writeAbs(uint8_t reg, uint8_t value) {
        shadow_[reg] = value;
        bus_.write(which_, reg, value);
    }

    // キーが上がっていればレベルは 0。エンベロープのスイッチも一緒に落ちる。
    // ソフトウェアエンベロープがあれば、キーが上がったあと（リリース）もそちらが決める。
    void writeLevel(uint8_t local) {
        uint8_t out = 0;
        if (!env_.output(which_, static_cast<uint8_t>(first_ + local), out)) {
            const uint8_t sav = level_[local];
            out = (sav & kSavKey) ? static_cast<uint8_t>(sav & (kVolEnv | kVolMax)) : 0;
        }
        write(local, static_cast<uint8_t>(kRegVol + subChannel(local)), out);
    }

    // エンベロープはデバイスのチャンネル番号で話す。
    void envRefresh(uint8_t ch) override { writeLevel(static_cast<uint8_t>(ch - first_)); }
    uint8_t envSync(uint8_t ch) override { return level_[static_cast<size_t>(ch - first_)]; }
    // ソフトウェアのほうが後から来た。トーンとノイズの選択はそのまま。
    void envChosen(uint8_t ch) override {
        uint8_t& l = level_[static_cast<size_t>(ch - first_)];
        l = static_cast<uint8_t>(l & ~kVolEnv);
    }
    void envReleased(uint8_t ch) override { keyOff(ch); }

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
        const uint8_t local = static_cast<uint8_t>(base + (r - kRegVol));
        level_[local] = static_cast<uint8_t>((level_[local] & kSavKey) | (value & (kVolEnv | kVolMax)));
    }

    ChipBus& bus_;
    Device   which_;
    uint8_t  first_;
    uint8_t  channels_;
    bool     pan_;
    SoftEnvelope& env_;
    std::array<uint8_t, 0x40> shadow_{};
    std::array<uint8_t, kMaxLocal> level_{};
    std::array<uint8_t, 2> enable_{};
};

} // namespace

std::unique_ptr<SoundDevice> makeSsgsDevice(ChipBus& bus, SoftEnvelope& envelope) {
    return std::make_unique<SsgDevice>(bus, Device::SSGS, uint8_t{0}, uint8_t{2}, true, envelope);
}

std::unique_ptr<SoundDevice> makeOpnSsg(ChipBus& bus, Device which, SoftEnvelope& envelope) {
    return std::make_unique<SsgDevice>(bus, which, kOpnSsgFirst, uint8_t{1}, false, envelope);
}

} // namespace y8960
