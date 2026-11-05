#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "config.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace settings {

Settings g;

// Meshtastic firmware modemPresetToParams() / getModemPresetDisplayName().
static const PresetParams kPresets[kPresetCount] = {
    {"LONG_FAST", "LongFast", 250.0f, 11, 5},
    {"LONG_SLOW", "LongSlow", 125.0f, 12, 8},
    {"LONG_MODERATE", "LongMod", 125.0f, 11, 8},
    {"LONG_TURBO", "LongTurbo", 500.0f, 11, 8},
    {"MEDIUM_SLOW", "MediumSlow", 250.0f, 10, 5},
    {"MEDIUM_FAST", "MediumFast", 250.0f, 9, 5},
    {"SHORT_SLOW", "ShortSlow", 250.0f, 8, 5},
    {"SHORT_FAST", "ShortFast", 250.0f, 7, 5},
    {"SHORT_TURBO", "ShortTurbo", 500.0f, 7, 5},
};

const PresetParams& presetParams(ModemPreset p) {
    return kPresets[p < kPresetCount ? p : kLongFast];
}

// US region (Meshtastic RDEF(US, 902.0f, 928.0f, ...), no channel spacing).
static constexpr float kUsFreqStart = 902.0f;
static constexpr float kUsFreqEnd = 928.0f;

// Defaults for the fields added in settings version 2 (also used when
// migrating a version 1 record).
static void applyV2Defaults() {
    g.pirSensitivity = kPirHigh;  // YoLink's default
    g.pirTempComp = kTempCompHot;
    g.envTelemetryEnabled = 1;
    g.deviceTelemetryEnabled = 1;
    g.envUpdateInterval = 3 * 60 * 60;      // ~8 temperature reports a day
    g.deviceUpdateInterval = 12 * 60 * 60;  // battery changes slowly: 2 a day
}

void loadDefaults() {
    memset(&g, 0, sizeof(g));
    g.modemPreset = kLongFast;
    g.txPower = config::kTxPowerDbm;
    g.hopLimit = config::kHopLimit;
#ifdef CHANNEL_NAME
    static const uint8_t key[] = CHANNEL_KEY;
    static_assert(sizeof(key) == 16 || sizeof(key) == 32, "channel key must be 16 or 32 bytes");
    strncpy(g.channelName, CHANNEL_NAME, sizeof(g.channelName) - 1);
    memcpy(g.psk, key, sizeof(key));
    g.pskLen = sizeof(key);
#endif
    g.nodeInfoBroadcastSecs = 3 * 60 * 60;  // Meshtastic default
    g.detectionEnabled = 1;
    g.minimumBroadcastSecs = 15;
    g.stateBroadcastSecs = 0;
    g.sendBell = 1;
    strncpy(g.detectionName, "Motion", sizeof(g.detectionName) - 1);
    g.pirThreshold = config::kPirThresholdDefault;
    applyV2Defaults();
}

float frequencyMHz() {
    if (g.overrideFrequency > 0) return g.overrideFrequency;
    const PresetParams& p = presetParams(static_cast<ModemPreset>(g.modemPreset));
    uint32_t numChannels = static_cast<uint32_t>((kUsFreqEnd - kUsFreqStart) / (p.bandwidthKHz / 1000));
    // Meshtastic hashes the primary channel's name, which is the preset's display
    // name when left unnamed. The sensor assumes that (the usual setup: default
    // primary, private secondary channel); otherwise set lora.channel_num.
    uint32_t slot;
    if (g.channelNum) {
        slot = (g.channelNum - 1) % numChannels;
    } else {
        uint32_t h = 5381;  // djb2, as in Meshtastic's RadioInterface hash()
        for (const char* c = p.displayName; *c; c++) h = ((h << 5) + h) + static_cast<uint8_t>(*c);
        slot = h % numChannels;
    }
    return kUsFreqStart + p.bandwidthKHz / 2000 + slot * (p.bandwidthKHz / 1000);
}

int8_t effectiveTxPower() {
    if (g.txPower <= 0 || g.txPower > RADIO_MAX_POWER_DBM) return RADIO_MAX_POWER_DBM;
    return g.txPower < 2 ? 2 : g.txPower;  // PA_BOOST range is 2..20 dBm
}

// ---- Persistence ---------------------------------------------------------

namespace {

constexpr uint32_t kMagic = 0x594C4D31;  // "YLM1"
constexpr uint32_t kVersion = 2;

// Record layout: magic, version, size (of the Settings bytes), the Settings
// bytes, then a CRC32 of those bytes. Older versions have a smaller Settings;
// they are loaded and their missing fields get defaults.
struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
};
static_assert(sizeof(Settings) % 4 == 0, "word-aligned record");

uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1));
    }
    return ~c;
}

