// OPNA（YM2608、デバイス 10）と OPNB（YM2610/YM2610B、デバイス 11）。
// ROM に写し元は無く、bytecode.md の定めから作る。
//
//   チャンネル 0-5  FM。0-2 が1組目（port 0）、3-5 が2組目（port 1）のレジスタ
//   チャンネル 6-8  SSG。SSGS と同じドライバ（dev_ssgs.cpp）に渡す
//   チャンネル 9    OPNA はリズム、OPNB は ADPCM-A。どちらも6つの楽器を叩く
//   チャンネル 10   ADPCM-B。OPL2EX の ADPCM チャンネルと同じくボイスファイルを鳴らす
//
// ADPCM-A と ADPCM-B のレジスタは、2つのチップで置き場所が違う。

#include "dev_internal.h"
#include "fmvoice.h"
#include "pitch.h"

#include <cmath>

namespace y8960 {

namespace {

constexpr uint8_t kFmChannels = 6;
constexpr uint8_t kSsgRegs    = 0x0E;  // port 0 の 00h-0Dh が SSG
constexpr uint8_t kChPerPort  = 3;
constexpr uint8_t kFxChannel  = 2;     // 効果音モードを持つのは3チャンネル目だけ
constexpr uint8_t kSubs       = 3;

constexpr uint8_t kRegMode    = 0x27;  // bit7-6 がチャンネル 2 のモード、bit5-0 はタイマー
constexpr uint8_t kModeFx     = 0x40;
constexpr uint8_t kRegKeyOn   = 0x28;
constexpr uint8_t kRegDtMul   = 0x30;
constexpr uint8_t kRegTl      = 0x40;
constexpr uint8_t kRegKsAr    = 0x50;
constexpr uint8_t kRegAmDr    = 0x60;
constexpr uint8_t kRegSr      = 0x70;
constexpr uint8_t kRegSlRr    = 0x80;
constexpr uint8_t kRegSsgEg   = 0x90;
constexpr uint8_t kRegFnumL   = 0xA0;
constexpr uint8_t kRegFnumH   = 0xA4;
constexpr uint8_t kRegFbAlg   = 0xB0;
constexpr uint8_t kRegPanSens = 0xB4;
constexpr uint8_t kPanL       = 0x80;  // ymfm の ch_output_0
constexpr uint8_t kPanR       = 0x40;
constexpr uint8_t kPanLR      = kPanL | kPanR;
constexpr uint8_t kTlMax      = 127;

// レコードのオペレータ（M1・C1・M2・C2）が置かれるレジスタの位置。スロット 1・2・3・4 は
// レジスタの上で +0・+8・+4・+12。
constexpr uint8_t kOpOffset[4] = {0, 8, 4, 12};

// 効果音モードのサブチャンネル 1-3（OP1-OP3）の Block/F-Number 上位と下位。
constexpr uint8_t kSubRegs[kSubs][2] = {{0xAD, 0xA9}, {0xAE, 0xAA}, {0xAC, 0xA8}};

// OPNA のリズム（port 0）。
constexpr uint8_t kRegRhyKey   = 0x10;   // bit7 がダンプ、bit5-0 が楽器
constexpr uint8_t kRegRhyTotal = 0x11;
constexpr uint8_t kRegRhyLevel = 0x18;   // 18h-1Dh。bit7-6 が定位、bit4-0 がレベル
// OPNB の ADPCM-A（port 1）。
constexpr uint8_t kRegAKey     = 0x00;
constexpr uint8_t kRegATotal   = 0x01;
constexpr uint8_t kRegALevel   = 0x08;
constexpr uint8_t kRegAStartL  = 0x10;
constexpr uint8_t kRegAStartH  = 0x18;
constexpr uint8_t kRegAEndL    = 0x20;
constexpr uint8_t kRegAEndH    = 0x28;
constexpr uint8_t kDump        = 0x80;
constexpr uint8_t kTotalMax    = 0x3F;
constexpr uint8_t kLevelMax    = 31;
constexpr uint8_t kLevelNormal = 24;     // 演奏を始めるときの通常音量とアクセント音量
constexpr uint8_t kLevelAccent = 31;
constexpr uint8_t kRhythmAll   = 0x3F;
constexpr int     kInstruments = 6;

// ADPCM-B の中の番号（ymfm の adpcm_b_registers）。OPNA は port 1 の 00h から、
// OPNB は port 0 の 10h から並ぶ。
constexpr uint8_t kBCtl1    = 0x00;
constexpr uint8_t kBCtl2    = 0x01;
constexpr uint8_t kBStartL  = 0x02;
constexpr uint8_t kBStartH  = 0x03;
constexpr uint8_t kBEndL    = 0x04;
constexpr uint8_t kBEndH    = 0x05;
constexpr uint8_t kBDeltaL  = 0x09;
constexpr uint8_t kBDeltaH  = 0x0A;
constexpr uint8_t kBLevel   = 0x0B;
constexpr uint8_t kBLimitL  = 0x0C;
constexpr uint8_t kBLimitH  = 0x0D;
// START と、再生元を外部メモリにする bit5（OPL2EX の ADPCM と同じ理由）。
constexpr uint8_t kBStart   = 0xA0;
constexpr uint8_t kBReset   = 0x01;
// OPNA のメモリを 8 ビット単位で使う。アドレスは 32 バイト単位になる。
constexpr uint8_t kBDram8   = 0x02;
constexpr uint8_t kBRefNote = 64;      // O5 E。録音した速さで鳴る音（OPL2EX と同じ）

uint8_t panBits(uint8_t value) {
    if (value < 4)  return kPanL;
    if (value < 12) return kPanLR;
    return kPanR;
}

class OpnDevice final : public SoundDevice {
public:
    OpnDevice(ChipBus& bus, Device which, SoftEnvelope& envelope)
        : bus_(bus), which_(which), ssg_(makeOpnSsg(bus, which, envelope)) {}

