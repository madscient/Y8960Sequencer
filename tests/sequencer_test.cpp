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

    // トラックのミュート。キーオンを止め、音量は 0。解けば次の音符から鳴る。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        ChannelActivity activity;
        Sequencer seq(devices, TickRate::Vdp60);
        seq.setActivity(&activity);
        const SequenceBlock block = seqtest::oneTrack(Device::SSGS, 0, {0x00, 48, 0x00, 48});
        seq.load(0, block);
        seq.setTrackMute(0, 0, true);
        bus.tick = 0;
        seq.start(0, 1);
        run(seq, bus, 20);                              // 1音目の途中
        CHECK(bus.count(Device::SSGS, kSsgVolA, 8) == 0);
        CHECK(activity.read(Device::SSGS, 0).noteOnSeq.load() == 0);

        seq.setTrackMute(0, 0, false);
        for (int i = 20; i < 50; ++i) { bus.tick = i; seq.interrupt(); }
        CHECK(bus.count(Device::SSGS, kSsgVolA, 8) >= 1);  // 2音目は鳴る
        CHECK(activity.read(Device::SSGS, 0).noteOnSeq.load() == 1);
    }

    // Q が 8 未満でのタイとレガート。& と数値の無い ~ の前の音符は音長いっぱい鳴り、
    // 後ろの音符は自分の長さに Q が効く。数値のある ~ と、間に挟まったイベントは
    // つながない。
    {
        struct Case {
            const char* name;
            std::initializer_list<int> events;
            int strikes;
        };
        const Case cases[] = {
            {"Q4 c&e",      {0x83, 4, 0x00, 48, 0x45, 0x04, 48, 0x0C, 48}, 1},
            {"Q4 c~e",      {0x83, 4, 0x00, 48, 0xD2, 0x00, 0x80, 0x04, 48, 0x0C, 48}, 1},
            {"Q4 c~100e",   {0x83, 4, 0x00, 48, 0xD2, 100, 0x00, 0x04, 48, 0x0C, 48}, 2},
            {"Q4 c V & e",  {0x83, 4, 0x00, 48, 0x81, kDefaultVol, 0x45, 0x04, 48, 0x0C, 48}, 2},
        };
        for (const Case& c : cases) {
            RecordingBus bus;
            DeviceSet devices(bus);
            devices.resetAll();
            Sequencer seq(devices, TickRate::Vdp60);
            const SequenceBlock block = seqtest::oneTrack(Device::SSGS, 0, c.events);
            seq.load(0, block);
            bus.tick = 0;
            seq.start(0, 1);
            run(seq, bus, 100);

            // レベルが 0 から立ち上がった回数がキーオンの回数。
            int strikes = 0;
            int firstOff = -1;
            int prev = 0;
            for (const auto& w : bus.writes) {
                if (w.chip != Device::SSGS || w.reg != kSsgVolA) continue;
                if (prev == 0 && w.value != 0) ++strikes;
                if (prev != 0 && w.value == 0 && firstOff < 0) firstOff = w.tick;
                prev = w.value;
            }
            if (strikes != c.strikes) std::printf("  %s: strikes %d\n", c.name, strikes);
            CHECK(strikes == c.strikes);
            if (c.strikes == 1) {
                // c は切れず、e が自分の Q4 で 24 tick 目に切れる。
                CHECK(near(firstOff, kQuarterInterrupts + kQuarterInterrupts / 2));
            } else {
                CHECK(near(firstOff, kQuarterInterrupts / 2));   // c が Q4 どおり切れる
            }
        }
    }

    // (DC) はキーオフしない。0E で始まるトラックは前の周の最後の音を保ち、音符で
    // 始まるトラックはその音符が鳴らし直す。繰り返しを使い切れば全部が止まる。
    // 先頭の 86（セーニョの目印）は読み飛ばされる。
    {
        RecordingBus bus;
        DeviceSet devices(bus);
        devices.resetAll();
        Sequencer seq(devices, TickRate::Vdp60);
        SequenceBlock block = seqtest::oneTrack(Device::SSGS, 0, {0x86, 0, 0x00, 48, 0x43});
        TrackData& held = block.tracks[1];
        held.assigned = true;
        held.device   = Device::SSGS;
        held.channel  = 1;
        held.events   = {0x0E, 24, 0x04, 72, 0xFF};
        seq.load(0, block);
        bus.tick = 0;
        seq.start(0, 2);
        run(seq, bus, 80);

        constexpr uint8_t kSsgVolB = kSsgVolA + 1;
        // 1周目の e は 24 tick 目から。(DC) の 48 tick 目を越えて、2周目の e が
        // 鳴らし直す 72 tick 目まで切れない。
        const int held0 = bus.firstTick(Device::SSGS, kSsgVolB, 0, kQuarterInterrupts / 2 + 2);
        CHECK(near(held0, kQuarterInterrupts * 3 / 2));
        // c は周ごとに鳴らし直す。86 を越えて鳴っている。巻き戻しは鳴っている c に
        // 音量を書き直すので、立ち上がりを数える。
        int strikes = 0;
        int prev = 0;
        for (const uint8_t v : bus.values(Device::SSGS, kSsgVolA)) {
            if (prev == 0 && v != 0) ++strikes;
            prev = v;
        }
        CHECK(strikes == 2);
        CHECK(seq.finished());
        CHECK(bus.writes.back().tick <= kQuarterInterrupts * 2 + 1);
        CHECK(bus.values(Device::SSGS, kSsgVolA).back() == 0);
        CHECK(bus.values(Device::SSGS, kSsgVolB).back() == 0);
    }

    return check::finish("sequencer_test");
}
