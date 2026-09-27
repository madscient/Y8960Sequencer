// ソフトウェアエンベロープ（`B2` とチャンク `04`）。期待する値の移り変わりは
// Y8960BasicExtension の tools/emu/tcl/softenv.tcl がフォークのレジスタで見ているもの。
// エンベロープ1 は AR 16、DR 20、SL 8、RR 10。

#include "check.h"
#include "seqtest.h"
#include "sequencer.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

using namespace y8960;
using seqtest::RecordingBus;

namespace {

constexpr uint8_t kSsgVolA  = 0x08;
constexpr uint8_t kSccVol1  = 0x8B;
constexpr uint8_t kSccEnable = 0x8F;

constexpr int kV12 = 12 * 8 + 7;
constexpr int kV15 = 15 * 8 + 7;

// 60Hz、T120 では四分音符 48 tick が 30 回の割り込み。
constexpr int kQuarter = 30;

SequenceBlock withEnvelope(Device device, uint8_t channel, std::initializer_list<int> events) {
    SequenceBlock b = seqtest::oneTrack(device, channel, events);
    b.envelopes[1] = {true, 16, 20, 8, 10};
    return b;
}

struct Run {
    RecordingBus bus;
    std::vector<uint8_t> levels;   // 書かれた順。同じ値が続いたものは1つにまとめる
    bool finished = false;
};

// 1本のトラックを鳴らし、pick が拾ったレジスタの値を集める。
void play(Run& r, const SequenceBlock& block, TickRate rate, int interrupts,
          const std::function<bool(const seqtest::Write&, uint8_t&)>& pick,
          int finishedAt = -1) {
    DeviceSet devices(r.bus);
    devices.resetAll();
    Sequencer seq(devices, rate);
    seq.load(0, block);
    r.bus.tick = 0;
    r.bus.writes.clear();
    seq.start(0, 1);
    for (int i = 0; i < interrupts; ++i) {
        r.bus.tick = i;
        seq.interrupt();
        if (i == finishedAt) r.finished = seq.finished();
    }
    for (const auto& w : r.bus.writes) {
        uint8_t v = 0;
        if (!pick(w, v)) continue;
        if (r.levels.empty() || r.levels.back() != v) r.levels.push_back(v);
    }
}

bool ssgA(const seqtest::Write& w, uint8_t& v) {
    if (w.chip != Device::SSGS || w.reg != kSsgVolA) return false;
    v = w.value;
    return true;
}

bool dcsg1Ch0(const seqtest::Write& w, uint8_t& v) {
    if (w.chip != Device::DCSG1 || (w.value & 0xF0) != 0x90) return false;   // ch0 の減衰
    v = static_cast<uint8_t>(15 - (w.value & 0x0F));
    return true;
}

bool scc1(const seqtest::Write& w, uint8_t& v) {
    if (w.chip != Device::SCC || w.reg != kSccVol1) return false;
    v = w.value;
    return true;
}

std::vector<uint8_t> seq(std::initializer_list<int> v) {
    std::vector<uint8_t> out;
    for (int x : v) out.push_back(static_cast<uint8_t>(x));
    return out;
}

std::vector<uint8_t> ramp(int from, int to) {
    std::vector<uint8_t> out;
    for (int x = from; x <= to; ++x) out.push_back(static_cast<uint8_t>(x));
    return out;
}

std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

bool same(const char* name, const std::vector<uint8_t>& got, const std::vector<uint8_t>& want) {
    if (got == want) return true;
    std::printf("  %s:", name);
    for (uint8_t v : got) std::printf(" %u", v);
    std::printf("\n  want:");
    for (uint8_t v : want) std::printf(" %u", v);
    std::printf("\n");
    return false;
}

// 最初に value が書かれた割り込み。after より後ろだけを見る。
int when(const RecordingBus& bus, const std::function<bool(const seqtest::Write&, uint8_t&)>& pick,
         uint8_t value, int after = -1) {
    for (const auto& w : bus.writes) {
        uint8_t v = 0;
        if (w.tick > after && pick(w, v) && v == value) return w.tick;
    }
    return -1;
}

bool near(int a, int b, int slack = 2) { return a >= b - slack && a <= b + slack; }

} // namespace

