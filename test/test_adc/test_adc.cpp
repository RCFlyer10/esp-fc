#include <ArduinoFake.h>
#include <unity.h>
#include "Model.h"
#include "Sensor/VoltageSensor.h"

using namespace Espfc;
using namespace Espfc::Sensor;

using namespace fakeit;

static Model model;
static uint32_t fakeMicros = 1000000;

// Mock to simulate 5mΩ shunt (1A = 5mV) with amplifier
// Adjust the calculation based on your ADC's reference voltage
uint16_t getAdcForAmps(float amps) {
    float voltageDrop = amps * 0.005f; // V = I * R
    float sense = voltageDrop * 50.0f; // voltageDrop * IN2180A2 Gain 50V/V
    // Assuming ESPFC_ADC_SCALE = (Vref / Resolution)
    return (uint16_t)(sense / ESPFC_ADC_SCALE);
}

uint16_t getAdcForVolts(float volts) {
    float voltageDrop = volts * 0.5f; // 2:1 divider
    return (uint16_t)(voltageDrop / ESPFC_ADC_SCALE);
}

void test_adc_current_measurment()
{   
    model.config.ibat.source = 1;
    model.config.ibat.scale = 40;
    model.config.ibat.offset = 1;
    model.config.pin[PIN_INPUT_ADC_1] = 34;

    VoltageSensor sensor(model);
    sensor.begin();

    float target = 4.5f; // Amps
    
    // Mock ADC: 
    When(Method(ArduinoFake(), analogRead)).AlwaysReturn(getAdcForAmps(target));
    When(Method(ArduinoFake(), micros)).AlwaysDo([&]() { return fakeMicros; });

    // Allow the filters to stabilize
    for (int i = 0; i < 25; i++)
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
    
    // 10 minutes (1/6 of an hour) in maH
    float targetConsumed = target * 1000.0f / 6.0f;
    float ampsError = target * .005f;
    float consumedError = targetConsumed * .005f;
    printf("Amps: %f mAh: %f\n", model.state.battery.current , model.state.battery.mahConsumed);
    
    TEST_ASSERT_FLOAT_WITHIN(ampsError, target, model.state.battery.current);
    TEST_ASSERT_FLOAT_WITHIN(consumedError, targetConsumed, model.state.battery.mahConsumed);
}

void test_adc_voltage_measurment()
{
    model.config.vbat.source = 1;
    model.config.vbat.scale = 214;
    model.config.vbat.resDiv = 10;
    model.config.vbat.resMult = 1;
    model.config.vbat.cellMax = 436;
    model.config.vbat.cellMin = 330;
    model.config.pin[PIN_INPUT_ADC_0] = 35;

    VoltageSensor sensor(model);
    sensor.begin();

    float target = 4.35f;

    When(Method(ArduinoFake(), analogRead)).AlwaysReturn(getAdcForVolts(target));

    // Allow the filters to stabilize
    for (int i = 0; i < 25; i++)
    {
        sensor.readVbat();
    }
    
    float voltsError = target * .005f;
    printf("Volts: %f cell count: %d\n", model.state.battery.voltage , model.state.battery.cells);
    TEST_ASSERT_EQUAL_INT8(1, model.state.battery.cells);
    TEST_ASSERT_FLOAT_WITHIN(voltsError, target, model.state.battery.voltage);
}

int main(int argc, char** argv)
{
  UNITY_BEGIN();
  RUN_TEST(test_adc_current_measurment);  
  RUN_TEST(test_adc_voltage_measurment);
  return UNITY_END();
}