    void reset() override {
        const bool a = isOpna();
        if (a) write(0, 0x29, 0x80);           // FM を6チャンネルにする
        write(0, kRegMode, 0);
        write(0, 0x22, 0);
        for (uint8_t ch = 0; ch < kFmChannels; ++ch) {
            write(0, kRegKeyOn, keyCode(ch));
            const uint8_t port = portOf(ch);
            const uint8_t local = localOf(ch);
            write(port, static_cast<uint8_t>(kRegFbAlg + local), 0);
            write(port, static_cast<uint8_t>(kRegPanSens + local), kPanLR);
            write(port, static_cast<uint8_t>(kRegFnumH + local), 0);
            write(port, static_cast<uint8_t>(kRegFnumL + local), 0);
            for (uint8_t op = 0; op < 4; ++op) {
                const uint8_t o = static_cast<uint8_t>(kOpOffset[op] + local);
                write(port, static_cast<uint8_t>(kRegDtMul + o), 0);
                write(port, static_cast<uint8_t>(kRegTl + o), kTlMax);
                write(port, static_cast<uint8_t>(kRegKsAr + o), 0);
                write(port, static_cast<uint8_t>(kRegAmDr + o), 0);
                write(port, static_cast<uint8_t>(kRegSr + o), 0);
                write(port, static_cast<uint8_t>(kRegSlRr + o), 0xFF);
                write(port, static_cast<uint8_t>(kRegSsgEg + o), 0);
            }
            channels_[ch] = Channel{};
        }
        for (const auto& r : kSubRegs) {
            write(0, r[0], 0);
            write(0, r[1], 0);
        }
        subs_.fill(0);

        // リズムと ADPCM-A の全体の音量はチップ全体のもの。演奏を始める前の値として最大にする。
        if (a) {
            write(0, kRegRhyKey, static_cast<uint8_t>(kDump | kRhythmAll));
            write(0, kRegRhyTotal, kTotalMax);
            for (uint8_t i = 0; i < kInstruments; ++i) write(0, static_cast<uint8_t>(kRegRhyLevel + i), kPanLR);
        } else {
            write(1, kRegAKey, static_cast<uint8_t>(kDump | kRhythmAll));
            write(1, kRegATotal, kTotalMax);
            for (uint8_t i = 0; i < kInstruments; ++i) write(1, static_cast<uint8_t>(kRegALevel + i), kPanLR);
        }
        rhythmPan_ = kPanLR;
        rhythm_ = RhythmState{};
        rhythm_.toDefaults(kLevelNormal, kLevelAccent);
        bound_.fill(kUnbound);

        writeB(kBCtl1, kBReset);
        writeB(kBCtl2, static_cast<uint8_t>(kPanLR | (a ? kBDram8 : 0)));
        if (a) {
            writeB(kBLimitL, 0xFF);
            writeB(kBLimitH, 0xFF);
        }
        for (uint8_t r = kBStartL; r <= kBEndH; ++r) writeB(r, 0);
        writeB(kBLevel, 0);
        file_ = kAdpcmFiles;
        baseDelta_ = 0;

        ssg_->reset();
    }

