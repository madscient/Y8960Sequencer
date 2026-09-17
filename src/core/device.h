#pragma once
// デバイスドライバ。シーケンサはデバイス番号とチャンネル番号だけで話し、
// どのチップかを知らない。Y8960BasicExtension の src/dev/devtab.asm と同じ形。

#include "block.h"
#include "chips.h"

#include <array>
#include <cstdint>
#include <memory>

namespace y8960 {

// SSGS のハードウェアエンベロープとパンポット（seqdef.inc の SSGENV_*）。
enum class SsgEnv : uint8_t { Shape = 0x00, PeriodLow = 0x10, PeriodHigh = 0x20, Pan = 0x30 };

class SoundDevice {
public:
    virtual ~SoundDevice() = default;

    // 音を止め、決まった状態に戻す。
    virtual void reset() = 0;
    // 演奏を始めるシーケンスが要るリズムモード（FM ブロックだけが持つ）。
    // rhythmVoices は OPL2EX が ch6-8 に載せる3本（ブロックの索引 32-34）。
    virtual void setRhythmMode(bool on, const VoiceRecord* rhythmVoices) {
        (void)on; (void)rhythmVoices;
    }

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

    virtual void rhythmVolume(bool accent, uint8_t level) { (void)accent; (void)level; }
    virtual void rhythmStrike(uint8_t instruments, uint8_t accents) { (void)instruments; (void)accents; }

    virtual void ssgEnv(SsgEnv kind, uint8_t ch, uint8_t value) { (void)kind; (void)ch; (void)value; }

    // ADPCM のボイスファイルの表。OPL2EX だけが使う。鳴らし始める前に渡すこと。
    virtual void setAdpcmDirectory(const AdpcmVoiceFile* directory) { (void)directory; }

    // `E0`。CY が立つ（= false）のは、そのデバイスに無いレジスタ番号のとき。
    virtual bool regRead(uint8_t reg, uint8_t& value) { (void)reg; (void)value; return false; }
    virtual bool regWrite(uint8_t reg, uint8_t value) { (void)reg; (void)value; return false; }
};

// 8ブロックぶんのドライバ。
class DeviceSet {
public:
    explicit DeviceSet(ChipBus& bus);
    ~DeviceSet();

    SoundDevice& operator[](Device d) { return *devices_[static_cast<size_t>(d)]; }
    void resetAll();
    // 表そのものは呼び出し側が持ち続けること。
    void setAdpcmDirectory(const AdpcmVoiceFile* directory);

private:
    std::array<std::unique_ptr<SoundDevice>, kDeviceCount> devices_;
};

} // namespace y8960
