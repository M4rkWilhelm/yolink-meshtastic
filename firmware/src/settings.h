// Persistent settings, stored in the STM32L0 data EEPROM.
//
// Names follow the Meshtastic CLI (`meshtastic --set <name> <value>`) wherever
// Meshtastic has an equivalent setting. Hardware-specific settings without a
// Meshtastic equivalent are kept under the yolink.* prefix.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace settings {

// Meshtastic LoRa modem presets (names as in Config.LoRaConfig.ModemPreset).
enum ModemPreset : uint8_t {
    kLongFast,
    kLongSlow,
    kLongModerate,
    kLongTurbo,
    kMediumSlow,
    kMediumFast,
    kShortSlow,
    kShortFast,
    kShortTurbo,
    kPresetCount,
};

struct PresetParams {
    const char* enumName;     // e.g. "LONG_FAST" (CLI value)
    const char* displayName;  // e.g. "LongFast" (default channel name, used for the frequency slot)
    float bandwidthKHz;
    uint8_t spreadingFactor;
    uint8_t codingRateDenom;
};
const PresetParams& presetParams(ModemPreset p);

// PIR amplifier gain steps (PC2/PC3), lowest to highest; see HARDWARE.md.
enum PirSensitivity : uint8_t { kPirLow, kPirMedium, kPirHigh, kPirMax };
enum PirTempComp : uint8_t {
    kTempCompOff,
    kTempCompHot,      // +1 step at 31..37 C, as the stock firmware does
    kTempCompHotCold,  // also -1 step below 0 C
};

struct Settings {
    // lora.*
    uint8_t modemPreset;       // lora.modem_preset
    uint16_t channelNum;       // lora.channel_num (0 = slot from the primary channel name)
    float overrideFrequency;   // lora.override_frequency, MHz (0 = off)
    int8_t txPower;            // lora.tx_power, dBm (0 = hardware max, 20)
    uint8_t hopLimit;          // lora.hop_limit
    // Channel the sensor sends on (meshtastic --ch-set name / psk).
    char channelName[12];
    uint8_t pskLen;            // 0 = no channel configured: the sensor won't transmit
    uint8_t psk[32];
    // Owner (meshtastic --set-owner / --set-owner-short). Empty = derived from node id.
    char longName[40];
    char shortName[5];
    // device.*
    uint8_t ledHeartbeatDisabled;     // device.led_heartbeat_disabled
    uint32_t nodeInfoBroadcastSecs;   // device.node_info_broadcast_secs
    // detection_sensor.*
    uint8_t detectionEnabled;         // detection_sensor.enabled
    uint32_t minimumBroadcastSecs;    // detection_sensor.minimum_broadcast_secs
    uint32_t stateBroadcastSecs;      // detection_sensor.state_broadcast_secs (0 = off)
    uint8_t sendBell;                 // detection_sensor.send_bell
    char detectionName[21];           // detection_sensor.name
    // yolink.* (not Meshtastic settings)
    uint8_t pirThreshold;             // yolink.pir_threshold: PC1 window half-width, 0 = no confirmation
    // --- added in settings version 2 ---
    uint8_t pirSensitivity;           // yolink.pir_sensitivity (PirSensitivity)
    uint8_t pirTempComp;              // yolink.pir_temp_comp (PirTempComp)
    uint8_t envTelemetryEnabled;      // telemetry.environment_measurement_enabled
    uint8_t deviceTelemetryEnabled;   // telemetry.device_telemetry_enabled
    uint32_t envUpdateInterval;       // telemetry.environment_update_interval (s)
    uint32_t deviceUpdateInterval;    // telemetry.device_update_interval (s)
};

// Live settings. load() fills it from EEPROM, or defaults if nothing valid is stored.
extern Settings g;

void loadDefaults();
bool load();   // true if valid settings were found in EEPROM
bool save();   // true on success (verified)

// Meshtastic frequency rule for this config (US region only for now).
float frequencyMHz();
// Transmit power actually used (applies the "0 = max" rule and hardware range).
int8_t effectiveTxPower();

// Text interface for the console: set/get one setting by its Meshtastic name.
// Returns nullptr on success, or an error message.
const char* set(const char* name, const char* value);
bool get(const char* name, char* out, size_t outLen);
// Iterate setting names for `show` (returns nullptr past the end).
const char* nameAt(size_t i);

}  // namespace settings
