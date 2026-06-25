#include "Device/InputSRXL2.h"
#include "Model.h"
#include <ArduinoFake.h>
#include <cstring>
#include <unity.h>
#include <vector>

using namespace Espfc;
using namespace Espfc::Device;
using namespace fakeit;

// ====================== CRC Helper ======================
uint16_t srxlCrc16(uint16_t crc, uint8_t data)
{
  crc = crc ^ ((uint16_t)data << 8);
  for (int i = 0; i < 8; ++i)
  {
    if (crc & 0x8000)
      crc = (crc << 1) ^ 0x1021;
    else
      crc = crc << 1;
  }
  return crc;
}

uint16_t computeSrxlCrc(const uint8_t* data, size_t len)
{
  uint16_t crc = 0;
  for (size_t i = 0; i < len; ++i)
  {
    crc = srxlCrc16(crc, data[i]);
  }
  return crc;
}

// ====================== MockSerial ======================
class MockSerial : public SerialDevice
{
public:
  virtual int available() override
  {
    return _rxData.size();
  }
  virtual int read() override
  {
    return -1;
  }
  virtual int peek() override
  {
    return -1;
  }
  virtual size_t readMany(uint8_t* buf, size_t len) override
  {
    if (len == 0 || _rxData.empty()) return 0;
    size_t copy = std::min(len, _rxData.size());
    memcpy(buf, _rxData.data(), copy);
    _rxData.erase(_rxData.begin(), _rxData.begin() + copy);
    return copy;
  }
  virtual size_t write(uint8_t c) override
  {
    _txData.push_back(c);
    return 1;
  }
  virtual size_t write(const uint8_t* data, size_t len) override
  {
    _txData.insert(_txData.end(), data, data + len);
    return len;
  }
  virtual void begin(const SerialDeviceConfig& conf) override {}
  virtual void updateBaudRate(int baud) override
  {
    _lastBaud = baud;
  }
  virtual int availableForWrite() override
  {
    return 256;
  }
  virtual bool isTxFifoEmpty() override
  {
    return true;
  }
  virtual bool isSoft() const override
  {
    return false;
  }
  virtual operator bool() const override
  {
    return true;
  }
  virtual void flush() override {}

  void feed(const std::vector<uint8_t>& data)
  {
    _rxData.insert(_rxData.end(), data.begin(), data.end());
  }
  const std::vector<uint8_t>& getTxData() const
  {
    return _txData;
  }
  int getLastBaud() const
  {
    return _lastBaud;
  }
  void clear()
  {
    _txData.clear();
    _rxData.clear();
  }

private:
  std::vector<uint8_t> _rxData;
  std::vector<uint8_t> _txData;
  int _lastBaud = 0;
};

static MockSerial mockSerial;
static Model model;
static uint32_t lastUid = 0;
static uint32_t fakeMicros = 1000000;

void setUp(void)
{
  mockSerial.clear();
  lastUid = 0;
  When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
  When(Method(ArduinoFake(), micros)).AlwaysDo([]() -> uint32_t {
    uint32_t t = fakeMicros;
    fakeMicros += 100;
    return t;
  });
}

// ====================== TESTS ======================

void test_input_srxl2_begin()
{
  InputSRXL2 input;
  TEST_ASSERT_EQUAL(1, input.begin(&mockSerial, model, true));
}

void test_input_srxl2_handshake_reply()
{
  InputSRXL2 input;
  input.begin(&mockSerial, model, true);

  std::vector<uint8_t> req = {0xA6, 0x21, 0x0E, 0x21, 0x30, 0x0A, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12};
  uint16_t crc = computeSrxlCrc(req.data(), req.size());
  req.push_back((crc >> 8) & 0xFF);
  req.push_back(crc & 0xFF);
  mockSerial.feed(req);
  
  InputStatus status = input.update();

  TEST_ASSERT_EQUAL(INPUT_FAILSAFE, status);

  const auto& tx = mockSerial.getTxData();
  TEST_ASSERT_GREATER_THAN(13, tx.size());
  TEST_ASSERT_EQUAL_UINT8(SRXL_HANDSHAKE_ID, tx[1]);
}

