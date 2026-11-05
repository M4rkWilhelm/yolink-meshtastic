// Meshtastic-compatible firmware for the YoLink YS7805 outdoor motion sensor.
//
// Behaves like a Meshtastic node running only the Detection Sensor module:
// on confirmed motion it broadcasts "<name> detected" (DETECTION_SENSOR_APP),
// throttled by detection_sensor.minimum_broadcast_secs, on a private channel.
// It also announces itself (NodeInfo) at boot, on button press, and every
// device.node_info_broadcast_secs, and sends Meshtastic telemetry (chip
// temperature, battery voltage). Between events the MCU sleeps in Stop mode
// and wakes on the PIR comparator (PB6), the button (PA2), or the RTC.
//
// Hold the button while powering up to enter the setup console (console.cpp).

#include <Arduino.h>
#include <IWatchdog.h>
#include <RadioLib.h>
#include <STM32LowPower.h>
#include <STM32RTC.h>
#include <stdio.h>

#include "board.h"
#include "config.h"
#include "console.h"
#include "meshtastic_packet.h"
#include "motion.h"
#include "radio.h"
#include "faults.h"
#include "sensors.h"
#include "settings.h"

// ---- State visible over SWD ----------------------------------------------
// Read with: halt, then read_memory &g_status (address from arm-none-eabi-nm).
struct Status {
    uint32_t magic;  // 'YLMS' once setup() has finished
    uint32_t nodeNum;
    int32_t radioInitState;  // RadioLib status code from init
    int32_t lastTxState;     // RadioLib status code from the last transmit
    uint32_t motionEdges;    // PB6 interrupts
    uint32_t motionAlerts;   // "detected" messages sent
    uint32_t buttonPresses;
    uint32_t packetsSent;
    uint32_t wakeups;        // returns from Stop mode
    int32_t temperatureC;    // last chip temperature reading
    uint32_t vddMv;          // last supply (battery) voltage reading
    uint32_t gainLevel;      // PIR gain step applied: 0 = low .. 3 = max
    uint32_t resetFlags;     // RCC_CSR at boot: bit 29 IWDG (watchdog), 27 POR, 28 SFT, 26 PIN
    uint32_t faultCount;     // CPU faults since power-on (see faults.h)
    uint32_t watchdogCount;  // watchdog resets since power-on
    uint32_t faultException; // last fault: exception number active (3 = HardFault)
    uint32_t faultPc;        // last fault: where
    uint32_t faultLr;        // last fault: return address (usually the caller)
    uint32_t stressPhase;    // stress test only: step in progress (see stressTest)
};
volatile Status g_status;

static STM32RTC& rtc = STM32RTC::getInstance();

static volatile bool g_motionPending;
static volatile bool g_buttonPending;

static void onMotion() {
    g_status.motionEdges = g_status.motionEdges + 1;
    g_motionPending = true;
}

static void onButton() { g_buttonPending = true; }

static uint32_t nowSecs() { return static_cast<uint32_t>(rtc.getEpoch()); }

// Seconds elapsed since `t`, read fresh. Timestamps get updated after slow work
// (a transmit takes about half a second), so a `now` captured earlier in loop()
// can be older than them; plain `now - t` would then wrap to ~4e9.
static uint32_t secsSince(uint32_t t) {
    uint32_t n = nowSecs();
    return n > t ? n - t : 0;
}

// Seconds until something last done at `t` is due again every `interval`.
static uint32_t secsUntil(uint32_t t, uint32_t interval) {
    uint32_t elapsed = secsSince(t);
    return elapsed >= interval ? 0 : interval - elapsed;
}

static bool ledsEnabled() { return !settings::g.ledHeartbeatDisabled; }

static void led(uint32_t pin, bool on) {
    if (ledsEnabled()) digitalWrite(pin, on ? LED_ON : LED_OFF);
}

static void blink(uint32_t pin, uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        led(pin, true);
        delay(80);
        led(pin, false);
        delay(120);
    }
}

static void recordTx(int state) {
    g_status.lastTxState = state;
    g_status.packetsSent = radio::packetsSent();
}

// Meshtastic DetectionSensorModule message formats.
static void sendDetected() {
    char text[40];
    snprintf(text, sizeof(text), "%s detected", settings::g.detectionName);
    recordTx(radio::sendText(mesh::kDetectionSensorApp, text, settings::g.sendBell));
}

static void sendState(int state) {
    char text[40];
    snprintf(text, sizeof(text), "%s state: %i", settings::g.detectionName, state);
    recordTx(radio::sendText(mesh::kDetectionSensorApp, text, false));
}

