#pragma once
#include <cstdint>
#include <cstddef>

namespace Espfc::Connect
{

enum LedType
{
  LED_SIMPLE,
  LED_STRIP,
};

enum LedStatus {
  LED_ON,
  LED_OK,
  LED_ERROR,
  LED_HEARTBEAT,
  LED_WARNING,
  LED_INIT, 
  LED_GYRO, 
  LED_DOUBLE_FLASH,
  LED_OFF,  
  LED_STATUS_COUNT
};

class StatusLed
{

public:
  StatusLed();
  void begin(int8_t pin, uint8_t type, uint8_t invert);
  void update();
  void setStatus(LedStatus newStatus, bool force = false);
  LedStatus getStatus() const { return _status; };

private:
  void write(uint8_t val);
  int8_t _pin;
  uint8_t _type;
  uint8_t _invert;
  LedStatus _status;
  uint32_t _next;  
  size_t _step;
  bool _locked;
  int * _pattern;
};

}