#pragma once

#include "Device/SerialDevice.h"
#include "Device/InputDevice.h"
#include "Rc/Srxl2.h"
#include "TelemetryManager.h"

namespace Espfc {
namespace Device {

class InputSRXL2 : public InputDevice 
{
  public:
    InputSRXL2();
    int begin(Device::SerialDevice * serial, TelemetryManager * telemetry);
    virtual InputStatus update() override;
    virtual uint16_t get(uint8_t i) const override;
    virtual void get(uint16_t * data, size_t len) const override;
    virtual size_t getChannelCount() const override;
    virtual bool needAverage() const override;

  private:
    Device::SerialDevice * _serial;
    TelemetryManager * _telemetry;
    Rc::Srxl2 _srxl; // The hardware layer "owns" the protocol layer    
};

}
}