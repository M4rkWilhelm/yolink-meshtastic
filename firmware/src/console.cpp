#include "console.h"

#include <Arduino.h>
#include <string.h>

#include "board.h"
#include "config.h"
#include "meshtastic_packet.h"
#include "radio.h"
#include "settings.h"

namespace console {
namespace {

Uart port(digitalPinToPinName(PIN_CONSOLE_RX), digitalPinToPinName(PIN_CONSOLE_TX));

settings::Settings g_saved;  // what's in EEPROM, to detect unsaved changes

bool unsaved() { return memcmp(&g_saved, &settings::g, sizeof(g_saved)) != 0; }

// ---- Line input ----------------------------------------------------------

bool readLine(char* buf, size_t len, uint32_t* lastActivity) {
    static size_t n = 0;
    static uint32_t lastBlink = 0;
    while (true) {
        uint32_t now = millis();
        // Green LED heartbeat shows setup mode is active.
        if (now - lastBlink >= 1000) {
            lastBlink = now;
            digitalWrite(PIN_LED_GREEN, LED_ON);
            delay(30);
            digitalWrite(PIN_LED_GREEN, LED_OFF);
        }
        if (now - *lastActivity > config::kSetupIdleTimeoutMs) return false;
        if (!port.available()) continue;
        *lastActivity = now;
        int c = port.read();
        if (c == '\r' || c == '\n') {
            if (n == 0 && c == '\n') continue;  // swallow the LF of a CRLF
            port.print("\r\n");
            buf[n] = '\0';
            n = 0;
            return true;
        }
        if ((c == 8 || c == 127) && n > 0) {  // backspace / delete
            n--;
            port.print("\b \b");
        } else if (c >= 32 && c < 127 && n < len - 1) {
            buf[n++] = static_cast<char>(c);
            port.write(static_cast<uint8_t>(c));
        }
    }
}

// Split into whitespace-separated words; "double quotes" group words.
int tokenize(char* line, char** argv, int maxArgs) {
    int argc = 0;
    char* p = line;
    while (*p && argc < maxArgs) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"') p++;
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
        }
        if (*p) *p++ = '\0';
    }
    return argc;
}

// ---- Key parsing ---------------------------------------------------------

int b64value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

// Returns decoded length, or -1 on bad input.
int decodeBase64(const char* in, uint8_t* out, size_t cap) {
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    for (; *in && *in != '='; in++) {
        int v = b64value(*in);
        if (v < 0) return -1;
        acc = acc << 6 | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= cap) return -1;
            out[n++] = static_cast<uint8_t>(acc >> bits);
        }
    }
    return static_cast<int>(n);
}

int decodeHex(const char* in, uint8_t* out, size_t cap) {
    size_t len = strlen(in);
    if (len % 2 || len / 2 > cap) return -1;
    for (size_t i = 0; i < len / 2; i++) {
        char pair[3] = {in[2 * i], in[2 * i + 1], 0};
        char* end;
        out[i] = static_cast<uint8_t>(strtoul(pair, &end, 16));
        if (*end) return -1;
    }
    return static_cast<int>(len / 2);
}

void printBase64(const uint8_t* p, size_t n) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        port.write(kAlphabet[v >> 18 & 63]);
        port.write(kAlphabet[v >> 12 & 63]);
        port.write(i + 1 < n ? kAlphabet[v >> 6 & 63] : '=');
        port.write(i + 2 < n ? kAlphabet[v & 63] : '=');
    }
}

// ---- Commands --------------------------------------------------------------

void cmdHelp() {
    port.print(
        "Settings use the same names as `meshtastic --set` where Meshtastic has them.\r\n"
        "  show                       list all settings\r\n"
        "  get <name>                 show one setting\r\n"
        "  set <name> <value>         change a setting (e.g. set lora.hop_limit 3)\r\n"
        "  ch-set name <name>         channel the sensor sends on (max 11 chars)\r\n"
        "  ch-set psk <key>           base64:<key> | 0x<hex> | random | none\r\n"
        "  set-owner \"<long name>\"    node long name (default: YoLink Motion <id>)\r\n"
        "  set-owner-short <name>     node short name, max 4 chars\r\n"
        "  test                       send one test message with the current settings\r\n"
        "  save                       store settings (they apply after exit)\r\n"
        "  factory-reset              restore build defaults and save\r\n"
        "  exit                       leave setup mode and restart\r\n");
}

