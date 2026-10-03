// OPL3（YMF262、デバイス 8）。ROM に写し元は無く、bytecode.md の定めから作る。
// リズムと音量の読み替えは OPL2EX（dev_opl2ex.cpp）に揃える。
//
// チャンネル 0-8 は1組目（port 0）、9-17 は2組目（port 1）のレジスタにある。
// 4OP のチャンネル 18-23 は 2OP の2つ（前側と、それに 3 を足した後ろ側）を組にし、
// 選ばれた音色の CS が、組を 4OP で鳴らすか 2OP 2つで鳴らすかを決める。

#include "dev_internal.h"
#include "pitch.h"

namespace y8960 {

namespace {

constexpr uint8_t kPhysChannels = 18;
constexpr uint8_t kChPerPort    = 9;
constexpr uint8_t kRegTest      = 0x01;
constexpr uint8_t kWse          = 0x20;
constexpr uint8_t kRegFourOp    = 0x04;   // port 1
constexpr uint8_t kRegNew       = 0x05;   // port 1
constexpr uint8_t kNew          = 0x01;
constexpr uint8_t kRegAmVib     = 0x20;
constexpr uint8_t kRegKslTl     = 0x40;
constexpr uint8_t kRegArDr      = 0x60;
constexpr uint8_t kRegSlRr      = 0x80;
constexpr uint8_t kRegFnumL     = 0xA0;
constexpr uint8_t kRegFnumH     = 0xB0;
constexpr uint8_t kRegRhythm    = 0xBD;
constexpr uint8_t kRegFbCon     = 0xC0;
constexpr uint8_t kRegWaveSel   = 0xE0;
constexpr uint8_t kOpRegs       = 22;
constexpr uint8_t kKey          = 0x20;
constexpr uint8_t kRhythmOn     = 0x20;
constexpr uint8_t kKslMask      = 0xC0;
constexpr uint8_t kTlMask       = 0x3F;
constexpr uint8_t kTlMin        = 63;
constexpr uint8_t kCarrier      = 3;
constexpr uint8_t kRhyFirst     = 6;
constexpr uint8_t kOutA         = 0x10;   // 左
constexpr uint8_t kOutB         = 0x20;   // 右
constexpr uint8_t kOutAB        = kOutA | kOutB;

constexpr uint8_t kSlot[kChPerPort] = {0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12};

// リズムの5つの楽器が使うオペレータ。OPL2 と同じ。
constexpr uint8_t kRhySlotBd  = 0x13;
constexpr uint8_t kRhySlotSd  = 0x14;
constexpr uint8_t kRhySlotTom = 0x12;
constexpr uint8_t kRhySlotTc  = 0x15;
constexpr uint8_t kRhySlotHh  = 0x11;

// リズムモードで ch6-8 が持つ音程。OPL2EX と同じ値（F-Number の数え方も同じ）。
constexpr uint8_t kRhythmPitch[][2] = {
    {kRegFnumL + 6, 0x40}, {kRegFnumH + 6, 0x0A},
    {kRegFnumL + 7, 0xA0}, {kRegFnumH + 7, 0x0A},
    {kRegFnumL + 8, 0x80}, {kRegFnumH + 8, 0x03},
};

// チャンク 01 のレコード（12 バイト）。
constexpr int kRecFb    = 0;
constexpr int kRecTrans = 1;
constexpr int kRecMod   = 2;
constexpr int kRecCar   = 7;
// チャンク 42 のレコード（24 バイト）。
constexpr int k4Alg     = 0;
constexpr int k4Trans   = 1;
constexpr int k4Detune  = 2;
constexpr int k4Cs      = 3;
constexpr int k4Op1     = 4;
constexpr int kOpSize   = 5;
// オペレータ（5 バイト）。
constexpr int kOpTl     = 0;
constexpr int kOpAr     = 1;
constexpr int kOpSl     = 2;
constexpr int kOpMult   = 3;
constexpr int kOpWave   = 4;

// `87` の 0-15 を、出力 A（左）と B（右）の入り切りに丸める。
uint8_t panBits(uint8_t value) {
    if (value < 4)  return kOutA;
    if (value < 12) return kOutAB;
    return kOutB;
}

class Opl3Device final : public SoundDevice {
public:
    explicit Opl3Device(ChipBus& bus) : bus_(bus) {}

