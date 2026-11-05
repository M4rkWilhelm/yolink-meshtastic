#include "ramlog.h"

#include <stdint.h>

// Ring buffer; g_ramLogTotal counts every byte ever written, so the reader can
// tell where the newest data ends and whether it wrapped.
extern "C" {
char g_ramLogBuf[4096];
volatile uint32_t g_ramLogTotal;
}

RamLog g_ramLog;

size_t RamLog::write(uint8_t c) {
    g_ramLogBuf[g_ramLogTotal % sizeof(g_ramLogBuf)] = static_cast<char>(c);
    g_ramLogTotal = g_ramLogTotal + 1;
    return 1;
}

size_t RamLog::write(const uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; i++) write(buf[i]);
    return n;
}