void cmdShow() {
    char val[48];
    for (size_t i = 0; const char* name = settings::nameAt(i); i++) {
        settings::get(name, val, sizeof(val));
        port.print("  ");
        port.print(name);
        for (size_t k = strlen(name); k < 42; k++) port.write(' ');
        port.print(val);
        port.print("\r\n");
    }
    const settings::Settings& s = settings::g;
    port.print("  channel.name                              ");
    port.print(s.channelName[0] ? s.channelName : "(none)");
    port.print("\r\n  channel.psk                               ");
    if (s.pskLen) {
        port.print("base64:");
        printBase64(s.psk, s.pskLen);
    } else {
        port.print("(none: the sensor will not transmit)");
    }
    char longName[40], shortName[5];
    radio::ownerNames(longName, sizeof(longName), shortName, sizeof(shortName));
    port.print("\r\n  owner                                     ");
    port.print(longName);
    port.print(" (");
    port.print(shortName);
    port.print(")\r\n");

    // Derived values, to compare against a node's LoRa settings.
    float f = settings::frequencyMHz();
    long khz = static_cast<long>(f * 1000 + 0.5f);
    snprintf(val, sizeof(val), "%ld.%03ld MHz, %d dBm", khz / 1000, khz % 1000,
             settings::effectiveTxPower());
    port.print("  -> transmits on                           ");
    port.print(val);
    port.print("\r\n");
    if (unsaved()) port.print("  (unsaved changes: type 'save')\r\n");
}

void cmdChSet(int argc, char** argv) {
    settings::Settings& s = settings::g;
    if (argc != 3) {
        port.print("usage: ch-set name <name> | ch-set psk <key>\r\n");
        return;
    }
    if (!strcmp(argv[1], "name")) {
        if (strlen(argv[2]) >= sizeof(s.channelName)) {
            port.print("error: channel names are at most 11 characters (Meshtastic limit)\r\n");
            return;
        }
        memset(s.channelName, 0, sizeof(s.channelName));
        strcpy(s.channelName, argv[2]);
    } else if (!strcmp(argv[1], "psk")) {
        const char* v = argv[2];
        uint8_t key[33];
        int n;
        if (!strcmp(v, "none")) {
            n = 0;
        } else if (!strcmp(v, "random")) {
            n = 32;
            if (!radio::randomBytes(key, n)) {
                port.print("error: radio not available for random numbers\r\n");
                return;
            }
        } else if (!strcmp(v, "default") || !strncmp(v, "simple", 6) || !strcmp(v, "base64:AQ==")) {
            port.print("error: the public default keys aren't allowed; this sensor only sends on\r\n"
                       "       private channels, to keep alerts off the shared default channel\r\n");
            return;
        } else if (!strncmp(v, "base64:", 7)) {
            n = decodeBase64(v + 7, key, sizeof(key));
        } else if (!strncmp(v, "0x", 2)) {
            n = decodeHex(v + 2, key, sizeof(key));
        } else {
            port.print("error: use base64:<key>, 0x<hex>, random or none\r\n");
            return;
        }
        if (n != 0 && n != 16 && n != 32) {
            port.print("error: key must be 16 or 32 bytes (AES-128 or AES-256)\r\n");
            return;
        }
        if (n == 16 && !memcmp(key, mesh::kDefaultPsk, 16)) {
            port.print("error: that's the public default key; use a private one\r\n");
            return;
        }
        memset(s.psk, 0, sizeof(s.psk));
        memcpy(s.psk, key, n);
        s.pskLen = n;
        if (n) {
            port.print("psk set: base64:");
            printBase64(s.psk, s.pskLen);
            port.print("\r\n  (add a channel with this name and key to your node)\r\n");
        }
    } else {
        port.print("usage: ch-set name <name> | ch-set psk <key>\r\n");
        return;
    }
    port.print("ok\r\n");
}

