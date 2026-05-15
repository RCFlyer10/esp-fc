#include "Rc/Srxl2.h"

#define ENABLE_SRXL_DEBUG_LOGGING 0

static Espfc::Rc::Srxl2* _instance = nullptr;

extern "C" {
#include "spm_srxl.h"
#include "Srxl2.h"

void srxlSendOnUart(uint8_t port, uint8_t* packet, uint8_t length) {
    
    auto* dev = Espfc::Rc::Srxl2::getSerialDevice(port);
    if (!dev) return;
    
    if (dev->availableForWrite() < length) return;
    
    if (srxlBus[SRXL_BUS_INDEX].baudRate == SRXL_BAUD_400000) {
        delayMicroseconds(50); 
    } else {
        delayMicroseconds(120);
    }
    
    while(dev->available()) dev->read();    
   
    dev->write(packet, length);   
    
    #if ENABLE_SRXL_DEBUG_LOGGING
    Serial.printf(">>> TX REPLY SENT (%u bytes) <<<\n", length);
    #endif
}

void srxlChangeBaudRate(uint8_t port, uint32_t baudrate) {
    auto* dev = Espfc::Rc::Srxl2::getSerialDevice(port);
    if (dev) {
        dev->flush();
        delayMicroseconds(100); // Rule 7.2.1.6: Stabilize line during divider shifts
        dev->updateBaudRate(baudrate);
    }
    #if ENABLE_SRXL_DEBUG_LOGGING
    Serial.printf("\n>>> PROTOCOL EVENT: SWITCHING SPEED TO %u NOW <<<\n\n", baudrate);
    #endif
}

void srxlFillTelemetry(SrxlTelemetryData* pTelemData) {
    if (pTelemData) {
        memset(pTelemData, 0, sizeof(SrxlTelemetryData));
    }
}

void srxlReceivedChannelData(SrxlChannelData* pChannelData, bool isFailsafe) {
    if (_instance && pChannelData && !isFailsafe) {
        _instance->onFrameReceived();
        
        // Change 'mask' to 'channelMask' to match the library struct
        uint32_t mask = pChannelData->mask; 
        int packedIdx = 0; 

        for (int ch = 0; ch < 32; ch++) {
            if (mask & (1UL << ch)) {
                if (ch < 16) {
                    _instance->setChannel(ch, pChannelData->values[packedIdx]);
                }
                packedIdx++;
            }
        }
    }
}

void srxlOnVtx(SrxlVtxData* pVtxData) {}
bool srxlOnBind(SrxlFullID boundID, SrxlBindData status) { return true; }
}

