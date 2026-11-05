// Crash and reset bookkeeping that survives a reset, kept in the RTC backup
// registers (BKP0R..BKP4R).
//
// A CPU fault (HardFault) is recorded (where it happened) and the chip is
// reset at once instead of hanging until the watchdog fires.
#pragma once

#include <stdint.h>

namespace faults {

struct Info {
    uint32_t faultCount;     // CPU faults since power-on
    uint32_t watchdogCount;  // watchdog resets since power-on
    // Most recent fault (all zero if none since power-on):
    uint32_t exception;      // exception number that was active: 3 = HardFault
    uint32_t pc;             // where it happened
    uint32_t lr;             // usually the calling function
    uint32_t xpsr;           // its IPSR part is the exception that was running (0 = none)
};

// Call once at boot, after the RTC is running, with RCC->CSR as read at reset.
// Clears the record on power-on and counts watchdog resets.
void boot(uint32_t resetFlags);

const Info& info();

}  // namespace faults