    void reset() override {
        write(1, kRegNew, kNew);                 // 2組目と 4OP はこれが無いと効かない
        write(1, kRegFourOp, 0);
        write(0, kRegTest, kWse);
        write(0, 0x08, 0);
        for (uint8_t port = 0; port < 2; ++port) {
            fill(port, kRegAmVib, kOpRegs, 0);
            fill(port, kRegKslTl, kOpRegs, kTlMin);
            fill(port, kRegArDr, kOpRegs, 0);
            fill(port, kRegSlRr, kOpRegs, 0);
            fill(port, kRegFnumL, kChPerPort, 0);
            fill(port, kRegFnumH, kChPerPort, 0);
            // 出力 C と D は `87` が触らない。演奏を始める前の値はここで決める。
            fill(port, kRegFbCon, kChPerPort, kOutAB);
            fill(port, kRegWaveSel, kOpRegs, 0);
        }
        write(0, kRegRhythm, 0);
        for (Logical& l : logical_) l = Logical{};
        for (uint8_t p = 0; p < kPhysChannels; ++p) {
            fnh_[p] = 0;
            tl_[p][0] = tl_[p][1] = kTlMin;
            carriers_[p] = 0;
        }
        fourOp_ = 0;
        rhythm_ = RhythmState{};
    }

    // OPL2EX と同じ。5つの楽器は ch6-8 のオペレータなので、その3本に音色が要る。
    void setRhythmMode(bool on, const VoiceRecord* rhythmVoices) override {
        rhythm_.mode = static_cast<uint8_t>((rhythm_.mode & 0xC0) | (on ? kRhythmOn : 0));
        write(0, kRegRhythm, rhythm_.mode);
        if (!on) return;
        rhythm_.toDefaults();
        if (rhythmVoices) {
            for (uint8_t i = 0; i < 3; ++i) {
                if (rhythmVoices[i].kind == RecordKind::None) continue;
                load2op(static_cast<uint8_t>(kRhyFirst + i), rhythmVoices[i].data.data());
            }
        }
        for (const auto& p : kRhythmPitch) {
            write(0, p[0], p[1]);
            if (p[0] >= kRegFnumH && p[0] < kRegFnumH + kChPerPort) fnh_[p[0] - kRegFnumH] = p[1];
        }
    }

    void keyOn(uint8_t ch) override  { key(ch, true); }
    void keyOff(uint8_t ch) override { key(ch, false); }

    void setVolume(uint8_t ch, uint8_t loudness) override {
        if (ch == kOpl3Rhythm) {
            rhythm_.scale = loudness;
            writeRhythmLevels(rhythm_.accents);
            return;
        }
        if (ch >= kOpl3Rhythm) return;
        Logical& l = logical_[ch];
        l.volume = loudness;
        uint8_t front, rear;
        if (pairOf(ch, front, rear)) {
            writeLevels(front, loudness);
            writeLevels(rear, loudness);
        } else {
            writeLevels(ch, loudness);
        }
    }

    void setPitch(uint8_t ch, uint8_t note, int16_t bend) override {
        if (ch >= kOpl3Rhythm) return;
        Logical& l = logical_[ch];
        const int wanted = static_cast<int>(note) + l.transpose;
        const uint8_t n = static_cast<uint8_t>(wanted < 0 ? 0 : (wanted > 255 ? 255 : wanted));
        uint8_t front, rear;
        if (!pairOf(ch, front, rear)) {
            writePitch(ch, n, bend);
            return;
        }
        writePitch(front, n, bend);
        // 2OP 2つで鳴らす組は、後ろ側だけを擬似デチューンのぶんずらす。
        if (!isFourOp(ch)) writePitch(rear, n, static_cast<int16_t>(bend + l.detune));
    }

