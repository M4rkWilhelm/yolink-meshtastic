#include "sensors.h"

#include <Arduino.h>
#include <stm32l0xx_ll_adc.h>  // calibrated conversion helpers

namespace sensors {
namespace {

// The low-power library stops in ultra-low-power mode with fast wake-up: VREFINT
// is off during Stop and code runs before it has restarted (up to ~3 ms), which
// makes VDD and temperature readings taken right after a wake-up garbage (seen:
// 243 C). Wait for it to report ready first.
void waitForVrefint() {
    uint32_t start = micros();
    while (!(PWR->CSR & PWR_CSR_VREFINTRDYF) && micros() - start < 5000) {}
}

// Average a few 12-bit conversions (the factory calibration values are 12-bit),
// discarding the first one after the channel switch.
uint32_t read12(uint32_t channel) {
    waitForVrefint();
    analogReadResolution(12);
    analogRead(channel);
    uint32_t sum = 0;
    for (int i = 0; i < 6; i++) sum += analogRead(channel);
    return sum / 6;
}

int32_t g_lastGoodTemperature = 20;

}  // namespace

uint32_t vddMv() {
    return __LL_ADC_CALC_VREFANALOG_VOLTAGE(read12(AVREF), LL_ADC_RESOLUTION_12B);
}

int32_t temperatureC() {
    uint32_t vdd = vddMv();
    int32_t t = __LL_ADC_CALC_TEMPERATURE(vdd, read12(ATEMP), LL_ADC_RESOLUTION_12B);
    // Outside the chip's rated range means a bad reading: keep the last good one.
    if (t < -40 || t > 85) return g_lastGoodTemperature;
    g_lastGoodTemperature = t;
    return t;
}

uint32_t batteryPercent(uint32_t mv) {
    if (mv >= 3000) return 100;
    if (mv <= 2000) return 0;
    return (mv - 2000) / 10;
}

}  // namespace sensors
