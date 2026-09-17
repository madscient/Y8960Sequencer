#pragma once
// 音符番号とベンドから、チップに書く値を出す。
// Y8960BasicExtension の src/dev/fnum.asm の写し。

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

} // namespace y8960
