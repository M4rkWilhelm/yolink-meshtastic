// Host harness: encode fixed packets with the firmware's encoder and print
// them as hex, one per line, for test/check_packet.py to decode independently.
#include <stdio.h>

#include "../src/meshtastic_packet.h"

// Fixed 32-byte key for the AES-256 private-channel case (bytes 0x00..0x1f).
static uint8_t key256[32];

static void emit(const char* label, const uint8_t* data, size_t dataLen, uint32_t id,
                 const char* channel = "LongFast", const uint8_t* key = mesh::kDefaultPsk,
                 size_t keyLen = 16, uint8_t hopLimit = 3) {
    const uint8_t hash = mesh::channelHash(channel, key, keyLen);
    mesh::Header hdr{mesh::kBroadcast, 0x1234ABCD, id, hopLimit, hash};
    uint8_t pkt[mesh::kMaxPacketLen];
    size_t len = mesh::buildPacket(pkt, sizeof(pkt), hdr, key, keyLen, data, dataLen);
    printf("%s ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", pkt[i]);
    printf("\n");
}

int main() {
    uint8_t data[239];
    emit("text", data, mesh::encodeTextData(data, sizeof(data), "Motion detected"), 0x0000BEEF);
    emit("nodeinfo", data,
         mesh::encodeNodeInfoData(data, sizeof(data), 0x1234ABCD, "YoLink Motion abcd", "abcd",
                                  mesh::kHwModelPrivate, mesh::kRoleSensor),
         0xDEADBEEF);
    // Longer than one AES block, to exercise the CTR counter increment.
    emit("long", data,
         mesh::encodeTextData(data, sizeof(data),
                              "A considerably longer test message spanning several AES blocks!"),
         0x7FFFFFFF);
    // Detection Sensor alert: port 10, "<name> detected" plus a trailing bell.
    for (int i = 0; i < 32; i++) key256[i] = i;
    const char alert[] = "Driveway detected\a";
    emit("detect", data,
         mesh::encodeData(data, sizeof(data), mesh::kDetectionSensorApp,
                          reinterpret_cast<const uint8_t*>(alert), sizeof(alert) - 1),
         0x0A0B0C0D, "YLtest", key256, sizeof(key256), 0);
    // Telemetry: environment (temperature) and device metrics.
    emit("tel_env", data, mesh::encodeEnvironmentTelemetryData(data, sizeof(data), 23.0f),
         0x11111111, "YLtest", key256, sizeof(key256), 0);
    emit("tel_dev", data, mesh::encodeDeviceTelemetryData(data, sizeof(data), 87, 2.87f, 3600),
         0x22222222, "YLtest", key256, sizeof(key256), 0);
    // AES-256 private channel with hop limit 0, as in the bench-test config.
    for (int i = 0; i < 32; i++) key256[i] = i;
    emit("aes256", data,
         mesh::encodeTextData(data, sizeof(data),
                              "Private channel message, longer than one AES block"),
         0x01020304, "YLtest", key256, sizeof(key256), 0);
    return 0;
}