void test_input_srxl2_handshake_baud_rate_switch()
{
    InputSRXL2 input;
    input.begin(&mockSerial, model, true);

    // Broadcast handshake requesting baud rate switch to 400k
    std::vector<uint8_t> packet = {
        0xA6, 0x21, 0x0E,     // Header
        0x21, 0xFF,           // Src=Receiver, Dest=Broadcast
        0x0A,                 // Priority
        0x01,                 // 1 = 400000 baud
        0x00,                 // Info
        0x78, 0x56, 0x34, 0x12 // UID
    };

    uint16_t crc = computeSrxlCrc(packet.data(), packet.size());
    packet.push_back((crc >> 8) & 0xFF);
    packet.push_back(crc & 0xFF);

    mockSerial.feed(packet);
    InputStatus status = input.update();

    TEST_ASSERT_EQUAL(INPUT_FAILSAFE, status);
    TEST_ASSERT_EQUAL(400000, mockSerial.getLastBaud());   
}

void test_input_srxl2_control_data_channels()
{
  InputSRXL2 input;
  input.begin(&mockSerial, model, true);

  // Use a vaid control data packet
  std::vector<uint8_t> packet = {
      0xA6, 0xCD, 0x1C,       // Header: 0xA6, 0xCD, Length (28 bytes total)
      0x00,                   // Command: Channel Data (0x00)
      0x00,                   // ReplyID
      0x58,                   // RSSI: 88%
      0x0B, 0x00,             // Frame Losses: 11 (Little Endian: 0x000B)
      0x37, 0x06, 0x00, 0x00, // Channel Mask: 0x00000637 (Channels 1, 2, 3, 5, 6, 10, 11)
      0xA0, 0x2A,             // CH 1: 10912 (0x2AA0) (approx -100% on Spektrum transmitter)
      0x00, 0x80,             // CH 2: 32768 (0x8000) (center position)
      0x04, 0x80,             // CH 3: 32772 (0x8004) (1 tick above center at 14-bit resolution)
      0xFC, 0x7F,             // CH 5: 32764 (0x7FFC) (1 tick below center at 14-bit resolution)
      0x54, 0xD5,             // CH 6: 54612 (0xD554) (approx. 100% on Spektrum transmitter)
      0xA0, 0x2A,             // CH 10: 10912 (0x2AA0)
      0xA0, 0x2A              // CH 11: 10912 (0x2AA0)
  };

  uint16_t crc = computeSrxlCrc(packet.data(), packet.size());
  packet.push_back((crc >> 8) & 0xFF);
  packet.push_back(crc & 0xFF);
  mockSerial.feed(packet);

  InputStatus status = input.update();

  TEST_ASSERT_EQUAL(INPUT_RECEIVED, status);
  TEST_ASSERT_EQUAL_UINT16(988, input.get(0));
  TEST_ASSERT_EQUAL_UINT16(1500, input.get(1));
  TEST_ASSERT_EQUAL_UINT16(2012, input.get(5));
  TEST_ASSERT_EQUAL_INT8(88, model.state.input.linkQuality);

  // Use a control data packet with no channel data, simmulating transmitter off or not bound
  packet = {0xA6, 0xCD, 0x1C, // Header: 0xA6, 0xCD, Length (28 bytes total)
            0x00,             // Command: Channel Data (0x00)
            0x00,             // ReplyID
            0,    0,    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  crc = computeSrxlCrc(packet.data(), packet.size());
  packet.push_back((crc >> 8) & 0xFF);
  packet.push_back(crc & 0xFF);
  mockSerial.feed(packet);

  status = input.update();

  TEST_ASSERT_EQUAL(INPUT_FAILSAFE, status);
}

void test_input_srxl2_telemetry_send()
{
  model.state.battery.cellVoltage = 3.85f; // Set test voltage
  model.state.battery.voltage = 3.85f;
  model.state.battery.cells = 1;

  InputSRXL2 input;
  input.begin(&mockSerial, model, true);

  std::vector<uint8_t> packet = {
      0xA6, 0xCD, 0x1C,       // Header: 0xA6, 0xCD, Length (28 bytes total)
      0x00,                   // Command: Channel Data (0x00)
      0x30,                   // ReplyID
      0x58,                   // RSSI: 88%
      0x0B, 0x00,             // Frame Losses: 11 (Little Endian: 0x000B)
      0x37, 0x06, 0x00, 0x00, // Channel Mask: 0x00000637 (Channels 1, 2, 3, 5, 6, 10, 11)
      0xA0, 0x2A,             // CH 1: 10912 (0x2AA0) (approx -100% on Spektrum transmitter)
      0x00, 0x80,             // CH 2: 32768 (0x8000) (center position)
      0x04, 0x80,             // CH 3: 32772 (0x8004) (1 tick above center at 14-bit resolution)
      0xFC, 0x7F,             // CH 5: 32764 (0x7FFC) (1 tick below center at 14-bit resolution)
      0x54, 0xD5,             // CH 6: 54612 (0xD554) (approx. 100% on Spektrum transmitter)
      0xA0, 0x2A,             // CH 10: 10912 (0x2AA0)
      0xA0, 0x2A              // CH 11: 10912 (0x2AA0)
  };

  uint16_t crc = computeSrxlCrc(packet.data(), packet.size());
  packet.push_back((crc >> 8) & 0xFF);
  packet.push_back(crc & 0xFF);
  mockSerial.feed(packet);

  InputStatus status = input.update();
  const auto& tx = mockSerial.getTxData();

  TEST_ASSERT_EQUAL(INPUT_RECEIVED, status);
  TEST_ASSERT_GREATER_THAN(21, tx.size());
  TEST_ASSERT_EQUAL_UINT8(SRXL_TELEM_ID, tx[1]);
  TEST_ASSERT_EQUAL_UINT8(TELE_DEVICE_LIPOMON, tx[4]);
  
  uint16_t expected_cv = (uint16_t)(model.state.battery.cellVoltage * 100.0f + 0.5f);
  TEST_ASSERT_EQUAL_UINT8(expected_cv & 0xFF, tx[6]);
  TEST_ASSERT_EQUAL_UINT8(expected_cv >> 8, tx[7]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, tx[8]);
  TEST_ASSERT_EQUAL_UINT8(0x7F, tx[9]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, tx[10]);
  TEST_ASSERT_EQUAL_UINT8(0x7F, tx[11]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, tx[12]);
  TEST_ASSERT_EQUAL_UINT8(0x7F, tx[13]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, tx[14]);
  TEST_ASSERT_EQUAL_UINT8(0x7F, tx[15]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, tx[16]);
  TEST_ASSERT_EQUAL_UINT8(0x7F, tx[17]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, tx[18]);
  TEST_ASSERT_EQUAL_UINT8(0x7F, tx[19]);
}

void test_input_srxl2_failsafe_channels()
{
  InputSRXL2 input;
  input.begin(&mockSerial, model, true);

  std::vector<uint8_t> packet = {0xA6, 0xCD, 0x1C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07,
                                 0x00, 0x00, 0x00, 0x54, 0xD5, 0x00, 0x80, 0xA0, 0x2A};

  uint16_t crc = computeSrxlCrc(packet.data(), packet.size());
  packet.push_back((crc >> 8) & 0xFF);
  packet.push_back(crc & 0xFF);

  mockSerial.feed(packet);
  InputStatus status = input.update();

  TEST_ASSERT_EQUAL(INPUT_FAILSAFE, status);
}

void test_input_srxl2_timeout_handshake()
{
  InputSRXL2 input;
  input.begin(&mockSerial, model, true);
  When(Method(ArduinoFake(), millis)).AlwaysReturn(1000000);

  for (int i = 0; i < 20; ++i)
  {
    input.update();
  }

  const auto& tx = mockSerial.getTxData();
  TEST_ASSERT_GREATER_THAN(13, tx.size());
  TEST_ASSERT_EQUAL_UINT8(SRXL_HANDSHAKE_ID, tx[1]);
}

void test_input_srxl2_rx_buffer_shift()
{
  InputSRXL2 input;
  input.begin(&mockSerial, model, true);

  // Push random data to the serial buffer
  std::vector<uint8_t> packet = {0xFF, 0x10, 0xFF, 0x30};
  mockSerial.feed(packet);

  // Push a failsafe channel packet behind the random data
  packet = {0xA6, 0xCD, 0x1C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07,
            0x00, 0x00, 0x00, 0x54, 0xD5, 0x00, 0x80, 0xA0, 0x2A};
  uint16_t crc = computeSrxlCrc(packet.data(), packet.size());
  packet.push_back((crc >> 8) & 0xFF);
  packet.push_back(crc & 0xFF);
  mockSerial.feed(packet);

  InputStatus status = input.update();

  TEST_ASSERT_EQUAL(INPUT_FAILSAFE, status);
}

void test_input_srxl2_packet_fragmenting()
{
  InputSRXL2 input;
  input.begin(&mockSerial, model, true);

  std::vector<uint8_t> packet = {
      0xA6, 0xCD, 0x1C,       // Header: 0xA6, 0xCD, Length (28 bytes total)
      0x00,                   // Command: Channel Data (0x00)
      0x30,                   // ReplyID
      0x58,                   // RSSI: 88%
      0x0B, 0x00,             // Frame Losses: 11 (Little Endian: 0x000B)
      0x37, 0x06, 0x00, 0x00, // Channel Mask: 0x00000637 (Channels 1, 2, 3, 5, 6, 10, 11)
      0xA0, 0x2A,             // CH 1: 10912 (0x2AA0) (approx -100% on Spektrum transmitter)
      0x00, 0x80,             // CH 2: 32768 (0x8000) (center position)
      0x04, 0x80,             // CH 3: 32772 (0x8004) (1 tick above center at 14-bit resolution)
      0xFC, 0x7F,             // CH 5: 32764 (0x7FFC) (1 tick below center at 14-bit resolution)
      0x54, 0xD5,             // CH 6: 54612 (0xD554) (approx. 100% on Spektrum transmitter)
      0xA0, 0x2A,             // CH 10: 10912 (0x2AA0)
      0xA0, 0x2A              // CH 11: 10912 (0x2AA0)
  };
  
  uint16_t crc = computeSrxlCrc(packet.data(), packet.size());
  packet.push_back((crc >> 8) & 0xFF);
  packet.push_back(crc & 0xFF);

  // Split the packet into two halves
  size_t splitPoint = packet.size() / 2;
  std::vector<uint8_t> firstHalf(packet.begin(), packet.begin() + splitPoint);
  std::vector<uint8_t> secondHalf(packet.begin() + splitPoint, packet.end());

  // 1. Feed the first half
  mockSerial.feed(firstHalf);
  InputStatus status = input.update();

  // Assert: Status should be FAILSAFE because the packet is incomplete
  TEST_ASSERT_EQUAL(INPUT_FAILSAFE, status);

  // 2. Feed the second half
  mockSerial.feed(secondHalf);
  status = input.update();

  // Assert: Status should now be RECEIVED because the full packet is present
  TEST_ASSERT_EQUAL(INPUT_RECEIVED, status);
}

int main(int argc, char** argv)
{
  UNITY_BEGIN();
  RUN_TEST(test_input_srxl2_begin);
  RUN_TEST(test_input_srxl2_handshake_reply);
  RUN_TEST(test_input_srxl2_handshake_baud_rate_switch);
  RUN_TEST(test_input_srxl2_control_data_channels);
  RUN_TEST(test_input_srxl2_failsafe_channels);
  RUN_TEST(test_input_srxl2_telemetry_send);
  RUN_TEST(test_input_srxl2_timeout_handshake);
  RUN_TEST(test_input_srxl2_rx_buffer_shift);
  RUN_TEST(test_input_srxl2_packet_fragmenting);
  return UNITY_END();
}