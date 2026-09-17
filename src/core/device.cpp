#include "dev_internal.h"

namespace y8960 {

DeviceSet::DeviceSet(ChipBus& bus) {
    devices_[static_cast<size_t>(Device::SSGS)]    = makeSsgsDevice(bus);
    devices_[static_cast<size_t>(Device::OPLLEX1)] = makeOpllexDevice(bus, Device::OPLLEX1);
    devices_[static_cast<size_t>(Device::OPLLEX2)] = makeOpllexDevice(bus, Device::OPLLEX2);
    devices_[static_cast<size_t>(Device::OPL2EX1)] = makeOpl2exDevice(bus, Device::OPL2EX1);
    devices_[static_cast<size_t>(Device::OPL2EX2)] = makeOpl2exDevice(bus, Device::OPL2EX2);
    devices_[static_cast<size_t>(Device::DCSG1)]   = makeDcsgDevice(bus, Device::DCSG1);
    devices_[static_cast<size_t>(Device::DCSG2)]   = makeDcsgDevice(bus, Device::DCSG2);
    devices_[static_cast<size_t>(Device::SCC)]     = makeSccDevice(bus);
}

DeviceSet::~DeviceSet() = default;

void DeviceSet::resetAll() {
    for (auto& d : devices_) d->reset();
}

} // namespace y8960
