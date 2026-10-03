#include "chips.h"

#include "pitch.h"

#include <algorithm>
#include <cmath>

namespace y8960 {

namespace {

// Y8960 の各ブロックのクロックは MSX 標準と同じ（Y8960BasicExtension の
// doc/hardware.md「搭載音源」）。SSGS は 1.7897725MHz だが、DSAemuEngine の SSGS は
// 5.12MHz 未満のマスタークロックを 1/2 して SSG 部に使うので、3.579545MHz を渡す。
// デバイス 8-11 のクロックは Y8SQ 形式が決めていないので、各チップの標準のもの
// （pitch.h）。音の高さの計算も同じ値を使う。
constexpr uint32_t kClockMsx = 3579545;

enum Library { kDsa = 0, kYmfm = 1 };

struct Spec {
    Device      device;
    Library     library;
    const char* chip;
    uint32_t    clock;
};

// OPNB は YM2610B として作る。形式は OPNB と OPNB-B を区別せず、FM を6チャンネル
// 持つのは OPNB-B のほう（bytecode.md「デバイス番号とチャンネル番号」）。
constexpr Spec kSpecs[kDeviceCount] = {
    {Device::SSGS,    kDsa,  "SSGS",   kClockMsx},
    {Device::OPLLEX1, kDsa,  "OPLLEX", kClockMsx},
    {Device::OPLLEX2, kDsa,  "OPLLEX", kClockMsx},
    {Device::OPL2EX1, kDsa,  "OPL2EX", kClockMsx},
    {Device::OPL2EX2, kDsa,  "OPL2EX", kClockMsx},
    {Device::DCSG1,   kDsa,  "DCSG",   kClockMsx},
    {Device::DCSG2,   kDsa,  "DCSG",   kClockMsx},
    {Device::SCC,     kDsa,  "SCC",    kClockMsx},
    {Device::OPL3,    kYmfm, "OPL3",   kClockOpl3},
    {Device::OPM,     kYmfm, "OPM",    kClockOpm},
    {Device::OPNA,    kYmfm, "OPNA",   kClockOpna},
    {Device::OPNB,    kYmfm, "OPNBB",  kClockOpnb},
};

// ADPCM-B を持ち、ボイスファイルのメモリを見るもの。
constexpr Device kAdpcmBChips[] = {Device::OPL2EX1, Device::OPL2EX2, Device::OPNA, Device::OPNB};

// そのメモリの、エンジンでの名前。OPNA と OPNB のものは FmEngineApi の仕様の表にあり、
// OPNA では RAM モード（ctrl2 の bit0 が 0）のメモリを指す。ドライバも RAM モードで
// 鳴らす（dev_opn.cpp）。OPL2EX は表に無く、DSAemuEngine が決めた名前。
constexpr char kAdpcmBMemory[] = "ADPCM_B";

} // namespace

const std::array<const char*, 2>& Y8960Chips::libraryBaseNames() {
    static const std::array<const char*, 2> names = {
        "DSAemuEngine", "YMFMEngine",
    };
    return names;
}

bool Y8960Chips::open(const std::filesystem::path& libraryDir, uint32_t sampleRate, std::string& error) {
    const auto& names = libraryBaseNames();
    for (size_t i = 0; i < libraries_.size(); ++i) {
        const auto path = libraryDir / sharedLibraryFileName(names[i]);
        std::string why;
        if (!libraries_[i].open(path, why)) {
            error = path.u8string() + ": " + why;
            return false;
        }
    }

    for (const Spec& s : kSpecs) {
        const size_t index = static_cast<size_t>(s.device);
        FmEngine& engine = engines_[index];
        if (!engine.create(libraries_[static_cast<size_t>(s.library)], sampleRate, error)) return false;
        if (!engine.addChip(s.chip, s.clock, chipIds_[index], error)) return false;
        gains_[index].store(1.0f, std::memory_order_relaxed);
        levels_[index].store(0.0f, std::memory_order_relaxed);
    }

    // 同じブロックを RAM として割り当てると、エンジンは複製せずにその場で読むので、
    // 共有メモリになる。ROM として割り当てるとエンジンは複製してよく、あとで
    // loadAdpcmMemory で写した中身が見えるとは限らない。
    adpcm_.assign(kAdpcmMemorySize, 0);
    for (Device d : kAdpcmBChips) {
        const size_t index = static_cast<size_t>(d);
        if (!engines_[index].setMemoryEx(chipIds_[index], kAdpcmBMemory, 0, adpcm_.data(), kAdpcmMemorySize,
                                         FM_ACCESS_RAM)) {
            error = "cannot share the ADPCM memory";
            return false;
        }
    }
    return true;
}

namespace {

// ADPCM-A のキーオンのレジスタ（bit7 が 0 ならキーオン）。OPNA はリズムがこれで鳴る。
bool isAdpcmAKeyOn(Device chip, uint8_t reg, uint8_t value, uint8_t port) {
    if (value & 0x80) return false;
    return (chip == Device::OPNA && port == 0 && reg == 0x10) ||
           (chip == Device::OPNB && port == 1 && reg == 0x00);
}

} // namespace

void Y8960Chips::write(Device chip, uint8_t reg, uint8_t value, uint8_t port) {
    // ADPCM-A のサンプル（OPNA は内蔵 ROM、OPNB はサンプル ROM）を、このプレイヤーは
    // エミュレータに渡せない。中身が無いと ymfm は 0 のバイト列を復号して大きな雑音を
    // 出すので、キーオンを捨てて黙らせる。
    if (isAdpcmAKeyOn(chip, reg, value, port)) return;
    const size_t index = static_cast<size_t>(chip);
    engines_[index].write(chipIds_[index], reg, value, port);
}

void Y8960Chips::loadAdpcmMemory(const std::vector<uint8_t>& image) {
    const size_t n = std::min<size_t>(image.size(), kAdpcmMemorySize);
    std::copy(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(n), adpcm_.begin());
    std::fill(adpcm_.begin() + static_cast<std::ptrdiff_t>(n), adpcm_.end(), uint8_t{0});
}

void Y8960Chips::setGain(Device chip, float gain) {
    gains_[static_cast<size_t>(chip)].store(gain, std::memory_order_relaxed);
}

float Y8960Chips::gain(Device chip) const {
    return gains_[static_cast<size_t>(chip)].load(std::memory_order_relaxed);
}

float Y8960Chips::takeLevel(Device chip) {
    return levels_[static_cast<size_t>(chip)].exchange(0.0f, std::memory_order_relaxed);
}

void Y8960Chips::render(float* outL, float* outR, uint32_t samples) {
    std::fill(outL, outL + samples, 0.0f);
    std::fill(outR, outR + samples, 0.0f);
    if (tmpL_.size() < samples) {
        tmpL_.resize(samples);
        tmpR_.resize(samples);
    }
    for (size_t i = 0; i < kDeviceCount; ++i) {
        engines_[i].generate(tmpL_.data(), tmpR_.data(), samples);
        const float gain = gains_[i].load(std::memory_order_relaxed);
        float peak = 0.0f;
        for (uint32_t s = 0; s < samples; ++s) {
            const float l = tmpL_[s] * gain;
            const float r = tmpR_[s] * gain;
            outL[s] += l;
            outR[s] += r;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        // 画面が読むまでの間の山を残す。
        const float seen = levels_[i].load(std::memory_order_relaxed);
        if (peak > seen) levels_[i].store(peak, std::memory_order_relaxed);
    }
}

} // namespace y8960
