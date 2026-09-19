// OPL2EX（YM3812 ＋ ADPCM-B）。Y8960BasicExtension の src/dev/opl2ex.asm と
// src/dev/rhythm.asm の写し。ADPCM チャンネル（9）はまだ鳴らさない。

#include "dev_internal.h"
#include "freqtab.h"
#include "pitch.h"

namespace y8960 {

namespace {

constexpr uint8_t kChannels   = 9;
constexpr uint8_t kRegAmVib   = 0x20;
constexpr uint8_t kRegKslTl   = 0x40;
constexpr uint8_t kRegArDr    = 0x60;
constexpr uint8_t kRegSlRr    = 0x80;
constexpr uint8_t kRegFnumL   = 0xA0;
constexpr uint8_t kRegFnumH   = 0xB0;
constexpr uint8_t kRegRhythm  = 0xBD;
constexpr uint8_t kRegFbCon   = 0xC0;
constexpr uint8_t kRegWaveSel = 0xE0;
constexpr uint8_t kRegFlagCtl = 0x04;
constexpr uint8_t kIrqReset   = 0x80;
constexpr uint8_t kMaskAll    = 0x78;
constexpr uint8_t kOpRegs     = 22;    // 1オペレータ範囲のレジスタ数
constexpr uint8_t kKey        = 0x20;
constexpr uint8_t kRhythmOn   = 0x20;
constexpr uint8_t kKslMask    = 0xC0;
constexpr uint8_t kTlMask     = 0x3F;
constexpr uint8_t kTlMin      = 63;    // 減衰なので、これが無音
constexpr uint8_t kFnumMask   = 0x03;
constexpr uint8_t kBlkShift   = 2;
constexpr uint8_t kCarrier    = 3;
constexpr uint8_t kRhyFirst   = 6;

// 3つのチャンネルが6つのスロットを分け合うので、算術では出せない。
constexpr uint8_t kSlot[kChannels] = {0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12};

// リズムの5つの楽器が使うオペレータ。
constexpr uint8_t kRhySlotBd  = 0x13;
constexpr uint8_t kRhySlotSd  = 0x14;
constexpr uint8_t kRhySlotTom = 0x12;
constexpr uint8_t kRhySlotTc  = 0x15;
constexpr uint8_t kRhySlotHh  = 0x11;

// リズムモードで ch6-8 が持つ音程。MSX-AUDIO BASIC の値（OPLL の F-Number の倍）。
constexpr uint8_t kRhythmPitch[][2] = {
    {kRegFnumL + 6, 0x40}, {kRegFnumH + 6, 0x0A},
    {kRegFnumL + 7, 0xA0}, {kRegFnumH + 7, 0x0A},
    {kRegFnumL + 8, 0x80}, {kRegFnumH + 8, 0x03},
};

// ADPCM（チャンネル9）。
constexpr uint8_t kRegAdpcmCtl  = 0x07;
constexpr uint8_t kRegAdpcmStL  = 0x09;
constexpr uint8_t kRegAdpcmStH  = 0x0A;
constexpr uint8_t kRegAdpcmEdL  = 0x0B;
constexpr uint8_t kRegAdpcmEdH  = 0x0C;
constexpr uint8_t kRegAdpcmDnL  = 0x10;
constexpr uint8_t kRegAdpcmDnH  = 0x11;
constexpr uint8_t kRegAdpcmVol  = 0x12;
// START だけではサンプルメモリを読まない。bit5（MEMORY_DATA）が再生元を外部
// メモリにする（ymfm の external()、openMSX の R07_MEMORY_DATA）。無いと CPU
// ポートの値を読み続け、出力が一定値に張り付く。
constexpr uint8_t kAdpcmStart   = 0xA0;
constexpr uint8_t kAdpcmReset   = 0x01;
constexpr uint8_t kAdpcmRefNote = 64;      // O5 E。録音した速さで鳴る音
constexpr uint16_t kAdpcmDnDefault = 10546; // 8000Hz を 49716Hz に対して
constexpr uint16_t kAdpcmDnMax     = 0xFFFF;

// サンプリング周波数から delta-N（adpcm.asm の MKDELTA）。
// delta-N は rate * 65536 / 49716 で、65536/49716 は 1 + 20856/65536。
uint16_t makeDelta(uint16_t hz) {
    const uint32_t product = static_cast<uint32_t>(hz) * 20856;
    uint32_t delta = (product >> 16) + hz;
    if ((product & 0xFFFF) >= 0x8000) ++delta;
    return (delta > 0xFFFF) ? kAdpcmDnMax : static_cast<uint16_t>(delta);
}

// DE × A ÷ 256（adpcm.asm の MULFRAC）。足した桁あふれは落ちる。
uint16_t mulFraction(uint16_t base, uint8_t fraction) {
    uint16_t acc = 0;
    uint8_t  bits = fraction;
    for (int i = 0; i < 8; ++i) {
        if (bits & 1) acc = static_cast<uint16_t>(acc + base);
        bits = static_cast<uint8_t>(bits >> 1);
        acc = static_cast<uint16_t>(acc >> 1);
    }
    return acc;
}

// 音色レコードの中の位置。
constexpr int kRecTrans = 8;    // 2バイト、8.8 の半音
constexpr int kRecFb    = 10;
constexpr int kRecMod   = 16;
constexpr int kRecCar   = 24;
constexpr int kOpMult   = 0;
constexpr int kOpTl     = 1;
constexpr int kOpAr     = 2;
constexpr int kOpSl     = 3;
constexpr int kOpWave   = 5;

class Opl2exDevice final : public SoundDevice {
public:
    Opl2exDevice(ChipBus& bus, Device which) : bus_(bus), which_(which) {}

