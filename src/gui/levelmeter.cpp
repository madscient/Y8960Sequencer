#include "levelmeter.h"

#include "engine.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace y8960 {

namespace {

constexpr float kBarWidth  = 24.0f;
constexpr float kBarGap    = 6.0f;
constexpr float kBarHeight = 72.0f;
constexpr float kLabelH    = 16.0f;
constexpr float kRowGap    = 6.0f;

// LED のセグメントと、下から何本目までを緑・黄とするか（残りが赤）。
constexpr int   kSegments   = 14;
constexpr int   kGreenSegs  = 8;
constexpr int   kYellowSegs = 3;
constexpr float kSegGap     = 2.0f;

constexpr float kNoteDecaySec    = 1.5f;   // キーオンからバーが落ちきるまで
constexpr float kReleaseDecaySec = 0.25f;  // キーオフからバーが落ちきるまで
constexpr float kPeakHoldSec     = 1.5f;

constexpr ImU32 kGreenLit  = IM_COL32( 80, 230, 110, 255);
constexpr ImU32 kGreenDim  = IM_COL32( 22,  55,  30, 255);
constexpr ImU32 kYellowLit = IM_COL32(235, 220,  60, 255);
constexpr ImU32 kYellowDim = IM_COL32( 58,  52,  20, 255);
constexpr ImU32 kRedLit    = IM_COL32(235,  70,  60, 255);
constexpr ImU32 kRedDim    = IM_COL32( 58,  22,  20, 255);

const char* const kBandNames[kDeviceCount] = {
    "SSGS", "OPLLEX 1", "OPLLEX 2", "OPL2EX 1", "OPL2EX 2", "DCSG 1", "DCSG 2", "SCC",
};

// SSGS は2つの SSG からできているので、セット番号とその中の名前で呼ぶ。
const char* const kSsgsLabels[6]  = {"1A", "1B", "1C", "2A", "2B", "2C"};
const char* const kDcsgLabels[4]  = {"1", "2", "3", "N"};
const char* const kNumberLabels[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
const char* const kRhythmLabels[5] = {"BD", "SD", "TM", "CY", "HH"};

const char* slotLabel(Device device, uint8_t slot) {
    if (slot >= kRhythmSlotFirst) return kRhythmLabels[slot - kRhythmSlotFirst];
    switch (device) {
    case Device::SSGS:  return kSsgsLabels[slot];
    case Device::DCSG1:
    case Device::DCSG2: return kDcsgLabels[slot];
    case Device::OPL2EX1:
    case Device::OPL2EX2: return (slot == kChannelAdpcm) ? "PCM" : kNumberLabels[slot];
    default: return kNumberLabels[slot];
    }
}

ImU32 blend(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const ImVec4 fa = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(fa.x + (fb.x - fa.x) * t,
                                                 fa.y + (fb.y - fa.y) * t,
                                                 fa.z + (fb.z - fa.z) * t,
                                                 fa.w + (fb.w - fa.w) * t));
}

} // namespace

namespace {

// そのブロックが持っているチャンネル（bytecode.md「デバイス番号とチャンネル番号」）。
int melodyChannels(Device device) {
    switch (device) {
    case Device::SSGS:    return 6;
    case Device::DCSG1:
    case Device::DCSG2:   return 4;
    case Device::SCC:     return 5;
    default:              return 9;    // OPLLEX と OPL2EX の旋律チャンネル
    }
}

bool hasRhythm(Device device) {
    return device != Device::SSGS && device != Device::DCSG1 &&
           device != Device::DCSG2 && device != Device::SCC;
}

bool hasAdpcm(Device device) {
    return device == Device::OPL2EX1 || device == Device::OPL2EX2;
}

} // namespace

