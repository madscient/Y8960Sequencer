#pragma once
// ドライバの実体を作る関数。device.cpp と各ドライバのファイルだけが使う。

#include "device.h"

#include <memory>

namespace y8960 {

std::unique_ptr<SoundDevice> makeSsgsDevice(ChipBus& bus);
std::unique_ptr<SoundDevice> makeDcsgDevice(ChipBus& bus, Device which);
std::unique_ptr<SoundDevice> makeSccDevice(ChipBus& bus);
std::unique_ptr<SoundDevice> makeOpllexDevice(ChipBus& bus, Device which);
std::unique_ptr<SoundDevice> makeOpl2exDevice(ChipBus& bus, Device which);

// リズムチャンネルの控え（rhythm.asm の CTL_RHYSAV）。OPLLEX と OPL2EX が持つ。
struct RhythmState {
    uint8_t mode    = 0;    // リズムレジスタのうち、打撃以外のビット
    uint8_t level   = 8;    // V
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

    void toDefaults() { level = 8; accent = 15; accents = 0; }

    // レベルにシーケンスの音量を掛けたもの。MULVOL と同じ +1。
    uint8_t scaled(uint8_t value) const {
        return static_cast<uint8_t>((static_cast<uint16_t>(value) * (scale + 1)) >> 7);
    }

    // 楽器 bit が、この打撃で受け取る減衰。叩かれない楽器も普通のレベルを受け取る。
    uint8_t attenuation(uint8_t bit, uint8_t struckAccents) const {
        const uint8_t level_ = scaled((bit & struckAccents) ? accent : level);
        return static_cast<uint8_t>(kVolMax - level_);
    }
};

} // namespace y8960
