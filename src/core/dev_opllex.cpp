// OPLLEX（YM2413 ＋ チャンネル独立プリセット音色バンク）。
// Y8960BasicExtension の src/dev/opllex.asm と src/dev/rhythm.asm の写し。

#include "dev_internal.h"
#include "pitch.h"

namespace y8960 {

namespace {

constexpr uint8_t kChannels  = 9;
constexpr uint8_t kRegUser   = 0x00;   // 00h-07h ユーザー音色
constexpr uint8_t kRegRhythm = 0x0E;
constexpr uint8_t kRegTest   = 0x0F;
constexpr uint8_t kRegFnumL  = 0x10;
constexpr uint8_t kRegFnumH  = 0x20;
constexpr uint8_t kRegInsVol = 0x30;
constexpr uint8_t kRegBank   = 0x40;
constexpr uint8_t kRegRhyLvl = 0x36;   // 36h-38h
constexpr uint8_t kRegCount  = 0x49;
constexpr uint8_t kKey       = 0x10;
constexpr uint8_t kRhythmOn  = 0x20;
constexpr uint8_t kVolMin    = 15;

// 音色レコード（チャンク 01、seqdef.inc の VP_*／VO_*）の中の位置。
constexpr int kRecFb    = 0;
constexpr int kRecTrans = 1;
constexpr int kRecMod   = 2;
constexpr int kRecCar   = 7;
constexpr int kOpTl     = 0;
constexpr int kOpAr     = 1;
constexpr int kOpSl     = 2;
constexpr int kOpMult   = 3;
constexpr int kOpWave   = 4;

// リズムモードで ch6-8 が持つ音程。MSX-MUSIC の FMBIOS が置く値。
constexpr uint8_t kRhythmPitch[][2] = {
    {kRegFnumL + 6, 0x20}, {kRegFnumH + 6, 0x05},   // バスドラム
    {kRegFnumL + 7, 0x50}, {kRegFnumH + 7, 0x05},   // ハイハットとスネア
    {kRegFnumL + 8, 0xC0}, {kRegFnumH + 8, 0x01},   // タムとシンバル
};

class OpllexDevice final : public SoundDevice {
public:
    OpllexDevice(ChipBus& bus, Device which) : bus_(bus), which_(which) {}

    void reset() override {
        for (uint8_t r = kRegUser; r < kRegUser + 8; ++r) write(r, 0);
        write(kRegRhythm, 0);
        write(kRegTest, 0);
        for (uint8_t ch = 0; ch < kChannels; ++ch) {
            write(static_cast<uint8_t>(kRegFnumL + ch), 0);
            write(static_cast<uint8_t>(kRegFnumH + ch), 0);   // キーオフ、ブロック 0
            write(static_cast<uint8_t>(kRegBank + ch), 0);
            write(static_cast<uint8_t>(kRegInsVol + ch), kVolMin);
            fnh_[ch] = 0;
            insVol_[ch] = kVolMin;
            transpose_[ch] = 0;
        }
        rhythm_ = RhythmState{};
    }

    // シーケンスが始まるときに一度だけ。繰り返しの頭では呼ばれないので、演奏中に
    // `Y` で変えたものは残る。
    void setRhythmMode(bool on, const VoiceRecord* rhythmVoices) override {
        (void)rhythmVoices;                      // OPLL はリズム音をチップに持つ
        rhythm_.mode = static_cast<uint8_t>((rhythm_.mode & 0xC0) | (on ? kRhythmOn : 0));
        write(kRegRhythm, rhythm_.mode);
        if (!on) return;
        rhythm_.toDefaults();
        for (const auto& p : kRhythmPitch) {
            write(p[0], p[1]);
            if (p[0] >= kRegFnumH && p[0] < kRegFnumH + kChannels) fnh_[p[0] - kRegFnumH] = p[1];
        }
    }

    void keyOn(uint8_t ch) override {
        if (ch >= kChannels) return;
        fnh_[ch] = static_cast<uint8_t>(fnh_[ch] | kKey);
        write(static_cast<uint8_t>(kRegFnumH + ch), fnh_[ch]);
    }

