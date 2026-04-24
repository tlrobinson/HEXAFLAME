#include "rgb_led.h"

#include <Adafruit_NeoPixel.h>
#include <cstring>

namespace {
constexpr uint8_t kLedPin = 16;
constexpr uint8_t kPixelCount = 1;

Adafruit_NeoPixel g_pixels(kPixelCount, kLedPin, NEO_GRB + NEO_KHZ800);
bool g_pixelsBegun = false;
}  // namespace

RgbLed::RgbLed() {
  if (!g_pixelsBegun) {
    g_pixels.begin();
    g_pixels.setBrightness(255);
    g_pixels.clear();
    g_pixels.show();
    g_pixelsBegun = true;
  }
}

void RgbLed::setPixel(uint8_t r, uint8_t g, uint8_t b) {
  g_pixels.setPixelColor(0, g_pixels.Color(r, g, b));
  g_pixels.show();
}

void RgbLed::clear() {
  setPixel(0, 0, 0);
}

void RgbLed::getColor(const char *color, float bright, uint8_t &r, uint8_t &g, uint8_t &b) const {
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

void RgbLed::flashColor(const char *color, float bright, int times, int timeMs) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  getColor(color, bright, r, g, b);

  const int flashes = max(1, times);
  const int pauseMs = max(0, timeMs);

  for (int i = 0; i < flashes; ++i) {
    setPixel(r, g, b);
    delay(pauseMs);
    clear();
    delay(pauseMs);
  }
}

void RgbLed::fastFlashRed(uint32_t ticks) {
  setPixel(255, 0, 0);
  for (uint32_t i = 0; i < ticks; ++i) {
    asm volatile("" ::: "memory");
  }
  clear();
}

void RgbLed::fastFlashGreen(uint32_t ticks) {
  setPixel(0, 255, 0);
  for (uint32_t i = 0; i < ticks; ++i) {
    asm volatile("" ::: "memory");
  }
  clear();
}

void RgbLed::fastFlashBlue(uint32_t ticks) {
  setPixel(0, 0, 255);
  for (uint32_t i = 0; i < ticks; ++i) {
    asm volatile("" ::: "memory");
  }
  clear();
}

bool RgbLed::heartBeat(int flashes, int delayMs) {
  flashColor("red", 0.06f, flashes, 50);
  delay(delayMs / 2);
  flashColor("green", 0.04f, flashes, 50);
  delay(delayMs / 2);
  flashColor("blue", 0.20f, flashes, 50);
  return true;
}
