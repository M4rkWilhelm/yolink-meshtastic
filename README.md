# YoLink Sensor Custom Firmware (Meshtastic-compatible)

Open replacement firmware that turns a **YoLink YS7805** outdoor motion sensor
into a battery-powered Meshtastic detection node: when it sees motion, it
broadcasts `"<name> detected"` on your private Meshtastic channel, like a
Meshtastic node running the Detection Sensor module.

Why switch:

- You don't want to use the YoLink app or hub.
- You want to get alerts via your private Meshtastic channel.
- It sleeps on about a third less power than the stock firmware: about 95 µA
  versus 145 µA, measured on the same sensor (see [HARDWARE.md](HARDWARE.md#power)).

> **Unofficial.** Not affiliated with or endorsed by YoSmart/YoLink or Meshtastic. Flashing replaces the YoLink firmware. The sensor will no longer work with the YoLink app or hub unless you restore your backup. You do this at your own risk.

## Status

Early but working (November 2026).

| Feature | Status |
|---|---|
| Motion detection with stock-style analog confirmation | ✅ tested on hardware |
| Meshtastic Detection Sensor alerts (`DETECTION_SENSOR_APP`), NodeInfo | ✅ sent; packet format checked against an independent decoder |
| Reception by a Meshtastic node | ✅ decoded by a Heltec V3 running Meshtastic 2.7.11 (alerts, test message, NodeInfo) |
| Setup console over serial, settings saved in EEPROM | ✅ tested |
| Stop-mode sleep, wake on motion or button | ✅ tested |
| Temperature and battery telemetry | ✅ decoded by the Heltec V3 |
| PIR sensitivity levels and hot-weather compensation | ✅ gain steps measured; compensation tested with a heat gun |
| Watchdog and crash recovery | ✅ tested (simulated hang and forced fault both recover); 19.5 h stress test with no crashes |
| Battery life | 🟡 sleep current measured (~95 µA); estimate about 2.5 years on alkaline AAs before motion alerts, not yet confirmed by a long run |
| Regions | US (902–928 MHz) only |

See [Known issues](#known-issues) and [Radio regulations](#radio-regulations)
before deploying.

## Contents

- [How it works](#how-it-works)
- [What you need](#what-you-need)
- [Installation](#installation)
- [Configuration](#configuration)
- [Testing](#testing)
- [Updating and restoring](#updating-and-restoring)
- [Troubleshooting](#troubleshooting)
- [Known issues](#known-issues)
- [Project layout](#project-layout)

## How it works

The YS7805 is built around YoSmart's **YL09** module, a rebranded
[AcSiP S76S](https://docs.zephyrproject.org/latest/boards/ronoth/lodev/doc/s76s.html). The S76S is
an STM32L073 microcontroller plus an SX1276 LoRa radio. That just so happens to be the same radio family Meshtastic uses. The microcontroller only has 20 KB of RAM, which is too little for the full Meshtastic firmware, so this custom firmware implements just enough of the protocol to send alerts and telemetry.

| When | What it sends |
|---|---|
| Confirmed motion | `<name> detected` + bell character, at most once per `detection_sensor.minimum_broadcast_secs` |
| Every `detection_sensor.state_broadcast_secs` (if enabled) | `<name> state: 0/1` |
| Boot, button press, every 3 hours | NodeInfo, so it shows up in your node list as "YoLink Motion xxxx" |
| Every 3 h / 12 h | Telemetry: chip temperature / battery level and voltage (standard Meshtastic telemetry, shown in the node's details): about 10 packets a day |
| Button press | also `<name> test` |

These are the same message formats and setting names Meshtastic's own Detection Sensor module uses. The sensor doesn't receive or relay packets.

Motion is confirmed the way YoLink's firmware does it: when the PIR comparator fires, the PIR's analog signal is sampled 5 times, and at least 3 samples must swing well away from its resting level. This filters out electrical noise and very small changes; people, vehicles and large animals still trigger it.

**It never transmits on the public default LongFast channel.** Until you give it a private channel key it stays silent, and it refuses the public default keys. Meshtastic nodes still relay packets they can't decrypt, so private channels still share airtime; keep `minimum_broadcast_secs` sensible.

A watchdog resets the sensor if the firmware ever stops responding for about 25 seconds. To keep it fed, the chip wakes briefly every 10 seconds even when nothing is happening. A CPU fault restarts the sensor at once instead. Either way it resends its NodeInfo and telemetry, just as at power-up, and the cause is kept for the debug tools (`tools/watch_status.py` shows the fault count and where the last one happened).

Full reverse-engineering notes: [HARDWARE.md](HARDWARE.md).

## What you need

**Hardware**
- YoLink **YS7805** outdoor motion sensor. You don't need a hub.
  - The module inside is marked `YL09`. Board revisions differ (the 2021 FCC
    sample and a 2024 unit split the PIR circuit differently), so please report
    your module's date code (the last line printed on the `YL09`).
- An ST-Link programmer. I bought two cheap clones on Amazon, and neither had a working reset pin.
- A way to briefly connect the sensor's reset pad to ground: a jumper wire or a
  small pushbutton. Clone ST-Links' RST pins don't work with this board (see below).
- A way to connect to the 5 small pads on the board (**1.27mm / 0.05" spacing**): 1.27mm ribbon cable or thin wire soldered in, or a 1.27mm header.
- For configuration: a **3.3V** USB-serial adapter (I used a FT232).
- A Meshtastic node to receive the alerts.
- 2 × AA batteries. Lithium batteries recommended (e.g. Energizer Ultimate Lithium) for outdoor use because they hold up better in low temps.

**Software** (tested on Linux)
- OpenOCD 0.12 or newer, Python 3.10+, git.

## Installation

### 1. Get the tools

```sh
sudo apt install openocd stlink-tools python3-venv git    # Debian/Ubuntu
git clone https://github.com/M4rkWilhelm/yolink-meshtastic.git
cd yolink-meshtastic
python3 -m venv .venv
.venv/bin/pip install platformio pyserial
```

If OpenOCD can't open the ST-Link or the serial adapter without `sudo`, add
yourself to the group that owns the device (`ls -l /dev/ttyUSB0`, usually
`dialout` or `plugdev`) and log in again.

### 2. Open the sensor and find the header

Open the housing to reach the main board (the battery holder is wired to it).
Next to the `YL09` module is a column of **5 small holes**; the one with the
**square pad is pad 1**.

| Pad | Signal | ST-Link | USB-serial |
|---|---|---|---|
| 1 ■ | VDD (battery +) | leave unconnected | leave unconnected |
| 2 | SWCLK / serial TX | **SWCLK** | **RXD** |
| 3 | SWDIO / serial RX | **SWDIO** | **TXD** |
| 4 | GND | **GND** | **GND** |
| 5 | nRESET | (see step 4) | — |

Solder or press-fit wires into pads 2–5. Mark which wire is which.

Power the board from its batteries. Don't connect the ST-Link's 3.3V pin. Set USB-serial adapters to **3.3V**.

### 3. Build the firmware

```sh
cd firmware
../.venv/bin/pio run -e ys7805
cd ..
```

The first build downloads the STM32 toolchain and libraries.

### 4. Connect to the chip

YoLink's bootloader switches the debug pins off within microseconds of power-up, so the debugger has to connect while the chip is held in reset:

1. Connect the ST-Link (pads 2, 3, 4) and plug it into USB. Batteries in.
2. Connect pad 5 to pad 4. This puts the chip in reset.
3. Run `tools/connect.sh`. It keeps OpenOCD running in the background.
4. When it prints **ARMED**, remove the jumper. The chip should stop at its first instruction.

If later steps report that they can't read the chip, repeat this step.

### 5. Back up the original firmware (do not skip)

```sh
.venv/bin/python tools/backup_stock.py
```

This reads the flash, EEPROM and option bytes twice, compares them, and saves them to `backup/<chip-id>/`. It refuses to continue if the chip is read-protected, because flashing would then destroy the original with no backup.

Keep the backup private. It contains YoLink's firmware and your sensor's own YoLink network keys. `.gitignore` already excludes `backup/`.

### 6. Flash

```sh
tools/flash.sh firmware/.pio/build/ys7805/firmware.elf
```

It writes both flash banks, checks every byte, and restarts the sensor. It must print `verified ... bytes`; anything else is a failure.

After a successful flash, the green LED blinks once (radio OK). Three red blinks mean the radio didn't start; see [Troubleshooting](#troubleshooting).

## Configuration

### Enter setup mode

1. Disconnect the ST-Link from pads 2–4 and connect the USB-serial adapter    instead (3.3 V, TXD → pad 3, RXD → pad 2, GND → pad 4).
2. Remove the batteries. **Hold the button and insert the batteries**; release    when the green LED comes on. It then blinks once a second.
3. Open a terminal at **115200 baud** and press Enter:
   ```sh
   screen /dev/ttyUSB0 115200        # quit: Ctrl-A, K, Y
   ```

### Set up your channel

The sensor needs a channel that your personal node also has (same **name** and **key**). A secondary channel on your node is fine.

Use an existing private channel (copy its key from the Meshtastic app or CLI):
```
> ch-set name Home
> ch-set psk base64:<your channel's key>
```
Or create a new one, and add the printed key to your node as a new channel:
```
> ch-set name Sensors
> ch-set psk random
psk set: base64:....
```
Then:
```
> set detection_sensor.name Driveway
> set lora.tx_power 0          # 0 = maximum (20 dBm)
> set lora.hop_limit 3         # Meshtastic default
> save
> test                         # sends "Driveway test"
> exit
```

The defaults are deliberately quiet for bench testing: **+2 dBm and hop limit 0**, so nothing gets relayed beyond your desk. Raise them before deploying.

### Commands

| Command | Meaning |
|---|---|
| `help` | List commands |
| `show` | All settings, plus the resulting frequency and power |
| `get <name>` / `set <name> <value>` | Read or change a setting |
| `ch-set name <name>` | Channel name (max 11 characters) |
| `ch-set psk <key>` | `base64:<key>`, `0x<hex>`, `random` (new AES-256 key), or `none` |
| `set-owner "<long name>"` / `set-owner-short <name>` | Node names shown in the app |
| `test` | Send one test message now |
| `save` | Store settings (needed before `exit`) |
| `factory-reset` | Restore defaults and save |
| `exit` | Leave setup mode and restart |

Setup mode also ends after 10 idle minutes (unsaved changes are discarded).

### Settings

Names marked **M** are real Meshtastic settings with the same meaning as in
`meshtastic --set`. Defaults are this firmware's choices unless noted.

| Name | M | Default | Meaning |
|---|---|---|---|
| `lora.region` | M | `US` | Only US is supported so far |
| `lora.modem_preset` | M | `LONG_FAST` | Must match your mesh (e.g. `MEDIUM_FAST`, `SHORT_FAST`) |
| `lora.channel_num` | M | `0` | 0 = Meshtastic's default slot for the preset; see below |
| `lora.override_frequency` | M | `0` | Exact frequency in MHz, overrides the slot |
| `lora.tx_power` | M | `2` | dBm; `0` = maximum (20 dBm) |
| `lora.hop_limit` | M | `0` | How many times nodes may relay it (Meshtastic default: 3) |
| `device.led_heartbeat_disabled` | M | `false` | `true` = LEDs off (stealth; setup mode still blinks) |
| `device.node_info_broadcast_secs` | M | `10800` | Meshtastic default |
| `detection_sensor.enabled` | M | `true` | Send motion alerts |
| `detection_sensor.minimum_broadcast_secs` | M | `15` | Minimum time between alerts |
| `detection_sensor.state_broadcast_secs` | M | `0` | Periodic `state` message, 0 = off |
| `detection_sensor.send_bell` | M | `true` | Add a bell character so phones can alert |
| `detection_sensor.name` | M | `Motion` | Alert text: `<name> detected` (max 20 chars) |
| `telemetry.environment_measurement_enabled` | M | `true` | Send the chip temperature (inside the housing) |
| `telemetry.environment_update_interval` | M | `10800` | Seconds between temperature reports (3 h) |
| `telemetry.device_telemetry_enabled` | M | `true` | Send battery level and voltage |
| `telemetry.device_update_interval` | M | `43200` | Seconds between battery reports (12 h) |
| `yolink.pir_sensitivity` | — | `high` | **Not a Meshtastic setting.** Motion sensor gain: `low`, `medium`, `high` (YoLink's default), `max`. Higher means longer range and more false alerts |
| `yolink.pir_temp_comp` | — | `hot` | **Not a Meshtastic setting.** `off`; `hot`: one step more sensitive at 31–37 °C, when people barely stand out (YoLink's behavior); `hot_cold`: also one step less below 0 °C |
| `yolink.pir_threshold` | — | `52` | **Not a Meshtastic setting.** Motion confirmation strictness: higher = less sensitive; 0 = accept every PIR trigger. 52 is YoLink's own value |

**Frequency.** Meshtastic picks the radio frequency from the *primary* channel's name. The sensor assumes the common setup, where your node's primary channel has the default (empty) name and your private channel is secondary; `show` prints the frequency it uses (906.875 MHz for US LongFast). If your mesh's primary channel has a custom name, copy the frequency slot from your node's LoRa settings into `lora.channel_num`, or the exact frequency into `lora.override_frequency`.

## Testing

1. With the sensor configured and your node nearby, press the sensor's
   **button**: the green LED lights for about a second while it sends NodeInfo
   and `<name> test`. The sensor should appear in your node list.
2. **Wave** in front of it: the red LED flashes and `<name> detected` arrives.
3. Wave again within `minimum_broadcast_secs`: nothing is sent (by design).

The first 10 seconds after power-up are ignored, while the PIR settles.

## Updating and restoring

**Updating:** the sensor spends nearly all its time in deep sleep, where the debugger can't reach it, so use the jumper procedure from step 4 to connect (`tools/connect.sh`). Then `tools/flash.sh <new firmware.elf>`; it waits for the chip to wake (up to about a minute). Settings in EEPROM are kept.

**Restoring YoLink's firmware:**
```sh
tools/flash.sh backup/<chip-id>/flash.bin
```
Flashing never touches the EEPROM area YoLink uses, so the sensor's YoLink keys are still in place.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Sensor sends NodeInfo and telemetry at odd times | It restarted: watchdog reset, crash, or a battery contact briefly lost (bumped holder). Please report if it happens often |
| `connect.sh`: "did not connect" | Reset not held, wiring, or SWDIO/SWCLK swapped. Check continuity pad → ST-Link pin |
| `connect.sh` works, later steps "can't read the chip" | The jumper was disturbed while being removed and the stock firmware started. Repeat step 4 |
| `flash.sh`: VERIFY FAILED | Don't use the sensor; reflash. Make sure `connect.sh` is current (it uses the dual-bank flash config) |
| Three red blinks at startup | Radio didn't start: possibly a different hardware revision. Please open an issue |
| No messages on the node | Node needs the same channel **name and key**; same preset; same frequency (compare with `show`); move closer (default +2 dBm) |
| Setup mode doesn't start | Hold the button *while* inserting the batteries, keep holding about half a second |
| No LED on motion, and the debugger can't connect even with reset held | Check the 5-wire ribbon to the PIR board: it breaks easily when the board is twisted, and a loose wire can upset the whole board. Resolder, add strain relief |
| No reply in the terminal | TXD/RXD swapped, adapter not at 3.3 V, wrong baud rate (115200), or ST-Link still connected |

## Known issues

- **Fixed: rare crash.** Early builds could crash now and then, traced to a debug setting (debug-in-Stop) that a debug build or session leaves on until power-off. The firmware now turns it off at every start, and a 19.5-hour
  stress test since then had no crashes. Details in [HARDWARE.md](HARDWARE.md#stability-investigation-solved). If your sensor reboots often (repeated NodeInfo packets), please report it.
- - **App display:** a Meshtastic node (Heltec V3, firmware 2.7.11) receives and decodes the alerts; how each phone app displays `DETECTION_SENSOR_APP` messages isn't verified yet.
- **US region only.** Other regions need their frequency plans added.
- **Battery life estimated, not proven.** Sleep current is about 95 µA (measured; YoLink's own firmware draws about 145 µA on the same board). About 75 µA of it is always-on hardware on the main board that no firmware setting
  controls; the PIR circuit takes only 10–15 µA. With ~18 status packets a day that works out to roughly 2.5 years on alkaline AAs, less with frequent motion or in the cold (lithium AAs hold up better below freezing). See [HARDWARE.md](HARDWARE.md#power). With lithium AAs the reported battery level stays near 100% for most of their life and then drops quickly: their voltage is almost flat until they're nearly empty.
- **No remote configuration** over the mesh; settings need the serial console.

## Radio regulations

The YS7805 is FCC-certified (FCC ID [2ATM77805](https://fccid.io/2ATM77805)) under **Part 15.249** as a single-channel transmitter on 910.3 MHz. YoLink's test report measured its fundamental at about 92 dBµV/m at 3 m, roughly **−3 dBm EIRP**, just
under that rule's limit.

This firmware transmits on Meshtastic frequencies and can be set up to **+20 dBm**, about 100 times the stock level. A device modified this way is no longer the device that was certified. Whether and how you may operate it depends on where you are and how you use it; that's your responsibility. The bench default (+2 dBm) is the lowest the radio's PA_BOOST output supports.

## Project layout

| Path | Contents |
|---|---|
| `firmware/` | PlatformIO project (`src/`: firmware; `test/`: packet encoder check) |
| `tools/connect.sh`, `flash.sh`, `backup_stock.py` | Connecting, flashing, backing up |
| `tools/make_channel.py` | Generates a private channel (name + random key) to build in as the default |
| `tools/heltec_listen.py` | Prints what a USB-connected Meshtastic node receives from the sensor |
| `tools/watch_status.py`, `ramlog.py`, `sx1276_regs.py`, `sx1276_txtest.py`, `ocd.py`, `pir_trial.py` | Debugging helpers over SWD (debug build: `pio run -e ys7805_debug`) |
| `firmware/` envs `ys7805_stress*` | Bench stress tests from the crash investigation; never transmit, not for use |
| `firmware/` envs `ys7805_waketest*` | Bench tests that wake 20 times a second, for measuring wake-up cost; not for use |
| `firmware/` env `ys7805_probe` | Bench build for splitting sleep current between the boards; not for use |
| `tools/bin2elf.py`, `disasm.py`, `gpio_calls.py`, `live_watch.py` | Reverse-engineering helpers for the stock firmware |
| `HARDWARE.md` | Pinout and reverse-engineering notes |

Packet encoder check: `firmware/test/run_packet_test.sh` (needs `pycryptodome`: `.venv/bin/pip install pycryptodome`).

## Credits and license

Built on [RadioLib](https://github.com/jgromes/RadioLib) (MIT), [STM32duino](https://github.com/stm32duino) core and libraries, and the AES/CTR code from [rweather/Crypto](https://github.com/rweather/arduinolibs) (MIT, vendored in `firmware/lib/CryptoAesCtr`). Packet formats follow the published [Meshtastic protobufs](https://github.com/meshtastic/protobufs).

This project's own code is licensed under the [MIT License](LICENSE).

Upstream contributions from this work: STM32duino Low Power [issue #135](https://github.com/stm32duino/STM32LowPower/issues/135) (deep sleep broken while debug-in-Stop is set) and its fix.

The firmware also links third-party code under its own licenses: RadioLib and rweather/Crypto (MIT); the STM32 HAL drivers and the STM32duino Low Power and RTC libraries (BSD-3-Clause); the STM32duino Arduino core (LGPL-2.1). If you publish **compiled firmware**, include those libraries' notices and point to this source and its build instructions, so people can rebuild it.

YoLink is a trademark of YoSmart, Inc.; Meshtastic is a trademark of Meshtastic LLC. Both are used only to describe compatibility.
