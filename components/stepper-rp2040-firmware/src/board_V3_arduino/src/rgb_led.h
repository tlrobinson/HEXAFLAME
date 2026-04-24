#pragma once
#include <Adafruit_NeoPixel.h>

class RgbLed {
public:
    static RgbLed& instance();

    // Initialize the underlying NeoPixel. Must be called from setup(),
    // not from a static constructor — arduino-pico isn't ready for
    // hardware access during global initialization.
    void begin();

    void flashColor(const char* color, float bright = 1.0f, int times = 1, float time_s = 0.01f);
    bool heartBeat(int n = 10, float delay_s = 0.0f);

private:
    RgbLed();
    RgbLed(const RgbLed&) = delete;
    RgbLed& operator=(const RgbLed&) = delete;

    Adafruit_NeoPixel _np;
    bool _started;
    static constexpr uint8_t LED_PIN = 16;
};

// Shorthand accessor. Safe to use — returns the lazily constructed singleton.
inline RgbLed& rgb_led_() { return RgbLed::instance(); }
#define rgb_led rgb_led_()
