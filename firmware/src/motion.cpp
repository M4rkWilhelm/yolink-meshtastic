#include "motion.h"

#include <Arduino.h>

#include "board.h"
#include "config.h"
#include "settings.h"

namespace motion {

volatile Stats g_stats;

namespace {
uint8_t g_level = 0xFF;  // nothing applied yet
}

bool applyGain(int32_t temperatureC) {
    int level = settings::g.pirSensitivity;
    const uint8_t comp = settings::g.pirTempComp;
    // Near body temperature a person barely stands out from the background, so
    // step the gain up (stock firmware: 31..37 C). Optionally step down in the
    // cold, where everything warm stands out and false alerts rise.
    if (comp != settings::kTempCompOff && temperatureC >= 31 && temperatureC <= 37) level++;
    if (comp == settings::kTempCompHotCold && temperatureC < 0) level--;
    if (level < settings::kPirLow) level = settings::kPirLow;
    if (level > settings::kPirMax) level = settings::kPirMax;
    if (level == g_level) return false;

    // Measured order, lowest to highest gain: (1,1), (1,0), (0,1), (0,0).
    static const uint8_t kPins[4][2] = {{1, 1}, {1, 0}, {0, 1}, {0, 0}};
    pinMode(PIN_PIR_GAIN_A, OUTPUT);
    pinMode(PIN_PIR_GAIN_B, OUTPUT);
    digitalWrite(PIN_PIR_GAIN_A, kPins[level][0] ? HIGH : LOW);
    digitalWrite(PIN_PIR_GAIN_B, kPins[level][1] ? HIGH : LOW);
    bool first = g_level == 0xFF;
    g_level = static_cast<uint8_t>(level);
    return !first;
}

uint8_t gainLevel() { return g_level; }

bool confirm() {
    const uint8_t t = settings::g.pirThreshold;
    if (t == 0) return true;
    const int lo = config::kPirCenter - t;
    const int hi = config::kPirCenter + t;

    analogReadResolution(8);  // the stock thresholds are 8-bit values
    // Stock: 3+ of 5 outside the window = motion, exactly 2 = keep watching,
    // fewer = noise. Keep watching for at most 3 rounds.
    for (int round = 0; round < 3; round++) {
        int outside = 0;
        for (int i = 0; i < 5; i++) {
            int v = analogRead(PIN_PIR_ANALOG);
            g_stats.samples[i] = v;
            if (v < lo || v > hi) outside++;
            delay(10);
        }
        if (outside >= 3) {
            g_stats.confirmed = g_stats.confirmed + 1;
            return true;
        }
        if (outside < 2) break;
    }
    g_stats.rejected = g_stats.rejected + 1;
    return false;
}

}  // namespace motion
