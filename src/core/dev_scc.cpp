// SCC。Y8960BasicExtension の src/dev/scc.asm の写し。
//
// チップは波形表を持たず、チャンネルごとに1つの波形を持つ。だから音色を選ぶとは
// 32バイトを書き写すことで、載っている波形が変わらないチャンネルは写さない。
// チャンネル3と4は最後のブロックを分け合う。

#include "dev_internal.h"
#include "pitch.h"

namespace y8960 {

namespace {

constexpr uint8_t kChannels   = 5;
constexpr uint8_t kWaveBlocks = 4;
constexpr uint8_t kWaveSize   = 32;
constexpr uint8_t kOffWave    = 0x00;
constexpr uint8_t kOffFreq    = 0x80;
constexpr uint8_t kOffVol     = 0x8A;
constexpr uint8_t kOffEnable  = 0x8F;
constexpr uint8_t kVolMax     = 15;

// PSG の目盛り（1段が数 dB）の 0-15 を、振幅に比例する SCC の音量に直す（SCCVOLTAB）。
// 1-15 は tiny-yarou.com の「SCC for BASIC」の PSG→SCC 表で、0 は表に無いので無音。
constexpr uint8_t kVolTable[kVolMax + 1] = {
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x03, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0F,
};

constexpr uint8_t kSavNone = 0xFF;   // まだ何も載っていない
constexpr uint8_t kSavSeq  = 0x80;   // 控えているのが集合の索引であることの印

class SccDevice final : public SoundDevice, private EnvelopeSink {
public:
    SccDevice(ChipBus& bus, SoftEnvelope& envelope) : bus_(bus), env_(envelope) {
        env_.attach(Device::SCC, this);
    }

    // 波形からイネーブルまでを 0 にする。ROM はこのあと音色表の波形 0 を4ブロックに
    // 載せるが、こちらは音色表を持たない ―― ブロックは要る音色を自分で持つ
    // （bytecode.md）ので、0 のままにする。波形が 0 のチャンネルは無音になる。
    void reset() override {
        for (uint16_t off = 0; off <= kOffEnable; ++off) {
            write(static_cast<uint8_t>(off), 0);
        }
        wave_.fill(kSavNone);
        vol_.fill(0);
        linear_ = 0;
        env_.resetDevice(Device::SCC);
    }

    // 5つのチャンネルが1バイトを分け合うので、組み立て直さず控えから読む。
    // そうすると `Y` が書いたビットもそのまま残る。
    // ソフトウェアエンベロープがあると、キーオフで許可ビットを落とさない。リリースを
    // 聞かせるためで、動かすのは音量レジスタのほう。落とすのはエンベロープを外すとき。
    void keyOn(uint8_t ch) override {
        if (ch >= kChannels) return;
        if (env_.keyOn(Device::SCC, ch)) writeVolume(ch);   // アタックは 0 から
        write(kOffEnable, static_cast<uint8_t>(shadow_[kOffEnable] | (1u << ch)));
    }

    void keyOff(uint8_t ch) override {
        if (ch >= kChannels) return;
        if (env_.keyOff(Device::SCC, ch)) {
            writeVolume(ch);
            return;
        }
        write(kOffEnable, static_cast<uint8_t>(shadow_[kOffEnable] & ~(1u << ch)));
    }

    void setVolume(uint8_t ch, uint8_t loudness) override {
        if (ch >= kChannels) return;
        vol_[ch] = static_cast<uint8_t>((loudness >> 3) & kVolMax);
        env_.setV(Device::SCC, ch, vol_[ch]);
        writeVolume(ch);
    }

    // レジスタが持つのは分周値から 1 引いた値。表を分け合う他の2つとの違いはそこだけ。
    void setPitch(uint8_t ch, uint8_t note, int16_t bend) override {
        if (ch >= kChannels) return;
        const uint16_t v = static_cast<uint16_t>(divPitch(Device::SCC, note, bend).divisor - 1);
        write(static_cast<uint8_t>(kOffFreq + ch * 2),     static_cast<uint8_t>(v & 0xFF));
        write(static_cast<uint8_t>(kOffFreq + ch * 2 + 1), static_cast<uint8_t>(v >> 8));
    }

