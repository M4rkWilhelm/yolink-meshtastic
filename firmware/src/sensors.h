// On-chip measurements: supply voltage and die temperature (STM32L0 internal
// ADC channels with factory calibration, as the stock firmware does at 0x0800426E).
#pragma once

#include <stdint.h>

namespace sensors {

// Supply voltage in mV (from VREFINT). With 2xAA straight to VDD, this is the
// battery voltage.
uint32_t vddMv();

// Chip temperature in whole degrees C. Inside the housing, so it reads somewhat
// high in direct sun.
int32_t temperatureC();

// Rough 2xAA charge estimate from vddMv(): 3.0 V or more = 100 %, 2.0 V = 0 %.
uint32_t batteryPercent(uint32_t mv);

}  // namespace sensors
