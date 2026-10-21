// OPM（YM2151、デバイス 4）。bytecode.md の定めから作り、ROM の src/dev/opm.asm（`d1db78e`）と
// 突き合わせた。形式が意味を定めない `87` の 0 は、ROM では左だけ、ここでは両方（panSide）。
// 音色はチャンク 40 のレコード（OPN 系と共通の 32 バイト）。

#include "dev_internal.h"
#include "fmvoice.h"
#include "pitch.h"

namespace y8960 {

namespace {

constexpr uint8_t kChannels  = 8;
constexpr uint8_t kRegKeyOn  = 0x08;
constexpr uint8_t kRegNoise  = 0x0F;
constexpr uint8_t kRegTimerCtl = 0x14;
constexpr uint8_t kTimerReset  = 0x30;   // 2つのフラグを落とし、どれも許さない
constexpr uint8_t kRegPanCon = 0x20;   // RL・FB・CON
constexpr uint8_t kRegKc     = 0x28;
constexpr uint8_t kRegKf     = 0x30;
constexpr uint8_t kRegPmsAms = 0x38;
constexpr uint8_t kRegDtMul  = 0x40;
constexpr uint8_t kRegTl     = 0x60;
constexpr uint8_t kRegKsAr   = 0x80;
constexpr uint8_t kRegAmD1r  = 0xA0;
constexpr uint8_t kRegDt2D2r = 0xC0;
constexpr uint8_t kRegD1lRr  = 0xE0;
constexpr uint8_t kNoiseCh   = 7;
constexpr uint8_t kNoiseEnable = 0x80;
constexpr uint8_t kPanL      = 0x40;   // ymfm の ch_output_0
constexpr uint8_t kPanR      = 0x80;
constexpr uint8_t kPanLR     = kPanL | kPanR;
constexpr uint8_t kTlMax     = 127;

// レコードのオペレータは M1・C1・M2・C2 の順。レジスタは M1・M2・C1・C2 の順に 8 ずつ。
constexpr uint8_t kOpOffset[4] = {0, 16, 8, 24};

uint8_t panBits(uint8_t value) {
    switch (panSide(value)) {
    case PanSide::Left:  return kPanL;
    case PanSide::Right: return kPanR;
    default:             return kPanLR;
    }
}

class OpmDevice final : public SoundDevice {
public:
    explicit OpmDevice(ChipBus& bus) : bus_(bus) {}

    // 値と順は ROM の OPM_RESET（OPMRESTAB）と同じ。LFO・ノイズ・タイマーはチップ全体の
    // もので、シーケンスは管理しない。RR を TL より先に書くのは、遅いリリースで鳴り残って
    // いる音を先に無音へ切らないため。
    void reset() override {
        for (uint8_t ch = 0; ch < kChannels; ++ch) write(kRegKeyOn, ch);
        write(0x01, 0);
        write(kRegTimerCtl, kTimerReset);
        write(kRegNoise, 0);
        write(0x18, 0);
        write(0x19, 0);           // AMD と PMD は 19h を bit7 で分け合う
        write(0x19, 0x80);
        write(0x1B, 0);
        const auto fill = [this](uint8_t base, int count, uint8_t value) {
            for (int i = 0; i < count; ++i) write(static_cast<uint8_t>(base + i), value);
        };
        fill(kRegD1lRr, 32, 0x0F);
        fill(kRegTl, 32, kTlMax);
        fill(kRegDtMul, 32, 0);
        fill(kRegKsAr, 32, 0);
        fill(kRegAmD1r, 32, 0);
        fill(kRegDt2D2r, 32, 0);
        fill(kRegPanCon, kChannels, kPanLR);
        fill(kRegKc, kChannels, 0);
        fill(kRegKf, kChannels, 0);
        fill(kRegPmsAms, kChannels, 0);
        for (Channel& c : channels_) c = Channel{};
    }

    void keyOn(uint8_t ch) override {
        if (ch >= kChannels) return;
        write(kRegKeyOn, static_cast<uint8_t>(channels_[ch].slots | ch));
    }

    void keyOff(uint8_t ch) override {
        if (ch >= kChannels) return;
        write(kRegKeyOn, ch);
    }

    void setVolume(uint8_t ch, uint8_t loudness) override {
        if (ch >= kChannels) return;
        channels_[ch].volume = loudness;
        writeLevels(ch);
    }

    void setPitch(uint8_t ch, uint8_t note, int16_t bend) override {
        if (ch >= kChannels) return;
        const OpmPitch p = opmPitch(transposed(note, channels_[ch].transpose), bend);
        write(static_cast<uint8_t>(kRegKc + ch), p.kc);
        write(static_cast<uint8_t>(kRegKf + ch), static_cast<uint8_t>(p.kf << 2));
    }

