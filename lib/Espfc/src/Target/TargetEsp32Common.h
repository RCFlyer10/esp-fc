#pragma once

#include "Esp.h"
#include "Debug_Espfc.h"
#include "Device/SerialDevice.h"
#include "soc/uart_struct.h"
#include "hal/uart_ll.h"
#include <driver/uart.h>
#include <driver/gpio.h>
// Handle version differences for the signal map
#if __has_include("soc/uart_sig_map.h")
  #include "soc/uart_sig_map.h"
#elif __has_include("uart_sig_map.h")
  #include "uart_sig_map.h"
#endif

// Hardware specific ROM functions for GPIO Matrix
#if __has_include("esp_rom_gpio.h")
  #include "esp_rom_gpio.h"
#else
  #include "rom/gpio.h"
#endif

#define SERIAL_UART_PARITY_NONE      0B00000000
#define SERIAL_UART_PARITY_EVEN      0B00000010
#define SERIAL_UART_PARITY_ODD       0B00000011

#define SERIAL_UART_NB_BIT_5         0B00000000
#define SERIAL_UART_NB_BIT_6         0B00000100
#define SERIAL_UART_NB_BIT_7         0B00001000
#define SERIAL_UART_NB_BIT_8         0B00001100

#define SERIAL_UART_NB_STOP_BIT_0    0B00000000
#define SERIAL_UART_NB_STOP_BIT_1    0B00010000
#define SERIAL_UART_NB_STOP_BIT_15   0B00100000
#define SERIAL_UART_NB_STOP_BIT_2    0B00110000

#define ESPFC_WIFI
#define ESPFC_ESPNOW
#define ESPFC_LED_WS2812

namespace Espfc {

template<typename T>
inline int targetSerialInit(T& dev, const SerialDeviceConfig& conf)
{
  uint32_t sc = 0x8000000;
  switch(conf.data_bits)
  {
    case 8: sc |= SERIAL_UART_NB_BIT_8; break;
    case 7: sc |= SERIAL_UART_NB_BIT_7; break;
    case 6: sc |= SERIAL_UART_NB_BIT_6; break;
    case 5: sc |= SERIAL_UART_NB_BIT_5; break;
    default: sc |= SERIAL_UART_NB_BIT_8; break;
  }
  switch(conf.parity)
  {
    case SDC_SERIAL_PARITY_EVEN: sc |= SERIAL_UART_PARITY_EVEN; break;
    case SDC_SERIAL_PARITY_ODD:  sc |= SERIAL_UART_PARITY_ODD;  break;
    default: sc |= SERIAL_UART_PARITY_NONE; break;
  }
  switch(conf.stop_bits)
  {
    case SDC_SERIAL_STOP_BITS_2:  sc |= SERIAL_UART_NB_STOP_BIT_2;  break;
    case SDC_SERIAL_STOP_BITS_15: sc |= SERIAL_UART_NB_STOP_BIT_15; break;
    case SDC_SERIAL_STOP_BITS_1:  sc |= SERIAL_UART_NB_STOP_BIT_1;  break;
    default: break;
  }
  if(dev) dev.end();
  dev.setTxBufferSize(SERIAL_TX_FIFO_SIZE);
  dev.begin(conf.baud, sc, conf.rx_pin, conf.tx_pin, conf.inverted);
  if (conf.halfDuplex) {    
    int uart_num = 0;
    if (&dev == &Serial) uart_num = 0;
    #if SOC_UART_NUM > 1
    else if (&dev == &Serial1) uart_num = 1;
    #endif
    #if SOC_UART_NUM > 2
    else if (&dev == &Serial2) uart_num = 2;
    #endif

    gpio_num_t pin = (gpio_num_t)conf.tx_pin;    

    // Open-drain + pull-up for single wire
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(pin, 1);
    gpio_pullup_en(pin);
    gpio_pulldown_dis(pin);    

    // Switch to half-duplex mode
    uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX);

    uint32_t tx_sig = (uart_num == 0) ? U0TXD_OUT_IDX : 
                              (uart_num == 1) ? U1TXD_OUT_IDX : U2TXD_OUT_IDX;

    uint32_t rx_sig = (uart_num == 0) ? U0RXD_IN_IDX : 
                              (uart_num == 1) ? U1RXD_IN_IDX : U2RXD_IN_IDX;

    // Force both TX and RX to the same pin
    esp_rom_gpio_connect_out_signal((gpio_num_t)pin, tx_sig, false, false);
    esp_rom_gpio_connect_in_signal((gpio_num_t)pin, rx_sig, false);

    // Important RS485 settings
    uart_dev_t *hw = (uart_num == 0) ? &UART0 : (uart_num == 1) ? &UART1 : &UART2;
    hw->rs485_conf.en = 1;
    hw->rs485_conf.rx_busy_tx_en = 0;   // Allow TX while receiving
    hw->conf0.loopback = 0;    
    hw->idle_conf.rx_idle_thrhd = 2;    // Idle threshold for end of frame
    hw->conf1.rx_tout_thrhd = 2;       // Timeout after 2 symbols of silence
    hw->conf1.rxfifo_full_thrhd = 1;    // Available for read after 1 byte
    hw->conf1.txfifo_empty_thrhd = 10;  // Signal empty while we still have buffer space

    Serial.printf("Converting to half-duplex on GPIO%d\n", conf.tx_pin);
  }
    
  return 1;
}

template<typename T>
inline int targetSPIInit(T& dev, int8_t sck, int8_t mosi, int8_t miso, int8_t ss)
{
  dev.begin(sck, miso, mosi, ss);
  return 1;
}

template<typename T>
inline int targetI2CInit(T& dev, int8_t sda, int8_t scl, uint32_t speed)
{
  dev.begin(sda, scl, speed);
  dev.setTimeout(50);
  return 1;
}

inline uint32_t getBoardId0()
{
  const int64_t mac = ESP.getEfuseMac();
  return (uint32_t)mac;
}

inline uint32_t getBoardId1()
{
  const int64_t mac = ESP.getEfuseMac();
  return (uint32_t)(mac >> 32);
}

inline uint32_t getBoardId2()
{
  return 0;
}

inline void targetReset()
{
  ESP.restart();
  while(1) {}
}

inline uint32_t targetCpuFreq()
{
  return ESP.getCpuFreqMHz();
}

inline uint32_t targetFreeHeap()
{
  return ESP.getFreeHeap();
}

};
