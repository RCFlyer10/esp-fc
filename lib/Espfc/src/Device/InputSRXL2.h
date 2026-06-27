#pragma once

#include "Model.h"
#include "Device/SerialDevice.h"
#include "Device/InputDevice.h"
#include "TelemetryManager.h"
#include "spm_srxl.h"
#include "spektrumTelemetrySensors.h"

#define SRXL2_MAX_CHANNELS 16

namespace Espfc {
namespace Device {

enum TelemetryState {
    SRXL_TELEM_STATE_CELL_VOLTS,
    SRXL_TELEM_STATE_CURRENT,
    SRXL_TELEM_STATE_BATT_VOLTS,
};

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
    
    void onChannelFrame() { _channelFrame = true; }
    void setRxFailsafe(bool failsafe) { _rxFailsafe = failsafe; }    
    Model& getModel() const { return *_model; }
    TelemetryState getTelemetryState() const { return _telemState; }
    void setTelemetryState(TelemetryState state) { _telemState = state; }
    bool isTelemetryEnabled() const { return _telemetryEnabled; }
    void updateFastBaud(bool isFast) { _fastBaud = isFast; }
    void shiftRxBuffer(uint8_t shiftAmount);
    
    static InputSRXL2* getInstance() { return _instance; }
    static Device::SerialDevice* getActiveSerial() { return _instance ? _instance->_serial : nullptr; }
    static bool safeToTransmit() {
        auto* instance = _instance;
        if (!instance) return true;
        uint32_t quiescentTime = instance->_fastBaud ? 50 : 170;
        return (micros() - instance->_busLastActivityMicros) >= quiescentTime;
    }    

  private:  
    Device::SerialDevice * _serial;
    Model* _model; 
    bool _telemetryEnabled = false;      
    bool _channelFrame = false;
    bool _rxFailsafe = true;
    bool _fastBaud = false;
    uint32_t _updateCycleTime = 0;
    uint32_t _busLastActivityMicros = 0;      
    uint8_t _rxBuffer[SRXL_MAX_BUFFER_SIZE * 3] = {0}; 
    uint8_t _bytesInRxBuffer = 0;
    TelemetryState _telemState;

    static InputSRXL2 * _instance;
};

}
}