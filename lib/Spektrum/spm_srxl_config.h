#pragma once

#define SRXL_NUM_OF_BUSES           1
// Optimization for ESP32
#define SRXL_CRC_OPTIMIZE_MODE      SRXL_CRC_OPTIMIZE_SPEED
#define SRXL_DEVICE_PRIORITY        10
#define SRXL_DEVICE_ID              0x30
#define SRXL_BUS_INDEX              0
#define SRXL_TIMEOUT_MS             50
#define SRXL_MIN_PACKET_LENGTH      5
#define SRXL2_MIN_RAW 10912
#define SRXL2_MAX_RAW 54612
#define MIN_PWM 988
#define MAX_PWM 2012

#ifdef __cplusplus
extern "C" {
#endif

// User-provided callbacks with correct types from spm_srxl.h
void srxlSendOnUart(uint8_t port, uint8_t* packet, uint8_t length);
void srxlChangeBaudRate(uint8_t port, uint32_t baudrate);
void srxlFillTelemetry(SrxlTelemetryData* pTelemData); 
void srxlReceivedChannelData(SrxlChannelData* pChannelData, bool isFailsafe);
void srxlOnVtx(SrxlVtxData* pVtxData);
bool srxlOnBind(SrxlFullID boundID, SrxlBindData status);

#ifdef __cplusplus
}
#endif
