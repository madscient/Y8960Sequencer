#pragma once
// Y8960 の音源ブロック8つを、3本のエミュレータに振り分ける。
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
//   DCSG: reg は使わず、value が 1 バイトのシリアル書き込み
//   SCC : reg は SCC のレジスタ窓の先頭からのオフセット
class ChipBus {
public:
    virtual ~ChipBus() = default;
    virtual void write(Device chip, uint8_t reg, uint8_t value) = 0;
};

class Y8960Chips final : public ChipBus {
public:
    static constexpr uint32_t kAdpcmMemorySize = 256 * 1024;

    Y8960Chips() = default;
    Y8960Chips(const Y8960Chips&) = delete;
    Y8960Chips& operator=(const Y8960Chips&) = delete;

    // libraryDir から3本のライブラリを読み、8ブロックを作る。
    bool open(const std::filesystem::path& libraryDir, uint32_t sampleRate, std::string& error);

    void write(Device chip, uint8_t reg, uint8_t value) override;

    // 2回路の OPL2EX が共有する ADPCM メモリに、アドレス 0 から写す。
    // 256KB を超えた分は捨て、足りない分は 0 で埋める。鳴らし始める前に呼ぶこと。
    void loadAdpcmMemory(const std::vector<uint8_t>& image);

    void render(float* outL, float* outR, uint32_t samples);

    // ブロックごとの音量。1.0 が素のまま。実機のデジタルミキサーのレジスタ仕様が
    // 決まるまでの間に合わせ（doc/plan.md）。
    void setGain(Device chip, float gain);
    float gain(Device chip) const;

    // 前に読んでからの、そのブロックの最大振幅。読むと 0 に戻る。
    // 音声のスレッドが書き、画面のスレッドが読む。
    float takeLevel(Device chip);

    // ライブラリの探す名前（拡張子と接頭辞の付かない形）。doc/plan.md の表と同じ。
    static const std::array<const char*, 3>& libraryBaseNames();

private:
    // ライブラリはエンジンより先に宣言する。エンジンの破棄に関数ポインタが要る。
    std::array<FmEngineLibrary, 3> libraries_;
    std::array<FmEngine, kDeviceCount> engines_;
    std::array<uint32_t, kDeviceCount> chipIds_{};
    std::array<std::atomic<float>, kDeviceCount> levels_{};
    std::array<std::atomic<float>, kDeviceCount> gains_{};

    std::vector<uint8_t> adpcm_;
    std::vector<float>   tmpL_;
    std::vector<float>   tmpR_;
};

} // namespace y8960
