// 音域の端でチップに書く値。期待値は Y8960BasicExtension の tools/emu/tcl/octave.tcl が
// フォークのレジスタで見ている値。

#include "check.h"
#include "pitch.h"

#include <cstdio>

using namespace y8960;

namespace {

// 音符番号は O0 C から数える。
constexpr uint8_t note(int octave, int semitone) { return static_cast<uint8_t>(octave * 12 + semitone); }

bool opl(bool isOpl2, uint8_t number, int block, int fnum) {
    const OplPitch p = oplPitch(isOpl2, number, 0);
    if (p.block == block && p.fnum == fnum) return true;
    std::printf("  %s note %u: block %u fnum %u, want %d %d\n", isOpl2 ? "OPL2" : "OPLL", number,
                p.block, p.fnum, block, fnum);
    return false;
}

bool ssg(uint8_t number, int divisor) {
    const DivPitch p = divPitch(Device::SSGS, number, 0);
    if (p.divisor == divisor) return true;
    std::printf("  SSGS note %u: %u, want %d\n", number, p.divisor, divisor);
    return false;
}

} // namespace

int main() {
    // OPLL。ブロックが 0 より下のぶんは F-Number のシフトで受ける。
    CHECK(opl(false, note(1, 0), 0, 345));
    CHECK(opl(false, note(0, 0), 0, 172));
    CHECK(opl(false, note(0, 6), 0, 244));
    CHECK(opl(false, note(0, 7), 0, 258));
    CHECK(opl(false, note(4, 0), 3, 345));       // N36
    // 上端はブロック 7 の最大の F-Number に張りつく。
    CHECK(opl(false, note(8, 7), 7, 511));
    CHECK(opl(false, note(9, 0), 7, 511));
    CHECK(opl(false, note(9, 11), 7, 511));
    CHECK(opl(false, 127 + 12, 7, 511));         // N127

    // OPL2 は F-Number が1ビット広い。
    CHECK(opl(true, note(1, 0), 0, 690));
    CHECK(opl(true, note(0, 0), 0, 345));

    // ベンドでさらに下げても、ブロック 0 のまま F-Number が細るだけで折り返さない。
    {
        const OplPitch p = oplPitch(false, note(0, 0), -8 * 768);
        CHECK(p.block == 0);
        CHECK(p.fnum <= 1);
    }

    // 分周器系は欄の端に張りつく。
    CHECK(ssg(note(0, 9), 4068));
    CHECK(ssg(note(0, 8), 4095));
    CHECK(ssg(note(0, 0), 4095));
    CHECK(ssg(note(9, 0), 13));
    CHECK(ssg(note(9, 11), 7));

    return check::finish("pitch_test");
}