#if defined(ARDUINO)
// Data EEPROM bank 1 start. The stock YoLink firmware kept its data higher up
// (0x08080C30 onward); the full original EEPROM is backed up in ../dumps.
constexpr uint32_t kEepromAddr = 0x08080000;
#endif

}  // namespace

bool load() {
#if defined(ARDUINO)
    loadDefaults();
    const uint8_t* base = reinterpret_cast<const uint8_t*>(kEepromAddr);
    Header h;
    memcpy(&h, base, sizeof(h));
    if (h.magic != kMagic || h.version < 1 || h.version > kVersion || h.size > sizeof(Settings) ||
        h.size % 4 != 0)
        return false;
    uint32_t crc;
    memcpy(&crc, base + sizeof(h) + h.size, sizeof(crc));
    if (crc != crc32(base + sizeof(h), h.size)) return false;
    memcpy(&g, base + sizeof(h), h.size);
    if (h.version < 2) applyV2Defaults();
    return true;
#else
    loadDefaults();
    return false;
#endif
}

bool save() {
#if defined(ARDUINO)
    uint32_t words[(sizeof(Header) + sizeof(Settings) + 4) / 4];
    uint8_t* rec = reinterpret_cast<uint8_t*>(words);
    Header h{kMagic, kVersion, sizeof(Settings)};
    uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(&g), sizeof(g));
    memcpy(rec, &h, sizeof(h));
    memcpy(rec + sizeof(h), &g, sizeof(g));
    memcpy(rec + sizeof(h) + sizeof(g), &crc, sizeof(crc));
    HAL_FLASHEx_DATAEEPROM_Unlock();
    bool ok = true;
    for (size_t i = 0; i < sizeof(words) / 4 && ok; i++) {
        uint32_t addr = kEepromAddr + 4 * i;
        if (*reinterpret_cast<volatile uint32_t*>(addr) == words[i]) continue;  // spare the cells
        ok = HAL_FLASHEx_DATAEEPROM_Program(FLASH_TYPEPROGRAMDATA_WORD, addr, words[i]) == HAL_OK;
    }
    HAL_FLASHEx_DATAEEPROM_Lock();
    return ok && memcmp(reinterpret_cast<const void*>(kEepromAddr), words, sizeof(words)) == 0;
#else
    return false;
#endif
}

// ---- Text interface ------------------------------------------------------

namespace {

enum class Kind { Bool, U8, U16, U32, I8, Float, Str, Preset, Enum };

struct Field {
    const char* name;
    Kind kind;
    size_t offset;
    size_t size;      // for strings: buffer size
    long minVal, maxVal;
    const char* const* names;  // Kind::Enum: value names, indexed by value
};

#define F(name, kind, member, lo, hi) \
    {name, Kind::kind, offsetof(Settings, member), sizeof(Settings::member), lo, hi, nullptr}
#define E(name, member, names) \
    {name, Kind::Enum, offsetof(Settings, member), sizeof(Settings::member), 0, \
     sizeof(names) / sizeof(names[0]) - 1, names}

const char* const kSensitivityNames[] = {"low", "medium", "high", "max"};
const char* const kTempCompNames[] = {"off", "hot", "hot_cold"};

const Field kFields[] = {
    F("lora.region", Str, channelName, 0, 0),  // special-cased: US only
    F("lora.modem_preset", Preset, modemPreset, 0, kPresetCount - 1),
    F("lora.channel_num", U16, channelNum, 0, 208),
    F("lora.override_frequency", Float, overrideFrequency, 0, 0),
    F("lora.tx_power", I8, txPower, 0, 30),
    F("lora.hop_limit", U8, hopLimit, 0, 7),
    F("device.led_heartbeat_disabled", Bool, ledHeartbeatDisabled, 0, 1),
    F("device.node_info_broadcast_secs", U32, nodeInfoBroadcastSecs, 3600, 0x7FFFFFFF),
    F("detection_sensor.enabled", Bool, detectionEnabled, 0, 1),
    F("detection_sensor.minimum_broadcast_secs", U32, minimumBroadcastSecs, 0, 86400),
    F("detection_sensor.state_broadcast_secs", U32, stateBroadcastSecs, 0, 0x7FFFFFFF),
    F("detection_sensor.send_bell", Bool, sendBell, 0, 1),
    F("detection_sensor.name", Str, detectionName, 0, 0),
    F("telemetry.environment_measurement_enabled", Bool, envTelemetryEnabled, 0, 1),
    F("telemetry.environment_update_interval", U32, envUpdateInterval, 60, 0x7FFFFFFF),
    F("telemetry.device_telemetry_enabled", Bool, deviceTelemetryEnabled, 0, 1),
    F("telemetry.device_update_interval", U32, deviceUpdateInterval, 60, 0x7FFFFFFF),
    E("yolink.pir_sensitivity", pirSensitivity, kSensitivityNames),
    E("yolink.pir_temp_comp", pirTempComp, kTempCompNames),
    F("yolink.pir_threshold", U8, pirThreshold, 0, 120),
};
#undef F
#undef E

const Field* find(const char* name) {
    for (const Field& f : kFields)
        if (strcmp(f.name, name) == 0) return &f;
    return nullptr;
}

bool parseBool(const char* v, bool* out) {
    if (!strcmp(v, "true") || !strcmp(v, "1") || !strcmp(v, "on")) *out = true;
    else if (!strcmp(v, "false") || !strcmp(v, "0") || !strcmp(v, "off")) *out = false;
    else return false;
    return true;
}

bool parseLong(const char* v, long* out) {
    char* end;
    *out = strtol(v, &end, 0);
    return *v && !*end;
}

}  // namespace

