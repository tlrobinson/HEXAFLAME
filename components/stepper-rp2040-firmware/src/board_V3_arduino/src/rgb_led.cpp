#include "rgb_led.h"
#include <Arduino.h>

RgbLed::RgbLed() : _np(1, LED_PIN, NEO_GRB + NEO_KHZ800), _started(false) {}

void RgbLed::begin() {
    if (_started) return;
    _started = true;
    Serial.println("\nUploading rgb_led ...");
    _np.begin();
    _np.show();
}

RgbLed& RgbLed::instance() {
    static RgbLed inst;
    return inst;
}

static uint32_t rgbFromName(const char* color, float bright) {
    uint8_t v = (uint8_t)(bright * 255.0f);
    if (!strcmp(color, "red"))   return Adafruit_NeoPixel::Color(v, 0, 0);
    if (!strcmp(color, "green")) return Adafruit_NeoPixel::Color(0, v, 0);
    if (!strcmp(color, "blue"))  return Adafruit_NeoPixel::Color(0, 0, v);
    return 0;
}

void RgbLed::flashColor(const char* color, float bright, int times, float time_s) {
    if (!_started) begin();
    if (bright < 0.0f) bright = 0.0f;
    if (bright > 1.0f) bright = 1.0f;
    if (times < 1) times = 1;
    if (time_s < 0.0f) time_s = 0.0f;

    uint32_t on = rgbFromName(color, bright);
    uint32_t off = 0;
    uint32_t delay_ms = (uint32_t)(time_s * 1000.0f);

    for (int i = 0; i < times; i++) {
        _np.setPixelColor(0, on);
        _np.show();
        delay(delay_ms);
        _np.setPixelColor(0, off);
        _np.show();
        delay(delay_ms);
    }
}

bool RgbLed::heartBeat(int n, float delay_s) {
    flashColor("red", 0.06f, n, 0.05f);
    delay((uint32_t)(delay_s * 500.0f));
    flashColor("green", 0.04f, n, 0.05f);
    delay((uint32_t)(delay_s * 500.0f));
    flashColor("blue", 0.20f, n, 0.05f);
    return true;
}
