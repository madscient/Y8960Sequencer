#pragma once
// 音符番号とベンドから、チップに書く値を出す。
// OPL 系と分周器系は Y8960BasicExtension の src/dev/fnum.asm の写し。デバイス 8-11 の
// ものは ROM に無いので、平均律（O4 A が 440Hz）から直接計算する。

#include "block.h"
#include "freqtab.h"

#include <cstdint>

namespace y8960 {

// OPL 系（OPLLEX・OPL2EX）の F-Number とブロック。
struct OplPitch {
    uint16_t fnum       = 0;
    uint8_t  block      = 0;
    bool     outOfRange = false;   // 音域の外。いちばん近い値が入っている
};

// isOpl2 で F-Number の幅が変わる（OPLL は 9bit、OPL2 は 10bit）。
OplPitch oplPitch(bool isOpl2, uint8_t note, int16_t bendSteps);

// 分周器系（SSGS・DCSG・SCC）の分周値。SCC のレジスタに書くのはこれから 1 引いた値。
struct DivPitch {
    uint16_t divisor    = 1;
    bool     outOfRange = false;
};

DivPitch divPitch(Device device, uint8_t note, int16_t bendSteps);

// デバイス 8-11 のクロック。Y8SQ 形式は決めていないので、各チップの標準のもの。
constexpr uint32_t kClockOpl3 = 14318180;   // 2OP の F-Number は OPL2 と同じになる
constexpr uint32_t kClockOpm  = 3579545;
constexpr uint32_t kClockOpna = 7987200;
constexpr uint32_t kClockOpnb = 8000000;

// OPN 系の F-Number（11bit）とブロック。
struct OpnPitch {
    uint16_t fnum  = 0;
    uint8_t  block = 0;
};
OpnPitch opnPitch(uint32_t clock, uint8_t note, int16_t bendSteps);

// OPM の KC（オクターブとノートコード）と KF（1/64 半音、レジスタの bit7-2 に置く前の値）。
struct OpmPitch {
    uint8_t kc = 0;
    uint8_t kf = 0;
};
OpmPitch opmPitch(uint8_t note, int16_t bendSteps);

// OPN 系の SSG の分周値（12bit）。
uint16_t opnSsgPeriod(uint32_t clock, uint8_t note, int16_t bendSteps);

// 音符番号とベンドが指す周波数（Hz）。
double noteFrequency(uint8_t note, int16_t bendSteps);

} // namespace y8960