void LevelMeter::update(const PlaybackEngine& engine, const SequenceBlock& block,
                        bool haveBlock, bool showAll, float now) {
    now_ = now;

    // どのブロックのどのチャンネルを出すかは、読み込んだシーケンスが決める。
    bandUsed_.fill(false);
    for (auto& row : slotUsed_) row.fill(false);
    if (showAll) {
        for (size_t d = 0; d < kDeviceCount; ++d) {
            const Device device = static_cast<Device>(d);
            bandUsed_[d] = true;
            for (int c = 0; c < melodyChannels(device); ++c) slotUsed_[d][static_cast<size_t>(c)] = true;
            if (hasAdpcm(device)) slotUsed_[d][kChannelAdpcm] = true;
            if (hasRhythm(device)) {
                for (int i = 0; i < 5; ++i) slotUsed_[d][static_cast<size_t>(kRhythmSlotFirst + i)] = true;
            }
        }
    } else if (haveBlock) {
        for (const TrackData& t : block.tracks) {
            if (!t.assigned) continue;
            const size_t d = static_cast<size_t>(t.device);
            bandUsed_[d] = true;
            if (t.channel == kChannelRhythm) {
                for (int i = 0; i < 5; ++i) slotUsed_[d][static_cast<size_t>(kRhythmSlotFirst + i)] = true;
            } else if (t.channel < kActivitySlots) {
                slotUsed_[d][t.channel] = true;
            }
        }
    }

    const ChannelActivity& activity = engine.activity();
    for (size_t d = 0; d < kDeviceCount; ++d) {
        if (!bandUsed_[d]) continue;
        for (size_t s = 0; s < kActivitySlots; ++s) {
            if (!slotUsed_[d][s]) continue;
            Bar& bar = bars_[d][s];
            const ChannelActivity::Slot& slot =
                activity.read(static_cast<Device>(d), static_cast<uint8_t>(s));
            const bool     sounding = slot.sounding.load(std::memory_order_relaxed);
            const uint32_t seq      = slot.noteOnSeq.load(std::memory_order_relaxed);
            const uint8_t  level    = slot.level.load(std::memory_order_relaxed);

            // 同じ音量で鳴らし直したときも拾えるよう、キーオンは番号の変化で見る。
            const bool noteOn  = sounding && seq != bar.lastSeq;
            const bool noteOff = !sounding && bar.wasSounding;
            if (noteOn) {
                const float peak = static_cast<float>(level) / 127.0f;
                bar.level = peak;
                bar.decayFrom = peak;
                bar.decayStart = now;
                bar.decayDur = kNoteDecaySec;
                bar.peak = peak;
                bar.peakStart = now;
            } else if (noteOff) {
                bar.decayFrom = bar.level;
                bar.decayStart = now;
                bar.decayDur = kReleaseDecaySec;
                bar.peak = 0.0f;
                bar.peakStart = -1.0f;
            }
            if (bar.decayStart >= 0.0f && bar.decayDur > 0.0f) {
                const float t = (now - bar.decayStart) / bar.decayDur;
                bar.level = (t >= 1.0f) ? 0.0f : bar.decayFrom * (1.0f - t);
            }
            bar.wasSounding = sounding;
            bar.lastSeq = seq;
        }
    }
}

void LevelMeter::draw() const {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float segH = (kBarHeight - kSegGap * (kSegments - 1)) / kSegments;

    for (size_t d = 0; d < kDeviceCount; ++d) {
        if (!bandUsed_[d]) continue;
        ImGui::SeparatorText(kBandNames[d]);
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        int column = 0;
        for (size_t s = 0; s < kActivitySlots; ++s) {
            if (!slotUsed_[d][s]) continue;
            const Bar& bar = bars_[d][s];
            const ImVec2 pos(origin.x + static_cast<float>(column) * (kBarWidth + kBarGap), origin.y);
            const ImVec2 trackMax(pos.x + kBarWidth, pos.y + kBarHeight);
            ++column;

            dl->AddRectFilled(pos, trackMax, IM_COL32(16, 16, 18, 255));

            const int lit = static_cast<int>(std::lround(std::clamp(bar.level, 0.0f, 1.0f) * kSegments));
            int   peakSeg = -1;
            float peakAlpha = 0.0f;
            if (bar.peakStart >= 0.0f) {
                const float t = (now_ - bar.peakStart) / kPeakHoldSec;
                if (t < 1.0f) {
                    peakAlpha = 1.0f - t;
                    peakSeg = std::clamp(static_cast<int>(std::lround(bar.peak * kSegments)) - 1,
                                         0, kSegments - 1);
                }
            }

            for (int i = 0; i < kSegments; ++i) {
                // セグメント 0 が最下段。下から上へ積む。
                const float top = trackMax.y - static_cast<float>(i + 1) * segH -
                                  static_cast<float>(i) * kSegGap;
                ImU32 litCol, dimCol;
                if (i < kGreenSegs)                    { litCol = kGreenLit;  dimCol = kGreenDim; }
                else if (i < kGreenSegs + kYellowSegs) { litCol = kYellowLit; dimCol = kYellowDim; }
                else                                   { litCol = kRedLit;    dimCol = kRedDim; }

                ImU32 col = (i < lit) ? litCol : dimCol;
                if (i == peakSeg) col = blend(col, IM_COL32(255, 255, 255, 255), peakAlpha * 0.85f);
                dl->AddRectFilled(ImVec2(pos.x + 1.0f, top), ImVec2(trackMax.x - 1.0f, top + segH), col);
            }
            dl->AddRect(pos, trackMax, IM_COL32(70, 70, 75, 255));

            const char* label = slotLabel(static_cast<Device>(d), static_cast<uint8_t>(s));
            const ImVec2 size = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(pos.x + (kBarWidth - size.x) * 0.5f, trackMax.y + 2.0f),
                        IM_COL32(200, 200, 200, 255), label);
        }

        // 直接描いたぶんカーソルを進める。
        ImGui::Dummy(ImVec2(static_cast<float>(column) * (kBarWidth + kBarGap),
                            kBarHeight + kLabelH + kRowGap));
    }
}

} // namespace y8960
