// Y8SQ 形式（bytecode.md）が定めるデバイス 0-7 の読み方とデバイス 8-11 を、
// ドライバが書くレジスタで見る。
// 期待値は bytecode.md の表と、ymfm のレジスタの定義から手で出したもの。

#include "check.h"
#include "fmvoice.h"
#include "pitch.h"
#include "seqtest.h"
#include "sequencer.h"

#include <cstdio>
#include <vector>

using namespace y8960;
using seqtest::RecordingBus;

namespace {

void run(Sequencer& seq, RecordingBus& bus, int interrupts) {
    for (int i = 0; i < interrupts; ++i) {
        bus.tick = i + 1;
        seq.interrupt();
    }
}

// 60Hz・テンポ 120 では、24 tick が 15 回の割り込み。
constexpr int kEighth = 15;

// チャンク 01 のレコード。FB 5・C 1、移調 +12。キャリアの WS は 7 で、チップが持つ
// ビットだけが効く。
VoiceRecord packedFm(int8_t transpose = 12) {
    VoiceRecord v;
    v.kind = RecordKind::FmVoice;
    const uint8_t d[12] = {0x0B, static_cast<uint8_t>(transpose),
                           0x9A, 0xF3, 0x24, 0xE5, 0x01,     // モジュレータ
                           0xC7, 0xE2, 0x35, 0x31, 0x07};    // キャリア
    for (int i = 0; i < 12; ++i) v.data[static_cast<size_t>(i)] = d[i];
    return v;
}

// チャンク 42 のレコード。オペレータの TL は 10h-13h、ほかは持続する値。
VoiceRecord fourOp(uint8_t alg, bool cs, int8_t detune) {
    VoiceRecord v;
    v.kind = RecordKind::Fm4op;
    v.data[0] = alg;
    v.data[1] = 0;
    v.data[2] = static_cast<uint8_t>(detune);
    v.data[3] = cs ? 1 : 0;
    for (int op = 0; op < 4; ++op) {
        uint8_t* o = v.data.data() + 4 + op * 5;
        o[0] = static_cast<uint8_t>(0x10 + op);
        o[1] = 0xF0;
        o[2] = 0x0F;
        o[3] = 0x21;
        o[4] = 0;
    }
    return v;
}

// チャンク 40 のレコード。オペレータの TL は 10h-13h（M1・C1・M2・C2）。
VoiceRecord opnVoice(uint8_t alg, uint8_t sens, uint8_t slots) {
    VoiceRecord v;
    v.kind = RecordKind::OpnVoice;
    v.data[kFmAlg] = alg;
    v.data[kFmSens] = sens;
    v.data[kFmTrans] = 0;
    v.data[kFmSlots] = slots;
    for (int op = 0; op < 4; ++op) {
        uint8_t* o = v.data.data() + kFmOp1 + op * kFmOpSize;
        o[kFmOpTl]    = static_cast<uint8_t>(0x10 + op);
        o[kFmOpKsAr]  = 0x5F;
        o[kFmOpAmDr]  = 0x85;
        o[kFmOpDt2Sr] = 0xC5;
        o[kFmOpSlRr]  = 0x2F;
        o[kFmOpSsgEg] = 0x0B;
        o[kFmOpDtMul] = 0x31;
    }
    return v;
}

void addTrack(SequenceBlock& b, int index, Device device, uint8_t channel, std::vector<uint8_t> events) {
    TrackData& t = b.tracks[static_cast<size_t>(index)];
    t.assigned = true;
    t.device   = device;
    t.channel  = channel;
    t.events   = std::move(events);
    t.events.push_back(0xFF);
}

struct Rig {
    RecordingBus bus;
    DeviceSet    devices{bus};
    Sequencer    seq{devices, TickRate::Vdp60};

