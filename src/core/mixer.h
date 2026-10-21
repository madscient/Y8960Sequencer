#pragma once
// Digital Mixer（Y8960 のブロックごとの音量）と、プレイヤーのマスター音量。
// 値は ROM の CALL MIXER と同じ 0-128 で、128 が素通し、0 が無音、間は線形の利得
// （Y8960BasicExtension の doc/hardware.md「Memory Mapped I/O」）。
//
// CLI の --mixer は CALL MIXER と同じ6つ（マスター、SSGS、OPLLEX、OPL2EX、SCC、OPM）を取る。
// 実機にマスターのレジスタは無く、ROM は覚えるだけだが、プレイヤーは出力全体に掛ける。

#include "block.h"

#include <array>
#include <cstdint>
#include <string>

namespace y8960 {

constexpr uint8_t kMixerLevelMax = 128;

struct MixerLevels {
    uint8_t master = kMixerLevelMax;
    // デバイス番号の順（SSGS、OPLLEX、OPL2EX、SCC、OPM）。デバイス 8・10・11 は実機の
    // ミキサーに無いので持たない。
    std::array<uint8_t, 5> blocks{kMixerLevelMax, kMixerLevelMax, kMixerLevelMax, kMixerLevelMax,
                                  kMixerLevelMax};
};

// 0-128 を線形の利得に。128 を超える値は 128 として扱う（ミキサーのレジスタは bit7 が
// 立てば最大）。
float mixerGain(uint8_t level);

// "<master>,<SSGS>,<OPLLEX>,<OPL2EX>,<SCC>,<OPM>" を読む。6つとも要り、どれも 0-128。
bool parseMixer(const std::string& text, MixerLevels& out, std::string& error);

} // namespace y8960