    void seqVoice(uint8_t ch, uint8_t slot, const VoiceRecord& record) override {
        (void)slot;
        if (ch >= kChannels) return;
        const uint8_t* r = record.data.data();
        Channel& c = channels_[ch];
        const uint8_t pan = static_cast<uint8_t>(shadow_[kRegPanCon + ch] & kPanLR);
        write(static_cast<uint8_t>(kRegPanCon + ch), static_cast<uint8_t>(pan | (r[kFmAlg] & 0x3F)));
        const uint8_t sens = r[kFmSens];
        write(static_cast<uint8_t>(kRegPmsAms + ch),
              static_cast<uint8_t>(((sens & 0x07) << 4) | ((sens >> 4) & 0x03)));
        // NE はチャンネル 7 だけのもの。ノイズの周波数（bit4-0）はシーケンスが管理しない。
        if (ch == kNoiseCh) {
            write(kRegNoise, static_cast<uint8_t>((shadow_[kRegNoise] & ~kNoiseEnable) |
                                                  (r[kFmAlg] & kNoiseEnable)));
        }
        for (uint8_t op = 0; op < 4; ++op) {
            const uint8_t* o = r + kFmOp1 + op * kFmOpSize;
            const uint8_t reg = static_cast<uint8_t>(kOpOffset[op] + ch);
            write(static_cast<uint8_t>(kRegDtMul + reg), static_cast<uint8_t>(o[kFmOpDtMul] & 0x7F));
            write(static_cast<uint8_t>(kRegTl + reg), static_cast<uint8_t>(o[kFmOpTl] & 0x7F));
            write(static_cast<uint8_t>(kRegKsAr + reg), static_cast<uint8_t>(o[kFmOpKsAr] & 0xDF));
            write(static_cast<uint8_t>(kRegAmD1r + reg), static_cast<uint8_t>(o[kFmOpAmDr] & 0x9F));
            write(static_cast<uint8_t>(kRegDt2D2r + reg), static_cast<uint8_t>(o[kFmOpDt2Sr] & 0xDF));
            write(static_cast<uint8_t>(kRegD1lRr + reg), o[kFmOpSlRr]);
            c.tl[op] = static_cast<uint8_t>(o[kFmOpTl] & 0x7F);
        }
        // レコードは OPN の 28h の並び（bit7-4）。OPM の 08h では bit6-3。
        c.slots = static_cast<uint8_t>((r[kFmSlots] >> 1) & 0x78);
        c.transpose = static_cast<int8_t>(r[kFmTrans]);
        writeLevels(ch);
    }

    void setPan(uint8_t ch, uint8_t value) override {
        if (ch >= kChannels) return;
        const uint8_t reg = static_cast<uint8_t>(kRegPanCon + ch);
        write(reg, static_cast<uint8_t>((shadow_[reg] & ~kPanLR) | panBits(value)));
    }

    // 14h は断る（ROM の OPMCHKREG と同じ）。タイマーの割り込みを許すレジスタで、実機では
    // 応える者のいない /INT が本体に届く。
    bool regRead(uint8_t port, uint8_t reg, uint8_t& value) override {
        if (port != 0 || reg == kRegTimerCtl) return false;
        value = shadow_[reg];
        return true;
    }

    bool regWrite(uint8_t port, uint8_t reg, uint8_t value) override {
        if (port != 0 || reg == kRegTimerCtl) return false;
        write(reg, value);
        return true;
    }

private:
    struct Channel {
        uint8_t volume    = 0;
        int8_t  transpose = 0;
        uint8_t slots     = 0;      // 08h の bit6-3。音色が決める。`85` の前はどれもキーオンしない
        std::array<uint8_t, 4> tl{kTlMax, kTlMax, kTlMax, kTlMax};
    };

    void write(uint8_t reg, uint8_t value) {
        shadow_[reg] = value;
        bus_.write(Device::OPM, reg, value);
    }

    void writeLevels(uint8_t ch) {
        const Channel& c = channels_[ch];
        const uint8_t take = fmAttenuation(c.volume, kTlMax);
        // キャリアはチップの 20h の CON から決める。`Y` で接続を変えても音量はそれに従う（ROM の
        // OPMTLOUT と同じ）。
        const uint8_t carriers = fmCarriers(shadow_[kRegPanCon + ch]);
        for (uint8_t op = 0; op < 4; ++op) {
            if (!((carriers >> op) & 1)) continue;
            const int level = c.tl[op] + take;
            write(static_cast<uint8_t>(kRegTl + kOpOffset[op] + ch),
                  static_cast<uint8_t>(level > kTlMax ? kTlMax : level));
        }
    }

    ChipBus& bus_;
    std::array<uint8_t, 256> shadow_{};
    std::array<Channel, kChannels> channels_{};
};

} // namespace

std::unique_ptr<SoundDevice> makeOpmDevice(ChipBus& bus) {
    return std::make_unique<OpmDevice>(bus);
}

} // namespace y8960