    // OPNA と OPNB はリズムモードを持たない。演奏を始めるときに、リズムの音量が既定に
    // 戻り、ADPCM-A の楽器はどのサンプルにも結び付いていない状態になる（bytecode.md）。
    void setRhythmMode(bool on, const VoiceRecord* rhythmVoices) override {
        (void)on; (void)rhythmVoices;
        rhythm_.toDefaults(kLevelNormal, kLevelAccent);
        bound_.fill(kUnbound);
    }

    void rewindChannel(uint8_t ch) override {
        if (ch == kFxChannel) subs_.fill(0);
    }

    void setAdpcmDirectory(const AdpcmVoiceFile* directory) override { directory_ = directory; }
    void setAdpcmASamples(const AdpcmASample* samples) override { samplesA_ = samples; }

    void keyOn(uint8_t ch) override {
        if (ch < kFmChannels) {
            write(0, kRegKeyOn, static_cast<uint8_t>(channels_[ch].slots | keyCode(ch)));
        } else if (ch == kOpnAdpcmB) {
            adpcmKeyOn();
        } else {
            ssg_->keyOn(ch);
        }
    }

    void keyOff(uint8_t ch) override {
        if (ch < kFmChannels) {
            write(0, kRegKeyOn, keyCode(ch));
        } else if (ch == kOpnAdpcmB) {
            writeB(kBCtl1, kBReset);          // release は無い。止めることが止めること
        } else {
            ssg_->keyOff(ch);
        }
    }

    void setVolume(uint8_t ch, uint8_t loudness) override {
        if (ch < kFmChannels) {
            channels_[ch].volume = loudness;
            writeLevels(ch);
        } else if (ch == kOpnRhythm) {
            rhythm_.scale = loudness;         // シーケンスの音量ぶんだけが来る
            writeRhythmLevels(rhythm_.accents);
        } else if (ch == kOpnAdpcmB) {
            writeB(kBLevel, adpcmLevel(loudness));
        } else {
            ssg_->setVolume(ch, loudness);
        }
    }

    void setPitch(uint8_t ch, uint8_t note, int16_t bend) override {
        if (ch < kFmChannels) {
            const uint8_t n = transposed(note, channels_[ch].transpose);
            writeFnum(portOf(ch), static_cast<uint8_t>(kRegFnumH + localOf(ch)),
                      static_cast<uint8_t>(kRegFnumL + localOf(ch)), n, bend);
            // 効果音モードのサブチャンネルは、親の高さを書くたびに追従する。
            if (ch == kFxChannel && channels_[ch].fx) {
                for (uint8_t i = 0; i < kSubs; ++i) {
                    writeFnum(0, kSubRegs[i][0], kSubRegs[i][1], n,
                              static_cast<int16_t>(bend + subs_[i]));
                }
            }
        } else if (ch == kOpnAdpcmB) {
            adpcmPitch(note, bend);
        } else {
            ssg_->setPitch(ch, note, bend);
        }
    }

    // FM は内蔵の音色を持たないので捨てる。SSG は SSGS と同じ、ADPCM-B はボイスファイル番号。
    void setVoice(uint8_t ch, uint8_t number) override {
        if (ch == kOpnAdpcmB) {
            if (number >= kAdpcmFiles) return;
            file_ = number;
            const AdpcmVoiceFile* e = fileEntry();
            if (e) baseDelta_ = e->sampleRateHz * 65536.0 / (clock() / 144.0);
        } else if (ch >= kOpnSsgFirst && ch < kOpnRhythm) {
            ssg_->setVoice(ch, number);
        }
    }

