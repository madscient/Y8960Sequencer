#pragma once
// チャンネルごとの発音の様子。画面のレベルメーターがこれを読む。
//
// エミュレータから取れるのはチップ単位の音だけなので、チャンネルごとの値は
// シーケンサが出すキーオン・キーオフ・音量から作る（FITOM_X の LevelMeterPanel と
// 同じ考え方）。音声のスレッドが書き、画面のスレッドが読む。

#include "block.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace y8960 {

// 0-9 がチャンネル、10-14 がリズムの5つの楽器。
constexpr int kActivitySlots = 15;
constexpr uint8_t kRhythmSlotFirst = 10;

// リズムの楽器の並び（bytecode.md のビットと同じ順）。
enum class RhythmInstrument : uint8_t { BassDrum = 0, Snare, Tom, Cymbal, HiHat };

class ChannelActivity {
public:
    struct Slot {
        std::atomic<uint32_t> noteOnSeq{0};   // キーオンのたびに増える
        std::atomic<uint8_t>  level{0};       // そのキーオンの音量 0-127
        std::atomic<bool>     sounding{false};
    };

    void noteOn(Device device, uint8_t channel, uint8_t level) {
        Slot& s = slot(device, channel);
        s.level.store(level, std::memory_order_relaxed);
        s.sounding.store(true, std::memory_order_relaxed);
        // 同じ音量で鳴らし直したときも拾えるように、番号を増やす。
        s.noteOnSeq.fetch_add(1, std::memory_order_relaxed);
    }

    void noteOff(Device device, uint8_t channel) {
        slot(device, channel).sounding.store(false, std::memory_order_relaxed);
    }

    void allOff() {
        for (auto& device : slots_) {
            for (auto& s : device) s.sounding.store(false, std::memory_order_relaxed);
        }
    }

    const Slot& read(Device device, uint8_t channel) const { return slot(device, channel); }

private:
    Slot& slot(Device device, uint8_t channel) {
        return slots_[static_cast<size_t>(device)][index(channel)];
    }
    const Slot& slot(Device device, uint8_t channel) const {
        return slots_[static_cast<size_t>(device)][index(channel)];
    }
    static size_t index(uint8_t channel) {
        return (channel < kActivitySlots) ? channel : 0;
    }

    std::array<std::array<Slot, kActivitySlots>, kDeviceCount> slots_{};
};

} // namespace y8960
