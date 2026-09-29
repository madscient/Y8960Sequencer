#pragma once
// ドライバの実体を作る関数。device.cpp と各ドライバのファイルだけが使う。

#include "device.h"

#include <array>
#include <memory>

namespace y8960 {

// この3つはソフトウェアエンベロープを持ち、作るときに自分を envelope に結び付ける。
std::unique_ptr<SoundDevice> makeSsgsDevice(ChipBus& bus, SoftEnvelope& envelope);
std::unique_ptr<SoundDevice> makeDcsgDevice(ChipBus& bus, Device which, SoftEnvelope& envelope);
std::unique_ptr<SoundDevice> makeSccDevice(ChipBus& bus, SoftEnvelope& envelope);
std::unique_ptr<SoundDevice> makeOpllexDevice(ChipBus& bus, Device which);
std::unique_ptr<SoundDevice> makeOpl2exDevice(ChipBus& bus, Device which);

// リズムチャンネルの控え（rhythm.asm の CTL_RHYSAV）。OPLLEX と OPL2EX が持つ。
// 通常音量は楽器ごと、アクセント音量は5つに共通。
struct RhythmState {
    static constexpr int kInstruments = 5;

    uint8_t mode    = 0;    // リズムレジスタのうち、打撃以外のビット
    std::array<uint8_t, kInstruments> levels{8, 8, 8, 8, 8};   // V と @B など。bit0（HH）から
    uint8_t accent  = 15;   // @A
    uint8_t scale   = 127;  // シーケンスの音量
    uint8_t accents = 0;    // 最後の打撃でアクセントが付いた楽器

    static constexpr uint8_t kBassDrum = 0x10;
    static constexpr uint8_t kSnare    = 0x08;
    static constexpr uint8_t kTom      = 0x04;
    static constexpr uint8_t kCymbal   = 0x02;
    static constexpr uint8_t kHiHat    = 0x01;
    static constexpr uint8_t kAll      = 0x1F;
    static constexpr uint8_t kVolMax   = 15;

    void toDefaults() { levels.fill(8); accent = 15; accents = 0; }

    // RHY_SETVOL。target は kRhythmAccent か、楽器のビットマップ。
    void setLevel(uint8_t target, uint8_t value) {
        if (target == kRhythmAccent) {
            accent = value;
            return;
        }
        for (int i = 0; i < kInstruments; ++i) {
            if (target & (1u << i)) levels[static_cast<size_t>(i)] = value;
        }
    }

    // レベルをシーケンスの音量ぶん下げたもの（RHYSCALE）。0-127 の `n*8+7` に直して
    // から引く。4bit のまま引くと、シーケンスの音量が 8 の倍数に丸まる。
    uint8_t scaled(uint8_t value) const {
        const int v = value * 8 + 7 + scale - 127;
        return static_cast<uint8_t>(v < 0 ? 0 : (v >> 3) & kVolMax);
    }

    // 楽器 bit が、この打撃で受け取る減衰。叩かれない楽器も普通のレベルを受け取る。
    uint8_t attenuation(uint8_t bit, uint8_t struckAccents) const {
        const uint8_t level_ = scaled((bit & struckAccents) ? accent : levels[static_cast<size_t>(index(bit))]);
        return static_cast<uint8_t>(kVolMax - level_);
    }

    static int index(uint8_t bit) {
        int i = 0;
        while (i < kInstruments - 1 && !(bit & (1u << i))) ++i;
        return i;
    }
};

} // namespace y8960