    void seqVoice(uint8_t ch, uint8_t slot, const VoiceRecord& record) override {
        (void)slot;
        if (ch >= kFmChannels) return;
        const uint8_t* r = record.data.data();
        Channel& c = channels_[ch];
        const uint8_t port = portOf(ch);
        const uint8_t local = localOf(ch);
        write(port, static_cast<uint8_t>(kRegFbAlg + local), static_cast<uint8_t>(r[kFmAlg] & 0x3F));
        const uint8_t panReg = static_cast<uint8_t>(kRegPanSens + local);
        write(port, panReg, static_cast<uint8_t>((shadow_[port][panReg] & kPanLR) | (r[kFmSens] & 0x37)));
        for (uint8_t op = 0; op < 4; ++op) {
            const uint8_t* o = r + kFmOp1 + op * kFmOpSize;
            const uint8_t reg = static_cast<uint8_t>(kOpOffset[op] + local);
            write(port, static_cast<uint8_t>(kRegDtMul + reg), static_cast<uint8_t>(o[kFmOpDtMul] & 0x7F));
            write(port, static_cast<uint8_t>(kRegTl + reg), static_cast<uint8_t>(o[kFmOpTl] & 0x7F));
            write(port, static_cast<uint8_t>(kRegKsAr + reg), static_cast<uint8_t>(o[kFmOpKsAr] & 0xDF));
            write(port, static_cast<uint8_t>(kRegAmDr + reg), static_cast<uint8_t>(o[kFmOpAmDr] & 0x9F));
            write(port, static_cast<uint8_t>(kRegSr + reg), static_cast<uint8_t>(o[kFmOpDt2Sr] & 0x1F));
            write(port, static_cast<uint8_t>(kRegSlRr + reg), o[kFmOpSlRr]);
            write(port, static_cast<uint8_t>(kRegSsgEg + reg), static_cast<uint8_t>(o[kFmOpSsgEg] & 0x0F));
            c.tl[op] = static_cast<uint8_t>(o[kFmOpTl] & 0x7F);
        }
        c.slots = static_cast<uint8_t>(r[kFmSlots] & 0xF0);
        c.carriers = fmCarriers(r[kFmAlg]);
        c.transpose = static_cast<int8_t>(r[kFmTrans]);
        // FX はチャンネル 2 だけのもの。27h の bit5-0 はタイマーの制御なので残す。
        if (ch == kFxChannel) {
            c.fx = (r[kFmAlg] & kFmFx) != 0;
            write(0, kRegMode, static_cast<uint8_t>((shadow_[0][kRegMode] & 0x3F) | (c.fx ? kModeFx : 0)));
        }
        writeLevels(ch);
    }

    // チャンネル 2 以外では捨てる。差は FX の無い音色のあいだも持ち、setPitch が
    // FX の音色のあいだだけ使う（bytecode.md の `E9`）。
    void setSubPitch(uint8_t ch, uint8_t sub, int16_t steps) override {
        if (ch != kFxChannel || sub < 1 || sub > kSubs) return;
        subs_[sub - 1] = steps;
    }

    void setPan(uint8_t ch, uint8_t value) override {
        const uint8_t bits = panBits(value);
        if (ch < kFmChannels) {
            const uint8_t port = portOf(ch);
            const uint8_t reg = static_cast<uint8_t>(kRegPanSens + localOf(ch));
            write(port, reg, static_cast<uint8_t>((shadow_[port][reg] & ~kPanLR) | bits));
        } else if (ch == kOpnRhythm) {
            rhythmPan_ = bits;
            writeRhythmLevels(rhythm_.accents);
        } else if (ch == kOpnAdpcmB) {
            const uint8_t v = bShadow(kBCtl2);
            writeB(kBCtl2, static_cast<uint8_t>((v & ~kPanLR) | bits));
        }
    }

    void rhythmVolume(uint8_t target, uint8_t level) override { rhythm_.setLevel(target, level); }

    // OPNA は 11h、OPNB は ADPCM-A の 01h。0-63 をそのまま書く。
    void rhythmTotal(uint8_t level) override {
        if (isOpna()) write(0, kRegRhyTotal, static_cast<uint8_t>(level & kTotalMax));
        else          write(1, kRegATotal, static_cast<uint8_t>(level & kTotalMax));
    }

    // OPNB では、サンプルに結び付いていない楽器は叩かない。
    void rhythmStrike(uint8_t instruments, uint8_t accents) override {
        rhythm_.accents = accents;
        writeRhythmLevels(accents);
        uint8_t hit = static_cast<uint8_t>(instruments & kRhythmAll);
        if (isOpna()) {
            if (hit) write(0, kRegRhyKey, hit);
            return;
        }
        for (int i = 0; i < kInstruments; ++i) {
            if (!sampleOf(i)) hit = static_cast<uint8_t>(hit & ~(1u << i));
        }
        if (hit) write(1, kRegAKey, hit);
    }

