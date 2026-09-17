#include "pitch.h"

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
    if (block < 0) {
        // O1 より下。間違った音程より無音のほうがまし。
        out.fnum = 0;
        out.block = 0;
        out.outOfRange = true;
        return out;
    }
    out.fnum  = shiftRound(entry & kFnumStored, shift);
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

} // namespace y8960
