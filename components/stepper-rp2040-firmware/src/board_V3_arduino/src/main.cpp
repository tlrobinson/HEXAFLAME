// Arduino port of board_V3 (MicroPython original by Andrea Favero, 25/06/2025).
// Target: RP2040-Zero. Demonstrates TMC2209 StallGuard sensorless homing with
// PIO-generated step pulses.

#include <Arduino.h>
#include "stepper.h"
#include "rgb_led.h"

static constexpr uint8_t ENABLE_PIN     = 2;
static constexpr uint8_t HOMING_PIN     = 9;

static constexpr uint32_t STEPPER_FREQS[2] = { 400, 2000 };
static constexpr uint32_t DEBOUNCE_MS = 10;

static volatile bool g_homing_requested = false;
static volatile bool g_centering = false;
static int g_last_idx = 1;

static Stepper* g_stepper = nullptr;

static void onHomingPin() {
    // Same debounce strategy as the Python original: block briefly, then re-check.
    uint32_t t0 = millis();
    while (millis() - t0 < DEBOUNCE_MS) { /* spin */ }
    if (digitalRead(HOMING_PIN) == LOW && !g_centering) {
        g_homing_requested = true;
    }
}

static void runCentering() {
    g_centering = true;
    Serial.println();
    Serial.println("##############################################################################");
    Serial.println("############  Stepper centering via SENSORLESS homing function   ############");
    Serial.println("##############################################################################");

    int idx = (g_last_idx == 1) ? 0 : 1;
    g_last_idx = idx;

    bool ok = g_stepper->centering(STEPPER_FREQS[idx]);
    Serial.println(ok ? "\nStepper is centered\n" : "\nFailed to center the stepper\n");

    g_centering = false;
}

void setup() {
    Serial.begin(115200);
    // Short startup delay to let USB CDC enumerate before the first prints.
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 2000) { /* wait */ }

    // Bring the NeoPixel up first so we have a visible sign of life even if
    // the serial port never enumerates (no external host, power-only cable, etc.).
    rgb_led.begin();
    rgb_led.flashColor("blue", 0.2f, 1, 0.1f);
    Serial.println("\nboard_V3 Arduino port booting\n");

    Serial.println("waiting time to eventually stop the code before further imports ...");
    rgb_led.heartBeat(10, 1.0f);

    // Serial1 (UART0) to the TMC2209 — pin remap and open before Stepper init.
    Serial1.setRX(13);
    Serial1.setTX(12);
    Serial1.begin(230400, SERIAL_8N1);

    // On RP2040-Zero, arduino-pico reports clk_sys at 125 MHz.
    static Stepper stepper(125000000, 5000000, /*debug=*/true);
    g_stepper = &stepper;

    // TMC driver enable: drive low to energize the stepper.
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW);

    // Homing request button.
    pinMode(HOMING_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HOMING_PIN), onHomingPin, FALLING);

    if (!stepper.tmcTest()) {
        Serial.println();
        Serial.println("###################################################################");
        Serial.println("#                                                                 #");
        Serial.println("#   The TMC driver UART does not react: IS THE DRIVER POWERED ?   #");
        Serial.println("#                                                                 #");
        Serial.println("###################################################################");
        return;
    }

    Serial.println("\nCode running on RP2040");
    Serial.println("Sensorless homing example");
    Serial.println("\nPress the push button for SENSORLESS homing demo");
}

void loop() {
    if (!g_stepper) {
        delay(100);
        return;
    }
    if (g_homing_requested) {
        g_homing_requested = false;
        runCentering();
    }
    delay(100);
}
