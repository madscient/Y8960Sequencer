#include "levelmeter.h"

#include "engine.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace y8960 {

namespace {

constexpr float kBarWidth  = 26.0f;
constexpr float kBarGap    = 8.0f;
constexpr float kBarHeight = 96.0f;
constexpr float kLabelH    = 16.0f;

// LED のセグメントと、下から何本目までを緑・黄とするか（残りが赤）。
constexpr int   kSegments   = 14;
constexpr int   kGreenSegs  = 8;
constexpr int   kYellowSegs = 3;
constexpr float kSegGap     = 2.0f;

constexpr float kFallPerSec  = 2.0f;    // バーが落ちる速さ（0-1 を秒で）
constexpr float kPeakHoldSec = 1.5f;

constexpr ImU32 kGreenLit  = IM_COL32( 80, 230, 110, 255);
constexpr ImU32 kGreenDim  = IM_COL32( 22,  55,  30, 255);
constexpr ImU32 kYellowLit = IM_COL32(235, 220,  60, 255);
constexpr ImU32 kYellowDim = IM_COL32( 58,  52,  20, 255);
constexpr ImU32 kRedLit    = IM_COL32(235,  70,  60, 255);
constexpr ImU32 kRedDim    = IM_COL32( 58,  22,  20, 255);

const char* const kLabels[kDeviceCount] = {
    "SSGS", "OPLL1", "OPLL2", "OPL2-1", "OPL2-2", "DCSG1", "DCSG2", "SCC",
};

ImU32 blend(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const ImVec4 fa = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(fa.x + (fb.x - fa.x) * t,
                                                 fa.y + (fb.y - fa.y) * t,
                                                 fa.z + (fb.z - fa.z) * t,
                                                 fa.w + (fb.w - fa.w) * t));
}

// 振幅をバーの高さにする。小さい音も見えるように対数で取り、-48dB を下端にする。
float toBar(float amplitude) {
    if (amplitude <= 0.0f) return 0.0f;
    const float db = 20.0f * std::log10(std::min(amplitude, 1.0f));
    return std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
}

} // namespace

void LevelMeter::update(PlaybackEngine& engine, float now) {
    now_ = now;
    for (size_t i = 0; i < kDeviceCount; ++i) {
        Bar& bar = bars_[i];
        const float dt = (bar.lastNow > 0.0f) ? std::max(0.0f, now - bar.lastNow) : 0.0f;
        bar.lastNow = now;

        const float level = toBar(engine.takeLevel(static_cast<Device>(i)));
        bar.level = std::max(level, bar.level - kFallPerSec * dt);

        // ピークは、更新されるか持ち時間が尽きるまでその位置に留まる。
        const bool expired = (bar.peakStart < 0.0f) || (now - bar.peakStart > kPeakHoldSec);
        if (level >= bar.peak || expired) {
            bar.peak      = level;
            bar.peakStart = (level > 0.0f) ? now : -1.0f;
        }
    }
}

void LevelMeter::draw() const {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float segH = (kBarHeight - kSegGap * (kSegments - 1)) / kSegments;

    for (size_t i = 0; i < kDeviceCount; ++i) {
        const Bar& bar = bars_[i];
        const ImVec2 pos(origin.x + static_cast<float>(i) * (kBarWidth + kBarGap), origin.y);
        const ImVec2 trackMax(pos.x + kBarWidth, pos.y + kBarHeight);
        dl->AddRectFilled(pos, trackMax, IM_COL32(16, 16, 18, 255));

        const int lit = static_cast<int>(std::lround(bar.level * kSegments));
        int peakSeg = -1;
        float peakAlpha = 0.0f;
        if (bar.peakStart >= 0.0f) {
            const float t = (now_ - bar.peakStart) / kPeakHoldSec;
            if (t < 1.0f) {
                peakAlpha = 1.0f - t;
                peakSeg = std::clamp(static_cast<int>(std::lround(bar.peak * kSegments)) - 1,
                                     0, kSegments - 1);
            }
        }

        for (int s = 0; s < kSegments; ++s) {
            // セグメント 0 が最下段。下から上へ積む。
            const float top = trackMax.y - static_cast<float>(s + 1) * segH - static_cast<float>(s) * kSegGap;
            ImU32 litCol, dimCol;
            if (s < kGreenSegs)                   { litCol = kGreenLit;  dimCol = kGreenDim; }
            else if (s < kGreenSegs + kYellowSegs) { litCol = kYellowLit; dimCol = kYellowDim; }
            else                                   { litCol = kRedLit;    dimCol = kRedDim; }

            ImU32 col = (s < lit) ? litCol : dimCol;
            if (s == peakSeg) col = blend(col, IM_COL32(255, 255, 255, 255), peakAlpha * 0.85f);
            dl->AddRectFilled(ImVec2(pos.x + 1.0f, top), ImVec2(trackMax.x - 1.0f, top + segH), col);
        }
        dl->AddRect(pos, trackMax, IM_COL32(70, 70, 75, 255));

        const ImVec2 size = ImGui::CalcTextSize(kLabels[i]);
        dl->AddText(ImVec2(pos.x + (kBarWidth - size.x) * 0.5f, trackMax.y + 2.0f),
                    IM_COL32(200, 200, 200, 255), kLabels[i]);
    }

    // 直接描いたぶんカーソルを進める。
    ImGui::Dummy(ImVec2(kDeviceCount * (kBarWidth + kBarGap), kBarHeight + kLabelH));
}

} // namespace y8960