    void keyOff(uint8_t ch) override {
        if (ch >= kChannels) return;
        fnh_[ch] = static_cast<uint8_t>(fnh_[ch] & ~kKey);
        write(static_cast<uint8_t>(kRegFnumH + ch), fnh_[ch]);
    }

    void setVolume(uint8_t ch, uint8_t loudness) override {
        if (ch == kChannelRhythm) {          // シーケンスの音量ぶんだけが来る
            rhythm_.scale = loudness;
            writeRhythmLevels(rhythm_.accents);
            return;
        }
        if (ch >= kChannels) return;
        const uint8_t att = static_cast<uint8_t>(kVolMin - volumeStep(loudness));
        insVol_[ch] = static_cast<uint8_t>((insVol_[ch] & 0xF0) | att);
        write(static_cast<uint8_t>(kRegInsVol + ch), insVol_[ch]);
    }

    // 音色の移調を先に足す（OPL2EX と同じ）。チップのプリセットの移調は 0。
    void setPitch(uint8_t ch, uint8_t note, int16_t bend) override {
        if (ch >= kChannels) return;
        const int wanted = static_cast<int>(note) + transpose_[ch];
        const uint8_t clamped = static_cast<uint8_t>(wanted < 0 ? 0 : (wanted > 255 ? 255 : wanted));
        const OplPitch p = oplPitch(false, clamped, bend);
        write(static_cast<uint8_t>(kRegFnumL + ch), static_cast<uint8_t>(p.fnum & 0xFF));
        // キーは演奏側のもの、サステインと bit7-6 は `Y` のもの。
        fnh_[ch] = static_cast<uint8_t>((fnh_[ch] & 0xF0) |
                                        ((p.block << 1) | ((p.fnum >> 8) & 1)));
        write(static_cast<uint8_t>(kRegFnumH + ch), fnh_[ch]);
    }

    // チップ内蔵の音色。bit5-4 がバンク、bit3-0 がプリセットで、bit7-6 は見ない。
    // プリセット 0 はチップのユーザー音色で、そのとき読み込まれているものが鳴る。
    void setVoice(uint8_t ch, uint8_t number) override {
        if (ch >= kChannels) return;
        transpose_[ch] = 0;
        write(static_cast<uint8_t>(kRegBank + ch), static_cast<uint8_t>((number >> 4) & 3));
        selectInstrument(ch, static_cast<uint8_t>(number & 0x0F));
    }

    // チップはレコードをユーザー音色レジスタにしか取らないので、1ブロックが同時に
    // 持てるのは1本だけ。チップの制約であってドライバのものではない。
    void seqVoice(uint8_t ch, uint8_t slot, const VoiceRecord& record) override {
        (void)slot;
        if (ch >= kChannels) return;
        const uint8_t* r = record.data.data();
        write(kRegUser + 0, r[kRecMod + kOpMult]);
        write(kRegUser + 1, r[kRecCar + kOpMult]);
        write(kRegUser + 2, r[kRecMod + kOpTl]);
        // 03h はキャリアのキースケール、両オペレータの波形の bit0、フィードバックを
        // 組み合わせたもの。キャリアのレベルと接続はチップに置き場所が無い。
        uint8_t v03 = static_cast<uint8_t>(r[kRecCar + kOpTl] & 0xC0);
        v03 = static_cast<uint8_t>(v03 | ((r[kRecCar + kOpWave] << 4) & 0x10));
        v03 = static_cast<uint8_t>(v03 | ((r[kRecMod + kOpWave] << 3) & 0x08));
        v03 = static_cast<uint8_t>(v03 | ((r[kRecFb] >> 1) & 0x07));
        write(kRegUser + 3, v03);
        write(kRegUser + 4, r[kRecMod + kOpAr]);
        write(kRegUser + 5, r[kRecCar + kOpAr]);
        write(kRegUser + 6, r[kRecMod + kOpSl]);
        write(kRegUser + 7, r[kRecCar + kOpSl]);
        // 移調はチャンネルごと。ユーザー音色はブロックに1つでも、プリセットで鳴る
        // チャンネルは自分の移調（0）を持つ。
        transpose_[ch] = static_cast<int8_t>(r[kRecTrans]);
        selectInstrument(ch, 0);         // ユーザー音色は楽器 0
    }

