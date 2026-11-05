// YoLink YS7805 outdoor motion sensor (YL09 = AcSiP S76S).
// Pin map recovered from the stock firmware; see ../../HARDWARE.md.
#pragma once

#include <Arduino.h>

// Sensors / UI
#define PIN_PIR          PB6   // comparator output, edges on motion (confirmed live)
#define PIN_PIR_ANALOG   PC1   // PIR analog level (ADC ch 11), checked by stock firmware
#define PIN_BUTTON       PA2   // active low, needs pull-up (confirmed live)
#define PIN_LED_RED      PB5   // active low (confirmed live)
#define PIN_LED_GREEN    PD2   // active low (confirmed live)
#define LED_ON           LOW
#define LED_OFF          HIGH

// PIR amplifier gain select (measured, see HARDWARE.md): (PC2,PC3) = (1,1) low,
// (1,0) medium, (0,1) high, (0,0) max.
#define PIN_PIR_GAIN_A   PC2
#define PIN_PIR_GAIN_B   PC3

// Output the stock firmware drives high in normal operation; purpose unknown
// (probably an enable), so mirror it.
#define PIN_AUX_PB7      PB7

// Setup console: LPUART1 on the SWD pads (pad 2 = TX out, pad 3 = RX in).
#define PIN_CONSOLE_TX   PA14
#define PIN_CONSOLE_RX   PA13

// SX1276 inside the S76S
#define PIN_RADIO_SCK    PB13
#define PIN_RADIO_MISO   PB14
#define PIN_RADIO_MOSI   PB15
#define PIN_RADIO_NSS    PB12
#define PIN_RADIO_RESET  PB10
#define PIN_RADIO_DIO0   PB11
#define PIN_RADIO_DIO1   PC13
// Antenna switch (S76S RF_FEM_CPS): stock SX1276SetAntSw drives it LOW to
// transmit and HIGH otherwise.
#define PIN_RF_SWITCH    PA1
#define RF_SWITCH_TX     LOW
#define RF_SWITCH_RX     HIGH

// The SX1276 is wired to PA_BOOST; stock firmware allows up to +20 dBm.
#define RADIO_MAX_POWER_DBM 20
