#include "status_led.h"

#include <cstring>

#if defined(BOARD_RP2040)

#include <Adafruit_NeoPixel.h>

namespace {
constexpr uint8_t kLedPin = 16;
constexpr uint8_t kPixelCount = 1;
Adafruit_NeoPixel g_pixels(kPixelCount, kLedPin, NEO_GRB + NEO_KHZ800);

void colorComponents(const char *color, float bright, uint8_t &r, uint8_t &g, uint8_t &b) {
  const float clamped = constrain(bright, 0.0f, 1.0f);
  const uint8_t value = static_cast<uint8_t>(clamped * 255.0f);
  r = 0;
  g = 0;
  b = 0;
  if (std::strcmp(color, "red") == 0) {
    r = value;
  } else if (std::strcmp(color, "green") == 0) {
    g = value;
  } else if (std::strcmp(color, "blue") == 0) {
    b = value;
  }
}
}  // namespace

StatusLed::StatusLed() {}

void StatusLed::begin() {
  if (begun_) {
    return;
  }
  g_pixels.begin();
  g_pixels.setBrightness(255);
  g_pixels.clear();
  g_pixels.show();
  begun_ = true;
}

void StatusLed::flashColor(const char *color, float bright, int times, int timeMs) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  colorComponents(color, bright, r, g, b);
  const int flashes = max(1, times);
  const int pauseMs = max(0, timeMs);
  for (int i = 0; i < flashes; ++i) {
    g_pixels.setPixelColor(0, g_pixels.Color(r, g, b));
    g_pixels.show();
    delay(pauseMs);
    g_pixels.clear();
    g_pixels.show();
    delay(pauseMs);
  }
}

bool StatusLed::heartBeat(int flashes, int delayMs) {
  flashColor("red", 0.06f, flashes, 50);
  delay(delayMs / 2);
  flashColor("green", 0.04f, flashes, 50);
  delay(delayMs / 2);
  flashColor("blue", 0.20f, flashes, 50);
  return true;
}

#else  // BOARD_OCTOPUS - single status LED, colour ignored.

namespace {
// PA13 doubles as SWDIO; the Octopus routes its status LED here and we follow
// the board convention. Using it as an output forfeits SWD debugging.
constexpr uint32_t kStatusLedPin = PA13;
}  // namespace

StatusLed::StatusLed() {}

void StatusLed::begin() {
  if (begun_) {
    return;
  }
  pinMode(kStatusLedPin, OUTPUT);
  digitalWrite(kStatusLedPin, LOW);
  begun_ = true;
}

void StatusLed::flashColor(const char *color, float bright, int times, int timeMs) {
  (void)color;
  (void)bright;
  const int flashes = max(1, times);
  const int pauseMs = max(0, timeMs);
  for (int i = 0; i < flashes; ++i) {
    digitalWrite(kStatusLedPin, HIGH);
    delay(pauseMs);
    digitalWrite(kStatusLedPin, LOW);
    delay(pauseMs);
  }
}

bool StatusLed::heartBeat(int flashes, int delayMs) {
  flashColor("white", 0.1f, flashes, 50);
  delay(delayMs / 2);
  return true;
}

#endif