    void seqVoice(uint8_t ch, uint8_t slot, const VoiceRecord& record) override {
        if (ch >= kChannels) return;
        const uint8_t mark = static_cast<uint8_t>(slot | kSavSeq);
        if (wave_[ch] == mark) return;               // もう載っている
        wave_[ch] = mark;
        markShared(ch, mark);
        loadWave(ch, record.data.data());
    }

    // `B3`。表を通さないチャンネルは、V もエンベロープの値もそのまま書く。
    void setVolumeTable(uint8_t ch, bool table) override {
        if (ch >= kChannels) return;
        const uint8_t bit = static_cast<uint8_t>(1u << ch);
        const uint8_t next = static_cast<uint8_t>(table ? (linear_ & ~bit) : (linear_ | bit));
        if (next == linear_) return;
        linear_ = next;
        writeVolume(ch);
    }

    void rewindChannel(uint8_t ch) override { setVolumeTable(ch, true); }

    bool regRead(uint8_t port, uint8_t reg, uint8_t& value) override {
        if (port != 0) return false;
        value = shadow_[reg];
        return true;
    }

    bool regWrite(uint8_t port, uint8_t reg, uint8_t value) override {
        if (port != 0) return false;
        write(reg, value);
        if (reg >= kOffFreq) return true;
        // Y が触った波形は、そのチャンネルが載せていたものではなくなる。次の `85` は
        // 同じ番号でも読み込み直す。
        const uint8_t block = static_cast<uint8_t>((reg / kWaveSize) & 0x07);
        wave_[block] = kSavNone;
        if (block >= kWaveBlocks - 1) wave_[kChannels - 1] = kSavNone;
        return true;
    }

private:
    void write(uint8_t offset, uint8_t value) {
        shadow_[offset] = value;
        bus_.write(Device::SCC, offset, value);
    }

    // エンベロープがあればその値、無ければ V。どちらも `B3` が選んだほうで書く。
    void writeVolume(uint8_t ch) {
        uint8_t level = 0;
        if (!env_.output(Device::SCC, ch, level)) level = vol_[ch];
        level = static_cast<uint8_t>(level & kVolMax);
        const bool linear = (linear_ >> ch) & 1;
        write(static_cast<uint8_t>(kOffVol + ch), linear ? level : kVolTable[level]);
    }

    void envRefresh(uint8_t ch) override { writeVolume(ch); }
    // SCC のキーは許可ビット。`Y` が立てたビットもキーとして読む（ROM の ENVSYNC）。
    uint8_t envSync(uint8_t ch) override {
        const bool key = (shadow_[kOffEnable] >> ch) & 1;
        return static_cast<uint8_t>((key ? SoftEnvelope::kKey : 0) | vol_[ch]);
    }
    void envReleased(uint8_t ch) override { keyOff(ch); }

    // チャンネル3と4はブロックを分け合うので、載せたことを両方に書く。
    void markShared(uint8_t ch, uint8_t mark) {
        if (ch < kWaveBlocks - 1) return;
        const uint8_t other = (ch == kChannels - 1) ? static_cast<uint8_t>(kWaveBlocks - 1)
                                                    : static_cast<uint8_t>(kChannels - 1);
        wave_[other] = mark;
    }

    void loadWave(uint8_t ch, const uint8_t* data) {
        const uint8_t block = (ch >= kWaveBlocks) ? static_cast<uint8_t>(kWaveBlocks - 1) : ch;
        for (uint8_t i = 0; i < kWaveSize; ++i) {
            write(static_cast<uint8_t>(kOffWave + block * kWaveSize + i), data[i]);
        }
    }

    ChipBus& bus_;
    SoftEnvelope& env_;
    std::array<uint8_t, 256> shadow_{};
    std::array<uint8_t, kChannels> wave_{};
    // V の控え。チップの音量レジスタはエンベロープの値で上書きされるので別に持つ。
    // `Y` で音量レジスタを書いても、ここは変わらない（ROM も同じ）。
    std::array<uint8_t, kChannels> vol_{};
    uint8_t linear_ = 0;   // チャンネルごとの bit。立っていれば表を通さない（ROM の CTL_SCCLIN）
};

} // namespace

std::unique_ptr<SoundDevice> makeSccDevice(ChipBus& bus, SoftEnvelope& envelope) {
    return std::make_unique<SccDevice>(bus, envelope);
}

} // namespace y8960
