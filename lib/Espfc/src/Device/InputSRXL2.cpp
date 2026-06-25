#include "InputSRXL2.h"
#include "ModelConfig.h"
#include "Utils/Math.hpp"
#include "Utils/MemoryHelper.h"
#include "Utils/Stats.h"

namespace Espfc {
namespace Device {

InputSRXL2* InputSRXL2::_instance = nullptr;

InputSRXL2::InputSRXL2(): _serial(nullptr), _model(nullptr), _telemetryEnabled(false) {}

int InputSRXL2::begin(Device::SerialDevice* serial, Model& model, bool telemetryEnabled)
{
  _serial = serial;
  _model = &model;
  _telemetryEnabled = telemetryEnabled;
  _instance = this;
  _updateCycleTime = millis();

  while (_serial->available())
    _serial->read();

#ifndef UNIT_TEST
  uint32_t uid = ESP.getEfuseMac() & 0xFFFFFFFFULL;
#else
  uint32_t uid = 0x12345678UL; // recognizable test value for native/unit tests
#endif

  srxlInitDevice(SRXL_DEVICE_ID, SRXL_DEVICE_PRIORITY, SRXL_DEVINFO_NO_RF, uid);
  srxlInitBus(SRXL_BUS_INDEX, 0, SRXL_BAUD_400000);

  return 1;
}

InputStatus FAST_CODE_ATTR InputSRXL2::update()
{
  if (!_serial) return INPUT_IDLE;

  uint32_t currentTime = millis();
  bool frameParsed = false;

  int available = _serial->available();
  if (available > 0)
  {
    _busLastActivityMicros = micros();
    int maxToRead = std::min(available, (int)(sizeof(_rxBuffer) - _bytesInRxBuffer));
    if (maxToRead > 0)
    {
      _serial->readMany(_rxBuffer + _bytesInRxBuffer, maxToRead);
      _bytesInRxBuffer += maxToRead;
    }
  }

  while (_bytesInRxBuffer >= SRXL_MIN_PACKET_LENGTH)
  {
    uint8_t* p = _rxBuffer;

    // Cheack the header for 0xA6
    if (p[0] == SPEKTRUM_SRXL_ID)
    {
      uint8_t len = p[2];

      // Not a full frame or too large for the buffer
      if (len < SRXL_MIN_PACKET_LENGTH || len > SRXL_MAX_BUFFER_SIZE)
      {
        shiftRxBuffer(1);
        continue;
      }
      // We have a full frame
      if (_bytesInRxBuffer >= len)
      {
        srxlParsePacket(SRXL_BUS_INDEX, p, len);
        _updateCycleTime = currentTime;
        frameParsed = true;
        shiftRxBuffer(len);
      }
      else
      {
        break;
      }
    }
    // Not the correct header, more than likely a framing error
    else
    {
      uint8_t shift = 1;
      bool foundHeader = false;
      for (uint8_t i = 1; i < _bytesInRxBuffer && i < 16; ++i)
      { // limit search depth
        if (_rxBuffer[i] == SPEKTRUM_SRXL_ID)
        {
          foundHeader = true;
          shift = i;
          break;
        }
      }
      shiftRxBuffer(shift);

      if (!foundHeader)
      {
        _bytesInRxBuffer = 0; // Clear buffer if no header found to prevent overflow on next reads

        // 3 calls will initiate baude rate switch whill allows us to resync with the receiver
        // after we have browned out
        srxlOnFrameError(SRXL_BUS_INDEX);
      }
    }
  }

  // Udpate the internal SRXL timer with the correct time passed with no frame parsed
  if (!frameParsed)
  {
    uint32_t elapsed = currentTime - _updateCycleTime;

    if (elapsed >= 2)
    {
      uint8_t delta = (elapsed > SRXL_TIMEOUT_MS) ? SRXL_TIMEOUT_MS : (uint8_t)elapsed;
      srxlRun(SRXL_BUS_INDEX, delta);
      _updateCycleTime = currentTime;
    }
  }

  if (_rxFailsafe)
  {
    return INPUT_FAILSAFE;
  }
  else if (_channelFrame)
  {
    _channelFrame = false;
    return INPUT_RECEIVED;
  }
  else
  {
    return INPUT_IDLE;
  }
}

uint16_t InputSRXL2::get(uint8_t channel) const
{
  if (channel >= SRXL2_MAX_CHANNELS) return 1500;
  /* conversion from RC value to PWM
   * for RC frame
   *        RC       PWM
   * min  10912 ->  988us
   * mid  32768 -> 1500us
   * max  54612 -> 2012us
   * scale factor = (2012-988) / (54612-10912) = 0.0234324942791762    => 1024 / 43700 = 0.02343
   * offset = 988 - 10912 * 0.0234324942791762 = 732.3046224256293     => 988 - 255.70 = 732.3046 */

  return (lrintf((srxlChData.values[channel] * 0.02343f) + 732.3046f));
}

void InputSRXL2::get(uint16_t* data, size_t len) const
{
  for (size_t i = 0; i < len && i < 16; i++)
  {
    data[i] = get(i);
  }
}

size_t InputSRXL2::getChannelCount() const
{
  return SRXL2_MAX_CHANNELS;
}

bool InputSRXL2::needAverage() const
{
  return false;
}

void FAST_CODE_ATTR InputSRXL2::shiftRxBuffer(uint8_t shiftAmount)
{
  if (shiftAmount == 0 || _bytesInRxBuffer == 0) return;
  if (shiftAmount >= _bytesInRxBuffer)
  {
    _bytesInRxBuffer = 0;
    return;
  }
  _bytesInRxBuffer -= shiftAmount;

  // Use memcpy when possible (compiler can optimize better than memmove for non-overlapping)
  if (shiftAmount >= 8)
  {
    memcpy(_rxBuffer, _rxBuffer + shiftAmount, _bytesInRxBuffer);
  }
  else
  {
    // Manual loop for tiny shifts (very common)
    for (uint8_t i = 0; i < _bytesInRxBuffer; ++i)
    {
      _rxBuffer[i] = _rxBuffer[i + shiftAmount];
    }
  }
}

} // namespace Device
} // namespace Espfc