    void rhythmVolume(uint8_t target, uint8_t level) override {
        rhythm_.setLevel(target, level);
    }

    // 打撃はビットが立ち上がること。同じ楽器を続けて叩くには、いったん落とす。
    void rhythmStrike(uint8_t instruments, uint8_t accents) override {
        rhythm_.accents = accents;
        writeRhythmLevels(accents);
        const uint8_t base = static_cast<uint8_t>(rhythm_.mode & ~RhythmState::kAll);
        write(kRegRhythm, base);
        rhythm_.mode = static_cast<uint8_t>(base | (instruments & RhythmState::kAll));
        write(kRegRhythm, rhythm_.mode);
    }

    bool regRead(uint8_t port, uint8_t reg, uint8_t& value) override {
        if (port != 0) return false;
        if (reg >= kRegCount) return false;
        value = shadow_[reg];
        return true;
    }

    bool regWrite(uint8_t port, uint8_t reg, uint8_t value) override {
        if (port != 0) return false;
        if (reg >= kRegCount) return false;
        write(reg, value);
        // `Y` の書き込みを、ドライバが組み立てに使う控えにも入れる。
        if (reg >= kRegFnumH && reg < kRegFnumH + kChannels) fnh_[reg - kRegFnumH] = value;
        else if (reg >= kRegInsVol && reg < kRegInsVol + kChannels) insVol_[reg - kRegInsVol] = value;
        else if (reg == kRegRhythm) rhythm_.mode = value;
        return true;
    }

private:
    void write(uint8_t reg, uint8_t value) {
        if (reg < kRegCount) shadow_[reg] = value;
        bus_.write(which_, reg, value);
    }

    void selectInstrument(uint8_t ch, uint8_t instrument) {
        insVol_[ch] = static_cast<uint8_t>((insVol_[ch] & 0x0F) | (instrument << 4));
        write(static_cast<uint8_t>(kRegInsVol + ch), insVol_[ch]);
    }

    // 2つの楽器が1つのレジスタを分け合うので、バイトを丸ごと組み立てる。
    void writeRhythmLevels(uint8_t accents) {
        const uint8_t bd = rhythm_.attenuation(RhythmState::kBassDrum, accents);
        // 36h の上位ニブルはチャンネル6の楽器で、リズムモードでは意味を持たない。
        // `Y` が置いたものとして残す。
        write(kRegRhyLvl, static_cast<uint8_t>((shadow_[kRegRhyLvl] & 0xF0) | bd));

        const uint8_t hh = rhythm_.attenuation(RhythmState::kHiHat, accents);
        const uint8_t sd = rhythm_.attenuation(RhythmState::kSnare, accents);
        write(kRegRhyLvl + 1, static_cast<uint8_t>((hh << 4) | sd));

        const uint8_t tom = rhythm_.attenuation(RhythmState::kTom, accents);
        const uint8_t tc  = rhythm_.attenuation(RhythmState::kCymbal, accents);
        write(kRegRhyLvl + 2, static_cast<uint8_t>((tom << 4) | tc));
    }

    ChipBus& bus_;
    Device   which_;
    std::array<uint8_t, kRegCount> shadow_{};
    std::array<uint8_t, kChannels> fnh_{};
    std::array<uint8_t, kChannels> insVol_{};
    std::array<int8_t,  kChannels> transpose_{};
    RhythmState rhythm_;
};

} // namespace

std::unique_ptr<SoundDevice> makeOpllexDevice(ChipBus& bus, Device which) {
    return std::make_unique<OpllexDevice>(bus, which);
}

} // namespace y8960
