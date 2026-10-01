// OPM（YM2151、デバイス 9）。ROM に写し元は無く、bytecode.md の定めから作る。
// 音色はチャンク 40 のレコード（OPN 系と共通の 32 バイト）。

#include "dev_internal.h"
#include "fmvoice.h"
#include "pitch.h"

namespace y8960 {

namespace {

constexpr uint8_t kChannels  = 8;
constexpr uint8_t kRegKeyOn  = 0x08;
constexpr uint8_t kRegNoise  = 0x0F;
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
    if (value < 4)  return kPanL;
    if (value < 12) return kPanLR;
    return kPanR;
}

class OpmDevice final : public SoundDevice {
public:
    explicit OpmDevice(ChipBus& bus) : bus_(bus) {}

    void reset() override {
        // LFO・ノイズ・タイマーはチップ全体のもので、シーケンスは管理しない。
        // 演奏を始める前の値としてここで 0 にする。
        write(0x01, 0);
        write(kRegNoise, 0);
        write(0x14, 0);
        write(0x18, 0);
        write(0x19, 0);           // AMD と PMD は 19h を bit7 で分け合う
        write(0x19, 0x80);
        write(0x1B, 0);
        for (uint8_t ch = 0; ch < kChannels; ++ch) {
            write(kRegKeyOn, ch);
            write(static_cast<uint8_t>(kRegPanCon + ch), kPanLR);
            write(static_cast<uint8_t>(kRegKc + ch), 0);
            write(static_cast<uint8_t>(kRegKf + ch), 0);
            write(static_cast<uint8_t>(kRegPmsAms + ch), 0);
            for (uint8_t op = 0; op < 4; ++op) {
                const uint8_t o = static_cast<uint8_t>(op * 8 + ch);
                write(static_cast<uint8_t>(kRegDtMul + o), 0);
                write(static_cast<uint8_t>(kRegTl + o), kTlMax);
                write(static_cast<uint8_t>(kRegKsAr + o), 0);
                write(static_cast<uint8_t>(kRegAmD1r + o), 0);
                write(static_cast<uint8_t>(kRegDt2D2r + o), 0);
                write(static_cast<uint8_t>(kRegD1lRr + o), 0xFF);
            }
            Channel& c = channels_[ch];
            c = Channel{};
        }
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
        c.carriers = fmCarriers(r[kFmAlg]);
        c.transpose = static_cast<int8_t>(r[kFmTrans]);
        writeLevels(ch);
    }

    void setPan(uint8_t ch, uint8_t value) override {
        if (ch >= kChannels) return;
        const uint8_t reg = static_cast<uint8_t>(kRegPanCon + ch);
        write(reg, static_cast<uint8_t>((shadow_[reg] & ~kPanLR) | panBits(value)));
    }

    bool regRead(uint8_t port, uint8_t reg, uint8_t& value) override {
        if (port != 0) return false;
        value = shadow_[reg];
        return true;
    }

    bool regWrite(uint8_t port, uint8_t reg, uint8_t value) override {
        if (port != 0) return false;
        write(reg, value);
        return true;
    }

private:
    struct Channel {
        uint8_t volume    = 0;
        int8_t  transpose = 0;
        uint8_t slots     = 0x78;   // 08h の bit6-3。音色が決める
        uint8_t carriers  = 0x08;   // レコードの並びの bit。C2 だけ
        std::array<uint8_t, 4> tl{kTlMax, kTlMax, kTlMax, kTlMax};
    };

    void write(uint8_t reg, uint8_t value) {
        shadow_[reg] = value;
        bus_.write(Device::OPM, reg, value);
    }

    void writeLevels(uint8_t ch) {
        const Channel& c = channels_[ch];
        const uint8_t take = fmAttenuation(c.volume, kTlMax);
        for (uint8_t op = 0; op < 4; ++op) {
            if (!((c.carriers >> op) & 1)) continue;
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
