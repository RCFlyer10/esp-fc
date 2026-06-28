#include <ArduinoFake.h>
#include <unity.h>
#include "Model.h"
#include "Sensor/VoltageSensor.h"

using namespace Espfc;
using namespace Espfc::Sensor;

using namespace fakeit;

static Model model;
static uint32_t fakeMicros = 1000000;

// Mock to simulate 100mΩ shunt (1A = 100mV)
// Adjust the calculation based on your ADC's reference voltage
uint16_t getAdcForAmps(float amps) {
    float voltageDrop = amps * 0.1f; // V = I * R
    // Assuming ESPFC_ADC_SCALE = (Vref / Resolution)
    return (uint16_t)(voltageDrop / ESPFC_ADC_SCALE);
}

void test_adc_current_measure()
{   
    model.config.ibat.source = 1;
    model.config.ibat.scale = 100;
    model.config.ibat.offset = 1;
    model.config.pin[PIN_INPUT_ADC_1] = 34;

    VoltageSensor sensor(model);
    sensor.begin();
    
    // Mock ADC: Return 5 Amps
    When(Method(ArduinoFake(), analogRead)).AlwaysReturn(getAdcForAmps(5.0f));
    When(Method(ArduinoFake(), micros)).AlwaysDo([&]() { return fakeMicros; });

    // Allow the filters to stabilize
    for (int i = 0; i < 30; i++)
    {
        sensor.readIbat();
    }

    // Simulation Loop    
    uint32_t duration = 600000000; // Total duration: 10 minutes = 600 seconds = 600,000,000 microseconds
    uint32_t step = 1000000; // Step by 1 second (1,000,000 microseconds)
    for (uint32_t i = 0; i < duration; i += step)
    {
        fakeMicros += step;
        sensor.readIbat();
    }
    
    // 5 Amps for 10 minutes (1/6 of an hour) = 5000mA / 6 = 833.33 mAh
    float expectedMah = 833.33f;
    printf("Amps: %f mAh: %f\n", model.state.battery.current , model.state.battery.mahUsed);
    
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, model.state.battery.current);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, expectedMah, model.state.battery.mahUsed);
}

int main(int argc, char** argv)
{
  UNITY_BEGIN();
  RUN_TEST(test_adc_current_measure);  
  return UNITY_END();
}
