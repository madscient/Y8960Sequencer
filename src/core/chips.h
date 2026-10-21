#pragma once
// デバイス 0-4（Y8960 の音源ブロック5つ）と 8・10・11（OPL3・OPNA・OPNB）を、
// 2本のエミュレータに振り分ける。
// ブロックごとにエンジンを1つ持つ ―― 出力を別々に取り出せると、ブロックごとの
// 音量とレベルメーターがそこから出せる。

#include "block.h"
#include "fmengine.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace y8960 {

// ドライバがチップに書く口。チップの並びはデバイス番号と同じ。
//   SCC : reg は SCC のレジスタ窓の先頭からのオフセット。port 1 の 00h-04h はチャンネルの定位
//   port: 2組のレジスタを持つチップ（OPL3・OPNA・OPNB）の2組目が 1。SCC は上のとおり
class ChipBus {
public:
    virtual ~ChipBus() = default;
    virtual void write(Device chip, uint8_t reg, uint8_t value, uint8_t port = 0) = 0;
};

class Y8960Chips final : public ChipBus {
public:
    static constexpr uint32_t kAdpcmMemorySize = 256 * 1024;

    Y8960Chips() = default;
    Y8960Chips(const Y8960Chips&) = delete;
    Y8960Chips& operator=(const Y8960Chips&) = delete;

    // libraryDir から2本のライブラリを読み、8 つのデバイスを作る。
    bool open(const std::filesystem::path& libraryDir, uint32_t sampleRate, std::string& error);

    void write(Device chip, uint8_t reg, uint8_t value, uint8_t port = 0) override;

    // ADPCM メモリに、アドレス 0 から写す。OPL2EX と、OPNA・OPNB の ADPCM-B が
    // 同じ中身を見る ―― ボイスファイル番号はデバイスを区別しない
    // （bytecode.md「ブロック」のチャンク 03）。
    // 256KB を超えた分は捨て、足りない分は 0 で埋める。鳴らし始める前に呼ぶこと。
    // エンジンはこのメモリをその場で読むので、render と同時に呼ばないこと。
    void loadAdpcmMemory(const std::vector<uint8_t>& image);

    void render(float* outL, float* outR, uint32_t samples);

    // ブロックごとの音量（Digital Mixer）とマスターの音量。線形の利得で、1.0 が素のまま。
    void setGain(Device chip, float gain);
    float gain(Device chip) const;
    void setMasterGain(float gain);
    float masterGain() const;

    // 前に読んでからの、そのブロックの最大振幅。読むと 0 に戻る。
    // 音声のスレッドが書き、画面のスレッドが読む。
    float takeLevel(Device chip);

    // ライブラリの探す名前（拡張子と接頭辞の付かない形）。doc/plan.md の表と同じ。
    static const std::array<const char*, 2>& libraryBaseNames();

private:
    // ライブラリと ADPCM メモリはエンジンより先に宣言し、エンジンより後に壊す。
    // エンジンの破棄に関数ポインタが要り、エンジンは壊れるまで ADPCM メモリを指す。
    std::array<FmEngineLibrary, 2> libraries_;
    std::vector<uint8_t> adpcm_;
    std::array<FmEngine, kDeviceCount> engines_;
    std::array<uint32_t, kDeviceCount> chipIds_{};
    std::array<std::atomic<float>, kDeviceCount> levels_{};
    std::array<std::atomic<float>, kDeviceCount> gains_{};
    std::atomic<float> masterGain_{1.0f};

    std::vector<float>   tmpL_;
    std::vector<float>   tmpR_;
};

} // namespace y8960