    // 型が名指すチャンネルに合わない `85` は黙って無視する（bytecode.md）。
    void seqVoice(uint8_t ch, uint8_t slot, const VoiceRecord& record) override {
        (void)slot;
        if (ch >= kOpl3Rhythm) return;
        const uint8_t* r = record.data.data();
        Logical& l = logical_[ch];
        uint8_t front, rear;
        if (!pairOf(ch, front, rear)) {
            if (record.kind != RecordKind::FmVoice) return;
            load2op(ch, r);
            l.transpose = static_cast<int8_t>(r[kRecTrans]);
            writeLevels(ch, l.volume);
            return;
        }
        if (record.kind != RecordKind::Fm4op) return;
        const uint8_t bit = static_cast<uint8_t>(1u << (ch - kOpl3FourOpFirst));
        const bool cs = r[k4Cs] & 1;
        const uint8_t next = static_cast<uint8_t>(cs ? (fourOp_ | bit) : (fourOp_ & ~bit));
        if (next != fourOp_) {
            fourOp_ = next;
            write(1, kRegFourOp, fourOp_);
        }
        const uint8_t alg = r[k4Alg];
        const uint8_t cnt1 = alg & 1;
        const uint8_t cnt2 = (alg >> 4) & 1;
        writeFbCon(front, static_cast<uint8_t>(alg & 0x0F));
        writeFbCon(rear, static_cast<uint8_t>((((alg >> 5) & 7) << 1) | cnt2));
        writeOperator(front, false, r + k4Op1);
        writeOperator(front, true,  r + k4Op1 + kOpSize);
        writeOperator(rear,  false, r + k4Op1 + kOpSize * 2);
        writeOperator(rear,  true,  r + k4Op1 + kOpSize * 3);
        tl_[front][0] = r[k4Op1 + kOpTl];
        tl_[front][1] = r[k4Op1 + kOpSize + kOpTl];
        tl_[rear][0]  = r[k4Op1 + kOpSize * 2 + kOpTl];
        tl_[rear][1]  = r[k4Op1 + kOpSize * 3 + kOpTl];
        // キャリア（音量を効かせるオペレータ）。bit0 がモジュレータ側、bit1 がキャリア側。
        if (cs) {
            // ymfm の s_algorithm_ops の 8-11。
            static constexpr uint8_t kFront[4] = {0, 1, 2, 1};   // CNT2:CNT1 の順
            static constexpr uint8_t kRear[4]  = {2, 2, 2, 3};
            const int a = (cnt2 << 1) | cnt1;
            carriers_[front] = kFront[a];
            carriers_[rear]  = kRear[a];
        } else {
            carriers_[front] = static_cast<uint8_t>(cnt1 ? 3 : 2);
            carriers_[rear]  = static_cast<uint8_t>(cnt2 ? 3 : 2);
        }
        l.transpose = static_cast<int8_t>(r[k4Trans]);
        l.detune = static_cast<int8_t>(r[k4Detune]);
        writeLevels(front, l.volume);
        writeLevels(rear, l.volume);
    }

    void setPan(uint8_t ch, uint8_t value) override {
        const uint8_t bits = panBits(value);
        if (ch == kOpl3Rhythm) {
            for (uint8_t p = kRhyFirst; p < kRhyFirst + 3; ++p) writePan(p, bits);
            return;
        }
        if (ch > kOpl3Rhythm) return;
        uint8_t front, rear;
        if (pairOf(ch, front, rear)) {
            writePan(front, bits);
            writePan(rear, bits);
        } else {
            writePan(ch, bits);
        }
    }

    void rhythmVolume(uint8_t target, uint8_t level) override { rhythm_.setLevel(target, level); }

    void rhythmStrike(uint8_t instruments, uint8_t accents) override {
        rhythm_.accents = accents;
        writeRhythmLevels(accents);
        const uint8_t base = static_cast<uint8_t>(rhythm_.mode & ~RhythmState::kAll);
        write(0, kRegRhythm, base);
        rhythm_.mode = static_cast<uint8_t>(base | (instruments & RhythmState::kAll));
        write(0, kRegRhythm, rhythm_.mode);
    }

    bool regRead(uint8_t port, uint8_t reg, uint8_t& value) override {
        if (port > 1) return false;
        value = shadow_[port][reg];
        return true;
    }

