#include "pitch.h"

#include <cmath>

namespace y8960 {

namespace {

constexpr uint16_t kFnumStored = 0x0FFF;   // 表が持つのは 12 ビット
constexpr int kOpllShift = 3;              // OPLL の 9 ビットまで下げる
constexpr int kOpl2Shift = 2;              // OPL2 は 1 ビット広い

constexpr uint16_t kSsgDivMax  = 4095;
constexpr uint16_t kDcsgDivMax = 1023;
constexpr uint16_t kSccDivMax  = 4096;     // レジスタは 1 少ない値を持つ

// 表はどのオクターブでも同じなので、索引が表から出たぶんはオクターブが動く。
int foldIndex(int& octave, int index) {
    while (index < 0) {
        index += kFreqSteps;
        --octave;
    }
    while (index >= kFreqSteps) {
        index -= kFreqSteps;
        ++octave;
    }
    return index;
}

// 1つ手前で止めて、1を足してからもう1度シフトする ―― 四捨五入。
uint16_t shiftRound(uint32_t v, int shift) {
    v >>= (shift - 1);
    return static_cast<uint16_t>((v + 1) >> 1);
}

} // namespace

OplPitch oplPitch(bool isOpl2, uint8_t note, int16_t bendSteps) {
    const int shift    = isOpl2 ? kOpl2Shift : kOpllShift;
    int       octave   = note / 12;
    const int semitone = note % 12;

    const int index = foldIndex(octave, semitone * kFreqSemitone + bendSteps);
    const uint16_t entry = freqEntry(FreqFamily::Opl, index);

    OplPitch out;
    int block = octave;
    if (entry & kFnumAdj) --block;        // この半音は1つ下のブロックで作ってある

    if (block >= 8) {
        // O8 の上端。ブロック 7 でこのチップが持てる最大の F-Number を出す。
        out.fnum = static_cast<uint16_t>(kFnumStored >> shift);
        out.block = 7;
        out.outOfRange = true;
        return out;
    }
    // ブロックが 0 より下（O0 C〜F+ と、ベンドでさらに下げたもの）は、足りない
    // ブロックの数だけ F-Number を余計にシフトし、ブロックは 0 にする。ROM の
    // FNUM_OPLOF と同じく範囲外の印は立てない ―― 下げるほど粗くなり、やがて 0 になる。
    int total = shift;
    if (block < 0) {
        total -= block;
        block = 0;
    }
    out.fnum  = shiftRound(entry & kFnumStored, total);
    out.block = static_cast<uint8_t>(block);
    return out;
}

DivPitch divPitch(Device device, uint8_t note, int16_t bendSteps) {
    int       octave   = note / 12;
    const int semitone = note % 12;

    const int index = foldIndex(octave, semitone * kFreqSemitone + bendSteps);
    const uint32_t stored = freqEntry(FreqFamily::Div, index);

    const uint16_t max = (device == Device::SSGS)  ? kSsgDivMax
                       : (device == Device::SCC)   ? kSccDivMax
                                                   : kDcsgDivMax;
    DivPitch out;
    if (octave < -1) {
        // 表の 1 オクターブ下より低い。分周値はもう出せないので下端で鳴らす。
        out.divisor = max;
        out.outOfRange = true;
        return out;
    }
    // 表はオクターブ 1 の分周値の 16 倍。オクターブが上がるぶん右へ寄せて丸める。
    const uint32_t v = shiftRound(stored, octave + 3);
    if (v > max) {
        out.divisor = max;
        out.outOfRange = true;
    } else if (v == 0) {
        out.divisor = 1;               // チップが数えられるいちばん短い周期
        out.outOfRange = true;
    } else {
        out.divisor = static_cast<uint16_t>(v);
    }
    return out;
}

double noteFrequency(uint8_t note, int16_t bendSteps) {
    constexpr int kA4 = 57;   // O4 A。音符番号は O0 C から数える
    const double semis = (static_cast<int>(note) - kA4) + bendSteps / double(kFreqSemitone);
    return 440.0 * std::pow(2.0, semis / 12.0);
}

// F-Number が 11bit に収まるいちばん小さいブロックを選ぶ ―― 細かさがいちばん大きい。
OpnPitch opnPitch(uint32_t clock, uint8_t note, int16_t bendSteps) {
    constexpr double kMax = 2047.0;
    const double fs = clock / 144.0;
    double fnum = noteFrequency(note, bendSteps) * (1 << 21) / fs;
    OpnPitch out;
    while (std::lround(fnum) > kMax && out.block < 7) {
        fnum /= 2.0;
        ++out.block;
    }
    const long v = std::lround(fnum);
    out.fnum = static_cast<uint16_t>(v > kMax ? kMax : v);
    return out;
}

// KC のノートコードは C# から始まり、C で終わる。3・7・11・15 は使わない。
OpmPitch opmPitch(uint8_t note, int16_t bendSteps) {
    static constexpr uint8_t kCode[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
    // C# を 0 にした 1/64 半音の歩数。オクターブ 0 の C# が O0 C# にあたる
    // （3.579545MHz で KC 4Ah・KF 0 が O4 A の 440Hz）。
    int steps = (static_cast<int>(note) - 1) * kFreqSemitone + bendSteps;
    OpmPitch out;
    if (steps < 0) return out;                        // 下端
    int octave = steps / kFreqSteps;
    if (octave > 7) {
        out.kc = static_cast<uint8_t>((7 << 4) | kCode[11]);
        out.kf = kFreqSemitone - 1;
        return out;
    }
    steps %= kFreqSteps;
    out.kc = static_cast<uint8_t>((octave << 4) | kCode[steps / kFreqSemitone]);
    out.kf = static_cast<uint8_t>(steps % kFreqSemitone);
    return out;
}

// SSG の入力はマスタークロックの 1/4 で、トーンはその 1/16 を分周する。
uint16_t opnSsgPeriod(uint32_t clock, uint8_t note, int16_t bendSteps) {
    const long v = std::lround(clock / 64.0 / noteFrequency(note, bendSteps));
    return static_cast<uint16_t>(v < 1 ? 1 : (v > kSsgDivMax ? kSsgDivMax : v));
}

} // namespace y8960