    void reset() override {
        // フラグは落としてから全部マスクする。開いたままだと、カートリッジが
        // 割り込み線を下げっぱなしにする。
        write(kRegFlagCtl, kIrqReset);
        write(kRegFlagCtl, kMaskAll);
        write(0x07, 0x01);                    // ADPCM リセット
        write(0x08, 0x00);
        for (uint8_t r = 0x09; r <= 0x0C; ++r) write(r, 0);
        for (uint8_t r = 0x10; r <= 0x12; ++r) write(r, 0);
        fill(kRegAmVib, kOpRegs, 0);
        fill(kRegKslTl, kOpRegs, kTlMin);
        fill(kRegArDr, kOpRegs, 0);
        fill(kRegSlRr, kOpRegs, 0);
        fill(kRegFnumL, kChannels, 0);
        fill(kRegFnumH, kChannels, 0);
        fill(kRegFbCon, kChannels, 0);
        fill(kRegWaveSel, kOpRegs, 0);
        for (uint8_t ch = 0; ch < kChannels; ++ch) {
            fnh_[ch] = 0;
            volume_[ch] = 0;
            transpose_[ch] = 0;
            voiceTl_[ch] = kTlMin;
        }
        write(kRegRhythm, 0);
        rhythm_ = RhythmState{};
        file_ = kAdpcmFiles;
        baseDelta_ = kAdpcmDnDefault;
    }

    void setRhythmMode(bool on, const VoiceRecord* rhythmVoices) override {
        // BDh の bit7-6 は AM と vibrato の深さで、プログラムが `Y` で決めるもの。
        rhythm_.mode = static_cast<uint8_t>((rhythm_.mode & 0xC0) | (on ? kRhythmOn : 0));
        write(kRegRhythm, rhythm_.mode);
        if (!on) return;
        rhythm_.toDefaults();
        // 5つの楽器は ch6-8 のオペレータなので、リズムモードではその3本に音色が要る。
        // どのイベントも名指さないので、ブロックが索引 32-34 に持っている。
        if (rhythmVoices) {
            for (uint8_t i = 0; i < 3; ++i) {
                if (rhythmVoices[i].kind == RecordKind::None) continue;
                loadVoice(static_cast<uint8_t>(kRhyFirst + i), rhythmVoices[i].data.data());
            }
        }
        for (const auto& p : kRhythmPitch) {
            write(p[0], p[1]);
            if (p[0] >= kRegFnumH && p[0] < kRegFnumH + kChannels) fnh_[p[0] - kRegFnumH] = p[1];
        }
    }