void cmdOwner(int argc, char** argv, bool shortName) {
    settings::Settings& s = settings::g;
    char* dst = shortName ? s.shortName : s.longName;
    size_t cap = shortName ? sizeof(s.shortName) : sizeof(s.longName);
    if (argc != 2) {
        port.print(shortName ? "usage: set-owner-short <name>\r\n"
                             : "usage: set-owner \"<long name>\"\r\n");
        return;
    }
    if (strlen(argv[1]) >= cap) {
        port.print(shortName ? "error: at most 4 characters\r\n" : "error: at most 39 characters\r\n");
        return;
    }
    memset(dst, 0, cap);
    strcpy(dst, argv[1]);
    port.print("ok\r\n");
}

void cmdTest() {
    int state = radio::init();
    if (state != 0) {
        port.print("radio init failed: ");
        port.print(state);
        port.print("\r\n");
        return;
    }
    char text[64];
    snprintf(text, sizeof(text), "%s test", settings::g.detectionName);
    state = radio::sendText(mesh::kDetectionSensorApp, text, false);
    if (state == 0) {
        port.print("sent \"");
        port.print(text);
        port.print("\"\r\n");
    } else if (settings::g.pskLen == 0) {
        port.print("not sent: no channel key set (ch-set psk ...)\r\n");
    } else {
        port.print("send failed: ");
        port.print(state);
        port.print("\r\n");
    }
}

[[noreturn]] void reboot() {
    port.print("restarting...\r\n");
    port.flush();
    delay(20);
    NVIC_SystemReset();
    while (true) {}
}

}  // namespace

void run() {
    port.begin(config::kConsoleBaud);
    g_saved = settings::g;
    radio::init();  // for `test` and `ch-set psk random`

    char banner[80];
    snprintf(banner, sizeof(banner), "\r\nYoLink Motion !%08lx setup mode. Type 'help'.\r\n",
             static_cast<unsigned long>(radio::nodeNum()));
    port.print(banner);
    if (settings::g.pskLen == 0) port.print("note: no channel set yet, so the sensor won't transmit.\r\n");

    char line[160];
    uint32_t lastActivity = millis();
    bool warnedUnsaved = false;
    while (true) {
        port.print("> ");
        if (!readLine(line, sizeof(line), &lastActivity)) {
            port.print("\r\nidle timeout, leaving setup mode");
            port.print(unsaved() ? " (unsaved changes discarded)\r\n" : "\r\n");
            reboot();
        }
        char* argv[8];
        int argc = tokenize(line, argv, 8);
        if (argc == 0) continue;
        const char* cmd = argv[0];
        if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
            cmdHelp();
        } else if (!strcmp(cmd, "show")) {
            cmdShow();
        } else if (!strcmp(cmd, "get") && argc == 2) {
            char val[48];
            if (settings::get(argv[1], val, sizeof(val))) {
                port.print(val);
                port.print("\r\n");
            } else {
                port.print("unknown setting\r\n");
            }
        } else if (!strcmp(cmd, "set") && argc == 3) {
            const char* err = settings::set(argv[1], argv[2]);
            port.print(err ? "error: " : "ok\r\n");
            if (err) {
                port.print(err);
                port.print("\r\n");
            }
        } else if (!strcmp(cmd, "ch-set")) {
            cmdChSet(argc, argv);
        } else if (!strcmp(cmd, "set-owner")) {
            cmdOwner(argc, argv, false);
        } else if (!strcmp(cmd, "set-owner-short")) {
            cmdOwner(argc, argv, true);
        } else if (!strcmp(cmd, "test")) {
            cmdTest();
        } else if (!strcmp(cmd, "save")) {
            if (settings::save()) {
                g_saved = settings::g;
                port.print("saved\r\n");
            } else {
                port.print("error: EEPROM write failed\r\n");
            }
        } else if (!strcmp(cmd, "factory-reset")) {
            settings::loadDefaults();
            port.print(settings::save() ? "defaults restored and saved\r\n"
                                        : "error: EEPROM write failed\r\n");
            g_saved = settings::g;
        } else if (!strcmp(cmd, "exit") || !strcmp(cmd, "reboot")) {
            if (unsaved() && !warnedUnsaved) {
                port.print("unsaved changes: type 'save' first, or 'exit' again to discard\r\n");
                warnedUnsaved = true;
                continue;
            }
            reboot();
        } else {
            port.print("unknown command, type 'help'\r\n");
        }
    }
}

}  // namespace console
