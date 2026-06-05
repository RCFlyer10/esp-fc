#include "Device/InputSRXL2.h"
#include "ModelConfig.h"
#include "Utils/MemoryHelper.h"
#include "Utils/Math.hpp"
#include "InputSRXL2.h"

#define ENABLE_SRXL_DEBUG_LOGGING 1

extern "C" SrxlBus srxlBus[]; 

namespace Espfc {
namespace Device {

InputSRXL2* InputSRXL2::_instance = nullptr;

InputSRXL2::InputSRXL2() : _serial(nullptr), _model(nullptr), _telemetryEnabled(false) {}

int InputSRXL2::begin(Device::SerialDevice * serial, Model& model, bool telemetryEnabled) {
    _serial = serial;
    _model = &model; 
    _telemetryEnabled = telemetryEnabled;       
    _instance = this; 
    
    // Check for framing errors at 115200 baud to determine if we need to switch to 400k 
    // due to us browning out and the receiver did not and is still at 400k baud. 
    bool switchBaud = isBaudSwitchNeeded(); 
    if (switchBaud) {        
        srxlChangeBaudRate(0, 400000);
    }     
    
    uint32_t uid = ESP.getEfuseMac() & 0xFFFFFFFFULL;    
    srxlInitDevice(SRXL_DEVICE_ID, SRXL_DEVICE_PRIORITY, SRXL_DEVINFO_NO_RF, uid);
    srxlInitBus(SRXL_BUS_INDEX, 0, SRXL_BAUD_400000); 
    
    if (switchBaud) {        
        srxlBus[SRXL_BUS_INDEX].baudRate = SRXL_BAUD_400000; 
    }

    while(serial->available()) serial->read();
    
    return 1;
}

InputStatus FAST_CODE_ATTR InputSRXL2::update() {
    if (!_serial) return INPUT_IDLE;  
    
    uint32_t currentTime = millis();    
    bool packetParsed = false;
    
    int available = _serial->available();
    if (available > 0) {
        int maxToRead = std::min(available, (int)(sizeof(_rxBuffer) - _bytesInRxBuffer));
        if (maxToRead > 0) {
            _serial->readMany(_rxBuffer + _bytesInRxBuffer, maxToRead);
            _bytesInRxBuffer += maxToRead;
        }
    }        
    
    while (_bytesInRxBuffer >= SRXL_MIN_PACKET_LENGTH) {
        uint8_t* p = _rxBuffer;
        if (p[0] == SPEKTRUM_SRXL_ID) {
            uint8_t len = p[2];            
            
            if (len < SRXL_MIN_PACKET_LENGTH || len > SRXL_MAX_BUFFER_SIZE) {
                shiftRxBuffer(1);
                continue; 
            }
            
            if (_bytesInRxBuffer >= len) {                
                srxlParsePacket(SRXL_BUS_INDEX, p, len);
                _updateCycleTime = currentTime;
                packetParsed = true;                 
                shiftRxBuffer(len);
            } else {                
                break; 
            }
        } 
        else {            
            shiftRxBuffer(1);
        }
    }        
    
    if (!packetParsed) {
        uint32_t elapsed = currentTime - _updateCycleTime;
        
        if (elapsed >= 2) {        
            uint8_t delta = (elapsed > SRXL_TIMEOUT_MS) ? SRXL_TIMEOUT_MS : (uint8_t)elapsed;        
            srxlRun(SRXL_BUS_INDEX, delta);        
            _updateCycleTime = currentTime; 
        }
    }

    if (_rxFailsafe) {
        return INPUT_FAILSAFE;
    } else if (_newRCFrame) {
        _newRCFrame = false;         
        return INPUT_RECEIVED;
    } else {
        return INPUT_IDLE;
    }    
}

void InputSRXL2::setChannel(uint8_t ch, uint16_t val) {
    if (ch < 16) _channels[ch] = val;
}

void InputSRXL2::shiftRxBuffer(uint8_t shiftAmount) {
    if (shiftAmount == 0 || _bytesInRxBuffer == 0) return;
    
    if (shiftAmount >= _bytesInRxBuffer) {
        _bytesInRxBuffer = 0;
    } else {
        _bytesInRxBuffer -= shiftAmount;
        memmove(_rxBuffer, _rxBuffer + shiftAmount, _bytesInRxBuffer);
    }
}

uint16_t InputSRXL2::get(uint8_t channel) const {
    if (channel >= 16) return 1500;     
    return Utils::mapi(_channels[channel], SRXL2_MIN_RAW, SRXL2_MAX_RAW, MIN_PWM, MAX_PWM);
}

void InputSRXL2::get(uint16_t * data, size_t len) const {
    for (size_t i = 0; i < len && i < 16; i++) {
        data[i] = get(i);
    }
}

size_t InputSRXL2::getChannelCount() const { return SRXL2_MAX_CHANNELS; }

bool InputSRXL2::needAverage() const { return false; }

bool InputSRXL2::isBaudSwitchNeeded() {
    int uart_num = 0; 
    if (_serial == (void*)&Serial) uart_num = 0;
#if SOC_UART_NUM > 1
    else if (_serial == (void*)&Serial1) uart_num = 1;
#endif
#if SOC_UART_NUM > 2
    else if (_serial == (void*)&Serial2) uart_num = 2;
#endif

    uart_dev_t * uart = UART_LL_GET_HW(uart_num);

    bool errorDetected = false;

    delay(SRXL_TIMEOUT_MS);

    for (int i = 0; i < 2; i++) {
        // Check framing error bit using the official hardware structure
        if (uart->int_st.val & UART_FRM_ERR_INT_ST) {
            // Clear it
            uart->int_clr.val = UART_FRM_ERR_INT_CLR;
            errorDetected = true;
        }
        delay(1);
    }    
    
    return errorDetected; 
}

} // namespace Device
} // namespace Espfc

