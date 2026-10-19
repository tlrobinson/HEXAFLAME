#include "status_led.h"

#include "esp_arduino_version.h"

StatusLed::StatusLed(int pin, int powerPin) : pin_(pin), powerPin_(powerPin) {}

void StatusLed::begin() {
  if (powerPin_ >= 0) {
    pinMode(powerPin_, OUTPUT);
    digitalWrite(powerPin_, HIGH);
  }
  show(0, 0, 40);
}

void StatusLed::flash(uint8_t r, uint8_t g, uint8_t b, uint32_t nowMs, uint32_t durationMs) {
  flashing_ = true;
  flashUntilMs_ = nowMs + durationMs;
  flashR_ = r;
  flashG_ = g;
  flashB_ = b;
}

void StatusLed::service(uint32_t nowMs) {
  if (flashing_) {
    if (static_cast<int32_t>(nowMs - flashUntilMs_) < 0) {
      show(flashR_, flashG_, flashB_);
      return;
    }
    flashing_ = false;
  }

  switch (mode_) {
    case Mode::Boot:
      show(0, 0, 40);
      break;
    case Mode::NoBoards:
      show(40, 0, 40);
      break;
    case Mode::Fault:
      if ((nowMs / 250) & 1) {
        show(60, 0, 0);
      } else {
        show(0, 0, 0);
      }
      break;
    case Mode::HostAway:
      show(40, 20, 0);
      break;
    case Mode::Ready:
      show(0, 30, 0);
      break;
  }
}

void StatusLed::show(uint8_t r, uint8_t g, uint8_t b) {
  if (r == lastR_ && g == lastG_ && b == lastB_) {
    return;
  }
  lastR_ = r;
  lastG_ = g;
  lastB_ = b;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  rgbLedWrite(static_cast<uint8_t>(pin_), r, g, b);
#else
  neopixelWrite(static_cast<uint8_t>(pin_), r, g, b);
#endif
}
