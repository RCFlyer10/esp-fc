#pragma once

#include "Model.h"
#include "Device/SerialDevice.h"
#include "Device/InputDevice.h"
#include "TelemetryManager.h"
#include "spm_srxl.h"
#include "spektrumTelemetrySensors.h"
#include "soc/uart_reg.h"
#include "soc/uart_struct.h"
#include "hal/uart_ll.h"

#define SRXL2_MAX_CHANNELS 16

namespace Espfc {
namespace Device {

class InputSRXL2 : public InputDevice 
{
  public:
    InputSRXL2();
    int begin(Device::SerialDevice * serial, Model& model, bool telemetryEnabled);
    virtual InputStatus update() override;
    virtual uint16_t get(uint8_t i) const override;
    virtual void get(uint16_t * data, size_t len) const override;
    virtual size_t getChannelCount() const override;
    virtual bool needAverage() const override;

    bool isBaudSwitchNeeded();
    void onRCFrameReceived() { _newRCFrame = true; }
    void setRxFailsafe(bool failsafe) { _rxFailsafe = failsafe; }
    void setChannel(uint8_t ch, uint16_t val);
    Model& getModel() const { return *_model; }
    bool isTelemetryEnabled() const { return _telemetryEnabled; }
    
    static InputSRXL2* getInstance() { return _instance; }
    static Device::SerialDevice* getActiveSerial() { return _instance ? _instance->_serial : nullptr; } 

  private:
    void shiftRxBuffer(uint8_t shiftAmount);
    Device::SerialDevice * _serial;
    Model* _model; 
    bool _telemetryEnabled = false;      
    bool _newRCFrame = false;
    bool _rxFailsafe = true;
    uint32_t _updateCycleTime = 0;  
    uint16_t _channels[SRXL2_MAX_CHANNELS] = {32768}; // Default to mid-point in raw 0-65535 range          
    uint8_t _rxBuffer[SRXL_MAX_BUFFER_SIZE * 2] = {0}; 
    uint8_t _bytesInRxBuffer = 0;

    static InputSRXL2 * _instance;
};

}
}