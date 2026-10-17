#include "dev_internal.h"

#include <cmath>

namespace y8960 {

uint8_t adpcmLevel(uint8_t loudness) {
    const double gain = std::pow(10.0, -0.75 * (127 - (loudness & 127)) / 20.0);
    return static_cast<uint8_t>(std::lround(255.0 * gain));
}

DeviceSet::DeviceSet(ChipBus& bus) {
    devices_[static_cast<size_t>(Device::SSGS)]    = makeSsgsDevice(bus, envelope_);
    devices_[static_cast<size_t>(Device::OPLLEX)]  = makeOpllexDevice(bus);
    devices_[static_cast<size_t>(Device::OPL2EX)]  = makeOpl2exDevice(bus);
    devices_[static_cast<size_t>(Device::SCC)]     = makeSccDevice(bus, envelope_);
    devices_[static_cast<size_t>(Device::OPL3)]    = makeOpl3Device(bus);
    devices_[static_cast<size_t>(Device::OPM)]     = makeOpmDevice(bus);
    devices_[static_cast<size_t>(Device::OPNA)]    = makeOpnDevice(bus, Device::OPNA, envelope_);
    devices_[static_cast<size_t>(Device::OPNB)]    = makeOpnDevice(bus, Device::OPNB, envelope_);
}

DeviceSet::~DeviceSet() = default;

void DeviceSet::resetAll() {
    envelope_.reset();
    for (Device d : kDevices) (*this)[d].reset();
}

void DeviceSet::setAdpcmDirectory(const AdpcmVoiceFile* directory) {
    for (Device d : kDevices) (*this)[d].setAdpcmDirectory(directory);
}

void DeviceSet::setAdpcmASamples(const AdpcmASample* samples) {
    for (Device d : kDevices) (*this)[d].setAdpcmASamples(samples);
}

} // namespace y8960
