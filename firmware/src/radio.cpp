#include "radio.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "meshtastic_packet.h"
#include "settings.h"

namespace radio {
namespace {

// Reset the SX1276 the way the stock firmware does (SX1276Reset @0x0800BAEC):
// pull NRESET low for 1 ms, then release it (input, not driven high) and wait
// (stock: 6 ms; 10 here for margin). RadioLib's default drives the line high.
class YlSx1276 : public SX1276 {
public:
    using SX1276::SX1276;
    void reset() override {
        pinMode(PIN_RADIO_RESET, OUTPUT);
        digitalWrite(PIN_RADIO_RESET, LOW);
        delay(1);
        pinMode(PIN_RADIO_RESET, INPUT);
        delay(10);
    }
};

SPIClass radioSpi(PIN_RADIO_MOSI, PIN_RADIO_MISO, PIN_RADIO_SCK);
YlSx1276 sx = new Module(PIN_RADIO_NSS, PIN_RADIO_DIO0, PIN_RADIO_RESET, PIN_RADIO_DIO1,
                         radioSpi);

const uint32_t kRfSwitchPins[Module::RFSWITCH_MAX_PINS] = {
    PIN_RF_SWITCH, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
// Idle (standby and sleep) uses the TX level (PA1 low): holding PA1 high costs
// about 35 uA through the switch's control input (measured: sleep current
// 130 -> 95 uA), and this firmware never receives.
const Module::RfSwitchMode_t kRfSwitchTable[] = {
    {Module::MODE_IDLE, {RF_SWITCH_TX}},
    {Module::MODE_RX, {RF_SWITCH_RX}},
    {Module::MODE_TX, {RF_SWITCH_TX}},
    END_OF_MODE_TABLE,
};

int g_initState = RADIOLIB_ERR_UNKNOWN;
int g_lastTx = RADIOLIB_ERR_NONE;
uint32_t g_sent;
uint32_t g_nextId;
bool g_spiStarted;

}  // namespace

int sendData(const uint8_t* data, size_t len) {
    const settings::Settings& s = settings::g;
    if (s.pskLen == 0) return RADIOLIB_ERR_INVALID_ENCODING;  // no channel: stay silent
    if (g_initState != RADIOLIB_ERR_NONE) return g_initState;
    if (len == 0) return RADIOLIB_ERR_PACKET_TOO_LONG;

    mesh::Header hdr{mesh::kBroadcast, nodeNum(), g_nextId++, s.hopLimit,
                     mesh::channelHash(s.channelName, s.psk, s.pskLen)};
    uint8_t pkt[mesh::kMaxPacketLen];
    size_t n = mesh::buildPacket(pkt, sizeof(pkt), hdr, s.psk, s.pskLen, data, len);
    if (n == 0) return RADIOLIB_ERR_PACKET_TOO_LONG;

    int state = sx.transmit(pkt, n);
    sx.sleep();
    g_lastTx = state;
    if (state == RADIOLIB_ERR_NONE) g_sent++;
    return state;
}

int init() {
    const settings::Settings& s = settings::g;
    const settings::PresetParams& p =
        settings::presetParams(static_cast<settings::ModemPreset>(s.modemPreset));
    if (!g_spiStarted) {
        radioSpi.begin();
        sx.setRfSwitchTable(kRfSwitchPins, kRfSwitchTable);
        g_spiStarted = true;
    }
    int state = sx.begin(settings::frequencyMHz(), p.bandwidthKHz, p.spreadingFactor,
                         p.codingRateDenom, 0x2B /* Meshtastic sync word */,
                         settings::effectiveTxPower(), 16 /* Meshtastic preamble */);
    if (state == RADIOLIB_ERR_NONE) state = sx.setCurrentLimit(140);  // needed for +20 dBm
    if (state == RADIOLIB_ERR_NONE) state = sx.setCRC(true);
    if (state == RADIOLIB_ERR_NONE) {
        // Random starting packet id, so a reboot doesn't repeat recent ids.
        g_nextId = static_cast<uint32_t>(sx.randomByte()) << 24 |
                   static_cast<uint32_t>(sx.randomByte()) << 16 |
                   static_cast<uint32_t>(sx.randomByte()) << 8 | sx.randomByte();
        sx.sleep();
    }
    g_initState = state;
    return state;
}

bool ready() { return g_initState == RADIOLIB_ERR_NONE; }

// Node number from the chip's 96-bit unique ID (same words the stock firmware XORs).
uint32_t nodeNum() {
    uint32_t n = *(volatile uint32_t*)0x1FF80050 ^ *(volatile uint32_t*)0x1FF80054 ^
                 *(volatile uint32_t*)0x1FF80064;
    if (n <= 3 || n == mesh::kBroadcast) n ^= 0x5A5A5A5A;  // avoid reserved numbers
    return n;
}

void ownerNames(char* longName, size_t longLen, char* shortName, size_t shortLen) {
    const settings::Settings& s = settings::g;
    char hex[5];
    snprintf(hex, sizeof(hex), "%04lx", static_cast<unsigned long>(nodeNum() & 0xFFFF));
    snprintf(shortName, shortLen, "%s", s.shortName[0] ? s.shortName : hex);
    if (s.longName[0]) snprintf(longName, longLen, "%s", s.longName);
    else snprintf(longName, longLen, "YoLink Motion %s", hex);
}

int sendText(uint32_t portnum, const char* text, bool bell) {
    // Data { portnum = 1; payload = 2 }, optionally with a trailing ASCII bell
    // (as Meshtastic's DetectionSensorModule does).
    char body[200];
    snprintf(body, sizeof(body) - 1, "%s", text);
    if (bell) strcat(body, "\a");
    uint8_t data[mesh::kMaxPacketLen - mesh::kHeaderLen];
    return sendData(data, mesh::encodeData(data, sizeof(data), portnum,
                                           reinterpret_cast<const uint8_t*>(body), strlen(body)));
}

int sendNodeInfo() {
    char longName[40], shortName[5];
    ownerNames(longName, sizeof(longName), shortName, sizeof(shortName));
    uint8_t data[mesh::kMaxPacketLen - mesh::kHeaderLen];
    return sendData(data, mesh::encodeNodeInfoData(data, sizeof(data), nodeNum(), longName,
                                                   shortName, mesh::kHwModelPrivate,
                                                   mesh::kRoleSensor));
}

bool randomBytes(uint8_t* out, size_t n) {
    if (!ready()) return false;
    for (size_t i = 0; i < n; i++) out[i] = sx.randomByte();
    sx.sleep();
    return true;
}

int lastTxState() { return g_lastTx; }
uint32_t packetsSent() { return g_sent; }

}  // namespace radio