    bool regWrite(uint8_t port, uint8_t reg, uint8_t value) override {
        if (port > 1) return false;
        write(port, reg, value);
        if (reg >= kRegFnumH && reg < kRegFnumH + kChPerPort) {
            fnh_[port * kChPerPort + reg - kRegFnumH] = value;
        } else if (port == 0 && reg == kRegRhythm) {
            rhythm_.mode = value;
        }
        return true;
    }

private:
    struct Logical {
        uint8_t volume    = 0;
        int8_t  transpose = 0;
        int8_t  detune    = 0;   // 1/64 半音
    };

    void write(uint8_t port, uint8_t reg, uint8_t value) {
        shadow_[port][reg] = value;
        bus_.write(Device::OPL3, reg, value, port);
    }

    void fill(uint8_t port, uint8_t first, uint8_t count, uint8_t value) {
        for (uint8_t i = 0; i < count; ++i) write(port, static_cast<uint8_t>(first + i), value);
    }

    // 物理チャンネル p（0-17）の、組の中でのレジスタの位置。
    static uint8_t portOf(uint8_t p)  { return static_cast<uint8_t>(p / kChPerPort); }
    static uint8_t localOf(uint8_t p) { return static_cast<uint8_t>(p % kChPerPort); }

    static bool pairOf(uint8_t ch, uint8_t& front, uint8_t& rear) {
        if (ch < kOpl3FourOpFirst || ch >= kOpl3Rhythm) return false;
        const uint8_t i = static_cast<uint8_t>(ch - kOpl3FourOpFirst);
        front = static_cast<uint8_t>((i < 3) ? i : kChPerPort + (i - 3));
        rear  = static_cast<uint8_t>(front + 3);
        return true;
    }

    bool isFourOp(uint8_t ch) const {
        return (fourOp_ >> (ch - kOpl3FourOpFirst)) & 1;
    }

    // 4OP で鳴らす組でも、後ろ側のキーを前側と揃えて書く。ymfm は 104h の変化を次の
    // クロックでオペレータの割り当てに反映するので、同じ並びで 104h を立ててすぐ前側だけを
    // キーオンすると OP3・OP4 がキーオンされず、FM-FM では鳴らない（YMEngine 9812c3d で
    // 確かめた）。実機は 4OP の組の後ろ側のキーを見ない（推測：YMF262 の 4OP の説明から。
    // 実機では確かめていない）。
    void key(uint8_t ch, bool on) {
        if (ch >= kOpl3Rhythm) return;
        uint8_t front, rear;
        if (!pairOf(ch, front, rear)) {
            writeKey(ch, on);
            return;
        }
        writeKey(front, on);
        writeKey(rear, on);
    }

    void writeKey(uint8_t p, bool on) {
        fnh_[p] = static_cast<uint8_t>(on ? (fnh_[p] | kKey) : (fnh_[p] & ~kKey));
        write(portOf(p), static_cast<uint8_t>(kRegFnumH + localOf(p)), fnh_[p]);
    }

    void writePitch(uint8_t p, uint8_t note, int16_t bend) {
        const OplPitch pitch = oplPitch(true, note, bend);
        const uint8_t port = portOf(p);
        const uint8_t local = localOf(p);
        write(port, static_cast<uint8_t>(kRegFnumL + local), static_cast<uint8_t>(pitch.fnum & 0xFF));
        const uint8_t bits = static_cast<uint8_t>((pitch.block << 2) | ((pitch.fnum >> 8) & 3));
        fnh_[p] = static_cast<uint8_t>((fnh_[p] & ~0x1F) | bits);
        write(port, static_cast<uint8_t>(kRegFnumH + local), fnh_[p]);
    }

    // FB と接続だけを書き、出力の4ビットは残す。
    void writeFbCon(uint8_t p, uint8_t fbcon) {
        const uint8_t reg = static_cast<uint8_t>(kRegFbCon + localOf(p));
        const uint8_t port = portOf(p);
        write(port, reg, static_cast<uint8_t>((shadow_[port][reg] & 0xF0) | (fbcon & 0x0F)));
    }

