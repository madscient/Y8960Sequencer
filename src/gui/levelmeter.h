#pragma once
// 音源ブロックごと・チャンネルごとのレベルメーター。見た目は FITOM_X の
// LevelMeterPanel に合わせる ―― LED のセグメントを積んだバー、下が緑・上が黄と
// 赤、消えているセグメントも暗い色、ピークホールドの発光、バーの下にラベル。
// ブロックを1つの帯として縦に積む。
//
// エミュレータから取れるのはチップ単位の音だけなので、バーはキーオンとその
// ときの音量から作り、落ち方はここで作る（FITOM_X と同じ）。

#include "activity.h"
#include "block.h"

#include <array>

namespace y8960 {

class PlaybackEngine;

// 画面で選んだミュート。チップ単位と、チャンネル単位（0-10。リズムは 10 の1本）。
struct MuteState {
    std::array<bool, kDeviceCount> chip{};
    std::array<std::array<bool, kChannelRhythm + 1>, kDeviceCount> channel{};

    bool muted(Device device, uint8_t ch) const {
        const size_t d = static_cast<size_t>(device);
        return chip[d] || (ch <= kChannelRhythm && channel[d][ch]);
    }
    // 黙らせるトラックのビット。
    uint16_t trackMask(const SequenceBlock& block) const;
};

class LevelMeter {
public:
    // 既定では、読み込んだシーケンスが使うチャンネルだけを出す。showAll を立てると
    // 8ブロックの全チャンネルを出す。
    void update(const PlaybackEngine& engine, const SequenceBlock& block, bool haveBlock,
                bool showAll, float now);
    // 帯の見出しのチェックボックスでチップを、バーのクリックでチャンネルを
    // ミュートする。変わったら true を返す。
    bool draw(MuteState& mutes) const;

private:
    struct Bar {
        float    level      = 0.0f;
        float    decayFrom  = 0.0f;
        float    decayStart = -1.0f;
        float    decayDur   = 0.0f;
        float    peak       = 0.0f;
        float    peakStart  = -1.0f;
        bool     wasSounding = false;
        uint32_t lastSeq     = 0;
    };

    struct Entry {
        Device      device;
        uint8_t     channel;
        const char* label;
    };

    std::array<std::array<Bar, kActivitySlots>, kDeviceCount> bars_{};
    std::array<bool, kDeviceCount> bandUsed_{};
    std::array<std::array<bool, kActivitySlots>, kDeviceCount> slotUsed_{};
    float now_ = 0.0f;
};

} // namespace y8960
