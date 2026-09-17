#include "check.h"
#include "timing.h"

using namespace y8960;

int main() {
    // TICKINC は tempo * K を 1 ビット右シフトしたもの（timer.asm）。
    {
        const TickIncrement t = tickIncrement(120, TickRate::Hz200);   // 120 * 525 = 63000
        CHECK(t.whole == 0 && t.frac == 31500);
    }
    {
        const TickIncrement t = tickIncrement(255, TickRate::Vdp50);   // 255 * 2097 = 534735
        CHECK(t.whole == 4 && t.frac == (267367 & 0xFFFF));
    }
    {
        const TickIncrement t = tickIncrement(32, TickRate::Vdp60);    // 32 * 1748 = 55936
        CHECK(t.whole == 0 && t.frac == 27968);
    }

    // 割り込みの間隔の合計は、端数を持ち越すので理論値からずれない。
    {
        constexpr uint32_t sr = 44100;
        InterruptClock c(TickRate::Hz100, sr);
        constexpr uint64_t count = 100000;
        uint64_t total = 0;
        for (uint64_t i = 0; i < count; ++i) total += c.nextInterval();
        const InterruptPeriod p = interruptPeriod(TickRate::Hz100);
        CHECK(total == count * p.num * sr / p.den);
    }
    {
        InterruptClock c(TickRate::Vdp60, 48000);
        CHECK(c.nextInterval() == 800);
        CHECK(c.nextInterval() == 800);
    }

    return check::finish("timing_test");
}
