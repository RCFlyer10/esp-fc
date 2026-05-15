#pragma once

#include <Arduino.h>
#include "Device/InputDevice.h"
#include "Device/SerialDevice.h"
#include "spm_srxl.h"
#include "spm_srxl_config.h"
#include "soc/uart_struct.h" 
#include "soc/uart_reg.h"    
#include "soc/soc.h"



extern SrxlBus srxlBus[];

namespace Espfc {
namespace Rc {

class Srxl2 : public Device::InputDevice {
public:
    Srxl2();

    int begin(Device::SerialDevice * serial);
    InputStatus update() override;

    size_t getChannelCount() const override { return 16; }
    uint16_t get(uint8_t channel) const override;
    void get(uint16_t * data, size_t len) const override;
    void checkAndClearHardwareErrors();
    bool needAverage() const override { return false; }

    void onFrameReceived();
    void setChannel(uint8_t ch, uint16_t val);

    static Device::SerialDevice* getSerialDevice(uint8_t port);

private:
    Device::SerialDevice* _serial = nullptr;
    static Device::SerialDevice* _serialStatic;

    uint16_t _channels[16] = {0};
    bool _newFrame = false;
    uint32_t _lastValidMillis = 0;
};

} // namespace Rc
} // namespace Espfc