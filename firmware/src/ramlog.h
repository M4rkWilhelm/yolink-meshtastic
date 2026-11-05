// RAM text log, read over SWD with tools/ramlog.py (the board has no serial
// port wired). The debug build force-includes this header and points
// RadioLib's debug output at g_ramLog.
#pragma once

#ifdef __cplusplus
#include <Print.h>

class RamLog : public Print {
public:
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t n) override;
};

extern RamLog g_ramLog;
#endif