    void play(const SequenceBlock& block, int interrupts, uint8_t repeat = 1) {
        devices.resetAll();
        devices.setAdpcmDirectory(block.adpcm.data());
        devices.setAdpcmASamples(block.adpcmA.data());
        bus.tick = 0;
        seq.load(0, block);
        seq.start(0, repeat);
        run(seq, bus, interrupts);
    }
};

// キーのビットを立てた書き込みごとの、ブロックの値（OPL 系の B0h・OPLL の 20h）。
std::vector<int> keyOnBlocks(const RecordingBus& bus, Device chip, uint8_t reg, uint8_t key, int shift) {
    std::vector<int> out;
    bool was = false;
    for (const auto& w : bus.writes) {
        if (w.chip != chip || w.reg != reg || w.port != 0) continue;
        const bool on = (w.value & key) != 0;
        if (on && !was) out.push_back((w.value >> shift) & 7);
        was = on;
    }
    return out;
}

// 指定のレジスタへの書き込みが、別のレジスタより前に来たか（最後のものどうしで比べる）。
bool writtenBefore(const RecordingBus& bus, Device chip, uint8_t port, uint8_t first, uint8_t second) {
    int a = -1, b = -1;
    for (size_t i = 0; i < bus.writes.size(); ++i) {
        const auto& w = bus.writes[i];
        if (w.chip != chip || w.port != port) continue;
        if (w.reg == first) a = static_cast<int>(i);
        if (w.reg == second) b = static_cast<int>(i);
    }
    return a >= 0 && b > a;
}

} // namespace

