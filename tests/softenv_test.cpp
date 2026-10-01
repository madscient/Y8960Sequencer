// ソフトウェアエンベロープ（`B2` とチャンク `04`）。期待する値の移り変わりは
// Y8960BasicExtension の tools/emu/tcl/softenv.tcl がフォークのレジスタで見ているもの。
// エンベロープ1 は AR 16、DR 20、SL 8、RR 10（MuSICA の 0-32）。チャンク 04 はそれを
// コマと変化の生のバイトで持つ（11h 12h 08h 31h。ROM の softenv.tcl と同じ）。キーオンと
// キーオフの割り込みがそれぞれアタックとリリースの1コマ目になる。

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

constexpr int kV12 = 12 * 4 + 67;
constexpr int kV15 = 15 * 4 + 67;

// 60Hz、T120 では四分音符 48 tick が 30 回の割り込み。
constexpr int kQuarter = 30;

SequenceBlock withEnvelope(Device device, uint8_t channel, std::initializer_list<int> events) {
    SequenceBlock b = seqtest::oneTrack(device, channel, events);
    b.envelopes[1] = {true, 0x11, 0x12, 8, 0x31};
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

// SCC のドライバが書く値。PSG の目盛りの 0-15 を表で直し、同じ値が続けば1つにまとめる。
std::vector<uint8_t> sccScale(const std::vector<uint8_t>& levels) {
    static constexpr uint8_t kTable[16] = {0, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 6, 8, 10, 12, 15};
    std::vector<uint8_t> out;
    for (uint8_t v : levels) {
        const uint8_t x = kTable[v & 0x0F];
        if (out.empty() || out.back() != x) out.push_back(x);
    }
    return out;
}

} // namespace

