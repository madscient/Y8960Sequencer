#pragma once
// チャンク 40 のレコード（OPM・OPNA・OPNB の FM 音色、32 バイト）の読み方。
// bytecode.md「OPM と OPN 系の音色」。

#include <cstdint>

namespace y8960 {

constexpr int kFmAlg    = 0;   // bit7 NE（OPM）、bit6 FX（OPN）、bit5-3 FB、bit2-0 AL
constexpr int kFmSens   = 1;   // bit5-4 AMS、bit2-0 PMS
constexpr int kFmTrans  = 2;   // 符号付きの半音
constexpr int kFmSlots  = 3;   // bit7-4 がキーオンする OP4-OP1
constexpr int kFmOp1    = 4;   // M1・C1・M2・C2 の順に 7 バイトずつ
constexpr int kFmOpSize = 7;

constexpr int kFmOpTl    = 0;
constexpr int kFmOpKsAr  = 1;
constexpr int kFmOpAmDr  = 2;
constexpr int kFmOpDt2Sr = 3;
constexpr int kFmOpSlRr  = 4;
constexpr int kFmOpSsgEg = 5;
constexpr int kFmOpDtMul = 6;

constexpr uint8_t kFmFx = 0x40;

// アルゴリズムで出力に出るオペレータ（キャリア）。bit0 が M1、bit1 が C1、bit2 が M2、
// bit3 が C2 ―― レコードの並び。OPM と OPN で同じ8通り。
inline uint8_t fmCarriers(uint8_t alg) {
    static constexpr uint8_t kCarriers[8] = {0x08, 0x08, 0x08, 0x08, 0x0A, 0x0E, 0x0E, 0x0F};
    return kCarriers[alg & 7];
}

// 音符番号に移調を足し、1バイトに収める。
inline uint8_t transposed(uint8_t note, int8_t transpose) {
    const int v = static_cast<int>(note) + transpose;
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

} // namespace y8960
