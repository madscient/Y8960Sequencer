#include "chips.h"

#include <algorithm>

namespace y8960 {

namespace {

// Y8960 の各ブロックのクロックは MSX 標準と同じ（Y8960BasicExtension の
// doc/hardware.md「搭載音源」）。SSGS は 1.7897725MHz だが、EPSGemuEngine の SSGS は
// 5.12MHz 未満のマスタークロックを 1/2 して SSG 部に使うので、3.579545MHz を渡す。
constexpr uint32_t kClockMsx = 3579545;

} // namespace

const std::array<const char*, 3>& Y8960Chips::libraryBaseNames() {
    static const std::array<const char*, 3> names = {
        "Y8960emuEngine", "EPSGemuEngine", "DSAemuEngine",
    };
    return names;
}

bool Y8960Chips::open(const std::filesystem::path& libraryDir, uint32_t sampleRate, std::string& error) {
    const auto& names = libraryBaseNames();
    const std::array<FmEngineLibrary*, 3> libs = {&libY8960_, &libEpsg_, &libDsa_};
    const std::array<FmEngine*, 3> engines = {&engY8960_, &engEpsg_, &engDsa_};

    for (size_t i = 0; i < libs.size(); ++i) {
        const auto path = libraryDir / sharedLibraryFileName(names[i]);
        std::string why;
        if (!libs[i]->open(path, why)) {
            error = path.u8string() + ": " + why;
            return false;
        }
        if (!engines[i]->create(*libs[i], sampleRate, why)) {
            error = path.u8string() + ": " + why;
            return false;
        }
    }

    struct Spec { Device dev; FmEngine* engine; const char* name; };
    const Spec specs[] = {
        {Device::SSGS,    &engEpsg_,  "SSGS"},
        {Device::OPLLEX1, &engY8960_, "OPLLEX"},
        {Device::OPLLEX2, &engY8960_, "OPLLEX"},
        {Device::OPL2EX1, &engY8960_, "OPL2EX"},
        {Device::OPL2EX2, &engY8960_, "OPL2EX"},
        {Device::DCSG1,   &engDsa_,   "DCSG"},
        {Device::DCSG2,   &engDsa_,   "DCSG"},
        {Device::SCC,     &engDsa_,   "SCC"},
    };
    for (const Spec& s : specs) {
        Route& r = routes_[static_cast<size_t>(s.dev)];
        r.engine = s.engine;
        if (!s.engine->addChip(s.name, kClockMsx, r.id, error)) return false;
    }

    // 2回路が同じバッファを指すことで、共有メモリになる。
    adpcm_.assign(kAdpcmMemorySize, 0);
    for (Device d : {Device::OPL2EX1, Device::OPL2EX2}) {
        const Route& r = routes_[static_cast<size_t>(d)];
        if (!r.engine->setMemory(r.id, adpcm_.data(), kAdpcmMemorySize)) {
            error = "ADPCM メモリを設定できません";
            return false;
        }
    }
    return true;
}

void Y8960Chips::write(Device chip, uint8_t reg, uint8_t value) {
    const Route& r = routes_[static_cast<size_t>(chip)];
    r.engine->write(r.id, reg, value);
}

void Y8960Chips::loadAdpcmMemory(const std::vector<uint8_t>& image) {
    const size_t n = std::min<size_t>(image.size(), kAdpcmMemorySize);
    std::copy(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(n), adpcm_.begin());
    std::fill(adpcm_.begin() + static_cast<std::ptrdiff_t>(n), adpcm_.end(), uint8_t{0});
}

void Y8960Chips::render(float* outL, float* outR, uint32_t samples) {
    std::fill(outL, outL + samples, 0.0f);
    std::fill(outR, outR + samples, 0.0f);
    if (tmpL_.size() < samples) {
        tmpL_.resize(samples);
        tmpR_.resize(samples);
    }
    for (FmEngine* e : {&engY8960_, &engEpsg_, &engDsa_}) {
        e->generate(tmpL_.data(), tmpR_.data(), samples);
        for (uint32_t i = 0; i < samples; ++i) {
            outL[i] += tmpL_[i];
            outR[i] += tmpR_[i];
        }
    }
}

} // namespace y8960