    void setAdpcmDirectory(const AdpcmVoiceFile* directory) override { directory_ = directory; }

    void keyOn(uint8_t ch) override {
        if (ch == kChannelAdpcm) {
            adpcmKeyOn();
            return;
        }
        if (ch >= kChannels) return;
        fnh_[ch] = static_cast<uint8_t>(fnh_[ch] | kKey);
        write(static_cast<uint8_t>(kRegFnumH + ch), fnh_[ch]);
    }

    void keyOff(uint8_t ch) override {
        if (ch == kChannelAdpcm) {
            // このチャンネルに release は無い。止めることが止めること。
            write(kRegAdpcmCtl, kAdpcmReset);
            return;
        }
        if (ch >= kChannels) return;
        fnh_[ch] = static_cast<uint8_t>(fnh_[ch] & ~kKey);
        write(static_cast<uint8_t>(kRegFnumH + ch), fnh_[ch]);
    }

    void setVolume(uint8_t ch, uint8_t loudness) override {
        if (ch == kChannelRhythm) {
            rhythm_.scale = loudness;
            writeRhythmLevels(rhythm_.accents);
            return;
        }
        if (ch == kChannelAdpcm) {
            // レベルレジスタは8ビットで、減衰ではなくレベル。ラウドネスを倍にする。
            write(kRegAdpcmVol, static_cast<uint8_t>(loudness * 2));
            return;
        }
        if (ch >= kChannels) return;
        volume_[ch] = loudness;
        writeLevel(ch);
    }

    // 音色のトランスポーズを先に足す。いくつかのプリセットは鳴る場所と違う高さで
    // 書かれていて、レコードがその補正を持っている。
    void setPitch(uint8_t ch, uint8_t note, int16_t bend) override {
        if (ch == kChannelAdpcm) {
            adpcmPitch(note, bend);
            return;
        }
        if (ch >= kChannels) return;
        const int wanted = static_cast<int>(note) + transpose_[ch];
        const uint8_t clamped = static_cast<uint8_t>(wanted < 0 ? 0 : (wanted > 255 ? 255 : wanted));
        const OplPitch p = oplPitch(true, clamped, bend);
        write(static_cast<uint8_t>(kRegFnumL + ch), static_cast<uint8_t>(p.fnum & 0xFF));
        const uint8_t bits = static_cast<uint8_t>((p.block << kBlkShift) | ((p.fnum >> 8) & kFnumMask));
        fnh_[ch] = static_cast<uint8_t>((fnh_[ch] & ~(kFnumMask | (7 << kBlkShift))) | bits);
        write(static_cast<uint8_t>(kRegFnumH + ch), fnh_[ch]);
    }

    // 旋律チャンネルの 0-63 は音色表の音色で、こちらは表を持たないので捨てる
    // （doc/rom-feedback.md の B1）。ADPCM チャンネルではボイスファイル番号。
    void setVoice(uint8_t ch, uint8_t number) override {
        if (ch != kChannelAdpcm || number >= kAdpcmFiles) return;
        file_ = number;
        // 音符はそのサンプル自身の速さに対して測るので、キーオンより前に出しておく。
        const AdpcmVoiceFile* entry = fileEntry();
        if (entry) baseDelta_ = makeDelta(entry->sampleRateHz);
    }

    void seqVoice(uint8_t ch, uint8_t slot, const VoiceRecord& record) override {
        (void)slot;
        if (ch >= kChannels) return;
        loadVoice(ch, record.data.data());
    }

    void rhythmVolume(bool accent, uint8_t level) override {
        if (accent) rhythm_.accent = level;
        else        rhythm_.level  = level;
    }