    void bindAdpcmA(uint8_t instruments, uint8_t sample) override {
        if (isOpna()) return;
        for (uint8_t i = 0; i < kInstruments; ++i) {
            if (!(instruments & (1u << i))) continue;
            bound_[i] = sample;
            const AdpcmASample* s = sampleOf(i);
            if (!s) continue;                 // 書き手の誤り。叩いても鳴らさない
            const uint16_t end = static_cast<uint16_t>(s->startPage + s->pages - 1);
            write(1, static_cast<uint8_t>(kRegAStartL + i), static_cast<uint8_t>(s->startPage & 0xFF));
            write(1, static_cast<uint8_t>(kRegAStartH + i), static_cast<uint8_t>(s->startPage >> 8));
            write(1, static_cast<uint8_t>(kRegAEndL + i), static_cast<uint8_t>(end & 0xFF));
            write(1, static_cast<uint8_t>(kRegAEndH + i), static_cast<uint8_t>(end >> 8));
        }
    }

    void ssgEnv(SsgEnv kind, uint8_t ch, uint8_t value) override { ssg_->ssgEnv(kind, ch, value); }

    // SSG のレジスタは SSG のドライバが控えを持つ。
    bool regRead(uint8_t port, uint8_t reg, uint8_t& value) override {
        if (port > 1) return false;
        if (port == 0 && reg < kSsgRegs) return ssg_->regRead(0, reg, value);
        value = shadow_[port][reg];
        return true;
    }

    bool regWrite(uint8_t port, uint8_t reg, uint8_t value) override {
        if (port > 1) return false;
        if (port == 0 && reg < kSsgRegs) return ssg_->regWrite(0, reg, value);
        write(port, reg, value);
        return true;
    }

private:
    struct Channel {
        uint8_t volume    = 0;
        int8_t  transpose = 0;
        uint8_t slots     = 0xF0;   // 28h の bit7-4。音色が決める
        uint8_t carriers  = 0x08;   // レコードの並びの bit。C2 だけ
        bool    fx        = false;
        std::array<uint8_t, 4> tl{kTlMax, kTlMax, kTlMax, kTlMax};
    };

    static constexpr uint16_t kUnbound = 0x100;   // サンプル番号は 0-255 の全域を使う

    bool isOpna() const { return which_ == Device::OPNA; }
    uint32_t clock() const { return isOpna() ? kClockOpna : kClockOpnb; }

    static uint8_t portOf(uint8_t ch)  { return static_cast<uint8_t>(ch / kChPerPort); }
    static uint8_t localOf(uint8_t ch) { return static_cast<uint8_t>(ch % kChPerPort); }
    // 28h の下位3ビット。2組目は 4 から数える。
    static uint8_t keyCode(uint8_t ch) {
        return static_cast<uint8_t>((portOf(ch) << 2) | localOf(ch));
    }

    void write(uint8_t port, uint8_t reg, uint8_t value) {
        shadow_[port][reg] = value;
        bus_.write(which_, reg, value, port);
    }

    void writeB(uint8_t index, uint8_t value) {
        if (isOpna()) write(1, index, value);
        else          write(0, static_cast<uint8_t>(0x10 + index), value);
    }

    uint8_t bShadow(uint8_t index) const {
        return isOpna() ? shadow_[1][index] : shadow_[0][0x10 + index];
    }

    // 上位（Block を含む側）を先に書く。チップは下位を書いたときに両方を取り込む。
    void writeFnum(uint8_t port, uint8_t regHigh, uint8_t regLow, uint8_t note, int16_t bend) {
        const OpnPitch p = opnPitch(clock(), note, bend);
        write(port, regHigh, static_cast<uint8_t>((p.block << 3) | ((p.fnum >> 8) & 7)));
        write(port, regLow, static_cast<uint8_t>(p.fnum & 0xFF));
    }

