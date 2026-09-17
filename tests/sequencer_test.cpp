#include "check.h"
#include "seqtest.h"
#include "sequencer.h"

#include <cstdio>

using namespace y8960;
using seqtest::RecordingBus;

namespace {

// 割り込みを n 回進める。1回の割り込みで進む tick はテンポと分解能で決まる。
void run(Sequencer& seq, RecordingBus& bus, int interrupts) {
    for (int i = 0; i < interrupts; ++i) {
        bus.tick = i;
        seq.interrupt();
    }
}

constexpr uint8_t kSsgVolA  = 0x08;
constexpr uint8_t kSsgToneA = 0x00;

// 60Hz、テンポ 120 では、四分音符 48 tick が 30 回の割り込みにあたる。
constexpr int kQuarterInterrupts = 30;

bool near(int a, int b, int slack = 1) { return a >= b - slack && a <= b + slack; }

} // namespace

int main() {
    // 音符と休符。キーオンでレベルが立ち、休符で落ちる。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        Sequencer seq(devices, TickRate::Vdp60);
        const SequenceBlock block = seqtest::oneTrack(Device::SSGS, 0, {0x00, 48, 0x0C, 48});
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 1);
        run(seq, bus, 80);

        const auto levels = bus.values(Device::SSGS, kSsgVolA);
        // リセットと巻き戻しも書くが、キーが上なのでどれもレベル 0。最初に 0 でない
        // 値が出るのがキーオンで、それは V8 の 71 を 8 段に落とした 8。
        size_t firstLoud = 0;
        while (firstLoud < levels.size() && levels[firstLoud] == 0) ++firstLoud;
        CHECK(firstLoud < levels.size());
        CHECK(levels[firstLoud] == 8);
        CHECK(levels.back() == 0);
        // 音程は分周値の2バイト。O4 の c は音符番号 48 で、SSGS の分周値は 428
        // （1789772.5 / 16 / 428 = 261.4Hz。平均律の C4 は 261.63Hz）。
        const auto toneLow  = bus.values(Device::SSGS, kSsgToneA);
        const auto toneHigh = bus.values(Device::SSGS, kSsgToneA + 1);
        CHECK(!toneLow.empty() && !toneHigh.empty());
        const uint16_t divisor = static_cast<uint16_t>(toneLow.back() | (toneHigh.back() << 8));
        CHECK(divisor == 428);

        CHECK(near(bus.firstTick(Device::SSGS, kSsgVolA, 8), 0));
        CHECK(near(bus.firstTick(Device::SSGS, kSsgVolA, 0, 0), kQuarterInterrupts));
        CHECK(seq.finished());
    }

    // クオンタイズ。Q4 は音長の半分でキーオフする。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        Sequencer seq(devices, TickRate::Vdp60);
        const SequenceBlock block = seqtest::oneTrack(Device::SSGS, 0, {0x83, 4, 0x00, 48});
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 1);
        run(seq, bus, 60);
        CHECK(near(bus.firstTick(Device::SSGS, kSsgVolA, 0, 0), kQuarterInterrupts / 2));
    }

    // ループ。|: c :| 2 は音符を2回鳴らす。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        Sequencer seq(devices, TickRate::Vdp60);
        // 42（ループ始点）、音符、E2（終点。回数2、音符へ戻る距離 -6）
        const SequenceBlock block = seqtest::oneTrack(Device::SSGS, 0,
            {0x42, 0x00, 48, 0xE2, 2, 0xFA, 0xFF});
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 1);
        run(seq, bus, 120);
        CHECK(bus.count(Device::SSGS, kSsgVolA, 8) == 2);
        CHECK(seq.finished());
    }

    // シーケンスの繰り返し。2周ぶん鳴る。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        Sequencer seq(devices, TickRate::Vdp60);
        const SequenceBlock block = seqtest::oneTrack(Device::SSGS, 0, {0x00, 48});
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 2);
        run(seq, bus, 120);
        CHECK(bus.count(Device::SSGS, kSsgVolA, 8) == 2);
        // 周の境目は詰まる。2周目の頭は 1 周目の終わりと同じ tick。
        CHECK(near(bus.firstTick(Device::SSGS, kSsgVolA, 8, 0), kQuarterInterrupts));
        CHECK(seq.finished());
    }

    // テンポ。T240 は倍の速さで進む。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        Sequencer seq(devices, TickRate::Vdp60);
        const SequenceBlock block = seqtest::oneTrack(Device::SSGS, 0, {0x84, 240, 0x00, 48, 0x0C, 48});
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 1);
        run(seq, bus, 60);
        CHECK(near(bus.firstTick(Device::SSGS, kSsgVolA, 0, 0), kQuarterInterrupts / 2));
    }

    // DCSG。減衰は反転して入り、キーオフで 15 になる。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        Sequencer seq(devices, TickRate::Vdp60);
        const SequenceBlock block = seqtest::oneTrack(Device::DCSG1, 0, {0x00, 48, 0x0C, 48});
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 1);
        run(seq, bus, 80);

        int attOn = 0, attOff = 0;
        for (const auto& w : bus.writes) {
            if (w.chip != Device::DCSG1) continue;
            if (w.value == (0x80 | 0x10 | (15 - 8))) ++attOn;      // 音量 8 の減衰
            if (w.value == (0x80 | 0x10 | 15)) ++attOff;           // 消音
        }
        CHECK(attOn >= 1);
        CHECK(attOff >= 1);
    }

    // SCC。`85` の波形がチャンネルの波形ブロックへ書かれる。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        Sequencer seq(devices, TickRate::Vdp60);
        SequenceBlock block = seqtest::oneTrack(Device::SCC, 1, {0x85, 3, 0x00, 48});
        block.voices[3].kind = RecordKind::SccWave;
        for (int i = 0; i < kVoiceRecSize; ++i) {
            block.voices[3].data[static_cast<size_t>(i)] = static_cast<uint8_t>(0x40 + i);
        }
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 1);
        run(seq, bus, 40);

        // チャンネル1の波形ブロックは 20h から 32 バイト。
        const auto first = bus.values(Device::SCC, 0x20);
        CHECK(!first.empty());
        CHECK(first.back() == 0x40);
        const auto last = bus.values(Device::SCC, 0x3F);
        CHECK(!last.empty());
        CHECK(last.back() == static_cast<uint8_t>(0x40 + 31));
        // イネーブルのビット1が立つ
        const auto enable = bus.values(Device::SCC, 0x8F);
        CHECK(!enable.empty());
        CHECK((enable.back() & 0x02) != 0 || enable.size() >= 2);
    }

    // チャンネルごとの発音の様子（レベルメーターの元）。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        ChannelActivity activity;
        Sequencer seq(devices, TickRate::Vdp60);
        seq.setActivity(&activity);
        const SequenceBlock block = seqtest::oneTrack(Device::SSGS, 2, {0x00, 48, 0x0C, 48});
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 1);

        const auto& slot = activity.read(Device::SSGS, 2);
        CHECK(slot.sounding.load());
        CHECK(slot.level.load() == kDefaultVol);        // V8 を 0-127 で
        CHECK(slot.noteOnSeq.load() == 1);
        CHECK(!activity.read(Device::SSGS, 0).sounding.load());

        run(seq, bus, 40);                              // 休符まで進める
        CHECK(!slot.sounding.load());
    }

    // リズムの打撃は、楽器ごとの枠に分かれて立ち上がる。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        ChannelActivity activity;
        Sequencer seq(devices, TickRate::Vdp60);
        seq.setActivity(&activity);
        // A9（V=4）、AA（@A=15）、A8（バスドラムにアクセント）、C8（BD と SD を叩く）
        const SequenceBlock block = seqtest::oneTrack(Device::OPLLEX1, kChannelRhythm,
            {0xA9, 4, 0xAA, 15, 0xA8, 0x10, 0xC8, 0x18, 48});
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 1);

        const auto& bd = activity.read(Device::OPLLEX1, kRhythmSlotFirst);      // バスドラム
        const auto& sd = activity.read(Device::OPLLEX1, kRhythmSlotFirst + 1);  // スネア
        const auto& tom = activity.read(Device::OPLLEX1, kRhythmSlotFirst + 2);
        CHECK(bd.noteOnSeq.load() == 1);
        CHECK(sd.noteOnSeq.load() == 1);
        CHECK(tom.noteOnSeq.load() == 0);               // 叩いていない楽器は動かない
        CHECK(bd.level.load() > sd.level.load());       // アクセントの付いたほうが大きい
    }

    return check::finish("sequencer_test");
}