    void rhythmStrike(uint8_t instruments, uint8_t accents) override {
        rhythm_.accents = accents;
        writeRhythmLevels(accents);
        const uint8_t base = static_cast<uint8_t>(rhythm_.mode & ~RhythmState::kAll);
        write(kRegRhythm, base);
        rhythm_.mode = static_cast<uint8_t>(base | (instruments & RhythmState::kAll));
        write(kRegRhythm, rhythm_.mode);
    }

    bool regRead(uint8_t reg, uint8_t& value) override {
        value = shadow_[reg];
        return true;
    }

    bool regWrite(uint8_t reg, uint8_t value) override {
        write(reg, value);
        if (reg >= kRegFnumH && reg < kRegFnumH + kChannels) fnh_[reg - kRegFnumH] = value;
        else if (reg == kRegRhythm) rhythm_.mode = value;
        return true;
    }

private:
    void write(uint8_t reg, uint8_t value) {
        shadow_[reg] = value;
        bus_.write(which_, reg, value);
    }

    void fill(uint8_t first, uint8_t count, uint8_t value) {
        for (uint8_t i = 0; i < count; ++i) write(static_cast<uint8_t>(first + i), value);
    }

    // チップが取るレベルは音色自身のものに V が引くぶんを足したもの。片方だけでは
    // 書けないので、対から組み立てる。
    void writeLevel(uint8_t ch) {
        const uint8_t take = static_cast<uint8_t>((kMixerMaxLoudness - volume_[ch]) >> 1);
        uint16_t level = static_cast<uint16_t>((voiceTl_[ch] & kTlMask) + take);
        if (level > kTlMin) level = kTlMin;
        const uint8_t value = static_cast<uint8_t>((voiceTl_[ch] & kKslMask) | level);
        write(static_cast<uint8_t>(kRegKslTl + kSlot[ch] + kCarrier), value);
    }

    void loadVoice(uint8_t ch, const uint8_t* r) {
        write(static_cast<uint8_t>(kRegFbCon + ch), static_cast<uint8_t>(r[kRecFb] & 0x0F));
        writeOperator(static_cast<uint8_t>(kSlot[ch]), r + kRecMod);
        writeOperator(static_cast<uint8_t>(kSlot[ch] + kCarrier), r + kRecCar);
        voiceTl_[ch] = r[kRecCar + kOpTl];
        // トランスポーズは 8.8 の符号付き半音。近いほうの整数半音を取る。
        int8_t whole = static_cast<int8_t>(r[kRecTrans + 1]);
        if (r[kRecTrans] >= 0x80) whole = static_cast<int8_t>(whole + 1);
        transpose_[ch] = whole;
        writeLevel(ch);
    }

    void writeOperator(uint8_t slot, const uint8_t* op) {
        write(static_cast<uint8_t>(kRegAmVib + slot), op[kOpMult]);
        write(static_cast<uint8_t>(kRegKslTl + slot), op[kOpTl]);
        write(static_cast<uint8_t>(kRegArDr + slot), op[kOpAr]);
        write(static_cast<uint8_t>(kRegSlRr + slot), op[kOpSl]);
        write(static_cast<uint8_t>(kRegWaveSel + slot), static_cast<uint8_t>(op[kOpWave] & 0x03));
    }

    // 打撃はレベルだけを持つ。その上のキースケールはドライバのものではないので、
    // 控えにあるものを残す。
    void writeRhythmOut(uint8_t slot, uint8_t attenuation) {
        const uint8_t reg = static_cast<uint8_t>(kRegKslTl + slot);
        const uint8_t level = static_cast<uint8_t>(attenuation * 4);   // OPLL の1段が4段
        write(reg, static_cast<uint8_t>((shadow_[reg] & kKslMask) | level));
    }

    void writeRhythmLevels(uint8_t accents) {
        writeRhythmOut(kRhySlotBd,  rhythm_.attenuation(RhythmState::kBassDrum, accents));
        writeRhythmOut(kRhySlotSd,  rhythm_.attenuation(RhythmState::kSnare, accents));
        writeRhythmOut(kRhySlotTom, rhythm_.attenuation(RhythmState::kTom, accents));
        writeRhythmOut(kRhySlotTc,  rhythm_.attenuation(RhythmState::kCymbal, accents));
        writeRhythmOut(kRhySlotHh,  rhythm_.attenuation(RhythmState::kHiHat, accents));
    }

