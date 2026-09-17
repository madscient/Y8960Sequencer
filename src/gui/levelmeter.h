#pragma once
// 音源ブロックごとのレベルメーター。見た目は FITOM_X の LevelMeterPanel に
// 合わせる ―― LED のセグメントを積んだバー、下半分が緑・上が黄と赤、消えて
// いるセグメントも暗い色で塗る、ピークホールドの発光、バーの下にラベル。
//
// FITOM_X は音を合成しないのでノートオンとベロシティからバーを作るが、
// こちらは実際の出力の最大振幅を使う。落ち方だけはこちらで作る。

#include "block.h"

#include <array>

namespace y8960 {

class PlaybackEngine;

class LevelMeter {
public:
    // 音声から取った山を読み、時間を進める。now は秒。
    void update(PlaybackEngine& engine, float now);
    void draw() const;

private:
    struct Bar {
        float level     = 0.0f;
        float peak      = 0.0f;
        float peakStart = -1.0f;
        float lastNow   = 0.0f;
    };
    std::array<Bar, kDeviceCount> bars_{};
    float now_ = 0.0f;
};

} // namespace y8960
