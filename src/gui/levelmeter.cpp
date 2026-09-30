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
constexpr float kLeverWidth = 36.0f;
constexpr float kLeverGap   = 10.0f;

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
    "OPL3", "OPM", "OPNA", "OPNB",
};

// SSGS は2つの SSG からできているので、セット番号とその中の名前で呼ぶ。
const char* const kSsgsLabels[6]  = {"1A", "1B", "1C", "2A", "2B", "2C"};
const char* const kDcsgLabels[4]  = {"1", "2", "3", "N"};
const char* const kNumberLabels[18] = {"1", "2", "3", "4", "5", "6", "7", "8", "9",
                                       "10", "11", "12", "13", "14", "15", "16", "17", "18"};
// OPL3 の 4OP のチャンネル 18-23。
const char* const kQuadLabels[6]  = {"Q1", "Q2", "Q3", "Q4", "Q5", "Q6"};
// OPNA・OPNB の SSG（チャンネル 6-8）。
const char* const kOpnSsgLabels[3] = {"SA", "SB", "SC"};
// リズムの楽器。枠の並び（activity.h）に合わせる。
const char* const kOplRhythmLabels[5]  = {"BD", "SD", "TM", "CY", "HH"};
const char* const kOpnaRhythmLabels[6] = {"BD", "SD", "CY", "HH", "TM", "RM"};
const char* const kOpnbRhythmLabels[6] = {"A1", "A2", "A3", "A4", "A5", "A6"};

const char* slotLabel(Device device, uint8_t slot) {
    if (slot >= kRhythmSlotFirst) {
        const int i = slot - kRhythmSlotFirst;
        if (device == Device::OPNA) return kOpnaRhythmLabels[i];
        if (device == Device::OPNB) return kOpnbRhythmLabels[i];
        return kOplRhythmLabels[i];
    }
    if (isAdpcmChannel(device, slot)) return "PCM";
    switch (device) {
    case Device::SSGS:  return kSsgsLabels[slot];
    case Device::DCSG1:
    case Device::DCSG2: return kDcsgLabels[slot];
    case Device::OPL3:  return (slot >= kOpl3FourOpFirst) ? kQuadLabels[slot - kOpl3FourOpFirst] : kNumberLabels[slot];
    case Device::OPNA:
    case Device::OPNB:  return (slot >= kOpnSsgFirst) ? kOpnSsgLabels[slot - kOpnSsgFirst] : kNumberLabels[slot];
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

void useRhythmSlots(std::array<bool, kActivitySlots>& used, Device device) {
    for (int i = 0; i < rhythmInstruments(device); ++i) used[static_cast<size_t>(kRhythmSlotFirst + i)] = true;
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
            for (uint8_t c = 0; c < kMaxChannels; ++c) {
                if (channelExists(device, c) && !isRhythmChannel(device, c)) slotUsed_[d][c] = true;
            }
            useRhythmSlots(slotUsed_[d], device);
        }
    } else if (haveBlock) {
        for (const TrackData& t : block.tracks) {
            if (!t.assigned) continue;
            const size_t d = static_cast<size_t>(t.device);
            bandUsed_[d] = true;
            if (isRhythmChannel(t.device, t.channel)) {
                useRhythmSlots(slotUsed_[d], t.device);
            } else if (t.channel < kRhythmSlotFirst) {
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

uint16_t MuteState::trackMask(const SequenceBlock& block) const {
    uint16_t mask = 0;
    for (int i = 0; i < kTrackCount; ++i) {
        const TrackData& t = block.tracks[static_cast<size_t>(i)];
        if (t.assigned && muted(t.device, t.channel)) mask = static_cast<uint16_t>(mask | (1u << i));
    }
    return mask;
}

float gainFromDb(float db) {
    return (db <= kGainDbMin) ? 0.0f : std::pow(10.0f, db / 20.0f);
}

bool LevelMeter::draw(MuteState& mutes, std::array<float, kDeviceCount>& gainDb) const {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float segH = (kBarHeight - kSegGap * (kSegments - 1)) / kSegments;
    bool changed = false;

    for (size_t d = 0; d < kDeviceCount; ++d) {
        if (!bandUsed_[d]) continue;
        ImGui::PushID(static_cast<int>(d));
        if (ImGui::Checkbox("Mute", &mutes.chip[d])) changed = true;
        ImGui::SameLine();
        ImGui::SeparatorText(kBandNames[d]);
        const ImVec2 band = ImGui::GetCursorScreenPos();

        // 下端は -inf（0 倍）として扱う。右クリックで 0 dB に戻す。
        float& db = gainDb[d];
        ImGui::VSliderFloat("##gain", ImVec2(kLeverWidth, kBarHeight), &db, kGainDbMin, kGainDbMax,
                            (db <= kGainDbMin) ? "-inf" : "%+.0f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) db = 0.0f;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s gain (dB). Right-click to reset.", kBandNames[d]);
        }
        const char* unit = "dB";
        const ImVec2 unitSize = ImGui::CalcTextSize(unit);
        dl->AddText(ImVec2(band.x + (kLeverWidth - unitSize.x) * 0.5f, band.y + kBarHeight + 2.0f),
                    IM_COL32(200, 200, 200, 255), unit);

        const ImVec2 origin(band.x + kLeverWidth + kLeverGap, band.y);

        int column = 0;
        for (size_t s = 0; s < kActivitySlots; ++s) {
            if (!slotUsed_[d][s]) continue;
            const Bar& bar = bars_[d][s];
            const ImVec2 pos(origin.x + static_cast<float>(column) * (kBarWidth + kBarGap), origin.y);
            const ImVec2 trackMax(pos.x + kBarWidth, pos.y + kBarHeight);
            ++column;

            // リズムの楽器は、リズムチャンネルの1本として黙る。
            const Device device = static_cast<Device>(d);
            const uint8_t channel = (s >= kRhythmSlotFirst) ? static_cast<uint8_t>(rhythmChannel(device))
                                                            : static_cast<uint8_t>(s);
            const bool muted = mutes.muted(device, channel);

            // バーをクリックすると、そのチャンネルのミュートが切り替わる。
            ImGui::SetCursorScreenPos(pos);
            ImGui::PushID(static_cast<int>(s));
            if (ImGui::InvisibleButton("bar", ImVec2(kBarWidth, kBarHeight + kLabelH))) {
                bool& flag = mutes.channel[d][channel];
                flag = !flag;
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(muted ? "Click to unmute" : "Click to mute");
            ImGui::PopID();

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
            if (muted) dl->AddRectFilled(pos, trackMax, IM_COL32(0, 0, 0, 170));
            dl->AddRect(pos, trackMax, muted ? IM_COL32(200, 60, 60, 255) : IM_COL32(70, 70, 75, 255));

            const char* label = slotLabel(device, static_cast<uint8_t>(s));
            const ImVec2 size = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(pos.x + (kBarWidth - size.x) * 0.5f, trackMax.y + 2.0f),
                        muted ? IM_COL32(230, 80, 80, 255) : IM_COL32(200, 200, 200, 255), label);
        }

        // 直接描いたぶんカーソルを進める。
        ImGui::SetCursorScreenPos(band);
        ImGui::Dummy(ImVec2(kLeverWidth + kLeverGap + static_cast<float>(column) * (kBarWidth + kBarGap),
                            kBarHeight + kLabelH + kRowGap));
        ImGui::PopID();
    }
    return changed;
}

} // namespace y8960
