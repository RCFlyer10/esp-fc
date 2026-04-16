#include "StatusLed.hpp"
#include "Target/Target.h"
#include <Arduino.h>

#ifdef ESPFC_LED_WS2812
#include "driver/i2s.h"

// https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/api-reference/peripherals/i2s.html
// https://github.com/vunam/esp32-i2s-ws2812/blob/master/ws2812.c

static constexpr size_t LED_NUMBER = 2;
static constexpr size_t PIXEL_SIZE = 12; // each colour takes 4 bytes in buffer
static constexpr size_t ZERO_BUFFER = 32;
static constexpr size_t SIZE_BUFFER = LED_NUMBER * PIXEL_SIZE + ZERO_BUFFER;
static constexpr uint32_t SAMPLE_RATE = 93750;
static constexpr i2s_port_t I2S_NUM = I2S_NUM_0;

typedef struct {
  uint8_t g;
  uint8_t r;
  uint8_t b;
} ws2812_pixel_t;

static i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_MSB,
  .intr_alloc_flags = 0,
  .dma_buf_count = 2,
  .dma_buf_len = SIZE_BUFFER / 2,
  .use_apll = false,
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0,
  .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,
  .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
};

static i2s_pin_config_t pin_config = {
  .bck_io_num = -1,
  .ws_io_num = -1,
  .data_out_num = -1,
  .data_in_num = -1
};

static uint8_t out_buffer[SIZE_BUFFER] = {0};

static const uint16_t bitpatterns[4] = {0x88, 0x8e, 0xe8, 0xee};

static void ws2812_init(int8_t pin)
{
  pin_config.data_out_num = pin;
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM);
  std::fill_n(out_buffer, SIZE_BUFFER, 0);
}

static void ws2812_write_pixel(uint8_t * buffer, const ws2812_pixel_t& pixel)
{
  *buffer++ = bitpatterns[pixel.g >> 6 & 0x03];
  *buffer++ = bitpatterns[pixel.g >> 4 & 0x03];
  *buffer++ = bitpatterns[pixel.g >> 2 & 0x03];
  *buffer++ = bitpatterns[pixel.g >> 0 & 0x03];

  *buffer++ = bitpatterns[pixel.r >> 6 & 0x03];
  *buffer++ = bitpatterns[pixel.r >> 4 & 0x03];
  *buffer++ = bitpatterns[pixel.r >> 2 & 0x03];
  *buffer++ = bitpatterns[pixel.r >> 0 & 0x03];

  *buffer++ = bitpatterns[pixel.b >> 6 & 0x03];
  *buffer++ = bitpatterns[pixel.b >> 4 & 0x03];
  *buffer++ = bitpatterns[pixel.b >> 2 & 0x03];
  *buffer++ = bitpatterns[pixel.b >> 0 & 0x03];
}

static void ws2812_update(const ws2812_pixel_t * pixels)
{
  size_t bytes_written = 0;
  for (size_t i = 0; i < LED_NUMBER; i++)
  {
    size_t loc = i * PIXEL_SIZE;
    ws2812_write_pixel(out_buffer + loc, pixels[i]);
  }
  i2s_zero_dma_buffer(I2S_NUM);
  i2s_write(I2S_NUM, out_buffer, SIZE_BUFFER, &bytes_written, portMAX_DELAY);
}

static const ws2812_pixel_t PIXEL_ON[] = {{0x40, 0x40, 0x80}, {0x40, 0x40, 0x80}};
static const ws2812_pixel_t PIXEL_OFF[] = {{0, 0, 0}, {0, 0, 0}};

#endif

namespace Espfc::Connect
{

static int LED_OK_PATTERN[] = {100, 900, 0};
static int LED_HEARTBEAT_PATTERN[] = {50, 450, 0};
static int LED_ERROR_PATTERN[] = {100, 100, 100, 100, 100, 1500, 0};
static int LED_INIT_PATTERN[] = { 200, 200, -1 };      // One quick flash
static int DOUBLE_FLASH_PATTERN[] = { 200, 200, 200, 200, -1}; // Two quick flashes 
static int LED_GYRO_PATTERN[] = { 100, 100, 100, 100, 100, 100, -1 }; // Three quick blinks

StatusLed::StatusLed() : _pin(-1), _invert(0), _status(LED_OFF), _next(0), _step(0), _pattern(nullptr), _locked(false) {}

void StatusLed::begin(int8_t pin, uint8_t type, uint8_t invert)
{
  if(pin == -1) return;

  _pin = pin;
  _type = type;
  _invert = invert;

#ifdef ESPFC_LED_WS2812
  if(_type == LED_STRIP) ws2812_init(_pin);
  if(_type == LED_SIMPLE) pinMode(_pin, OUTPUT);
#else
  pinMode(_pin, OUTPUT);
#endif
  setStatus(LED_ON, true);
}

void StatusLed::setStatus(LedStatus newStatus, bool force)
{
  if(_pin == -1) return;
  if(_locked && !force) return; // Prevent re-entrancy if setStatus is called again before the previous call finishes
  if(!force && newStatus == _status) return;

  _status = newStatus;  
  _step = 0;
  _next = millis();

  _locked = (newStatus == LED_INIT || newStatus == LED_GYRO || newStatus == LED_DOUBLE_FLASH);

  switch (_status)
  {
    case LED_ON:
      _pattern = nullptr;
      write(1);        
      break;    
    case LED_OK:
      _pattern = LED_OK_PATTERN;
      break;
    case LED_ERROR:
      _pattern = LED_ERROR_PATTERN;
      break;    
    case LED_HEARTBEAT:
      _pattern = LED_HEARTBEAT_PATTERN;
      break;
    case LED_INIT:
      _pattern = LED_INIT_PATTERN;
      break;
    case LED_DOUBLE_FLASH:
      _pattern = DOUBLE_FLASH_PATTERN;
      break;
    case LED_GYRO:
      _pattern = LED_GYRO_PATTERN;
      break;
    default:
      _pattern = nullptr;
      write(0);
      break;
  }  
}

void StatusLed::update()
{
  if(_pin == -1 || !_pattern) return;  
  
  uint32_t now = millis();
  if(now < _next) return;

  // 1. ADVANCE STEP FIRST (since we just finished the previous 'next' wait)
  // But wait—we need to know if the CURRENT step is a terminator.
  
  if (_pattern[_step] == 0) // REPEAT
  {    
    _step = 0; 
  }
  else if (_pattern[_step] == -1) // STOP
  {
    _pattern = nullptr;
    _locked = false;
    return;   
  }

  // 2. Perform the write based on current step
  write(!(_step & 1));  

  // 3. Schedule the NEXT update and move the index forward
  _next = now + _pattern[_step];
  _step++;
}

void StatusLed::write(uint8_t val)
{
#ifdef ESPFC_LED_WS2812
  if(_type == LED_STRIP) ws2812_update(val ? PIXEL_ON : PIXEL_OFF);
  if(_type == LED_SIMPLE) digitalWrite(_pin, val ^ _invert);
#else
  digitalWrite(_pin, val ^ _invert);
#endif
}

}
