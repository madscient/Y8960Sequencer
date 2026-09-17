#pragma once
// シーケンサの試験の道具。レジスタ書き込みを記録するバスと、ブロックの組み立て。

#include "block.h"
#include "chips.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace seqtest {

struct Write {
    int           tick = 0;      // 何回目の割り込みで書かれたか
    y8960::Device chip = y8960::Device::SSGS;
    uint8_t       reg  = 0;
    uint8_t       value = 0;
};

class RecordingBus final : public y8960::ChipBus {
public:
    void write(y8960::Device chip, uint8_t reg, uint8_t value) override {
        writes.push_back({tick, chip, reg, value});
    }

    // 指定のレジスタに書かれた値を、書かれた順に返す。
    std::vector<uint8_t> values(y8960::Device chip, uint8_t reg) const {
        std::vector<uint8_t> out;
        for (const Write& w : writes) {
            if (w.chip == chip && w.reg == reg) out.push_back(w.value);
        }
        return out;
    }

    // 指定の値が最初に書かれた tick。after より後ろだけを見る。無ければ -1。
    int firstTick(y8960::Device chip, uint8_t reg, uint8_t value, int after = -1) const {
        for (const Write& w : writes) {
            if (w.tick <= after) continue;
            if (w.chip == chip && w.reg == reg && w.value == value) return w.tick;
        }
        return -1;
    }

    // 指定の値が書かれた回数。
    int count(y8960::Device chip, uint8_t reg, uint8_t value) const {
        int n = 0;
        for (const Write& w : writes) {
            if (w.chip == chip && w.reg == reg && w.value == value) ++n;
        }
        return n;
    }

    int tick = 0;
    std::vector<Write> writes;
};

// トラック1本のブロックを組み立てる。
inline y8960::SequenceBlock oneTrack(y8960::Device device, uint8_t channel,
                                     std::initializer_list<int> events) {
    y8960::SequenceBlock b;
    b.version = 1;
    y8960::TrackData& t = b.tracks[0];
    t.assigned = true;
    t.device   = device;
    t.channel  = channel;
    for (int e : events) t.events.push_back(static_cast<uint8_t>(e));
    t.events.push_back(0xFF);
    return b;
}

} // namespace seqtest
