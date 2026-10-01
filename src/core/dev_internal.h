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
std::unique_ptr<SoundDevice> makeOpl3Device(ChipBus& bus);
std::unique_ptr<SoundDevice> makeOpmDevice(ChipBus& bus);
// OPNA と OPNB。SSG 部（チャンネル 6-8）はソフトウェアエンベロープを持つ。
std::unique_ptr<SoundDevice> makeOpnDevice(ChipBus& bus, Device which, SoftEnvelope& envelope);
// OPNA・OPNB の SSG 部。SSGS と同じドライバを1セットで使う。
std::unique_ptr<SoundDevice> makeOpnSsg(ChipBus& bus, Device which, SoftEnvelope& envelope);

// 音量 0-127（1 が 0.75 dB、127 がいちばん大きい）を、4 ビットの音量しか持たない
// チップの段 0-15 に（devtab.asm の VOLSTEP）。チップの1段は約 3 dB なので、127 から
// 4 下げるごとに1段下げ、15 段より下へは下げない。MML の V n（4n + 67）はちょうど段 n。
inline uint8_t volumeStep(uint8_t loudness) {
    const int down = (127 - (loudness & 127)) >> 2;
    return static_cast<uint8_t>(15 - (down > 15 ? 15 : down));
}

// 音量を、線形の利得を持つ ADPCM のレベル 0-255 に（adpcm.asm の ADPVOLTAB と同じ式）。
uint8_t adpcmLevel(uint8_t loudness);

// 音量を、FM のオペレータの減衰に足す段数に。1段が 0.75 dB で、チップの幅（max）を
// 超えて下げたぶんは幅の底で止める。
inline uint8_t fmAttenuation(uint8_t loudness, uint8_t max) {
    const int down = 127 - (loudness & 127);
    return static_cast<uint8_t>(down > max ? max : down);
}

// リズムチャンネルの控え（rhythm.asm の CTL_RHYSAV）。OPLLEX・OPL2EX・OPL3 は
// 5つの楽器で音量 0-15、OPNA のリズムと OPNB の ADPCM-A は6つの楽器で音量 0-31 に使う。
// 通常音量は楽器ごと、アクセント音量はすべてに共通。
struct RhythmState {
    static constexpr int kInstruments = 6;

    uint8_t mode    = 0;    // リズムレジスタのうち、打撃以外のビット
    std::array<uint8_t, kInstruments> levels{8, 8, 8, 8, 8, 8};   // V と @B など。bit0 から
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

    // リズムモードに入るとき（OPNA・OPNB は演奏を始めるとき）の値。
    void toDefaults(uint8_t normal = 8, uint8_t accentLevel = 15) {
        levels.fill(normal);
        accent = accentLevel;
        accents = 0;
    }

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

    // 段 0-15 をシーケンスの音量ぶん下げたもの（RHYSCALE）。V と同じ 4n + 67 に直して
    // から引き、段に戻す。段のまま引くと、シーケンスの音量が 4 の倍数に丸まる。
    uint8_t scaled(uint8_t value) const {
        const int v = value * 4 + 67 + scale - 127;
        return volumeStep(static_cast<uint8_t>(v < 0 ? 0 : v));
    }

    // 楽器 bit の、この打撃での音量（シーケンスの音量を引く前）。
    uint8_t raw(uint8_t bit, uint8_t struckAccents) const {
        return (bit & struckAccents) ? accent : levels[static_cast<size_t>(index(bit))];
    }

    // 楽器 bit が、この打撃で受け取る段 0-15。叩かれない楽器も普通のレベルを受け取る。
    uint8_t level(uint8_t bit, uint8_t struckAccents) const {
        return scaled(raw(bit, struckAccents));
    }

    // 同じく減衰で。OPLL と OPL の書き方。
    uint8_t attenuation(uint8_t bit, uint8_t struckAccents) const {
        return static_cast<uint8_t>(kVolMax - level(bit, struckAccents));
    }

    static int index(uint8_t bit) {
        int i = 0;
        while (i < kInstruments - 1 && !(bit & (1u << i))) ++i;
        return i;
    }
};

} // namespace y8960
