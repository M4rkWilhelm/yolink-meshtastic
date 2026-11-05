# YoLink YS7805 Outdoor Motion Sensor: hardware map

Recovered from the stock firmware (`dumps/ys7805_flash_stock.bin`, sha256 `d256d5d4…`)
by static analysis, with key points confirmed live under SWD (2026-11-03).

## Module
- **YL09** = rebranded **AcSiP S76S** SiP: STM32L073RZ (Cortex-M0+, 192 KB flash,
  20 KB RAM, 6 KB EEPROM) + SX1276 + PA. Device ID 0x447. Shipped with RDP Level 0.
- Silicon revision: DBG_IDCODE = 0x20086447, REV_ID **0x2008** (marking 1/P/Q/Z in
  [ES0292](https://www.st.com/resource/en/errata_sheet/es0292-stm32l07xxxl08xxx-device-errata-stmicroelectronics.pdf)).
  The Stop-mode errata 2.1.2 (flash wake-up after an EXTI near Stop entry) and 2.1.3
  (reset on wake with the low-power regulator) list Rev A only, so they don't apply.
- Power: 2×AA, straight to VDD (no regulator).
- SX1276 clock: crystal (no TCXO; RadioRegsInit at 0x080107F8 has no RegTcxo entry).
  Confirmed on hardware: TX completes with RegTcxo = 0x09 and hangs with TcxoInputOn.
- STM32 HSE: **24 MHz crystal** (PLL ×4 ÷3 = 32 MHz SYSCLK). STM32duino's `acsip_s76s`
  board doesn't set `HSE_VALUE`, and the HAL default of 8 MHz makes every delay and
  timeout 3× too short. Build with `-D HSE_VALUE=24000000`. Measured over SWD
  (SysTick vs. wall clock).
- That clock setup comes from STM32duino's board file: the PLL runs at 96 MHz (the
  range 1 maximum) so it can also give USB its 48 MHz. This board has no USB.
  Registers read live: PWR_CR VOS = range 1, FLASH_ACR = 1 wait state + prefetch,
  RCC_CFGR = 0x0085000F (PLL from HSE, ×4 ÷3).

## Pins

| Function | Pin | Config in stock FW | Evidence |
|---|---|---|---|
| PIR motion (comparator out) | **PB6** | EXTI both edges, no pull | live: handler 0x08008E90 hit on motion |
| Button | **PA2** | EXTI falling, pull-up | live: handler 0x08007C40 hit on press |
| LED 1 (red) | **PB5** | output, active low | confirmed live: lit on motion by our firmware |
| LED 2 (green) | **PD2** | output, active low | confirmed live: lit on button by our firmware |
| PIR analog level | **PC1** (ADC ch 11) | analog | stock confirms each PB6 edge with 5 ADC reads > 0xA2 (0x08009340) |
| PB7 | output, high in normal operation | probably a power/enable line; low only in a special boot branch (0x080076D4) | 0x0800EB88 (high), 0x0800EB54 (low) |
| **PIR sensitivity select** | **PC2, PC3** | 2-bit gain/threshold select | 0x08001B68, see below |
| **RF antenna switch** | **PA1** (S76S `RF_FEM_CPS`) | **TX → PA1 = 0, RX/idle → PA1 = 1** | SX1276SetAntSw 0x0800BC1C |
| SX1276 SPI | SPI2: PB13 SCK, PB14 MISO, PB15 MOSI, PB12 NSS | | S76S datasheet + 0x0800BA52 |
| SX1276 reset | PB10 | | 0x0800BAFC |
| SX1276 DIO0 / DIO1 / DIO2 | PB11 / PC13 / PB9 | | IoInit/IoDeInit 0x0800B3E4 / 0x0800B384 |
| SWD | PA13 SWDIO / PA14 SWCLK | | header pads 3 / 2 |
| Factory console (LPUART1, AF6) | PA13 RX / PA14 TX, 57600 8N1 | shares SWD pins | MspInit 0x08006B20 |

## Watchdog (stock firmware)
- Stock enables the IWDG at start-up (HAL_IWDG_Init 0x08005158, called from
  0x08008EAC via 0x0800F824): prescaler /256, reload 0x500 = 1280, window 0x500, so
  about **9 s** at the nominal 37 kHz LSI (4.5–18 s across LSI tolerance). Reloaded
  (HAL_IWDG_Refresh 0x080051BC) from 19 call sites. Our firmware uses ~25 s and a
  10 s maximum sleep.

## Radio
- Transmits on **PA_BOOST** always (RegPaConfig |= 0xF0). Above 17 dBm, RegPaDac = 0x87.
  Power clamped to 5–20 dBm (SX1276SetRfTxPower 0x0800BDDC).
- Stock sync word: 0x34 public / 0x12 private (0x0800BDB8). Meshtastic uses 0x2B.

## 5-pad header (square = pad 1)

| Pad | Signal |
|---|---|
| 1 ■ | VDD (battery +) |
| 2 | SWCLK / PA14 (LPUART1 TX) |
| 3 | SWDIO / PA13 (LPUART1 RX) |
| 4 | GND |
| 5 | nRESET |

## Debug notes
- The bootloader sets every GPIO to analog (MODER = 0xFFFFFFFF) within microseconds of
  reset (GPIOA store at 0x08000B06), so SWD only works under reset. Clone ST-Link RST
  pins don't work here: hold pad 5 to GND by hand, run `tools/connect.sh`, and remove
  the jumper cleanly when it prints ARMED.
- `tools/live_watch.py` runs the stock firmware with SWD kept alive and logs motion and
  button events.
- STM32L0 erased flash reads 0x00. Flash layout: bootloader 0x08000000 (4 KB), app 0x08001000.
- When firmware idles in WFI, debugger memory reads return zeros unless the core is
  halted first (or DBGMCU_CR.DBG_SLEEP is set). The tools in `tools/` halt before reading.
- `tools/sx1276_regs.py` and `tools/sx1276_txtest.py` drive SPI2 from the debugger to
  read SX1276 registers and do a manual transmit. `tools/ramlog.py` reads RadioLib debug
  output from the `ys7805_debug` build.
- Watchdog: our firmware runs the IWDG (~25 s). It keeps counting while the core is
  halted unless DBGMCU_APB1FZ.DBG_IWDG_STOP is set; `tools/flash.sh` sets it before
  writing (after enabling the DBGMCU clock, RCC_APB2ENR.DBGEN). The DBGMCU registers
  survive system resets and only clear on power-up.
- Sticky FLASH_SR error flags left by a failed write make every later erase fail
  ("invalid program address"); `tools/flash.sh` clears them (write 0x32F00 to 0x40022018).
- Cortex-M0+ software can't read DHCSR, so firmware can't tell whether a debugger is
  attached. A BKPT with no debugger in a fault handler locks the core up.
- Crash records live in the RTC backup registers, which survive system and watchdog
  resets (cleared by our firmware on power-on): BKP0R = 0xFA17xxxx | exception number,
  BKP1R = faulting PC, BKP2R = LR, BKP3R = xPSR, BKP4R = fault count << 16 | watchdog
  reset count. STM32RTC uses backup registers only on F1 parts.

## Power

Measured 2026-11-05, 2×AA alkaline at 2.9 V, Fluke 8060A in series with the battery
(200 mA range, 10 µA resolution), our release firmware asleep in Stop:

| Change (one at a time, over SWD) | Sleep current |
|---|---|
| As built (PA1 high, PB7 high, PC3 high) | 130 µA |
| PA4 floating input → analog | 130 µA |
| PC3 (PIR gain) high → low | 130 µA |
| PB7 high → low | 110–120 µA (and PIR false triggers: PB7 feeds the PIR board) |
| PA1 (RF switch control) high → low or analog | **90–100 µA** |

PA1 high costs about 35 µA through the RF switch's control input. The firmware now
holds it low whenever the radio isn't receiving (it never does), confirmed in
firmware at 90–100 µA with transmissions still received normally. The SX1276 itself
is in sleep (RegOpMode 0x88). Most of the remaining ~80 µA is presumably the PIR
board (~15 µA of it via PB7).

**Stock firmware, same setup: 140–150 µA** (steady, no spikes, with its hub out of
reach). Ours sleeps about a third lower, and the hardware floor is at or below 90 µA.

### Cost of a wake-up

Measured with the `ys7805_waketest*` builds, which wake 20 times a second and do
nothing else, so the Fluke averages several wakes per reading (sleep: 0.09 mA):

| Build | Average | Charge per wake | At one wake per 10 s |
|---|---|---|---|
| 32 MHz, with STM32duino's 10 ms post-wake delay | 1.85 mA | ~88 µC | ~8.8 µA |
| 16 MHz HSI16, with the delay | 0.97 mA | ~44 µC | ~4.4 µA |
| 32 MHz, delay skipped | 0.195 mA | ~5 µC | ~0.5 µA |
| 16 MHz HSI16, delay skipped | 0.15 mA | ~3 µC | ~0.3 µA |

`LowPower_stop()` in STM32duino Low Power (`low_power.c`) busy-waits `HAL_Delay(10)`
after every wake from Stop, apparently for its UART-wake feature. The firmware skips
that one call (it overrides the HAL's weak `HAL_Delay`; `YL_KEEP_WAKE_DELAY`
restores it), which also makes it react 10 ms sooner. With the delay gone, the clock
choice is worth only ~0.2 µA, so the firmware stays on the 32 MHz crystal.

## Stability investigation (solved)

Two hangs seen in normal use, both under heavy PIR activity, one with a heat gun on
the sensor (during radio start-up, before the first sleep). The `ys7805_stress` build
(fake PIR interrupts at 1–16 kHz while looping radio init, sensor reads and short Stop
sleeps; no transmissions) reproduces a HardFault about every 15 minutes at 32 MHz.
Captured faults, at room temperature:

| Round | Context | What went wrong |
|---|---|---|
| 152 | thread mode | `pop {r4-r7, pc}` loaded stale `printf` stack data; PC = 0x20000344 (`_impure_data`) |
| 496 | TIM6 interrupt, during radio init | return from a 3-instruction function landed mid-way into an unrelated one (`RamLog::write`), which then faulted on address 2 |
| 78 (16 MHz HSI16) | EXTI4_15 interrupt, during radio init | `HAL_GPIO_EXTI_IRQHandler` pushed its return address and popped back 0 |

All are control transfers to places no code path leads to, with the stack pointer
inside the stack, no stack overflow and no interrupt handler that writes the stack:
what was read back from the stack differs from what was written.

Running the core at 16 MHz from HSI16 (`YL_CLOCK_HSI16`, range 1, 1 wait state)
didn't cure it: one fault and one watchdog reset in about 2.5 hours, against about
one fault per 18 minutes at 32 MHz. Too few events to call that a real difference.

Every fault has the same signature: an interrupt (or a `pop`) brings back a return
address that isn't the one saved, while the code itself is fine.

Stress runs at 32 MHz, faults counted in the backup registers:

| Build | Debugger | `DBGMCU_CR` | Time | Faults |
|---|---|---|---|---|
| debug | attached, polled every 5–30 s | 7 | ~37 min | 2 |
| debug | detached (OpenOCD stopped) | 7 | 2 h | 1 |
| release (no RAM log) | detached | 7 | 2 h | 1 |
| release | detached | **0** | 2 h | 0 |
| release | detached | **0** | 8 h (overnight) | 0 |

`DBGMCU_CR` = 7 (DBG_SLEEP | DBG_STOP | DBG_STANDBY) keeps the debug domain and core
clocks alive in Sleep/Stop, so "Stop" isn't true Stop. It survives every reset
except power-on. The debug build sets it (`YL_DEBUG`), and it stayed set through all
later flashing, including during both hangs seen in normal use. OpenOCD's
`stm32l0.cfg` also tries to set it on connect, but that write fails on this chip:
DBGMCU registers ignore writes while RCC_APB2ENR.DBGEN is 0 (reads still work).
To clear it without a power cycle: `mmw 0x40021034 0x00400000 0`,
`mww 0x40015804 0`, `mmw 0x40021034 0 0x00400000`.

**Cause: Stop mode with DBG_STOP set.** 10 h and ~216,000 rounds with the bits clear
gave no faults, where about 5 were expected at the rate with them set (chance of
zero: under 1%). An active debugger multiplies the rate. The firmware now clears the
bits at every boot (both builds), and the debug build no longer sets them.

The 16 MHz HSI16 result above was also with DBG_STOP set; the option (`YL_CLOCK_HSI16`)
stays available but isn't needed for stability.

Side effect: with DBG_STOP clear the debugger can't reach the chip while it's in
Stop. OpenOCD can't even connect then (use the reset jumper), and a halt that lands
inside Stop reports "halted" while every memory read returns 0 and flash writes fail.
`flash.sh` therefore waits until the chip ID reads back, then sets the DBGMCU bits for
the rest of the session (the firmware clears them at its next boot).

## PIR sensitivity (PC2/PC3)

`0x08001B68` drives PC2/PC3 from the app's sensitivity setting (byte at
`0x20002A5C+0x0C`, values 0/1/2) and a signed temperature byte (`0x2000017C`,
probably °C). When the temperature is 31..37 (close to body temperature, where a
PIR's contrast drops), every setting moves one step along the same scale:

| Setting | normal (PC2, PC3) | 31..37 °C (PC2, PC3) |
|---|---|---|
| 1 | 1, 1 | 1, 0 |
| 2 | 1, 0 | 0, 1 |
| 0 | 0, 1 | 0, 0 |

So the scale is (1,1) → (1,0) → (0,1) → (0,0), and heat shifts one step toward
(0,0), which is most likely the highest gain. Not yet confirmed on hardware: measure
PC1's swing for the same motion at each PC2/PC3 combination. The status report to
the hub (0x0800E7EE) also carries the alert interval, LED flag, no-motion timeout,
sensitivity and a temperature byte.

## FCC filing (FCC ID 2ATM77805)

From YoLink's public exhibits (local copies in `fcc/`, not redistributed):
- Certified under **Part 15.249**: one channel, **910.3 MHz**, LoRa, internal 1 dBi
  antenna. Fundamental: 92.28 dBµV/m average / 98.58 peak at 3 m (limit 94 / 114),
  i.e. about −3 dBm EIRP average.
- User guide: range about 0–40 ft (12 m); best detection angle 90° across the motion.
  Sensitivity **High / Medium / Low, default High** ("changing sensitivity will
  shorten the detection range"). "Time to enter No-Motion State": default 1 minute.
  While motion continues, re-alerts every 30 minutes; after more than 1 minute of
  no motion, the next motion alerts again. The sensor also reports a temperature value.
- SET button: press once = power on / sync settings; hold 5–10 s = pair (YoLink
  Control); 10–15 s = unpair; 20–25 s = factory reset (over 25 s aborts).
- Stock LED codes: red once = alert or normal; red then green = power-on; fast red
  every 30 s = low battery; red/green alternating = factory reset.
- Internal photos show a 2021 board (YL09 date code 2110) where the 8-pin IC sits on
  the PIR daughterboard. On the 2024 unit (2419) the SGM8705 comparator is on the
  main board. The 5-pad header is in the same place. The SET button is on the main
  board's back.
- Sensitivity mapping, inferred: the stock default is High, and the code value that
  needs no setup is 0, so probably 0 = High (0,1 → 0,0 when hot), 2 = Medium (1,0),
  1 = Low (1,1). The 5-wire ribbon probably carries PC2/PC3 to gain resistors next
  to the PIR amplifier (not confirmed).

### Measured gain (2026-11-04)

`firmware-tests/pir_sensitivity` holds each PC2/PC3 combination and samples PC1
continuously (`tools/pir_trial.py`). Stimulus: 3 hand waves at about 30 cm with a
sheet of paper over the bare PIR element (without the paper every setting
saturates at 0..188). Peak deviation of PC1 from rest (8-bit), two rounds in
opposite order:

| PC2, PC3 | Round 1 | Round 2 | Mean | Relative | Inferred stock setting |
|---|---|---|---|---|---|
| 1, 1 | 21 | 22 | 21.5 | 1.0 | Low (1) |
| 1, 0 | 20 | 28 | 24 | 1.1 | Medium (2) |
| 0, 1 | 32 | 42 | 37 | 1.7 | High (0, default) |
| 0, 0 | 39 | 54 | 46.5 | 2.2 | High + hot-weather boost |

The pins change the amplifier gain (the comparator threshold stays put; no PB6
edges fired at all with the paper on). Idle noise rises in the same order
(average |dev| 0.88 → 1.29). Resting level about 92.