    // 出力 A と B だけを書き、C と D は残す（bytecode.md の `87`）。
    void writePan(uint8_t p, uint8_t bits) {
        const uint8_t reg = static_cast<uint8_t>(kRegFbCon + localOf(p));
        const uint8_t port = portOf(p);
        write(port, reg, static_cast<uint8_t>((shadow_[port][reg] & ~kOutAB) | bits));
    }

    void writeOperator(uint8_t p, bool carrier, const uint8_t* op) {
        const uint8_t slot = static_cast<uint8_t>(kSlot[localOf(p)] + (carrier ? kCarrier : 0));
        const uint8_t port = portOf(p);
        write(port, static_cast<uint8_t>(kRegAmVib + slot), op[kOpMult]);
        write(port, static_cast<uint8_t>(kRegKslTl + slot), op[kOpTl]);
        write(port, static_cast<uint8_t>(kRegArDr + slot), op[kOpAr]);
        write(port, static_cast<uint8_t>(kRegSlRr + slot), op[kOpSl]);
        write(port, static_cast<uint8_t>(kRegWaveSel + slot), static_cast<uint8_t>(op[kOpWave] & 0x07));
    }

    void load2op(uint8_t p, const uint8_t* r) {
        writeFbCon(p, static_cast<uint8_t>(r[kRecFb] & 0x0F));
        writeOperator(p, false, r + kRecMod);
        writeOperator(p, true,  r + kRecCar);
        tl_[p][0] = r[kRecMod + kOpTl];
        tl_[p][1] = r[kRecCar + kOpTl];
        carriers_[p] = static_cast<uint8_t>((r[kRecFb] & 1) ? 3 : 2);
    }

    // キャリアのレベルは、音色自身のものに V が引くぶんを足したもの（OPL2EX と同じ量）。
    void writeLevels(uint8_t p, uint8_t loudness) {
        const uint8_t take = fmAttenuation(loudness, kTlMin);
        for (uint8_t i = 0; i < 2; ++i) {
            if (!((carriers_[p] >> i) & 1)) continue;
            uint16_t level = static_cast<uint16_t>((tl_[p][i] & kTlMask) + take);
            if (level > kTlMin) level = kTlMin;
            const uint8_t slot = static_cast<uint8_t>(kSlot[localOf(p)] + (i ? kCarrier : 0));
            write(portOf(p), static_cast<uint8_t>(kRegKslTl + slot),
                  static_cast<uint8_t>((tl_[p][i] & kKslMask) | level));
        }
    }

    void writeRhythmOut(uint8_t slot, uint8_t attenuation) {
        const uint8_t reg = static_cast<uint8_t>(kRegKslTl + slot);
        const uint8_t level = static_cast<uint8_t>(attenuation * 4);   // OPLL の1段が4段
        write(0, reg, static_cast<uint8_t>((shadow_[0][reg] & kKslMask) | level));
    }

    void writeRhythmLevels(uint8_t accents) {
        writeRhythmOut(kRhySlotBd,  rhythm_.attenuation(RhythmState::kBassDrum, accents));
        writeRhythmOut(kRhySlotSd,  rhythm_.attenuation(RhythmState::kSnare, accents));
        writeRhythmOut(kRhySlotTom, rhythm_.attenuation(RhythmState::kTom, accents));
        writeRhythmOut(kRhySlotTc,  rhythm_.attenuation(RhythmState::kCymbal, accents));
        writeRhythmOut(kRhySlotHh,  rhythm_.attenuation(RhythmState::kHiHat, accents));
    }

    ChipBus& bus_;
    std::array<std::array<uint8_t, 256>, 2> shadow_{};
    std::array<Logical, kOpl3Rhythm> logical_{};
    std::array<uint8_t, kPhysChannels> fnh_{};
    std::array<std::array<uint8_t, 2>, kPhysChannels> tl_{};   // モジュレータ、キャリアの 40h
    std::array<uint8_t, kPhysChannels> carriers_{};
    uint8_t fourOp_ = 0;     // 4OP のチャンネルのうち、いま 4OP で鳴らしているもの（104h）
    RhythmState rhythm_;
};

} // namespace

std::unique_ptr<SoundDevice> makeOpl3Device(ChipBus& bus) {
    return std::make_unique<Opl3Device>(bus);
}

} // namespace y8960
