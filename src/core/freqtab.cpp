#include "freqtab.h"

#include <array>
#include <cmath>

namespace y8960 {

namespace {

constexpr double kClock  = 3579545.0;          // カートリッジのマスタークロック
constexpr double kFs     = kClock / 72.0;      // OPL 系が数える速さ
constexpr double kDivClk = kClock / 32.0;      // 分周器系
constexpr double kA4     = 440.0;

// Python の round() は偶数丸め。表を1つでも違えないために同じにする。
int64_t roundHalfEven(double v) {
    const double r = std::nearbyint(v);   // 既定の丸めモードが偶数丸め
    return static_cast<int64_t>(r);
}

double c4() { return kA4 * std::pow(2.0, -9.0 / 12.0); }

int adjUntil() {
    // F-Number が1つ下のブロックで 12 ビットに収まらなくなる最初の歩。
    for (int i = 0; i < kFreqSteps; ++i) {
        const double f = c4() * std::pow(2.0, double(i) / kFreqSteps);
        if (roundHalfEven(f * std::pow(2.0, 22) / kFs / 8.0) > 0x0FFF) return i;
    }
    return kFreqSteps;
}

struct Tables {
    std::array<uint16_t, kFreqSteps> opl{};
    std::array<uint16_t, kFreqSteps> div{};
    std::array<uint16_t, kFreqSteps> pcm{};

    Tables() {
        const int    adj  = adjUntil();
        const double cdiv = c4() / 128.0;      // 分周器の表は7オクターブ下
        for (int i = 0; i < kFreqSteps; ++i) {
            const double ratio = std::pow(2.0, double(i) / kFreqSteps);
            const int    block = (i < adj) ? 3 : 4;
            const double f     = c4() * ratio;
            const uint16_t v   = static_cast<uint16_t>(
                roundHalfEven(f * std::pow(2.0, 22) / kFs / std::pow(2.0, block)));
            opl[i] = static_cast<uint16_t>(v | (i < adj ? kFnumAdj : 0));
            div[i] = static_cast<uint16_t>(roundHalfEven(kDivClk / (cdiv * ratio)));
            pcm[i] = static_cast<uint16_t>(roundHalfEven((ratio - 1.0) * 65536.0));
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

} // namespace

uint16_t freqEntry(FreqFamily family, int index) {
    const Tables& t = tables();
    switch (family) {
    case FreqFamily::Opl: return t.opl[static_cast<size_t>(index)];
    case FreqFamily::Div: return t.div[static_cast<size_t>(index)];
    case FreqFamily::Pcm: return t.pcm[static_cast<size_t>(index)];
    }
    return 0;
}

int16_t centToSteps(int16_t cents) {
    // 41/64 は 64/100 を全範囲で1歩以内に近似し、半音では正確（100 セントが 64 歩）。
    // 符号を外してから掛けて戻すのは、ROM が符号なしでシフトするのに合わせるため。
    const int32_t mag = (cents < 0) ? -int32_t(cents) : int32_t(cents);
    const int32_t v   = (mag * 41) >> 6;
    return static_cast<int16_t>(cents < 0 ? -v : v);
}

int16_t clampBend(int32_t steps) {
    constexpr int32_t limit = 8 * kFreqSteps;
    if (steps >  limit) return  static_cast<int16_t>(limit);
    if (steps < -limit) return static_cast<int16_t>(-limit);
    return static_cast<int16_t>(steps);
}

} // namespace y8960