extern "C" {

void srxlSendOnUart(uint8_t port, uint8_t* packet, uint8_t length)
{
  auto* dev = Espfc::Device::InputSRXL2::getActiveSerial();
  if (!dev || length == 0) return;

  if (dev->availableForWrite() < length) return;

  while (!Espfc::Device::InputSRXL2::safeToTransmit())
  {
  }

  dev->write(packet, length);
}

void srxlChangeBaudRate(uint8_t port, uint32_t baudrate)
{
  auto* dev = Espfc::Device::InputSRXL2::getActiveSerial();
  if (dev)
  {
    dev->flush();
    dev->updateBaudRate(baudrate);
    if (baudrate == 400000)
    {
      Espfc::Device::InputSRXL2::getInstance()->updateFastBaud(true);
    }
    else
    {
      Espfc::Device::InputSRXL2::getInstance()->updateFastBaud(false);
    }
  }
}

void srxlFillTelemetry(SrxlTelemetryData* pTelemData)
{
  if (!pTelemData) return;

  auto* instance = Espfc::Device::InputSRXL2::getInstance();

  if (!instance || !instance->isTelemetryEnabled()) return;

  Espfc::Utils::Stats::Measure measure(instance->getModel().state.stats, Espfc::COUNTER_TELEMETRY);

  memset(pTelemData->raw, 0, sizeof(pTelemData->raw));

  pTelemData->sensorID = TELE_DEVICE_LIPOMON;
  pTelemData->secondaryID = 0;

  auto& battery = instance->getModel().state.battery;
  float cellVoltage = battery.cellVoltage;
  float batteryVoltage = battery.voltage;
  int cellCount = battery.cells;

  if (batteryVoltage < 2.0f || cellCount <= 0)
    cellCount = 0;
  else if (cellCount > 6)
    cellCount = 6;

  uint16_t cell_cv = (uint16_t)(cellVoltage * 100.0f + 0.5f);

  // Faster cell filling
  for (int i = 0; i < cellCount; ++i)
  {
    pTelemData->data[i * 2] = cell_cv & 0xFF;
    pTelemData->data[i * 2 + 1] = cell_cv >> 8;
  }

  // Pad remaining cells + temp
  for (int i = cellCount * 2; i < 14; ++i)
  {
    pTelemData->data[i] = (i & 1) ? 0x7F : 0xFF;
  }
}

void srxlReceivedChannelData(SrxlChannelData* pChannelData, bool isFailsafe)
{
  auto* instance = Espfc::Device::InputSRXL2::getInstance();

  if (instance && pChannelData)
  {
    bool failsafe = (isFailsafe || pChannelData->mask == 0)
                        ? true
                        : false; // Consider failsafe if mask is zero (no channels present)

    instance->setRxFailsafe(failsafe);

    if (!failsafe)
    {
      auto& input = instance->getModel().state.input;

      instance->onChannelFrame();

      // Process Digital RSSI (dBm or %)
      // Per SRXL2 spec, rssi is a signed byte.
      // Positive = % Link Quality, Negative = dBm.
      int8_t rawRssi = pChannelData->rssi;

      if (rawRssi < 0)
      {
        // It's dBm: store directly as signed int16_t
        input.rssi = static_cast<int16_t>(rawRssi);
      }
      else
      {
        // It's Link Quality %: store in linkQuality field
        input.linkQuality = static_cast<uint8_t>(rawRssi);
      }
    }
  }
}

void srxlOnVtx(SrxlVtxData* pVtxData)
{
  // Stub
}

bool srxlOnBind(SrxlFullID boundID, SrxlBindData status)
{
  return true;
}

} // extern "C"