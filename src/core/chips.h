#pragma once
// Y8960 の音源ブロック8つを、3本のエミュレータに振り分ける。

#include "block.h"
#include "fmengine.h"

#include <array>
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

    // ライブラリの探す名前（拡張子と接頭辞の付かない形）。doc/plan.md の表と同じ。
    static const std::array<const char*, 3>& libraryBaseNames();

private:
    struct Route {
        FmEngine* engine = nullptr;
        uint32_t  id     = 0;
    };

    // ライブラリはエンジンより先に宣言する。エンジンの破棄に関数ポインタが要る。
    FmEngineLibrary libY8960_;
    FmEngineLibrary libEpsg_;
    FmEngineLibrary libDsa_;
    FmEngine        engY8960_;
    FmEngine        engEpsg_;
    FmEngine        engDsa_;

    std::array<Route, kDeviceCount> routes_{};
    std::vector<uint8_t> adpcm_;
    std::vector<float>   tmpL_;
    std::vector<float>   tmpR_;
};

} // namespace y8960