int main() {
    // SSGS `@E1V15L2O4CR1`。アタック 15 コマ、ディケイは SL と同じ値でもう1段下げて
    // 8 で止まり、休符でリリースが 8 から 0 まで 24 コマ。どちらもキーの瞬間が1コマ目。
    // キーオンは start の中（割り込み 0 より前）なので 15 は割り込み 13。96 tick を
    // 読み終えるのは割り込み 59 で、休符のキーオフはそこ。0 はその 23 コマ後。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 96, 0x0C, 192}),
             TickRate::Vdp60, 200, ssgA);
        CHECK(same("SSGS V15", r.levels, concat({ramp(0, 15), seq({13, 11, 9}), {8, 7, 6, 5, 4, 3, 2, 1, 0}})));
        const int top = when(r.bus, ssgA, 15);
        if (top != 13) std::printf("  60Hz: 15 at %d\n", top);
        CHECK(top == 13);
        const int off = kQuarter * 2 - 1;
        const int silent = when(r.bus, ssgA, 0, off - 1);
        if (silent != off + 23) std::printf("  60Hz: 0 at %d\n", silent);
        CHECK(silent == off + 23);
    }

    // DCSG と SCC は `V12`。値から 3 を引いたものが出る。SCC はそれを表で直して書く。
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
        CHECK(same("SCC V12", heard, sccScale(want)));
        // リリースのあいだ許可ビットは立ったまま。
        bool dropped = false;
        for (const auto& w : s.bus.writes) {
            if (w.chip == Device::SCC && w.reg == kSccEnable && w.tick > 0 && !(w.value & 0x02)) dropped = true;
        }
        CHECK(!dropped);
    }

    // `@E1V15L16O4CCR1`。2音目でアタックをやり直す。やり直した割り込みで1コマ目が
    // 数えられるので、0 ではなく 1 に落ちる。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 12, 0x00, 12, 0x0C, 192}),
             TickRate::Vdp60, 200, ssgA);
        // 2音目がどこまで上がるかは音長 12 tick（7.5 回の割り込み）の位相で変わる。
        // ROM の試験と同じく、途中まで上がった値からリリースを経ずに 1 以下へ落ちることを見る。
        const auto& v = r.levels;
        int drops = 0;
        for (size_t i = 1; i < v.size(); ++i) drops += (v[i] <= 1 && v[i - 1] > 3);
        CHECK(drops == 1);
        // ここでは1音目が割り込み 0-7 の8コマとキーオンの1コマで 9、2音目が 1 から始まる。
        CHECK(same("SSGS C C", std::vector<uint8_t>(v.begin(), v.begin() + std::min<size_t>(v.size(), 12)),
                   concat({ramp(0, 9), seq({1, 2})})));
    }

    // アタックの途中でキーオフすると、リリースは SL からではなくその時点の値から下がる。
    // 8 tick（5 回の割り込み）で 6 まで上がったところで休符。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 8, 0x0C, 192}),
             TickRate::Vdp60, 100, ssgA);
        CHECK(same("SSGS release mid-attack", r.levels, concat({ramp(0, 6), seq({5, 4, 3, 2, 1, 0})})));
    }

    // キーオフのたびにリリースを数え直す。Q2 の音符はゲートでキーオフし、そのあとの休符が
    // もう一度キーオフする。エンベロープ2 は AR 32 で即 15、SL 15、RR 0（15 コマで1段）。
    // 数え直せば、休符から次の段までが 14 コマ。数え直さなければゲートからの続きになる。
    {
        SequenceBlock b = seqtest::oneTrack(Device::SSGS, 0,
                                            {0xB2, 2, 0x81, kV15, 0x83, 2, 0x00, 48, 0x0C, 192});
        b.envelopes[2] = {true, 0x1F, 0x1F, 15, 0xF1};   // 0-32 の 32、32、15、0
        Run r;
        play(r, b, TickRate::Vdp60, 120, ssgA);
        const int rest = kQuarter - 1;   // 48 tick を読み終える割り込み
        const int next = when(r.bus, ssgA, 13, rest - 1);
        if (next != rest + 14) std::printf("  re-release: 13 at %d, rest at %d\n", next, rest);
        CHECK(next == rest + 14);
    }

    // `V15L4O4C&@E1CR1`。タイの途中で選ぶとその場でアタックし（その割り込みで 1 になる）、
    // タイの先の音符はアタックをやり直さない。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0,
                             {0x81, kV15, 0x00, 48, 0x45, 0xB2, 1, 0x00, 48, 0x0C, 192}),
             TickRate::Vdp60, 200, ssgA);
        const size_t head = std::min<size_t>(r.levels.size(), 5);
        CHECK(same("SSGS tie", std::vector<uint8_t>(r.levels.begin(), r.levels.begin() + head), seq({0, 15, 1, 2, 3})));
        CHECK(near(when(r.bus, ssgA, 1, 1), kQuarter));
        // アタックは 15 まで上がってから下がる。最後のリリースまで 0 に戻らない。
        int zeros = 0;
        for (size_t i = 0; i + 1 < r.levels.size(); ++i) zeros += (r.levels[i] == 0);
        CHECK(zeros == 1);
    }

    // 割り込みの周期によらず 1/60 秒で進む。200Hz では 15 に着くのがキーオンの 14 コマ後
    // （約 47 回）。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 96, 0x0C, 192}),
             TickRate::Hz200, 600, ssgA);
        CHECK(same("SSGS 200Hz", r.levels, concat({ramp(0, 15), seq({13, 11, 9}), {8, 7, 6, 5, 4, 3, 2, 1, 0}})));
        const int top = when(r.bus, ssgA, 15);
        if (!near(top, 47)) std::printf("  200Hz: 15 at %d\n", top);
        CHECK(near(top, 47));
    }

    // シーケンスが終わってもリリースは続く。
    {
        Run r;
        play(r, withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 96}),
             TickRate::Vdp60, 120, ssgA, kQuarter * 2 + 2);
        CHECK(r.finished);
        CHECK(r.levels.back() == 0);
        CHECK(when(r.bus, ssgA, 0, kQuarter * 2 - 2) == kQuarter * 2 - 1 + 23);
    }

    // `@E0` はその場で V の音量に戻す。SCC はキーが離れていれば許可ビットを落とす。
    {
        Run r;
        // 音符（Q8 で休符まで鳴る）の途中で @E0、休符のあいだにもう一度 @E1 と @E0。
        // @E0 はアタックの途中（表で 1 のところ）に置き、V12 の表の値 8 と見分ける。
        play(r, withEnvelope(Device::SCC, 1,
                             {0xB2, 1, 0x81, kV12, 0x00, 12, 0xB2, 0, 0x0E, 36, 0x0C, 24,
                              0xB2, 1, 0xB2, 0, 0x0C, 48}),
             TickRate::Vdp60, 120, scc1);
        CHECK(!r.levels.empty());
        // @E0 で V12（表で 8）がそのまま出る。それより前にアタックは 8 に届いていない。
        const int offTick = when(r.bus, scc1, 8);
        CHECK(offTick >= 0);
        for (const auto& w : r.bus.writes) {
            if (w.chip == Device::SCC && w.reg == kSccVol1 && w.tick < offTick) CHECK(w.value <= 2);
        }
        bool enableDropped = false;
        for (const auto& w : r.bus.writes) {
            if (w.chip == Device::SCC && w.reg == kSccEnable && w.tick > offTick && !(w.value & 0x02)) {
                enableDropped = true;
            }
        }
        CHECK(enableDropped);
        // 休符のあいだの @E0 は、許可ビットを先に落としてから V の音量を書く。逆だと
        // 2つの書き込みのあいだ V の音量で鳴る（ENVSOFF）。
        std::vector<seqtest::Write> scc;
        for (const auto& w : r.bus.writes) {
            if (w.chip == Device::SCC) scc.push_back(w);
        }
        int lastV = -1;
        for (size_t i = 0; i < scc.size(); ++i) {
            if (scc[i].reg == kSccVol1 && scc[i].value == 8) lastV = static_cast<int>(i);
        }
        CHECK(lastV >= 1);
        if (lastV >= 1) {
            const auto& before = scc[static_cast<size_t>(lastV - 1)];
            CHECK(before.reg == kSccEnable && !(before.value & 0x02));
        }
    }

    // チャンク 04 のバイトは MuSICA の 33 通りに限らない。22h は2コマごとに 2 ずつ上がる
    // （ROM の softenv.tcl の「AR を 22h に書き換えて MLOAD」）。
    {
        Run r;
        SequenceBlock b = withEnvelope(Device::SSGS, 0, {0xB2, 1, 0x81, kV15, 0x00, 96});
        b.envelopes[1].ar = 0x22;
        play(r, b, TickRate::Vdp60, 20, ssgA);
        const auto& v = r.levels;
        CHECK(v.size() >= 4 && v[0] == 0 && v[1] == 2 && v[2] == 4 && v[3] == 6);
        const int t2 = when(r.bus, ssgA, 2), t4 = when(r.bus, ssgA, 4);
        CHECK(t2 >= 0 && t4 - t2 == 2);
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
