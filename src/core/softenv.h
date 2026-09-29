#pragma once
// SSGS・DCSG・SCC のソフトウェアエンベロープ。Y8960BasicExtension の src/dev/env.asm の写し。
//
// 状態はトラックではなくチャンネルに付く。キーオフの後のリリースは、キーを切った
// トラックやシーケンスが止まっても鳴り続けるため。ドライバはキーオン・キーオフ・
// 音量をここへ知らせ、チップに書くレベルをここに問い合わせる。割り込みごとに
// tick() が全チャンネルを進め、動いたチャンネルをドライバに書き直させる。

#include "block.h"
#include "timing.h"

#include <array>
#include <cstdint>

namespace y8960 {

// エンベロープを持つドライバが、ここから呼ばれる口。
class EnvelopeSink {
public:
    virtual ~EnvelopeSink() = default;
    // いまの状態でチャンネルのレベルをチップに書く（ENVREF）。
    virtual void envRefresh(uint8_t ch) = 0;
    // ドライバの控えにあるキー（bit7）と V（bit3-0）。エンベロープを持ち始める
    // チャンネルが読む（ENVSYNC）。
    virtual uint8_t envSync(uint8_t ch) = 0;
    // エンベロープを選んだ。SSGS はハードウェアエンベロープの選択を落とす。
    virtual void envChosen(uint8_t ch) { (void)ch; }
    // キーが上がっているときにエンベロープを外した。ドライバ自身のキーオフで消す
    // （SCC はリリースのあいだ許可ビットを立てたままにしている）。
    virtual void envReleased(uint8_t ch) = 0;
};

class SoftEnvelope {
public:
    static constexpr uint8_t kKey = 0x80;   // envSync が返すキーのビット

    void attach(Device device, EnvelopeSink* sink);

    // MINIT にあたる。全チャンネルを無しにし、1/60 秒の端数も捨てる。
    void reset();
    // ドライバの RESET（ENVRESDEV）。そのデバイスのチャンネルを無しに戻す。
    void resetDevice(Device device);

    // ドライバが知らせるもの。戻り値は、そのチャンネルがエンベロープを持つか。
    bool keyOn(Device device, uint8_t ch);
    bool keyOff(Device device, uint8_t ch);
    bool setV(Device device, uint8_t ch, uint8_t v);   // v は 0-15
    // SSGS のハードウェアエンベロープ（@16 以上）が勝った（ENVNONE）。
    void none(Device device, uint8_t ch);

    // チップに書くレベル 0-15。エンベロープを持たなければ false。
    bool output(Device device, uint8_t ch, uint8_t& level) const;

    // `B2`（ENVSET）。number が 0 なら外す。record は number が 1 以上のときだけ読む。
    void select(Device device, uint8_t ch, uint8_t number, const EnvelopeRecord& record);

    // 割り込み1回（ENVTICK）。
    void tick(TickRate rate);

private:
    enum class Phase : uint8_t { None, Attack, Decay, Sustain, Release, Stop };

    struct Channel {
        Phase   phase = Phase::None;
        uint8_t level = 0;
        uint8_t count = 0;    // 次の段までのコマ
        uint8_t vol   = 0;    // キー（bit7）と V（bit3-0）
        // ENVRTAB の形（bit7-4 がコマ、bit3-0 が変化）。sl はそのまま。
        uint8_t ar = 0, dr = 0, sl = 0, rr = 0;
    };

    static constexpr int kChannels = 6 + 4 + 4 + 5;

    static int index(Device device, uint8_t ch);
    // 次の段までのコマを数え始め、1段の変化を返す（ENVRATE）。
    static uint8_t rate(Channel& c, uint8_t packed);
    void attack(Channel& c);
    void release(Channel& c);
    void now(Channel& c);
    void step();
    void advance(Channel& c, int index);
    void stepLevel(Channel& c);
    void refresh(int index);

    std::array<Channel, kChannels> channels_{};
    std::array<EnvelopeSink*, kDeviceCount> sinks_{};
    bool     moving_ = false;
    uint16_t acc_    = 0;
};

} // namespace y8960