    const AdpcmVoiceFile* fileEntry() const {
        if (!directory_ || file_ >= kAdpcmFiles) return nullptr;
        const AdpcmVoiceFile& e = directory_[file_];
        return e.present ? &e : nullptr;
    }

    // ページ（256バイト）を、アドレスレジスタが数える4バイト単位に直す。
    static uint16_t pageToAddress(uint16_t page) { return static_cast<uint16_t>(page << 6); }

    void adpcmKeyOn() {
        const AdpcmVoiceFile* e = fileEntry();
        if (!e) return;                 // 置き場所を持たないボイスファイル
        const uint16_t last = pageToAddress(static_cast<uint16_t>(e->startPage + e->pages));
        write(kRegAdpcmEdL, static_cast<uint8_t>((last - 1) & 0xFF));
        write(kRegAdpcmEdH, static_cast<uint8_t>((last - 1) >> 8));
        const uint16_t start = pageToAddress(e->startPage);
        write(kRegAdpcmStL, static_cast<uint8_t>(start & 0xFF));
        write(kRegAdpcmStH, static_cast<uint8_t>(start >> 8));
        write(kRegAdpcmCtl, kAdpcmStart);
    }

    // 音符は O5 E から測る。半音ごとに再生速度が 2 の 12 乗根倍になるので、
    // 半音は表引き、オクターブはシフト。
    void adpcmPitch(uint8_t note, int16_t bend) {
        int octave = 0;
        int semitone = static_cast<int>(note) - kAdpcmRefNote;
        while (semitone < 0)   { --octave; semitone += 12; }
        while (semitone >= 12) { ++octave; semitone -= 12; }

        int index = semitone * kFreqSemitone + bend;
        while (index < 0)            { index += kFreqSteps; --octave; }
        while (index >= kFreqSteps)  { index -= kFreqSteps; ++octave; }

        const uint16_t ratio = freqEntry(FreqFamily::Pcm, index);
        // 表の 16 ビットを、MULFRAC が取る 8 ビットへ丸める。
        uint16_t rounded = static_cast<uint16_t>((ratio >> 8) + ((ratio & 0xFF) >= 0x80 ? 1 : 0));
        const uint8_t fraction = (rounded > 0xFF) ? 0xFF : static_cast<uint8_t>(rounded);

        uint32_t rate = mulFraction(baseDelta_, fraction);
        rate = static_cast<uint16_t>(rate + baseDelta_);      // 比率が持つ 1.0
        bool over = false;
        for (int i = 0; i < octave; ++i) {
            if (rate & 0x8000) { over = true; break; }
            rate = static_cast<uint16_t>(rate << 1);
        }
        for (int i = 0; i > octave; --i) rate = static_cast<uint16_t>(rate >> 1);
        if (over) rate = kAdpcmDnMax;   // チップの上限で止める

        write(kRegAdpcmDnL, static_cast<uint8_t>(rate & 0xFF));
        write(kRegAdpcmDnH, static_cast<uint8_t>(rate >> 8));
    }

    static constexpr uint8_t kMixerMaxLoudness = 127;

    ChipBus& bus_;
    Device   which_;
    std::array<uint8_t, 256> shadow_{};
    std::array<uint8_t, kChannels> fnh_{};
    std::array<uint8_t, kChannels> volume_{};
    std::array<int8_t,  kChannels> transpose_{};
    std::array<uint8_t, kChannels> voiceTl_{};
    RhythmState rhythm_;
    const AdpcmVoiceFile* directory_ = nullptr;
    uint8_t  file_      = kAdpcmFiles;          // まだ選ばれていない
    uint16_t baseDelta_ = kAdpcmDnDefault;
};

} // namespace

std::unique_ptr<SoundDevice> makeOpl2exDevice(ChipBus& bus, Device which) {
    return std::make_unique<Opl2exDevice>(bus, which);
}

} // namespace y8960