int main() {
    // SSGS `@E1V15L2O4CR1`。アタック 15 コマ、ディケイは SL と同じ値でもう1段下げて
    // 8 で止まり、休符でリリースが 8 から 0 まで 24 コマ。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 96, 0x0C, 192}),
             TickRate::Vdp60, 200, ssgA);
        CHECK(same("SSGS V15", r.levels, concat({ramp(0, 15), seq({13, 11, 9}), {8, 7, 6, 5, 4, 3, 2, 1, 0}})));
        CHECK(near(when(r.bus, ssgA, 15), 14));
        const int off = kQuarter * 2;
        CHECK(near(when(r.bus, ssgA, 0, off - 1), off + 24));
    }

    // DCSG と SCC は `V12`。値から 3 を引いたものが出る。
    {
        const auto want = concat({ramp(0, 12), seq({10, 8, 6, 5, 4, 3, 2, 1, 0})});
        Run d;
        play(d, withEnvelope(Device::DCSG1, 0, {0xB2, 1, 0x81, kV12, 0x00, 96, 0x0C, 192}),
             TickRate::Vdp60, 200, dcsg1Ch0);
        CHECK(same("DCSG V12", d.levels, want));

        // SCC の音量レジスタはキーが上でも V を持つので、ROM の試験と同じく許可ビットが
        // 立っているあいだだけを数える。
        Run s;
        play(s, withEnvelope(Device::SCC, 1, {0xB2, 1, 0x81, kV12, 0x00, 96, 0x0C, 192}),
             TickRate::Vdp60, 200, scc1);
        std::vector<uint8_t> heard;
        uint8_t enable = 0, vol = 0;
        for (const auto& w : s.bus.writes) {
            if (w.chip != Device::SCC) continue;
            if (w.reg == kSccEnable) enable = w.value;
            else if (w.reg == kSccVol1) vol = w.value;
            else continue;
            const uint8_t v = (enable & 0x02) ? vol : 0;
            if (heard.empty() || heard.back() != v) heard.push_back(v);
        }
        CHECK(same("SCC V12", heard, want));
        // リリースのあいだ許可ビットは立ったまま。
        bool dropped = false;
        for (const auto& w : s.bus.writes) {
            if (w.chip == Device::SCC && w.reg == kSccEnable && w.tick > 0 && !(w.value & 0x02)) dropped = true;
        }
        CHECK(!dropped);
    }

    // `@E1V15L16O4CCR1`。2音目でアタックをやり直す。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 12, 0x00, 12, 0x0C, 192}),
             TickRate::Vdp60, 200, ssgA);
        // 2音目がどこまで上がるかは音長 12 tick（7.5 回の割り込み）の位相で 7 か 8 になる。
        // ROM の試験と同じく、途中まで上がった値からリリースを経ずに 0 へ落ちることを見る。
        const auto& v = r.levels;
        int drops = 0;
        for (size_t i = 1; i < v.size(); ++i) drops += (v[i] == 0 && v[i - 1] > 3);
        CHECK(drops == 1);
        CHECK(v.size() > 9 && v[8] == 8 && v[9] == 0);
    }

    // アタックの途中でキーオフすると、リリースは SL からではなくその時点の値から下がる。
    // 8 tick（5 回の割り込み）で 5 まで上がったところで休符。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 8, 0x0C, 192}),
             TickRate::Vdp60, 100, ssgA);
        CHECK(same("SSGS release mid-attack", r.levels, concat({ramp(0, 5), seq({4, 3, 2, 1, 0})})));
    }

    // `V15L4O4C&@E1CR1`。タイの途中で選ぶとその場でアタックし、タイの先の音符は
    // アタックをやり直さない。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0,
                             {0x81, kV15, 0x00, 48, 0x45, 0xB2, 1, 0x00, 48, 0x0C, 192}),
             TickRate::Vdp60, 200, ssgA);
        const size_t head = std::min<size_t>(r.levels.size(), 5);
        CHECK(same("SSGS tie", std::vector<uint8_t>(r.levels.begin(), r.levels.begin() + head), seq({0, 15, 0, 1, 2})));
        CHECK(near(when(r.bus, ssgA, 0, 1), kQuarter));
        // 2つ目の 0 のあとは 15 まで上がってから下がる。途中で 0 に戻らない。
        int zeros = 0;
        for (size_t i = 0; i + 1 < r.levels.size(); ++i) zeros += (r.levels[i] == 0);
        CHECK(zeros == 2);
    }

    // 割り込みの周期によらず 1/60 秒で進む。200Hz でもアタックは約 0.25 秒（約 50 回）。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 96, 0x0C, 192}),
             TickRate::Hz200, 600, ssgA);
        CHECK(same("SSGS 200Hz", r.levels, concat({ramp(0, 15), seq({13, 11, 9}), {8, 7, 6, 5, 4, 3, 2, 1, 0}})));
        const int top = when(r.bus, ssgA, 15);
        if (!near(top, 50)) std::printf("  200Hz: 15 at %d\n", top);
        CHECK(near(top, 50));
    }

    // シーケンスが終わってもリリースは続く。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 96}),
             TickRate::Vdp60, 120, ssgA, kQuarter * 2 + 2);
        CHECK(r.finished);
        CHECK(r.levels.back() == 0);
        CHECK(near(when(r.bus, ssgA, 0, kQuarter * 2), kQuarter * 2 + 24));
    }

    // `@E0` はその場で V の音量に戻す。SCC はキーが離れていれば許可ビットを落とす。
    {
        Run r;
        // 音符（Q8 で休符まで鳴る）の途中で @E0、休符のあいだにもう一度 @E1 と @E0。
        play(r, withEnvelope(Device::SCC, 1,
                             {0xB2, 1, 0x81, kV12, 0x00, 24, 0xB2, 0, 0x0E, 24, 0x0C, 24,
                              0xB2, 1, 0xB2, 0, 0x0C, 48}),
             TickRate::Vdp60, 120, scc1);
        CHECK(!r.levels.empty());
        // @E0 で V12 がそのまま出る。
        CHECK(when(r.bus, scc1, 12) >= 0);
        const int offTick = when(r.bus, scc1, 12);
        bool enableDropped = false;
        for (const auto& w : r.bus.writes) {
            if (w.chip == Device::SCC && w.reg == kSccEnable && w.tick > offTick && !(w.value & 0x02)) {
                enableDropped = true;
            }
        }
        CHECK(enableDropped);
    }

    // SSGS の @16 以上はソフトウェアエンベロープを止め、@E はハードウェアの選択を落とす。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0,
                             {0x81, kV15, 0x82, 0x1F, 0xB2, 1, 0x00, 24, 0x82, 0x1F, 0x0E, 24}),
             TickRate::Vdp60, 60, ssgA);
        // @E1 のあいだはソフトウェアの値（bit4 無し）が上がっていき、音符の途中の @31 で
        // 1Fh になったら、キーオフまでそのまま動かない。
        const auto& v = r.levels;
        size_t hw = 0;
        while (hw < v.size() && v[hw] != 0x1F) ++hw;
        CHECK(hw < v.size());
        CHECK(hw >= 2 && v[hw - 1] > 1 && !(v[hw - 1] & 0x10));
        for (size_t i = hw + 1; i < v.size(); ++i) CHECK(v[i] == 0);
    }

    // 逆に @16 以上のあとの @E はハードウェアの選択を落とす（`@16@e1V15L2O4CR1`）。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0x82, 0x10, 0xB2, 1, 0x81, kV15, 0x00, 96, 0x0C, 192}),
             TickRate::Vdp60, 200, ssgA);
        CHECK(same("SSGS @16 @E1", r.levels, concat({ramp(0, 15), seq({13, 11, 9}), {8, 7, 6, 5, 4, 3, 2, 1, 0}})));
    }

    // 周の頭で `@E0` に戻る。1周目の終わりで選んだエンベロープは、2周目の頭の音符には
    // 効かず、V15 がそのまま出る。戻らなければ 2周目の音符はアタックから始まる。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0x81, kV15, 0x00, 48, 0xB2, 1, 0x0E, 1}),
             TickRate::Vdp60, 40, ssgA);
        // 1周目の終わり（49 tick）で選んだのでアタックが始まっている。
        CHECK(when(r.bus, ssgA, 1) >= 0);
    }
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        Sequencer sq(devices, TickRate::Vdp60);
        const SequenceBlock b = withEnvelope(Device::SSGS, 0, {0x81, kV15, 0x00, 48, 0xB2, 1, 0x0E, 1});
        sq.load(0, b);
        bus.tick = 0;
        sq.start(0, 2);
        for (int i = 0; i < 40; ++i) { bus.tick = i; sq.interrupt(); }
        // 2周目の頭は 49 tick、割り込み 30。そこで 15 が出る。
        const int second = when(bus, ssgA, 15, kQuarter - 1);
        if (second != kQuarter) std::printf("  rewind: 15 at %d\n", second);
        CHECK(second == kQuarter);
    }

    return check::finish("softenv_test");
}