// Chip temperature, re-read at most once a minute (or when forced). Also keeps
// the PIR gain in step with it.
static int32_t g_temperatureC;
static uint32_t g_temperatureAt;
static bool g_haveTemperature;
static uint32_t g_gainChangedAt;
static bool g_gainChanged;

static void refreshTemperature(bool force) {
    if (!force && g_haveTemperature && secsSince(g_temperatureAt) < 60) return;
    g_temperatureC = sensors::temperatureC();
    g_temperatureAt = nowSecs();
    g_haveTemperature = true;
    g_status.temperatureC = g_temperatureC;
    if (motion::applyGain(g_temperatureC)) {
        g_gainChangedAt = nowSecs();
        g_gainChanged = true;
    }
    g_status.gainLevel = motion::gainLevel();
}

// Meshtastic telemetry: Telemetry.environment_metrics / device_metrics.
static void sendEnvironmentTelemetry() {
    refreshTemperature(true);
    uint8_t data[mesh::kMaxPacketLen - mesh::kHeaderLen];
    size_t len = mesh::encodeEnvironmentTelemetryData(data, sizeof(data),
                                                      static_cast<float>(g_temperatureC));
    recordTx(radio::sendData(data, len));
}

static void sendDeviceTelemetry(uint32_t uptimeSecs) {
    uint32_t mv = sensors::vddMv();
    g_status.vddMv = mv;
    uint8_t data[mesh::kMaxPacketLen - mesh::kHeaderLen];
    size_t len = mesh::encodeDeviceTelemetryData(data, sizeof(data), sensors::batteryPercent(mv),
                                                 mv / 1000.0f, uptimeSecs);
    recordTx(radio::sendData(data, len));
}

static bool buttonHeld(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
        if (digitalRead(PIN_BUTTON) != LOW) return false;
    }
    return true;
}

#ifdef YL_STRESS
// Bench stress test (env ys7805_stress), never in a release: hammers the PIR
// interrupt path while repeating radio start-up, sensor reads and short Stop
// sleeps, to reproduce a rare hang seen under heavy PIR activity. It never
// transmits. g_status.wakeups counts completed rounds.
#include <HardwareTimer.h>
static void stressTest() {
    static HardwareTimer timer(TIM6);
    // Each tick fakes a PB6 edge: a software trigger on EXTI line 6 runs the
    // same interrupt handler and callback as a real one.
    timer.attachInterrupt([] { EXTI->SWIER = EXTI_SWIER_SWIER6; });
    uint32_t round = 0;
    for (;;) {
        // Vary the rate (1..16 kHz) so interrupts land at different points.
        timer.setOverflow(1000 + (round * 7919u) % 15000, HERTZ_FORMAT);
        timer.resume();
        g_status.stressPhase = 1;
        g_status.radioInitState = radio::init();
        g_status.stressPhase = 2;
        sensors::temperatureC();
        g_status.stressPhase = 3;
        motion::confirm();
        g_status.stressPhase = 4;
        motion::applyGain(round & 1 ? 35 : 20);  // also toggles the gain pins
        timer.pause();
        IWatchdog.reload();
        g_status.stressPhase = 5;
        HAL_SuspendTick();
        LowPower.deepSleep(20 + round % 50);
        HAL_ResumeTick();
        g_status.stressPhase = 6;
        g_status.wakeups = ++round;
    }
}
#endif

#ifndef YL_KEEP_WAKE_DELAY
// STM32duino's LowPower_stop() busy-waits HAL_Delay(10) after every wake from
// Stop (low_power.c), apparently for its UART-wake feature, which we don't use.
// That's 10 ms at full run current on every wake: measured 88 uC per wake at
// 32 MHz with the delay, 5 uC without (about 8 uA average at one wake per 10 s,
// and 10 ms sooner to react). This replaces the HAL's weak
// HAL_Delay and skips exactly that one call: stopFor() arms the flag, and it's
// the only HAL_Delay between there and the return from deepSleep().
static volatile bool g_skipWakeDelay;
extern "C" void HAL_Delay(uint32_t ms) {
    if (g_skipWakeDelay) {
        g_skipWakeDelay = false;
        return;
    }
    uint32_t start = HAL_GetTick();
    uint32_t wait = ms;
    if (wait < HAL_MAX_DELAY) wait += static_cast<uint32_t>(uwTickFreq);
    while (HAL_GetTick() - start < wait) {}
}
#endif