extern "C" {

void srxlSendOnUart(uint8_t port, uint8_t* packet, uint8_t length) {
    auto* dev = Espfc::Device::InputSRXL2::getActiveSerial();
    if (!dev || length == 0) return;

    if (dev->availableForWrite() < length) return;

    if (srxlBus[port].baudRate == SRXL_BAUD_400000) {
        delayMicroseconds(50);
    } else {
        delayMicroseconds(174);
    }    

    dev->write(packet, length);    
}

void srxlChangeBaudRate(uint8_t port, uint32_t baudrate) {
    auto* dev = Espfc::Device::InputSRXL2::getActiveSerial();
    if (dev) {
        dev->flush();
        delayMicroseconds(100); 
        dev->updateBaudRate(baudrate);
    }
#if ENABLE_SRXL_DEBUG_LOGGING
    Serial.printf(">>> SRXL BAUD CHANGE TO %u <<<\n", baudrate);
#endif
}

void srxlFillTelemetry(SrxlTelemetryData* pTelemData) {     
    if (pTelemData) {
        memset(pTelemData->raw, 0, sizeof(pTelemData->raw));        
        
        auto* instance = Espfc::Device::InputSRXL2::getInstance();

        if (instance && instance->isTelemetryEnabled()) {
            pTelemData->sensorID = TELE_DEVICE_LIPOMON; 
            pTelemData->secondaryID = 0;
            
            auto& battery = instance->getModel().state.battery;
            int cellCount = battery.cells;
            float averageCellVolts = battery.cellVoltage;
            
            if (battery.voltage < 2.0f || cellCount <= 0) {
                cellCount = 0;
            } else if (cellCount > 6) {
                cellCount = 6;
            }

            // Convert average cell voltage to centivolts (.01V steps) per the Spektrum header spec
            uint16_t cell_cv = (uint16_t)(averageCellVolts * 100.0f + 0.5f);

            // Populate the active cells using Little-Endian format (Low byte first)
            for (int i = 0; i < cellCount; i++) {
                pTelemData->data[i * 2]     = (uint8_t)(cell_cv & 0xFF);        // Low Byte
                pTelemData->data[i * 2 + 1] = (uint8_t)((cell_cv >> 8) & 0xFF); // High Byte
            }
            
            // Pad any remaining/inactive cells up to 6S with NOT PRESENT (0x7FFF)
            // This dynamically forces the Spektrum radio to hide unused cell rows on your display.
            for (int i = cellCount; i < 6; i++) {
                pTelemData->data[i * 2]     = 0xFF; // Low Byte
                pTelemData->data[i * 2 + 1] = 0x7F; // High Byte
            }
            
            // Mark the Temperature field as "No Data" (0x7FFF)
            pTelemData->data[12] = 0xFF; // Low Byte
            pTelemData->data[13] = 0x7F; // High Byte
        }         
    }    
}

void srxlReceivedChannelData(SrxlChannelData* pChannelData, bool isFailsafe) {
    auto* instance = Espfc::Device::InputSRXL2::getInstance();    

    if (instance && pChannelData) {        
        instance->setRxFailsafe(isFailsafe);
        
        if (!isFailsafe) {
            auto& input = instance->getModel().state.input;
        
            instance->onRCFrameReceived();

            for (int ch = 0; ch < SRXL2_MAX_CHANNELS; ch++) { 
                instance->setChannel(ch, srxlChData.values[ch]); 
            }                   

            // Process Digital RSSI (dBm or %)
            // Per SRXL2 spec, rssi is a signed byte. 
            // Positive = % Link Quality, Negative = dBm.
            int8_t rawRssi = pChannelData->rssi;           
            
            if (rawRssi < 0) {
                // It's dBm: store directly as signed int16_t
                input.rssi = static_cast<int16_t>(rawRssi);
            } else {
                // It's Link Quality %: store in linkQuality field
                input.linkQuality = static_cast<uint8_t>(rawRssi);                
            }
        }
    }
}

void srxlOnVtx(SrxlVtxData* pVtxData) {
    // Stub
}

bool srxlOnBind(SrxlFullID boundID, SrxlBindData status) {
    return true;
}

} // extern "C"