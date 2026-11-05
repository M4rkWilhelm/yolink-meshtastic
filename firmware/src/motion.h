// PIR motion confirmation, modelled on the stock firmware (0x08009340).
#pragma once

#include <stdint.h>

namespace motion {

// Called after a PB6 (comparator) edge. Samples the PIR analog level on PC1
// and returns true if it swung far enough from the resting level: 3 of 5
// samples outside center +/- yolink.pir_threshold (0 = skip the check).
bool confirm();

// Sets the PIR amplifier gain (PC2/PC3) from yolink.pir_sensitivity, adjusted
// for temperature per yolink.pir_temp_comp. Returns true if the gain changed;
// the amplifier output steps when it does, so callers ignore motion briefly.
bool applyGain(int32_t temperatureC);

// Gain step currently applied: 0 = low .. 3 = max.
uint8_t gainLevel();

// Most recent samples and verdicts, for inspection over SWD.
struct Stats {
    uint8_t samples[5];
    uint32_t confirmed;
    uint32_t rejected;
};
extern volatile Stats g_stats;

}  // namespace motion