// Sleep in Stop mode for up to ms (motion, the button or the RTC end it early).
static void stopFor(uint32_t ms) {
    // LowPower_stop() doesn't suspend SysTick (LowPower_sleep() does). In true
    // Stop mode it halts anyway, but with DBG_STOP set the core clock keeps
    // running and the 1 ms tick would end every sleep.
    IWatchdog.reload();
    HAL_SuspendTick();
#ifndef YL_KEEP_WAKE_DELAY
    g_skipWakeDelay = true;
#endif
    LowPower.deepSleep(ms);
#ifndef YL_KEEP_WAKE_DELAY
    g_skipWakeDelay = false;
#endif
    HAL_ResumeTick();
}

void setup() {
    // Why we (re)started, before anything else; then clear the flags.
    g_status.resetFlags = RCC->CSR;
    __HAL_RCC_CLEAR_RESET_FLAGS();

    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    digitalWrite(PIN_LED_RED, LED_OFF);
    digitalWrite(PIN_LED_GREEN, LED_OFF);
    pinMode(PIN_AUX_PB7, OUTPUT);
    digitalWrite(PIN_AUX_PB7, HIGH);
    pinMode(PIN_PIR, INPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    settings::load();
    g_status.nodeNum = radio::nodeNum();

    // Button held at power-up: setup console (LEDs always on in this mode).
    if (buttonHeld(300)) {
        digitalWrite(PIN_LED_GREEN, LED_ON);
        while (digitalRead(PIN_BUTTON) == LOW) {}  // wait for release
        digitalWrite(PIN_LED_GREEN, LED_OFF);
        console::run();
    }

    // Debug-in-low-power bits (DBGMCU_CR DBG_SLEEP/STOP/STANDBY) off. With
    // DBG_STOP set, Stop mode keeps the core clocks running and this chip
    // crashes now and then (a return address read back wrong; about one per
    // 2 h in a stress test, several times more often with a debugger polling).
    // The bits survive every reset except power-on, so an earlier build or
    // debug session may have left them set. Debuggers can still halt the chip
    // without them (tools halt before reading memory). See HARDWARE.md.
    __HAL_RCC_DBGMCU_CLK_ENABLE();  // DBGMCU ignores writes without its clock
    DBGMCU->CR &= ~(DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY);
#ifdef YL_DEBUG
    __HAL_DBGMCU_FREEZE_IWDG();  // don't let the watchdog fire while halted in a debugger
#endif
    __HAL_RCC_DBGMCU_CLK_DISABLE();

    // Safety net: if the firmware ever hangs, reset within ~25 s. Started after
    // the setup console, which can legitimately sit idle for minutes.
#ifndef YL_NO_WATCHDOG  // only for catching a hang under the debugger
    IWatchdog.begin(config::kWatchdogUs);
#endif

    rtc.begin();
    faults::boot(g_status.resetFlags);
    {
        const faults::Info& f = faults::info();
        g_status.faultCount = f.faultCount;
        g_status.watchdogCount = f.watchdogCount;
        g_status.faultException = f.exception;
        g_status.faultPc = f.pc;
        g_status.faultLr = f.lr;
    }
    refreshTemperature(true);  // also sets the PIR gain before motion is watched
    g_status.vddMv = sensors::vddMv();
    LowPower.begin();
    LowPower.attachInterruptWakeup(PIN_PIR, onMotion, CHANGE, DEEP_SLEEP_MODE);
    LowPower.attachInterruptWakeup(PIN_BUTTON, onButton, FALLING, DEEP_SLEEP_MODE);
#ifdef YL_STRESS
    stressTest();  // never returns
#endif

    g_status.radioInitState = radio::init();
    // Green blink = radio OK, three red blinks = radio failed (if LEDs enabled).
    if (radio::ready()) blink(PIN_LED_GREEN, 1);
    else blink(PIN_LED_RED, 3);

    recordTx(radio::sendNodeInfo());
    g_status.magic = 0x534D4C59;  // "YLMS"

#ifdef YL_WAKETEST
    // Bench test (envs ys7805_waketest*): wake 20 times a second and do
    // nothing else, so the cost of each wake dominates the average current
    // (and a multimeter averages several wakes per reading).
    for (;;) {
        stopFor(50);
        g_status.wakeups = g_status.wakeups + 1;
    }
#endif
}

void loop() {
    static const uint32_t bootSecs = nowSecs();
    static uint32_t lastSentToMesh;  // detection or state message, as in Meshtastic
    static bool sentToMesh;
    static uint32_t lastMotion;
    static bool motionSeen;
    static uint32_t lastNodeInfo = nowSecs();
    static uint32_t lastEnvTelemetry, lastDeviceTelemetry;
    static bool sentEnvTelemetry, sentDeviceTelemetry;
    const settings::Settings& s = settings::g;

    IWatchdog.reload();
    refreshTemperature(false);

    if (g_motionPending) {
        g_motionPending = false;
        // Ignore edges during PIR warm-up, and briefly after a gain change (the
        // amplifier output steps when its gain is switched).
        bool warm = secsSince(bootSecs) >= config::kPirWarmupMs / 1000 &&
                    (!g_gainChanged || secsSince(g_gainChangedAt) >= 3);
        bool throttled = sentToMesh && secsSince(lastSentToMesh) < s.minimumBroadcastSecs;
        // Like Meshtastic, edges inside the throttle window are ignored, so the
        // ADC confirmation only runs when an alert could actually be sent.
        if (s.detectionEnabled && warm && !throttled && motion::confirm()) {
            lastMotion = nowSecs();
            motionSeen = true;
            led(PIN_LED_RED, true);
            sendDetected();
            led(PIN_LED_RED, false);
            g_status.motionAlerts = g_status.motionAlerts + 1;
            lastSentToMesh = nowSecs();
            sentToMesh = true;
        }
    }

    if (g_buttonPending) {
        g_buttonPending = false;
        delay(30);  // debounce
        if (digitalRead(PIN_BUTTON) == LOW) {
            g_status.buttonPresses = g_status.buttonPresses + 1;
            led(PIN_LED_GREEN, true);
            recordTx(radio::sendNodeInfo());
            char text[40];
            snprintf(text, sizeof(text), "%s test", s.detectionName);
            recordTx(radio::sendText(mesh::kDetectionSensorApp, text, false));
            led(PIN_LED_GREEN, false);
            lastNodeInfo = nowSecs();
        }
    }

    // Periodic state heartbeat (detection_sensor.state_broadcast_secs). The PIR
    // only pulses, so "state" is 1 while motion was confirmed within the last
    // minimum_broadcast_secs, else 0.
    if (s.detectionEnabled && s.stateBroadcastSecs &&
        secsSince(lastSentToMesh) >= s.stateBroadcastSecs) {
        bool active = motionSeen && secsSince(lastMotion) < s.minimumBroadcastSecs;
        sendState(active ? 1 : 0);
        lastSentToMesh = nowSecs();
        sentToMesh = true;
    }

    if (secsSince(lastNodeInfo) >= s.nodeInfoBroadcastSecs) {
        recordTx(radio::sendNodeInfo());
        lastNodeInfo = nowSecs();
    }

    // Telemetry: once soon after boot, then every update interval.
    if (s.envTelemetryEnabled &&
        (!sentEnvTelemetry || secsSince(lastEnvTelemetry) >= s.envUpdateInterval)) {
        sendEnvironmentTelemetry();
        lastEnvTelemetry = nowSecs();
        sentEnvTelemetry = true;
    }
    if (s.deviceTelemetryEnabled &&
        (!sentDeviceTelemetry || secsSince(lastDeviceTelemetry) >= s.deviceUpdateInterval)) {
        sendDeviceTelemetry(secsSince(bootSecs));
        lastDeviceTelemetry = nowSecs();
        sentDeviceTelemetry = true;
    }

    if (g_motionPending || g_buttonPending) return;  // more work arrived meanwhile

    // Sleep until the next scheduled message, capped by the watchdog period.
    // PB6/PA2 interrupts wake us earlier.
    uint32_t sleepSecs = config::kMaxSleepSecs;  // the watchdog needs a reload by then
    uint32_t untilNodeInfo = secsUntil(lastNodeInfo, s.nodeInfoBroadcastSecs);
    if (untilNodeInfo < sleepSecs) sleepSecs = untilNodeInfo;
    if (s.detectionEnabled && s.stateBroadcastSecs) {
        uint32_t untilState = secsUntil(lastSentToMesh, s.stateBroadcastSecs);
        if (untilState < sleepSecs) sleepSecs = untilState;
    }
    if (s.envTelemetryEnabled) {
        uint32_t untilEnv = secsUntil(lastEnvTelemetry, s.envUpdateInterval);
        if (untilEnv < sleepSecs) sleepSecs = untilEnv;
    }
    if (s.deviceTelemetryEnabled) {
        uint32_t untilDevice = secsUntil(lastDeviceTelemetry, s.deviceUpdateInterval);
        if (untilDevice < sleepSecs) sleepSecs = untilDevice;
    }
    if (sleepSecs == 0) sleepSecs = 1;
    stopFor(sleepSecs * 1000);
    g_status.wakeups = g_status.wakeups + 1;
}
