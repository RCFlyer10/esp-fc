#include "Device/InputSRXL2.h"

extern "C" {
    // Relative path to reach the library from the Device folder
    #include "spm_srxl.h"
}

namespace Espfc {
namespace Device {

// The linker was missing this constructor
InputSRXL2::InputSRXL2() : _serial(nullptr) {}

int InputSRXL2::begin(Device::SerialDevice * serial, TelemetryManager * telemetry) {
    _serial = serial;
    // Protocol layer initialization
    return _srxl.begin(_serial);
}

InputStatus InputSRXL2::update() {
    if (!_serial) return INPUT_IDLE;

    // Process serial data through our protocol class
    InputStatus status = _srxl.update();   

    return status;
}

uint16_t InputSRXL2::get(uint8_t channel) const { return _srxl.get(channel); }
void InputSRXL2::get(uint16_t * data, size_t len) const { _srxl.get(data, len); }
size_t InputSRXL2::getChannelCount() const { return _srxl.getChannelCount(); }
bool InputSRXL2::needAverage() const { return _srxl.needAverage(); }

}
}