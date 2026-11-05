// Minimal encoder for Meshtastic over-the-air packets (send-only).
//
// On-air layout (all integers little endian):
//   to(4) from(4) id(4) flags(1) channel_hash(1) next_hop(1) relay_node(1)
//   then the protobuf `Data` message, AES-CTR encrypted with the channel key.
// The AES nonce is: packet id as uint64, sender node number as uint32, 4 zero
// bytes; counter is the last 4 bytes (rweather CTR with setCounterSize(4)), the
// same as the Meshtastic firmware's CryptoEngine. Keys are 16 bytes (AES-128)
// or 32 bytes (AES-256).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mesh {

constexpr uint32_t kBroadcast = 0xFFFFFFFF;
constexpr size_t kHeaderLen = 16;
constexpr size_t kMaxPacketLen = 255;

// Default Meshtastic channel key (PSK index 1, "AQ==").
extern const uint8_t kDefaultPsk[16];

// Meshtastic portnums used here.
enum PortNum : uint32_t {
    kTextMessageApp = 1,
    kNodeInfoApp = 4,
    kDetectionSensorApp = 10,  // "Same as Text Message but originating from Detection Sensor Module"
    kTelemetryApp = 67,
};

// User.role / User.hw_model values used here.
constexpr uint32_t kRoleSensor = 6;
constexpr uint32_t kHwModelPrivate = 255;

// XOR of the channel name bytes and the key bytes.
uint8_t channelHash(const char* name, const uint8_t* key, size_t keyLen);

// Serialize a `Data` message. Each returns the encoded length, 0 if it doesn't fit.
size_t encodeData(uint8_t* out, size_t cap, uint32_t portnum, const uint8_t* payload,
                  size_t payloadLen);
size_t encodeTextData(uint8_t* out, size_t cap, const char* text);
size_t encodeNodeInfoData(uint8_t* out, size_t cap, uint32_t nodeNum,
                          const char* longName, const char* shortName,
                          uint32_t hwModel, uint32_t role);
// Telemetry { environment_metrics { temperature } } on TELEMETRY_APP.
size_t encodeEnvironmentTelemetryData(uint8_t* out, size_t cap, float temperatureC);
// Telemetry { device_metrics { battery_level, voltage, uptime_seconds } }.
size_t encodeDeviceTelemetryData(uint8_t* out, size_t cap, uint32_t batteryLevel,
                                 float voltage, uint32_t uptimeSeconds);

struct Header {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t hopLimit;
    uint8_t channelHash;
};

// Header + encrypted Data. Returns total length, 0 if it doesn't fit.
size_t buildPacket(uint8_t* out, size_t cap, const Header& hdr,
                   const uint8_t* key, size_t keyLen,
                   const uint8_t* data, size_t dataLen);

}  // namespace mesh
