#pragma once

#include <Arduino.h>

// On-board NeoPixel as a one-glance status indicator.
//
//   blue            booting
//   magenta         no solenoid boards found on the I2C bus
//   red (blinking)  a board that was working has stopped answering
//   amber           boards fine, host has not opened the serial port
//   green           boards fine, host connected
//   white flash     a valid request was handled
//   red flash       a request was answered with an exception
class StatusLed {
public:
  enum class Mode : uint8_t { Boot, NoBoards, Fault, HostAway, Ready };

  StatusLed(int pin, int powerPin);

  void begin();
  void setMode(Mode mode) { mode_ = mode; }
  void flash(uint8_t r, uint8_t g, uint8_t b, uint32_t nowMs, uint32_t durationMs = 15);
  void service(uint32_t nowMs);

private:
  void show(uint8_t r, uint8_t g, uint8_t b);

  int pin_;
  int powerPin_;
  Mode mode_ = Mode::Boot;
  bool flashing_ = false;
  uint32_t flashUntilMs_ = 0;
  uint8_t flashR_ = 0, flashG_ = 0, flashB_ = 0;
  uint8_t lastR_ = 255, lastG_ = 255, lastB_ = 255;
};
