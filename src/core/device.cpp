#include "dev_internal.h"

#include <cmath>

namespace y8960 {

uint8_t adpcmLevel(uint8_t loudness) {
    const double gain = std::pow(10.0, -0.75 * (127 - (loudness & 127)) / 20.0);
    return static_cast<uint8_t>(std::lround(255.0 * gain));
}

DeviceSet::DeviceSet(ChipBus& bus) {
    devices_[static_cast<size_t>(Device::SSGS)]    = makeSsgsDevice(bus, envelope_);
    devices_[static_cast<size_t>(Device::OPLLEX1)] = makeOpllexDevice(bus, Device::OPLLEX1);
    devices_[static_cast<size_t>(Device::OPLLEX2)] = makeOpllexDevice(bus, Device::OPLLEX2);
    devices_[static_cast<size_t>(Device::OPL2EX1)] = makeOpl2exDevice(bus, Device::OPL2EX1);
    devices_[static_cast<size_t>(Device::OPL2EX2)] = makeOpl2exDevice(bus, Device::OPL2EX2);
    devices_[static_cast<size_t>(Device::DCSG1)]   = makeDcsgDevice(bus, Device::DCSG1, envelope_);
    devices_[static_cast<size_t>(Device::DCSG2)]   = makeDcsgDevice(bus, Device::DCSG2, envelope_);
    devices_[static_cast<size_t>(Device::SCC)]     = makeSccDevice(bus, envelope_);
    devices_[static_cast<size_t>(Device::OPL3)]    = makeOpl3Device(bus);
    devices_[static_cast<size_t>(Device::OPM)]     = makeOpmDevice(bus);
    devices_[static_cast<size_t>(Device::OPNA)]    = makeOpnDevice(bus, Device::OPNA, envelope_);
    devices_[static_cast<size_t>(Device::OPNB)]    = makeOpnDevice(bus, Device::OPNB, envelope_);
}

DeviceSet::~DeviceSet() = default;

void DeviceSet::resetAll() {
    envelope_.reset();
    for (auto& d : devices_) d->reset();
}

void DeviceSet::setAdpcmDirectory(const AdpcmVoiceFile* directory) {
    for (auto& d : devices_) d->setAdpcmDirectory(directory);
}

void DeviceSet::setAdpcmASamples(const AdpcmASample* samples) {
    for (auto& d : devices_) d->setAdpcmASamples(samples);
}

} // namespace y8960
