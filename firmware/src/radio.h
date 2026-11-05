// SX1276 + Meshtastic sending, configured from settings::g.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace radio {

// (Re)configure the radio from settings::g. Returns a RadioLib status code.
int init();
bool ready();

uint32_t nodeNum();
void ownerNames(char* longName, size_t longLen, char* shortName, size_t shortLen);

// Each returns a RadioLib status code (RADIOLIB_ERR_NONE on success). They
// refuse to send (and return an error) when no channel is configured.
// Send an already-encoded Data message (see meshtastic_packet.h).
int sendData(const uint8_t* data, size_t len);
int sendText(uint32_t portnum, const char* text, bool bell);
int sendNodeInfo();

// Random bytes from the SX1276's wideband RSSI (needs a successful init()).
bool randomBytes(uint8_t* out, size_t n);

int lastTxState();
uint32_t packetsSent();

}  // namespace radio
