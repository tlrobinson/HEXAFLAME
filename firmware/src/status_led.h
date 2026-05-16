#pragma once

#include <Arduino.h>

// Status indicator. On the RP2040 this is the on-board NeoPixel; on the
// Octopus it is the single status LED. The colour-aware calls degrade to
// plain blinks where only a monochrome LED is available, so the motion code
// can call flashColor()/heartBeat() without caring which board it runs on.
class StatusLed {
public:
  StatusLed();

  void begin();
  void flashColor(const char *color, float bright = 1.0f, int times = 1, int timeMs = 10);
  bool heartBeat(int flashes = 10, int delayMs = 0);

private:
  bool begun_ = false;
};
