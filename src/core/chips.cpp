#include "chips.h"

#include <algorithm>
#include <cmath>

namespace y8960 {

namespace {

// Y8960 の各ブロックのクロックは MSX 標準と同じ（Y8960BasicExtension の
// doc/hardware.md「搭載音源」）。SSGS は 1.7897725MHz だが、EPSGemuEngine の SSGS は
// 5.12MHz 未満のマスタークロックを 1/2 して SSG 部に使うので、3.579545MHz を渡す。
constexpr uint32_t kClockMsx = 3579545;

enum Library { kY8960emu = 0, kEpsg = 1, kDsa = 2 };

struct Spec {
    Device      device;
    Library     library;
    const char* chip;
};

constexpr Spec kSpecs[kDeviceCount] = {
    {Device::SSGS,    kEpsg,     "SSGS"},
    {Device::OPLLEX1, kY8960emu, "OPLLEX"},
    {Device::OPLLEX2, kY8960emu, "OPLLEX"},
    {Device::OPL2EX1, kY8960emu, "OPL2EX"},
    {Device::OPL2EX2, kY8960emu, "OPL2EX"},
    {Device::DCSG1,   kDsa,      "DCSG"},
    {Device::DCSG2,   kDsa,      "DCSG"},
    {Device::SCC,     kDsa,      "SCC"},
};

} // namespace

const std::array<const char*, 3>& Y8960Chips::libraryBaseNames() {
    static const std::array<const char*, 3> names = {
        "Y8960emuEngine", "EPSGemuEngine", "DSAemuEngine",
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
        if (!engine.addChip(s.chip, kClockMsx, chipIds_[index], error)) return false;
        gains_[index].store(1.0f, std::memory_order_relaxed);
        levels_[index].store(0.0f, std::memory_order_relaxed);
    }

    // 2回路が同じバッファを指すことで、共有メモリになる。
    adpcm_.assign(kAdpcmMemorySize, 0);
    for (Device d : {Device::OPL2EX1, Device::OPL2EX2}) {
        const size_t index = static_cast<size_t>(d);
        if (!engines_[index].setMemory(chipIds_[index], adpcm_.data(), kAdpcmMemorySize)) {
            error = "ADPCM メモリを設定できません";
            return false;
        }
    }
    return true;
}

void Y8960Chips::write(Device chip, uint8_t reg, uint8_t value) {
    const size_t index = static_cast<size_t>(chip);
    engines_[index].write(chipIds_[index], reg, value);
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