const char* nameAt(size_t i) { return i < sizeof(kFields) / sizeof(kFields[0]) ? kFields[i].name : nullptr; }

const char* set(const char* name, const char* value) {
    const Field* f = find(name);
    if (!f) return "unknown setting (type 'show' for the list)";
    uint8_t* p = reinterpret_cast<uint8_t*>(&g) + f->offset;

    if (!strcmp(name, "lora.region")) {
        return strcmp(value, "US") == 0 ? nullptr : "only US is supported so far";
    }
    switch (f->kind) {
        case Kind::Bool: {
            bool b;
            if (!parseBool(value, &b)) return "expected true or false";
            *p = b;
            return nullptr;
        }
        case Kind::Enum:
            for (long i = 0; i <= f->maxVal; i++) {
                if (!strcmp(value, f->names[i])) {
                    *p = static_cast<uint8_t>(i);
                    return nullptr;
                }
            }
            return "unknown value (the README lists the choices)";
        case Kind::Preset:
            for (uint8_t i = 0; i < kPresetCount; i++) {
                if (!strcmp(value, kPresets[i].enumName)) {
                    *p = i;
                    return nullptr;
                }
            }
            return "unknown preset (e.g. LONG_FAST, MEDIUM_FAST, SHORT_FAST)";
        case Kind::Float: {
            char* end;
            float v = strtof(value, &end);
            if (!*value || *end) return "expected a number";
            if (v != 0 && (v < 902.0f || v > 928.0f)) return "must be 0 (off) or 902..928 MHz";
            memcpy(p, &v, sizeof(v));
            return nullptr;
        }
        case Kind::Str: {
            size_t n = strlen(value);
            if (n >= f->size) return "too long";
            memset(p, 0, f->size);
            memcpy(p, value, n);
            return nullptr;
        }
        default: {
            long v;
            if (!parseLong(value, &v)) return "expected a whole number";
            if (v < f->minVal || v > f->maxVal) return "out of range";
            if (f->kind == Kind::U8) *p = static_cast<uint8_t>(v);
            else if (f->kind == Kind::I8) *reinterpret_cast<int8_t*>(p) = static_cast<int8_t>(v);
            else if (f->kind == Kind::U16) { uint16_t x = v; memcpy(p, &x, 2); }
            else { uint32_t x = v; memcpy(p, &x, 4); }
            return nullptr;
        }
    }
}

bool get(const char* name, char* out, size_t outLen) {
    const Field* f = find(name);
    if (!f) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&g) + f->offset;
    if (!strcmp(name, "lora.region")) {
        snprintf(out, outLen, "US");
        return true;
    }
    switch (f->kind) {
        case Kind::Bool: snprintf(out, outLen, "%s", *p ? "true" : "false"); break;
        case Kind::Preset: snprintf(out, outLen, "%s", kPresets[*p < kPresetCount ? *p : 0].enumName); break;
        case Kind::Enum: snprintf(out, outLen, "%s", f->names[*p <= f->maxVal ? *p : 0]); break;
        case Kind::Float: {
            float v;
            memcpy(&v, p, sizeof(v));
            // printf %f isn't linked in by default on newlib-nano; format by hand.
            long milli = static_cast<long>(v * 1000 + 0.5f);
            snprintf(out, outLen, "%ld.%03ld", milli / 1000, milli % 1000);
            break;
        }
        case Kind::Str: snprintf(out, outLen, "%s", reinterpret_cast<const char*>(p)); break;
        case Kind::U8: snprintf(out, outLen, "%u", *p); break;
        case Kind::I8: snprintf(out, outLen, "%d", *reinterpret_cast<const int8_t*>(p)); break;
        case Kind::U16: { uint16_t x; memcpy(&x, p, 2); snprintf(out, outLen, "%u", x); break; }
        default: { uint32_t x; memcpy(&x, p, 4); snprintf(out, outLen, "%lu", static_cast<unsigned long>(x)); break; }
    }
    return true;
}

}  // namespace settings