int main() {
    // OPLLEX：チャンク 01 の 12 バイトをユーザー音色の 00h-07h へ。03h は組み立てる。
    // 音色の移調が効き、`82` のプリセットでは 0 に戻る。`82` の bit7-6 は見ない。
    {
        Rig r;
        SequenceBlock b;
        b.voices[0] = packedFm();
        addTrack(b, 0, Device::OPLLEX1, 0, {0x85, 0, 0x00, 24, 0x82, 0xD2, 0x00, 24});
        r.play(b, 2 * kEighth);
        const Device d = Device::OPLLEX1;
        CHECK(r.bus.last(d, 0x00) == 0xE5);
        CHECK(r.bus.last(d, 0x01) == 0x31);
        CHECK(r.bus.last(d, 0x02) == 0x9A);
        CHECK(r.bus.last(d, 0x03) == 0xDD);   // KSL 3、DC、DM、FB 5
        CHECK(r.bus.last(d, 0x04) == 0xF3);
        CHECK(r.bus.last(d, 0x05) == 0xE2);
        CHECK(r.bus.last(d, 0x06) == 0x24);
        CHECK(r.bus.last(d, 0x07) == 0x35);
        // 12 半音の移調は F-Number を変えずブロックだけを変えるので、キーオンの 20h で見る。
        const auto keyOns = keyOnBlocks(r.bus, d, 0x20, 0x10, 1);
        CHECK(keyOns.size() == 2);
        CHECK(keyOns.front() == oplPitch(false, 60, 0).block);   // O4 C ＋ 12
        CHECK(keyOns.back() == oplPitch(false, 48, 0).block);
        CHECK(r.bus.last(d, 0x10) == (oplPitch(false, 48, 0).fnum & 0xFF));
        CHECK(r.bus.last(d, 0x40) == 0x01);   // バンク 1
        CHECK((r.bus.last(d, 0x30) >> 4) == 2);
    }

    // OPL2EX：同じレコード。WS は bit1-0 だけ、移調は1バイトの半音。
    {
        Rig r;
        SequenceBlock b;
        b.voices[0] = packedFm();
        addTrack(b, 0, Device::OPL2EX1, 1, {0x85, 0, 0x00, 24});
        r.play(b, kEighth);
        const Device d = Device::OPL2EX1;
        CHECK(r.bus.last(d, 0xC1) == 0x0B);
        CHECK(r.bus.last(d, 0x21) == 0xE5);
        CHECK(r.bus.last(d, 0x41) == 0x9A);
        CHECK(r.bus.last(d, 0x61) == 0xF3);
        CHECK(r.bus.last(d, 0x81) == 0x24);
        CHECK(r.bus.last(d, 0xE1) == 0x01);
        CHECK(r.bus.last(d, 0x24) == 0x31);
        CHECK(r.bus.last(d, 0x44) == (0xC0 | (0x07 + 28)));   // V8 が 28 段引く
        CHECK(r.bus.last(d, 0xE4) == 0x03);
        CHECK(r.bus.last(d, 0xA1) == (oplPitch(true, 60, 0).fnum & 0xFF));
        const auto keyOns = keyOnBlocks(r.bus, d, 0xB1, 0x20, 2);
        CHECK(keyOns.size() == 1 && keyOns.front() == oplPitch(true, 60, 0).block);
    }

    // SSGS の定位は `87`。`B1` はもう何もしない。
    {
        Rig r;
        SequenceBlock b;
        addTrack(b, 0, Device::SSGS, 0, {0xB1, 5, 0x87, 3, 0x00, 24});
        addTrack(b, 1, Device::SSGS, 4, {0x87, 12, 0x00, 24});
        r.play(b, kEighth);
        CHECK(r.bus.values(Device::SSGS, 0x10) == (std::vector<uint8_t>{8, 3}));   // リセットの中央と `87`
        CHECK(r.bus.last(Device::SSGS, 0x31) == 12);
    }

    // SCC の `B3`。0 は表を通さず、周の頭で表に戻る。
    {
        Rig r;
        SequenceBlock b;
        addTrack(b, 0, Device::SCC, 0, {0xB3, 0, 0x81, 103, 0x00, 24});
        r.play(b, 3 * kEighth, 2);
        // V8 を表で書く（02h）のは、2つの周の頭で1回ずつ。周の頭で表に戻らなければ
        // 2周目は 08h になる。
        CHECK(r.bus.count(Device::SCC, 0x8A, 0x02) == 2);
        CHECK(r.bus.count(Device::SCC, 0x8A, 0x0C) == 2);   // V12 をそのまま
    }

    // `E0` は1組目、`E8` は2組目。2組目を持たないデバイスは `E8` を捨てる。
    {
        Rig r;
        SequenceBlock b;
        addTrack(b, 0, Device::OPL3, 0, {0xE8, 0x20, 0x35, 0x00, 0xE8, 0x20, 0x01, 0xF0,
                                         0xE0, 0x20, 0x12, 0x00, 0x00, 24});
        addTrack(b, 1, Device::SSGS, 0, {0xE8, 0x07, 0x00, 0x00, 0x00, 24});
        r.play(b, kEighth);
        CHECK(r.bus.last(Device::OPL3, 0x20, 1) == 0x31);   // (35h AND F0h) OR 01h
        CHECK(r.bus.last(Device::OPL3, 0x20, 0) == 0x12);
        int ssgsPort1 = 0;
        for (const auto& w : r.bus.writes) {
            if (w.chip == Device::SSGS && w.port != 0) ++ssgsPort1;
        }
        CHECK(ssgsPort1 == 0);
    }

    // OPL3 の 2OP。チャンネル 9-17 は2組目。WS は bit2-0 を使う。
    {
        Rig r;
        SequenceBlock b;
        b.voices[0] = packedFm(0);
        addTrack(b, 0, Device::OPL3, 10, {0x85, 0, 0x00, 24});
        r.play(b, 4);
        const Device d = Device::OPL3;
        CHECK(r.bus.last(d, 0xC1, 1) == 0x3B);   // 出力 A・B と FB 5・C 1
        CHECK(r.bus.last(d, 0x21, 1) == 0xE5);
        CHECK(r.bus.last(d, 0xE4, 1) == 0x07);
        CHECK(r.bus.last(d, 0x44, 1) == (0xC0 | (0x07 + 28)));
        CHECK((r.bus.last(d, 0xB1, 1) & 0x20) != 0);        // キーオン
        CHECK(r.bus.last(d, 0xA1, 1) == (oplPitch(true, 48, 0).fnum & 0xFF));
        CHECK(r.bus.last(d, 0x05, 1) == 0x01);              // NEW
    }

    // OPL3 の 4OP。CS 1 の音色は組を 4OP にし、高さは前側だけに書く。キーは後ろ側にも
    // 揃えて書く（ymfm が 104h を次のクロックで反映するため。dev_opl3.cpp）。
    // キャリアは CNT1・CNT2 で決まる（1・1 なら OP1・OP3・OP4）。
    {
        Rig r;
        SequenceBlock b;
        b.voices[2] = fourOp(0x75, true, 0);   // FB2 3・CNT2 1・FB1 2・CNT1 1
        addTrack(b, 0, Device::OPL3, 19, {0x85, 2, 0x00, 24});
        r.play(b, 4);
        const Device d = Device::OPL3;
        CHECK(r.bus.last(d, 0x04, 1) == 0x02);   // 19 は 104h の bit1
        CHECK(r.bus.last(d, 0xC1) == 0x35);
        CHECK(r.bus.last(d, 0xC4) == 0x37);
        CHECK(r.bus.last(d, 0x41) == 0x10 + 28);   // OP1（キャリア）
        CHECK(r.bus.last(d, 0x44) == 0x11);        // OP2（モジュレータ）
        CHECK(r.bus.last(d, 0x49) == 0x12 + 28);   // OP3
        CHECK(r.bus.last(d, 0x4C) == 0x13 + 28);   // OP4
        CHECK((r.bus.last(d, 0xB1) & 0x20) != 0);
        CHECK(r.bus.last(d, 0xB4) == 0x20);         // 後ろ側はキーだけで、高さは書かない
        CHECK(r.bus.values(d, 0xA4) == (std::vector<uint8_t>{0}));
    }

    // CS 0 の音色は組を 2OP 2つで鳴らす。後ろ側だけ擬似デチューン（1/64 半音）ずらす。
    // 型の合わない `85` は無視する。
    {
        Rig r;
        SequenceBlock b;
        b.voices[2] = fourOp(0x00, false, 32);
        b.voices[3] = packedFm(0);
        addTrack(b, 0, Device::OPL3, 20, {0x85, 2, 0x85, 3, 0x00, 24});
        addTrack(b, 1, Device::OPL3, 0, {0x85, 2, 0x00, 24});
        r.play(b, 4);
        const Device d = Device::OPL3;
        CHECK(r.bus.last(d, 0x04, 1) == 0x00);
        CHECK((r.bus.last(d, 0xB2) & 0x20) != 0);
        CHECK((r.bus.last(d, 0xB5) & 0x20) != 0);
        CHECK(r.bus.last(d, 0xA2) == (oplPitch(true, 48, 0).fnum & 0xFF));
        CHECK(r.bus.last(d, 0xA5) == (oplPitch(true, 48, 32).fnum & 0xFF));
        CHECK(r.bus.last(d, 0x45) == 0x11 + 28);   // CNT1 0 は OP2 がキャリア
        CHECK(r.bus.last(d, 0x42) == 0x10);
        CHECK(r.bus.count(d, 0xC0, 0x30) == 1);     // 2OP のチャンネルは 4OP の音色を取らない
        CHECK(r.bus.last(d, 0xC0) == 0x30);
    }

    // OPL3 のリズムは 24。定位はリズムの3チャンネルに効き、出力 C・D は残す。
    {
        Rig r;
        SequenceBlock b;
        b.rhythmMode[static_cast<size_t>(Device::OPL3)] = true;
        addTrack(b, 0, Device::OPL3, kOpl3Rhythm, {0x87, 0, 0xC8, 0x10, 24});
        addTrack(b, 1, Device::OPL3, 3, {0xE0, 0xC3, 0xF0, 0x00, 0x87, 15, 0x00, 24});
        r.play(b, 4);
        const Device d = Device::OPL3;
        CHECK((r.bus.last(d, 0xBD) & 0x30) == 0x30);    // リズムモードとバスドラム
        CHECK(r.bus.last(d, 0xC6) == 0x10);
        CHECK(r.bus.last(d, 0xC8) == 0x10);
        CHECK(r.bus.last(d, 0xC3) == 0xE0);             // C・D を残して B だけ
    }

    // OPM。チャンク 40 のレコードはデバイスの集合から。オペレータはレジスタの並びへ。
    {
        Rig r;
        SequenceBlock b;
        b.deviceVoices[0][0] = opnVoice(0xAC, 0x23, 0xF0);   // NE・FB 5・AL 4
        b.voices[0] = packedFm();                            // 共有の集合は見ない
        addTrack(b, 0, Device::OPM, 7, {0x85, 0, 0x80, 4, 0x09, 24, 0xD0, 50, 0, 0x87, 15});
        r.play(b, 2 * kEighth);
        const Device d = Device::OPM;
        CHECK(r.bus.values(d, 0x27).size() >= 2);
        CHECK(r.bus.values(d, 0x27)[r.bus.values(d, 0x27).size() - 2] == 0xEC);
        CHECK(r.bus.last(d, 0x27) == 0xAC);        // 右だけ
        CHECK(r.bus.last(d, 0x3F) == 0x32);        // PMS 3、AMS 2
        CHECK(r.bus.last(d, 0x0F) == 0x80);
        CHECK(r.bus.last(d, 0x67) == 0x10);        // M1
        CHECK(r.bus.last(d, 0x77) == 0x11 + 28);   // C1（キャリア）
        CHECK(r.bus.last(d, 0x6F) == 0x12);        // M2
        CHECK(r.bus.last(d, 0x7F) == 0x13 + 28);   // C2
        CHECK(r.bus.last(d, 0xDF) == 0xC5);        // C2 の DT2 と SR。DT2 は OPM だけのもの
        CHECK(r.bus.last(d, 0x47) == 0x31);
        CHECK(r.bus.count(d, 0x08, 0x7F) >= 1);    // 4つともキーオン
        CHECK(r.bus.last(d, 0x2F) == 0x4A);        // O4 A
        CHECK(r.bus.last(d, 0x37) == (32 << 2));   // 50 セントは 32/64 半音
    }

    // OPNA の FM。チャンネル 3-5 は2組目、キーオンは 28h の 4 から。上位を先に書く。
    {
        Rig r;
        SequenceBlock b;
        b.deviceVoices[1][0] = opnVoice(0x14, 0x35, 0xF0);   // FB 2・AL 4
        addTrack(b, 0, Device::OPNA, 3, {0x85, 0, 0x00, 24});
        r.play(b, 4);
        const Device d = Device::OPNA;
        CHECK(r.bus.last(d, 0xB0, 1) == 0x14);
        CHECK(r.bus.last(d, 0xB4, 1) == 0xF5);     // L・R と AMS 3・PMS 5
        CHECK(r.bus.last(d, 0x40, 1) == 0x10);     // M1（スロット 1）
        CHECK(r.bus.last(d, 0x48, 1) == 0x11 + 28);// C1（スロット 2）
        CHECK(r.bus.last(d, 0x44, 1) == 0x12);     // M2（スロット 3）
        CHECK(r.bus.last(d, 0x4C, 1) == 0x13 + 28);// C2（スロット 4）
        CHECK(r.bus.last(d, 0x70, 1) == 0x05);     // DT2 は OPM だけのもの
        CHECK(r.bus.last(d, 0x90, 1) == 0x0B);
        CHECK(r.bus.last(d, 0x28) == 0xF4);
        const OpnPitch p = opnPitch(kClockOpna, 48, 0);
        CHECK(r.bus.last(d, 0xA4, 1) == ((p.block << 3) | (p.fnum >> 8)));
        CHECK(r.bus.last(d, 0xA0, 1) == (p.fnum & 0xFF));
        CHECK(writtenBefore(r.bus, d, 1, 0xA4, 0xA0));
    }

    // OPNA の効果音モード。`E9` の差を親の高さに足してサブチャンネルへ書く。差は周の頭で 0。
    {
        Rig r;
        SequenceBlock b;
        b.deviceVoices[1][0] = opnVoice(kFmFx | 0x07, 0, 0xF0);
        addTrack(b, 0, Device::OPNA, 2, {0x85, 0, 0xE9, 2, 0x9C, 0xFF, 0xE9, 3, 0xB0, 0x04, 0x00, 24,
                                         0xE9, 1, 100, 0, 0xE9, 4, 100, 0, 0x00, 24});
        r.play(b, 5 * kEighth, 2);
        const Device d = Device::OPNA;
        CHECK((r.bus.last(d, 0x27) & 0xC0) == 0x40);
        const auto low = [](int note) { return static_cast<uint8_t>(opnPitch(kClockOpna, static_cast<uint8_t>(note), 0).fnum & 0xFF); };
        const uint8_t n48 = low(48), n49 = low(49);
        CHECK(r.bus.values(d, 0xA9) == (std::vector<uint8_t>{0, n48, n49, n48, n49}));   // OP1
        CHECK(r.bus.last(d, 0xAA) == low(47));   // OP2 は -100 セント
        CHECK(r.bus.last(d, 0xA8) == low(60));   // OP3 は +1200 セント
        CHECK(r.bus.last(d, 0xA2) == n48);       // 親（OP4）
    }

    // FX の無い音色と、チャンネル 2 以外では `E9` を捨てる。
    {
        Rig r;
        SequenceBlock b;
        b.deviceVoices[1][0] = opnVoice(0x07, 0, 0xF0);
        addTrack(b, 0, Device::OPNA, 2, {0x85, 0, 0xE9, 1, 100, 0, 0x00, 24});
        addTrack(b, 1, Device::OPNA, 1, {0x85, 0, 0xE9, 1, 100, 0, 0x00, 24});
        r.play(b, 4);
        CHECK(r.bus.values(Device::OPNA, 0xA9) == (std::vector<uint8_t>{0}));   // リセットだけ
        CHECK((r.bus.last(Device::OPNA, 0x27) & 0xC0) == 0);
    }

    // OPNA の SSG（6-8）は SSGS と同じドライバで、分周値はマスタークロック ÷ 64。
    {
        Rig r;
        SequenceBlock b;
        addTrack(b, 0, Device::OPNA, 6, {0x80, 4, 0x09, 24});
        r.play(b, 4);
        CHECK(r.bus.last(Device::OPNA, 0x00) == 0x1C);   // 7987200 / 64 / 440 = 283.6 → 284
        CHECK(r.bus.last(Device::OPNA, 0x01) == 0x01);
        CHECK(r.bus.last(Device::OPNA, 0x08) == 8);
    }

    // OPNA のリズム（9）。6つの楽器のレベルは max(31 - (15 - V), 0) を 18h-1Dh へ。
    {
        Rig r;
        SequenceBlock b;
        addTrack(b, 0, Device::OPNA, kOpnRhythm, {0xA9, 10, 0xD8, 0x04, 13, 0xD8, 0x20, 2, 0xAA, 15,
                                                  0xA8, 0x01, 0xC8, 0x05, 24, 0x87, 0});
        r.play(b, kEighth + 2);
        const Device d = Device::OPNA;
        CHECK(r.bus.last(d, 0x10) == 0x05);
        CHECK(r.bus.values(d, 0x18).size() >= 2);
        const auto bd = r.bus.values(d, 0x18);
        CHECK(bd[bd.size() - 2] == (0xC0 | 31));   // アクセント 15
        CHECK(bd.back() == (0x80 | 31));           // 定位を左に
        CHECK((r.bus.last(d, 0x19) & 0x1F) == 26); // V10
        CHECK((r.bus.last(d, 0x1A) & 0x1F) == 29); // D8 で 13
        CHECK((r.bus.last(d, 0x1D) & 0x1F) == 18); // D8 で 2（bit5 は OPNA では使う）
        CHECK(r.bus.last(d, 0x11) == 0x3F);        // 全体の音量はリセットで最大
    }

    // OPNB の ADPCM-A。`D9` で結び付けたサンプルだけを叩く。番号は 0-255 の全域。
    {
        Rig r;
        SequenceBlock b;
        b.adpcmA[255] = AdpcmASample{true, 0x0110, 4};
        addTrack(b, 0, Device::OPNB, kOpnRhythm, {0xD9, 0x03, 255, 0xC8, 0x07, 24});
        r.play(b, 4);
        const Device d = Device::OPNB;
        CHECK(r.bus.last(d, 0x10, 1) == 0x10);
        CHECK(r.bus.last(d, 0x18, 1) == 0x01);
        CHECK(r.bus.last(d, 0x21, 1) == 0x13);
        CHECK(r.bus.last(d, 0x29, 1) == 0x01);
        CHECK(r.bus.last(d, 0x00, 1) == 0x03);     // 結び付いていない楽器 2 は叩かない
        CHECK(r.bus.last(d, 0x08, 1) == (0xC0 | 24));   // 既定の V8
        CHECK(r.bus.last(d, 0x01, 1) == 0x3F);
    }

    // ADPCM-B（10）。OPNA は port 1 の 00h から、8 ビット単位のメモリで 32 バイト刻み。
    // OPNB は port 0 の 10h から、256 バイト刻み。速さは O5 E がサンプルの周波数。
    {
        for (Device d : {Device::OPNA, Device::OPNB}) {
            Rig r;
            SequenceBlock b;
            b.adpcm[3] = AdpcmVoiceFile{true, 4, 2, 8000};
            addTrack(b, 0, d, kOpnAdpcmB, {0x82, 3, 0x80, 5, 0x04, 24});
            r.play(b, 4);
            const bool a = (d == Device::OPNA);
            const uint8_t port = a ? 1 : 0;
            const uint8_t base = a ? 0x00 : 0x10;
            CHECK(r.bus.last(d, static_cast<uint8_t>(base + 0x02), port) == (a ? 0x20 : 0x04));
            CHECK(r.bus.last(d, static_cast<uint8_t>(base + 0x04), port) == (a ? 0x2F : 0x05));
            CHECK(r.bus.count(d, static_cast<uint8_t>(base + 0x00), 0xA0, port) == 1);
            // 8000 * 65536 / (クロック / 144)
            const int expect = a ? 9452 : 9437;
            const int delta = r.bus.last(d, static_cast<uint8_t>(base + 0x09), port) |
                              (r.bus.last(d, static_cast<uint8_t>(base + 0x0A), port) << 8);
            CHECK(delta == expect);
            if (a) CHECK(r.bus.last(d, 0x01, 1) == 0xC2);
        }
    }

    return check::finish("devices_test");
}
