#include "meshtastic_packet.h"

#include <AES.h>
#include <CTR.h>
#include <stdio.h>
#include <string.h>

namespace mesh {

const uint8_t kDefaultPsk[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

namespace {

// Bounded protobuf writer; sets `ok` false instead of overflowing.
struct Writer {
    uint8_t* buf;
    size_t cap;
    size_t len = 0;
    bool ok = true;

    void byte(uint8_t b) {
        if (len < cap) buf[len++] = b;
        else ok = false;
    }
    void varint(uint32_t v) {
        while (v >= 0x80) {
            byte(static_cast<uint8_t>(v | 0x80));
            v >>= 7;
        }
        byte(static_cast<uint8_t>(v));
    }
    void tag(uint32_t field, uint32_t wireType) { varint(field << 3 | wireType); }
    void fieldVarint(uint32_t field, uint32_t v) {
        tag(field, 0);
        varint(v);
    }
    void fieldFixed32(uint32_t field, uint32_t v) {
        tag(field, 5);
        for (int i = 0; i < 4; i++) byte(static_cast<uint8_t>(v >> (8 * i)));
    }
    void fieldFloat(uint32_t field, float f) {
        uint32_t v;
        memcpy(&v, &f, sizeof(v));
        fieldFixed32(field, v);
    }
    void fieldBytes(uint32_t field, const uint8_t* p, size_t n) {
        tag(field, 2);
        varint(static_cast<uint32_t>(n));
        for (size_t i = 0; i < n; i++) byte(p[i]);
    }
    void fieldString(uint32_t field, const char* s) {
        fieldBytes(field, reinterpret_cast<const uint8_t*>(s), strlen(s));
    }
    size_t result() const { return ok ? len : 0; }
};

void putLe32(uint8_t* p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

template <typename Cipher>
void encryptCtr(uint8_t* out, const uint8_t* in, size_t len, const uint8_t* key,
                size_t keyLen, const uint8_t* nonce) {
    CTR<Cipher> ctr;
    ctr.setKey(key, keyLen);
    ctr.setIV(nonce, 16);
    ctr.setCounterSize(4);
    ctr.encrypt(out, in, len);
}

}  // namespace

uint8_t channelHash(const char* name, const uint8_t* key, size_t keyLen) {
    uint8_t h = 0;
    for (const char* c = name; *c; c++) h ^= static_cast<uint8_t>(*c);
    for (size_t i = 0; i < keyLen; i++) h ^= key[i];
    return h;
}

size_t encodeData(uint8_t* out, size_t cap, uint32_t portnum, const uint8_t* payload,
                  size_t payloadLen) {
    Writer w{out, cap};
    w.fieldVarint(1, portnum);                 // Data.portnum
    w.fieldBytes(2, payload, payloadLen);      // Data.payload
    return w.result();
}

size_t encodeTextData(uint8_t* out, size_t cap, const char* text) {
    return encodeData(out, cap, kTextMessageApp, reinterpret_cast<const uint8_t*>(text),
                      strlen(text));
}

// Wrap a sub-message as Telemetry.<variant> and then as Data on TELEMETRY_APP.
// Telemetry.time is left out (0 = unknown): the sensor has no wall clock.
static size_t telemetryData(uint8_t* out, size_t cap, uint32_t variant,
                            const uint8_t* metrics, size_t metricsLen) {
    uint8_t telemetry[48];
    Writer t{telemetry, sizeof(telemetry)};
    t.fieldBytes(variant, metrics, metricsLen);
    if (!t.ok) return 0;
    return encodeData(out, cap, kTelemetryApp, telemetry, t.len);
}

size_t encodeEnvironmentTelemetryData(uint8_t* out, size_t cap, float temperatureC) {
    uint8_t env[8];
    Writer e{env, sizeof(env)};
    e.fieldFloat(1, temperatureC);  // EnvironmentMetrics.temperature
    if (!e.ok) return 0;
    return telemetryData(out, cap, 3, env, e.len);  // Telemetry.environment_metrics
}

size_t encodeDeviceTelemetryData(uint8_t* out, size_t cap, uint32_t batteryLevel,
                                 float voltage, uint32_t uptimeSeconds) {
    uint8_t dev[20];
    Writer d{dev, sizeof(dev)};
    d.fieldVarint(1, batteryLevel);   // DeviceMetrics.battery_level
    d.fieldFloat(2, voltage);         // DeviceMetrics.voltage
    d.fieldVarint(5, uptimeSeconds);  // DeviceMetrics.uptime_seconds
    if (!d.ok) return 0;
    return telemetryData(out, cap, 2, dev, d.len);  // Telemetry.device_metrics
}

size_t encodeNodeInfoData(uint8_t* out, size_t cap, uint32_t nodeNum,
                          const char* longName, const char* shortName,
                          uint32_t hwModel, uint32_t role) {
    // User message first, then wrap it as Data.payload.
    uint8_t user[96];
    char id[10];
    snprintf(id, sizeof(id), "!%08lx", static_cast<unsigned long>(nodeNum));
    Writer u{user, sizeof(user)};
    u.fieldString(1, id);         // User.id
    u.fieldString(2, longName);   // User.long_name
    u.fieldString(3, shortName);  // User.short_name
    u.fieldVarint(5, hwModel);    // User.hw_model
    u.fieldVarint(7, role);       // User.role
    if (!u.ok) return 0;

    Writer w{out, cap};
    w.fieldVarint(1, kNodeInfoApp);
    w.fieldBytes(2, user, u.len);
    return w.result();
}

size_t buildPacket(uint8_t* out, size_t cap, const Header& hdr,
                   const uint8_t* key, size_t keyLen,
                   const uint8_t* data, size_t dataLen) {
    if (cap > kMaxPacketLen) cap = kMaxPacketLen;
    if ((keyLen != 16 && keyLen != 32) || kHeaderLen + dataLen > cap) return 0;

    putLe32(out, hdr.to);
    putLe32(out + 4, hdr.from);
    putLe32(out + 8, hdr.id);
    // flags: hop_limit in bits 0-2, hop_start in bits 5-7; no want_ack / via_mqtt.
    out[12] = (hdr.hopLimit & 0x07) | (hdr.hopLimit & 0x07) << 5;
    out[13] = hdr.channelHash;
    out[14] = 0;                              // next_hop: none
    out[15] = static_cast<uint8_t>(hdr.from);  // relay_node: ourselves

    uint8_t nonce[16] = {};
    putLe32(nonce, hdr.id);  // id as uint64, upper half zero
    putLe32(nonce + 8, hdr.from);

    if (keyLen == 16) encryptCtr<AES128>(out + kHeaderLen, data, dataLen, key, keyLen, nonce);
    else encryptCtr<AES256>(out + kHeaderLen, data, dataLen, key, keyLen, nonce);
    return kHeaderLen + dataLen;
}

}  // namespace mesh