namespace Espfc {
namespace Rc {

Srxl2::Srxl2() {
    _instance = this;    
}

int Srxl2::begin(Device::SerialDevice * serial) {
    _serial = serial;
    _serialStatic = serial;
    uint32_t uid = ESP.getEfuseMac() & 0xFFFFFFFFULL;    
    
    srxlInitDevice(SRXL_DEVICE_ID, SRXL_DEVICE_PRIORITY, SRXL_DEVINFO_NO_RF, uid);
    srxlInitBus(SRXL_BUS_INDEX, 0, SRXL_BAUD_115200 | SRXL_BAUD_400000);    
    
    return 1;
}

InputStatus Srxl2::update() {
    if (!_serial) return INPUT_IDLE;
    checkAndClearHardwareErrors();

    static uint8_t buffer[SRXL_MAX_BUFFER_SIZE];
    static uint8_t idx = 0;    
    static uint32_t bootTime = 0;

    #if ENABLE_SRXL_DEBUG_LOGGING
    static uint8_t debugLogBuffer[SRXL_MAX_BUFFER_SIZE];
    static uint8_t debugLogLen = 0;
    static bool debugLogReady = false;    
    static int lastKnownState = -1;
    #endif

    if (bootTime == 0) bootTime = millis();

    // 1. Intake serial data. Processing outside the 5ms timer minimizes turnaround lag.
    while (_serial->available()) {
        uint8_t b = _serial->read();
        
        // Header Sync: Wait for the SRXL2 ID (0xA6)
        if (idx == 0 && b != SPEKTRUM_SRXL_ID) continue;
        
        buffer[idx++] = b;

        // Check length as soon as we have the header bytes
        if (idx >= 3) {
            uint8_t len = buffer[2]; 
            
            if (len > SRXL_MAX_BUFFER_SIZE || len < 5) {
                idx = 0;
                continue;
            }

            if (idx >= len) {
                #if ENABLE_SRXL_DEBUG_LOGGING
                if (!debugLogReady) {
                    memcpy(debugLogBuffer, buffer, len);
                    debugLogLen = len;
                    debugLogReady = true; 
                }                
                #endif

                if (buffer[1] == 0x21 && buffer[3] == SRXL_DEVICE_ID) {
                    idx = 0;
                    continue; 
                }

                // 2. Parse the packet and ONLY reset the index if the library accepts it.
                // This ensures we don't prematurely dump data if the parse failed.
                if (srxlParsePacket(SRXL_BUS_INDEX, buffer, len)) {                                
                    _lastValidMillis = millis();
                    #if ENABLE_SRXL_DEBUG_LOGGING
                    Serial.println("SRXL2: Packet accepted by library");
                    #endif                   
                } else {
                    // If it returns false, it was likely a CRC failure or partial frame.
                    // We reset idx here to start fresh for the next 0xA6 header.
                    srxlOnFrameError(SRXL_BUS_INDEX);
                }

                idx = 0;
            }
        }
    }    
    
    uint32_t now = millis();
    if (now - _lastValidMillis >= 50) {
        // We've had 50ms of silence. Tell the library to advance its state.
        srxlRun(SRXL_BUS_INDEX, 50);
        _lastValidMillis = now;
    }

    #if ENABLE_SRXL_DEBUG_LOGGING
    int currentState = (int)srxlBus[SRXL_BUS_INDEX].state;
    if (currentState != lastKnownState) {
        Serial.printf("\n[STATE CHANGE] Bus changed from %d to %d\n", lastKnownState, currentState);
        lastKnownState = currentState;
    }

    // Process debug logs
    if (debugLogReady) {
        Serial.print("DIAG_RX: ");
        for (uint8_t i = 0; i < debugLogLen; i++) {
            if (debugLogBuffer[i] < 0x10) Serial.print("0");
            Serial.print(debugLogBuffer[i], HEX);
            Serial.print(" ");
        }
        Serial.printf("| State=%d\n", currentState);
        debugLogReady = false; 
    }
    #endif

    return _newFrame ? INPUT_RECEIVED : INPUT_IDLE;
}

void Srxl2::onFrameReceived() {
    _newFrame = true;
}

void Srxl2::setChannel(uint8_t ch, uint16_t val) {
    if (ch < 16) _channels[ch] = val;
}

uint16_t Srxl2::get(uint8_t channel) const {
    if (channel >= 16) return 1500;

    // Use 32-bit math to map 0-65535 down to 1000-2000
    // Center (32768) will map exactly to 1500
    return (uint16_t)(((uint32_t)_channels[channel] * 1000) >> 16) + 1000;
}

void Srxl2::get(uint16_t * data, size_t len) const {
    for (size_t i = 0; i < len && i < 16; i++) {
        data[i] = get(i);
    }
}

void Srxl2::checkAndClearHardwareErrors() {
    int uart_num = 0;
    if (_serial == Espfc::Rc::Srxl2::getSerialDevice(0)) {
        uart_num = 0;
    }
#if SOC_UART_NUM > 1
    else if (_serial == Espfc::Rc::Srxl2::getSerialDevice(1)) {
        uart_num = 1;
    }
#endif
#if SOC_UART_NUM > 2
    else if (_serial == Espfc::Rc::Srxl2::getSerialDevice(2)) {
        uart_num = 2;
    }
#endif

    uart_dev_t *hw = &UART0;
#if SOC_UART_NUM > 1
    if (uart_num == 1) hw = &UART1;
#endif
#if SOC_UART_NUM > 2
    else if (uart_num == 2) hw = &UART2;
#endif

    if (hw->int_raw.frm_err || hw->int_raw.parity_err) {
        hw->int_clr.frm_err = 1;
        hw->int_clr.parity_err = 1;
        srxlOnFrameError(SRXL_BUS_INDEX);
    }
}

Device::SerialDevice* Srxl2::_serialStatic = nullptr;
Device::SerialDevice* Srxl2::getSerialDevice(uint8_t port) {
    return _serialStatic;
}

} // namespace Rc
} // namespace Espfc
