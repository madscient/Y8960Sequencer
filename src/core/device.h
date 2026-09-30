#pragma once
// デバイスドライバ。シーケンサはデバイス番号とチャンネル番号だけで話し、
// どのチップかを知らない。Y8960BasicExtension の src/dev/devtab.asm と同じ形。

#include "block.h"
#include "chips.h"
#include "softenv.h"

#include <array>
#include <cstdint>
#include <memory>

namespace y8960 {

// rhythmVolume の target のうち、アクセント音量を指すもの（seqdef.inc の RHYVOL_ACC）。
constexpr uint8_t kRhythmAccent = 0x80;

// SSG のハードウェアエンベロープ（seqdef.inc の SSGENV_*）。
enum class SsgEnv : uint8_t { Shape = 0x00, PeriodLow = 0x10, PeriodHigh = 0x20 };

class SoundDevice {
public:
    virtual ~SoundDevice() = default;

    // 音を止め、決まった状態に戻す。
    virtual void reset() = 0;
    // 演奏を始めるシーケンスが要るリズムモード（リズムチャンネルを持つデバイス）。
    // rhythmVoices は OPL2EX と OPL3 が ch6-8 に載せる3本（ブロックの索引 32-34）。
    // リズムの音量はここで既定に戻る。OPNA・OPNB はモードを持たず、音量だけを戻す。
    virtual void setRhythmMode(bool on, const VoiceRecord* rhythmVoices) {
        (void)on; (void)rhythmVoices;
    }
    // 周の頭。走行状態のうちドライバが持つもの（SCC の `B3`、効果音モードの `E9`）を
    // 初期値に戻す。
    virtual void rewindChannel(uint8_t ch) { (void)ch; }

    virtual void keyOn(uint8_t ch)  { (void)ch; }
    virtual void keyOff(uint8_t ch) { (void)ch; }
    virtual void setVolume(uint8_t ch, uint8_t loudness) { (void)ch; (void)loudness; }
    // bend は 1/64 半音。ベンド・ポルタメント・MTUNE を足したもの。
    virtual void setPitch(uint8_t ch, uint8_t note, int16_t bend) { (void)ch; (void)note; (void)bend; }
    // `82`。チップが自分で解決する番号。
    virtual void setVoice(uint8_t ch, uint8_t number) { (void)ch; (void)number; }
    // `85`。シーケンスが持つレコード。slot は集合の索引で、SCC が同じ波形かの判定に使う。
    virtual void seqVoice(uint8_t ch, uint8_t slot, const VoiceRecord& record) {
        (void)ch; (void)slot; (void)record;
    }
    // `87`。0 が左端、8 が中央、15 が右端。
    virtual void setPan(uint8_t ch, uint8_t value) { (void)ch; (void)value; }
    // `B3`。音量を表で直すか（SCC）。
    virtual void setVolumeTable(uint8_t ch, bool table) { (void)ch; (void)table; }
    // `E9`。効果音モードのサブチャンネル（1-3）の高さの、親からの差（1/64 半音）。
    virtual void setSubPitch(uint8_t ch, uint8_t sub, int16_t steps) { (void)ch; (void)sub; (void)steps; }

    // target が kRhythmAccent なら @A、それ以外は通常音量を変える楽器のビットマップ。
    virtual void rhythmVolume(uint8_t target, uint8_t level) { (void)target; (void)level; }
    virtual void rhythmStrike(uint8_t instruments, uint8_t accents) { (void)instruments; (void)accents; }

    // `D9`。ADPCM-A の楽器（ビットマップ）にサンプルを結び付ける。
    virtual void bindAdpcmA(uint8_t instruments, uint8_t sample) { (void)instruments; (void)sample; }

    virtual void ssgEnv(SsgEnv kind, uint8_t ch, uint8_t value) { (void)kind; (void)ch; (void)value; }

    // ADPCM のボイスファイルの表。ADPCM チャンネルを持つものが使う。鳴らし始める前に渡すこと。
    virtual void setAdpcmDirectory(const AdpcmVoiceFile* directory) { (void)directory; }
    // ADPCM-A のサンプルの表（チャンク 41）。OPNB だけが使う。
    virtual void setAdpcmASamples(const AdpcmASample* samples) { (void)samples; }

    // `E0`（port 0）と `E8`（port 1）。false は、そのデバイスに無いレジスタのとき。
    virtual bool regRead(uint8_t port, uint8_t reg, uint8_t& value) {
        (void)port; (void)reg; (void)value; return false;
    }
    virtual bool regWrite(uint8_t port, uint8_t reg, uint8_t value) {
        (void)port; (void)reg; (void)value; return false;
    }
};

// 12 デバイスぶんのドライバ。
class DeviceSet {
public:
    explicit DeviceSet(ChipBus& bus);
    ~DeviceSet();

    SoundDevice& operator[](Device d) { return *devices_[static_cast<size_t>(d)]; }
    // MINIT にあたる。ソフトウェアエンベロープの端数も捨てる。
    void resetAll();
    // 表そのものは呼び出し側が持ち続けること。
    void setAdpcmDirectory(const AdpcmVoiceFile* directory);
    void setAdpcmASamples(const AdpcmASample* samples);

    // `B2`。ソフトウェアエンベロープを持たないデバイスは無視する。
    void setEnvelope(Device d, uint8_t ch, uint8_t number, const EnvelopeRecord& record) {
        envelope_.select(d, ch, number, record);
    }
    // 割り込み1回ぶん、ソフトウェアエンベロープを進める。シーケンスより先に呼ぶこと。
    void envelopeTick(TickRate rate) { envelope_.tick(rate); }

private:
    // ドライバより先に作り、後で壊す。ドライバが参照を持つため。
    SoftEnvelope envelope_;
    std::array<std::unique_ptr<SoundDevice>, kDeviceCount> devices_;
};

} // namespace y8960
