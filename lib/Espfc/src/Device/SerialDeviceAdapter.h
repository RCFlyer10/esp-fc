#ifndef _ESPFC_SERIAL_DEVICE_ADAPTER_H_
#define _ESPFC_SERIAL_DEVICE_ADAPTER_H_

#include "Device/SerialDevice.h"
#ifdef ESPFC_SERIAL_SOFT_0_WIFI
#include <WiFiClient.h>
#endif
#ifdef ESP32
#include <esp_rom_gpio.h>
#endif

namespace Espfc {

namespace Device {

template<typename T>
class SerialDeviceAdapter: public SerialDevice
{
  public:
    SerialDeviceAdapter(T& dev, int uartIndex): _dev(dev), _uartIndex(uartIndex) {}
    void begin(const SerialDeviceConfig& conf) override 
    {
      targetSerialInit(_dev, conf);

      if (conf.halfDuplex)
      {
        setupHalfDuplex(conf.tx_pin, _uartIndex);
      }
    }
    void updateBaudRate(int baud) override { _dev.updateBaudRate(baud); };
    int available() override { return _dev.available(); }
    int read() override { return _dev.read(); }
    size_t readMany(uint8_t * c, size_t l) override {
#if defined(ARCH_RP2040)
      size_t count = std::min(l, (size_t)available());
      for(size_t i = 0; i < count; i++)
      {
        c[i] = read();
      }
      return count;
#else
      return _dev.read(c, l);
#endif
    }
    int peek() override { return _dev.peek(); }
    void flush() override { _dev.flush(); }
    size_t write(uint8_t c) override { return _dev.write(c); }
    size_t write(const uint8_t * c, size_t l) override { return _dev.write(c, l); }
    bool isSoft() const override { return false; };
    int availableForWrite() override { return _dev.availableForWrite(); }
    bool isTxFifoEmpty() override { return _dev.availableForWrite() >= SERIAL_TX_FIFO_SIZE; }
    operator bool() const override { return (bool)_dev; }
  private:
    void setupHalfDuplex(int8_t tx_pin, int8_t uartIndex)
  {
#ifndef UNIT_TEST
    if (tx_pin < 0) return;

    gpio_num_t pin = (gpio_num_t)tx_pin;
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(pin, 1);
    gpio_pullup_en(pin);
    gpio_pulldown_dis(pin);

    uint32_t tx_sig = U0TXD_OUT_IDX;
    uint32_t rx_sig = U0RXD_IN_IDX;

    switch (uartIndex)
    {
      case 0:
        tx_sig = U0TXD_OUT_IDX;
        rx_sig = U0RXD_IN_IDX;
        break;
      case 1:
        tx_sig = U1TXD_OUT_IDX;
        rx_sig = U1RXD_IN_IDX;
        break;
      case 2:
#if SOC_UART_NUM > 2
        tx_sig = U2TXD_OUT_IDX;
        rx_sig = U2RXD_IN_IDX;
#endif
        break;
      default:
        return; // Invalid UART index → do nothing
    }

    esp_rom_gpio_connect_out_signal(pin, tx_sig, false, false);
    esp_rom_gpio_connect_in_signal(pin, rx_sig, false);
#else
    // Native build - do nothing
    (void)tx_pin;    
#endif
  }
    T& _dev;
    int _uartIndex;
};

// WiFiClient specializations
#ifdef ESPFC_SERIAL_SOFT_0_WIFI
template<>
inline void SerialDeviceAdapter<WiFiClient>::begin(const SerialDeviceConfig& conf)
{
}

template<>
inline int SerialDeviceAdapter<WiFiClient>::availableForWrite()
{
  return SERIAL_TX_FIFO_SIZE;
}

template<>
inline bool SerialDeviceAdapter<WiFiClient>::isTxFifoEmpty()
{
  return true;
}

template<>
inline void SerialDeviceAdapter<WiFiClient>::updateBaudRate(int baud) {}

#endif

#if defined(ESP32C3) || defined(ESP32S3)
template<>
inline void SerialDeviceAdapter<HWCDC>::updateBaudRate(int baud) {}
#endif

#if defined(ESP32S2)
template<>
inline void SerialDeviceAdapter<USBCDC>::updateBaudRate(int baud) {}
#endif

#if defined(ARCH_RP2040)
template<>
inline void SerialDeviceAdapter<SerialUART>::updateBaudRate(int baud)
{
  _dev.begin(baud);
}

template<>
inline void SerialDeviceAdapter<SerialUSB>::updateBaudRate(int baud) {}
#endif

}

}

#endif