    void writeLevels(uint8_t ch) {
        const Channel& c = channels_[ch];
        const uint8_t take = fmAttenuation(c.volume, kTlMax);
        const uint8_t port = portOf(ch);
        for (uint8_t op = 0; op < 4; ++op) {
            if (!((c.carriers >> op) & 1)) continue;
            const int level = c.tl[op] + take;
            write(port, static_cast<uint8_t>(kRegTl + kOpOffset[op] + localOf(ch)),
                  static_cast<uint8_t>(level > kTlMax ? kTlMax : level));
        }
    }

    // 楽器ごとのレベルは 0-31 をそのまま書く。チップの1段は 0.75 dB で音量の1と同じ
    // なので、シーケンスの音量は下げたぶんをそのまま引く。
    void writeRhythmLevels(uint8_t accents) {
        const uint8_t port = isOpna() ? 0 : 1;
        const uint8_t base = isOpna() ? kRegRhyLevel : kRegALevel;
        for (uint8_t i = 0; i < kInstruments; ++i) {
            const int raw = rhythm_.raw(static_cast<uint8_t>(1u << i), accents) & kLevelMax;
            const int v = raw - (127 - rhythm_.scale);
            write(port, static_cast<uint8_t>(base + i),
                  static_cast<uint8_t>(rhythmPan_ | (v < 0 ? 0 : v)));
        }
    }

    const AdpcmASample* sampleOf(int instrument) const {
        const uint16_t n = bound_[static_cast<size_t>(instrument)];
        if (n == kUnbound || !samplesA_ || !samplesA_[n].present) return nullptr;
        return &samplesA_[n];
    }

    const AdpcmVoiceFile* fileEntry() const {
        if (!directory_ || file_ >= kAdpcmFiles) return nullptr;
        const AdpcmVoiceFile& e = directory_[file_];
        return e.present ? &e : nullptr;
    }

    // チャンク 03 のページ（256 バイト）を、アドレスレジスタの単位に直す。OPNA は
    // 8 ビット単位の DRAM で 32 バイト、OPNB は 256 バイト（ymfm の address_shift）。
    uint16_t pageToAddress(uint16_t page) const {
        return static_cast<uint16_t>(isOpna() ? page << 3 : page);
    }

    void adpcmKeyOn() {
        const AdpcmVoiceFile* e = fileEntry();
        if (!e) return;                 // 置き場所を持たないボイスファイル
        const uint16_t start = pageToAddress(e->startPage);
        const uint16_t last  = static_cast<uint16_t>(pageToAddress(static_cast<uint16_t>(e->startPage + e->pages)) - 1);
        writeB(kBStartL, static_cast<uint8_t>(start & 0xFF));
        writeB(kBStartH, static_cast<uint8_t>(start >> 8));
        writeB(kBEndL, static_cast<uint8_t>(last & 0xFF));
        writeB(kBEndH, static_cast<uint8_t>(last >> 8));
        writeB(kBCtl1, kBStart);
    }

    // 音符は O5 E で録音した速さ。OPL2EX と同じ基準で、比は直接計算する。
    void adpcmPitch(uint8_t note, int16_t bend) {
        const double semis = (static_cast<int>(note) - kBRefNote) + bend / double(kFreqSemitone);
        const double v = baseDelta_ * std::pow(2.0, semis / 12.0);
        const long delta = std::lround(v > 65535.0 ? 65535.0 : v);
        writeB(kBDeltaL, static_cast<uint8_t>(delta & 0xFF));
        writeB(kBDeltaH, static_cast<uint8_t>(delta >> 8));
    }

    ChipBus& bus_;
    Device   which_;
    std::unique_ptr<SoundDevice> ssg_;
    std::array<std::array<uint8_t, 256>, 2> shadow_{};
    std::array<Channel, kFmChannels> channels_{};
    std::array<int16_t, kSubs> subs_{};        // `E9` の差。1/64 半音
    RhythmState rhythm_;
    uint8_t rhythmPan_ = kPanLR;
    std::array<uint16_t, kInstruments> bound_{};
    const AdpcmASample*   samplesA_  = nullptr;
    const AdpcmVoiceFile* directory_ = nullptr;
    uint8_t file_      = kAdpcmFiles;          // まだ選ばれていない
    double  baseDelta_ = 0;
};

} // namespace

std::unique_ptr<SoundDevice> makeOpnDevice(ChipBus& bus, Device which, SoftEnvelope& envelope) {
    return std::make_unique<OpnDevice>(bus, which, envelope);
}

} // namespace y8960
