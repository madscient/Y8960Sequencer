#pragma once
// 音律表。1オクターブを 1/64 半音で持つ3系統。
// Y8960BasicExtension の tools/zbuild/freqtab.py と同じ式で作る（表を写さない）。
//   OPL  : OPLL と OPL2 が使う F-Number。bit15 はブロックが1つ下であることの印
//   DIV  : SSGS・DCSG・SCC が分け合う分周値
//   PCM  : ADPCM の再生速度の比率から 1.0 を引いた小数部

#include <cstdint>

namespace y8960 {

constexpr int      kFreqSteps    = 768;    // 1オクターブ
constexpr int      kFreqSemitone = 64;     // 半音あたりの歩数
constexpr uint16_t kFnumAdj      = 0x8000;

enum class FreqFamily { Opl, Div, Pcm };

// 索引は 0 から kFreqSteps-1。範囲外は呼び出し側で畳んでおくこと。
uint16_t freqEntry(FreqFamily family, int index);

// セントを 1/64 半音の歩数に直す（fnum.asm の CENTSTEP）。
int16_t centToSteps(int16_t cents);

// ベンドの上限（±8オクターブ、fnum.asm の BENDCLAMP）。
int16_t clampBend(int32_t steps);

} // namespace y8960
