#include "timing.h"

namespace y8960 {

namespace {

// MSX-TIMER の基準クロックと、分解能 1 の単位（timer.asm の TMRTAB、
// Y8960BasicExtension の doc/hardware.md「MSX-TIMER」）。
constexpr uint64_t kTimerClock = 85909080;
constexpr uint64_t kTimerUnit  = 4096;   // 2^(10 + 2*1)
constexpr uint64_t kCompare100 = 209;
constexpr uint64_t kCompare200 = 104;

// timer.asm の TICKKTAB。2 * 65536 * 48 / 60 / 周期。
constexpr uint32_t kTickK[] = {1748, 2097, 1050, 525};

} // namespace

InterruptPeriod interruptPeriod(TickRate rate) {
    switch (rate) {
    // ROM の TICKKTAB は VDP 割り込みを 60Hz・50Hz ちょうどとして作られているので、
    // それに合わせる。実機の VDP 割り込みの正確な周期とは比べていない（未検証）。
    case TickRate::Vdp60: return {1, 60};
    case TickRate::Vdp50: return {1, 50};
    case TickRate::Hz100: return {(kCompare100 + 1) * kTimerUnit, kTimerClock};
    case TickRate::Hz200: return {(kCompare200 + 1) * kTimerUnit, kTimerClock};
    }
    return {1, 60};
}

TickIncrement tickIncrement(uint8_t tempo, TickRate rate) {
    const uint32_t product = static_cast<uint32_t>(tempo) * kTickK[static_cast<int>(rate)];
    const uint32_t half = product >> 1;
    return {static_cast<uint8_t>(half >> 16), static_cast<uint16_t>(half & 0xFFFF)};
}

InterruptClock::InterruptClock(TickRate rate, uint32_t sampleRate) {
    const InterruptPeriod p = interruptPeriod(rate);
    step_ = p.num * sampleRate;
    den_  = p.den;
}

uint32_t InterruptClock::nextInterval() {
    acc_ += step_;
    const uint64_t n = acc_ / den_;
    acc_ -= n * den_;
    return static_cast<uint32_t>(n);
}

} // namespace y8960
