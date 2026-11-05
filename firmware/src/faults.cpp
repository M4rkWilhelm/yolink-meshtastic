#include "faults.h"

#include <Arduino.h>

namespace faults {
namespace {

// Backup register layout:
//   BKP0R  kMagic | exception number (low 8 bits)   (magic absent = no fault)
//   BKP1R  stacked PC
//   BKP2R  stacked LR
//   BKP3R  stacked xPSR
//   BKP4R  fault count << 16 | watchdog reset count
constexpr uint32_t kMagic = 0xFA170000;

Info g_info;

void enableBackupWrites() {
    __HAL_RCC_PWR_CLK_ENABLE();
    PWR->CR |= PWR_CR_DBP;
}

}  // namespace

void boot(uint32_t resetFlags) {
    enableBackupWrites();
    if (resetFlags & RCC_CSR_PORRSTF) {
        // Fresh power: whatever is in the backup registers is meaningless.
        RTC->BKP0R = RTC->BKP1R = RTC->BKP2R = RTC->BKP3R = RTC->BKP4R = 0;
    } else if (resetFlags & RCC_CSR_IWDGRSTF) {
        uint32_t c = RTC->BKP4R;
        if ((c & 0xFFFF) != 0xFFFF) RTC->BKP4R = c + 1;
    }
    uint32_t counts = RTC->BKP4R;
    g_info.faultCount = counts >> 16;
    g_info.watchdogCount = counts & 0xFFFF;
    if ((RTC->BKP0R & 0xFFFF0000) == kMagic) {
        g_info.exception = RTC->BKP0R & 0xFF;
        g_info.pc = RTC->BKP1R;
        g_info.lr = RTC->BKP2R;
        g_info.xpsr = RTC->BKP3R;
    }
}

const Info& info() { return g_info; }

}  // namespace faults

// Called from HardFault_Handler with the stack pointer at the fault (where the
// CPU pushed r0-r3, r12, lr, pc, xpsr) and the handler's EXC_RETURN value.
extern "C" __attribute__((used, noreturn)) void faults_capture(uint32_t* frame, uint32_t excReturn) {
    (void)excReturn;
    uint32_t pc = 0, lr = 0, xpsr = 0;
    // Only trust the frame if it lies in RAM (a stack overflow can fault too).
    uintptr_t f = reinterpret_cast<uintptr_t>(frame);
    if (f >= SRAM_BASE && f + 32 <= SRAM_BASE + 20 * 1024) {
        lr = frame[5];
        pc = frame[6];
        xpsr = frame[7];
    }
    faults::enableBackupWrites();
    RTC->BKP0R = faults::kMagic | (__get_IPSR() & 0xFF);
    RTC->BKP1R = pc;
    RTC->BKP2R = lr;
    RTC->BKP3R = xpsr;
    uint32_t c = RTC->BKP4R;
    if ((c >> 16) != 0xFFFF) RTC->BKP4R = c + 0x10000;
    // To inspect a fault live, set a hardware breakpoint on faults_capture. (The
    // firmware can't tell a debugger is attached: on Cortex-M0+ DHCSR isn't
    // readable by software, and a BKPT with no debugger would lock the core.)
    NVIC_SystemReset();
}

// Replaces the startup code's default (an endless loop shared with every
// unused interrupt, which left a hung sensor for the watchdog to find).
extern "C" __attribute__((naked)) void HardFault_Handler() {
    __asm volatile(
        "mov r0, sp\n"
        "mov r1, lr\n"
        "bl faults_capture\n");
}
