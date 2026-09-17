#pragma once
// シーケンサを進める割り込みの周期と、テンポから tick への換算。
// Y8960BasicExtension の src/resident/timer.asm を写したもの。

#include <cstdint>

namespace y8960 {

enum class TickRate : uint8_t {
    Vdp60,   // VDP 割り込み、60Hz
    Vdp50,   // VDP 割り込み、50Hz
    Hz100,   // MSX-TIMER、約 99.87Hz
    Hz200,   // MSX-TIMER、約 199.75Hz
};

// 1 回の割り込みの長さ。num / den 秒。
struct InterruptPeriod {
    uint64_t num;
    uint64_t den;
};

InterruptPeriod interruptPeriod(TickRate rate);

// 1 回の割り込みで進む tick。整数部と、65536 分の小数部。
struct TickIncrement {
    uint8_t  whole;
    uint16_t frac;
};

constexpr uint8_t kDefaultTempo = 120;

TickIncrement tickIncrement(uint8_t tempo, TickRate rate);

// 割り込みの時刻をサンプル数に直す。端数を持ち越すので、長く鳴らしてもずれない。
class InterruptClock {
public:
    InterruptClock(TickRate rate, uint32_t sampleRate);
    // 次の割り込みまでのサンプル数を返し、その割り込みの分だけ時刻を進める。
    uint32_t nextInterval();

private:
    uint64_t step_;   // 1 回の割り込みで足す量（den を 1 サンプルとする単位）
    uint64_t den_;
    uint64_t acc_ = 0;
};

} // namespace y8960
