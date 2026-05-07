#pragma once

#include <Arduino.h>

class RgbLed {
public:
  RgbLed();

  void flashColor(const char *color, float bright = 1.0f, int times = 1, int timeMs = 10);
  void fastFlashRed(uint32_t ticks = 1);
  void fastFlashGreen(uint32_t ticks = 1);
  void fastFlashBlue(uint32_t ticks = 1);
  bool heartBeat(int flashes = 10, int delayMs = 0);

private:
  void setPixel(uint8_t r, uint8_t g, uint8_t b);
  void clear();
  void getColor(const char *color, float bright, uint8_t &r, uint8_t &g, uint8_t &b) const;
};

