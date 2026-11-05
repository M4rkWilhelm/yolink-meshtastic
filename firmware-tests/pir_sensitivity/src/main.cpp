// PIR sensitivity measurement for the YoLink YS7805 (test firmware only).
//
// Holds PC2/PC3 at the combination in g_test.select (writable over SWD) and
// continuously samples the PIR analog level on PC1, tracking the resting level
// and the largest swing from it, plus comparator edges on PB6. The red LED
// mirrors PB6. No radio, no sleep. SWD stays available (PA13/PA14 untouched).
//
// Over SWD: write g_test.select (0..3, bit1 = PC2, bit0 = PC3) and set
// g_test.reset = 1 to clear the statistics; read the struct back afterwards.
#include <Arduino.h>

#define PIN_PIR        PB6
#define PIN_PIR_ANALOG PC1
#define PIN_SENS_A     PC2
#define PIN_SENS_B     PC3
#define PIN_AUX_PB7    PB7
#define PIN_LED_RED    PB5   // active low
#define PIN_LED_GREEN  PD2   // active low

struct Test {
    uint32_t magic;          // 'PIRT'
    volatile uint32_t select;  // bit1 -> PC2, bit0 -> PC3
    volatile uint32_t reset;   // write 1 to clear the stats below
    uint32_t applied;        // select value currently on the pins
    uint32_t samples;
    uint32_t edges;          // PB6 changes
    uint32_t minRaw;         // 8-bit ADC
    uint32_t maxRaw;
    uint32_t baselineX16;    // slow-moving resting level, x16
    uint32_t maxDev;         // largest |sample - baseline|
    uint32_t sumDev;         // sum of |sample - baseline|, for an average
    uint32_t pir;            // PB6 level now
};
volatile Test g_test;

static volatile uint32_t g_edges;
static void onEdge() { g_edges = g_edges + 1; }

static void applySelect(uint32_t sel) {
    digitalWrite(PIN_SENS_A, (sel & 2) ? HIGH : LOW);
    digitalWrite(PIN_SENS_B, (sel & 1) ? HIGH : LOW);
    g_test.applied = sel;
}

static void clearStats() {
    noInterrupts();
    g_edges = 0;
    interrupts();
    g_test.samples = 0;
    g_test.edges = 0;
    g_test.minRaw = 255;
    g_test.maxRaw = 0;
    g_test.maxDev = 0;
    g_test.sumDev = 0;
    g_test.reset = 0;
}

void setup() {
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_LED_GREEN, HIGH);
    pinMode(PIN_AUX_PB7, OUTPUT);
    digitalWrite(PIN_AUX_PB7, HIGH);  // as the stock firmware runs it
    pinMode(PIN_SENS_A, OUTPUT);
    pinMode(PIN_SENS_B, OUTPUT);
    pinMode(PIN_PIR, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_PIR), onEdge, CHANGE);
    analogReadResolution(8);

    g_test.select = 1;  // stock "High" at normal temperature: PC2=0, PC3=1
    applySelect(g_test.select);
    g_test.baselineX16 = static_cast<uint32_t>(analogRead(PIN_PIR_ANALOG)) * 16;
    clearStats();
    g_test.magic = 0x54524950;  // "PIRT"
}

void loop() {
    if (g_test.select != g_test.applied) applySelect(g_test.select & 3);
    if (g_test.reset) clearStats();

    uint32_t v = analogRead(PIN_PIR_ANALOG);
    // Baseline: slow exponential average (time constant ~1-2 s at ~1 kHz).
    int32_t base = static_cast<int32_t>(g_test.baselineX16);
    base += (static_cast<int32_t>(v) * 16 - base) / 1024;
    g_test.baselineX16 = base;
    uint32_t dev = static_cast<uint32_t>(abs(static_cast<int32_t>(v) - base / 16));

    g_test.samples = g_test.samples + 1;
    if (v < g_test.minRaw) g_test.minRaw = v;
    if (v > g_test.maxRaw) g_test.maxRaw = v;
    if (dev > g_test.maxDev) g_test.maxDev = dev;
    g_test.sumDev = g_test.sumDev + dev;
    g_test.edges = g_edges;

    uint32_t pir = digitalRead(PIN_PIR);
    g_test.pir = pir;
    digitalWrite(PIN_LED_RED, pir ? LOW : HIGH);  // red mirrors the comparator
    delayMicroseconds(900);
}
