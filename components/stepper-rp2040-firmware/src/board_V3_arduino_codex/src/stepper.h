#pragma once

#include <Arduino.h>
#include <hardware/pio.h>

#include "rgb_led.h"
#include "tmc2209.h"

class Stepper {
public:
  struct MicrostepSetting {
    const char *label;
    float reduction;
    uint8_t ms1;
    uint8_t ms2;
    uint8_t sgAdjustment;
    uint8_t serialPortNodeAddress;
  };

  Stepper(RgbLed &rgbLed, uint32_t maxFrequency = 125000000, bool debug = false);

  bool begin();
  bool tmcTest();
  bool centering(uint32_t requestedFrequency);
  bool moveToPercent(float percent, uint32_t requestedFrequency = 0);
  bool moveToStep(uint32_t targetStep, uint32_t requestedFrequency = 0);
  bool isCalibrated() const;
  uint32_t getTravelSteps() const;
  uint32_t getCurrentPositionSteps() const;
  float getPositionPercent() const;
  uint8_t getRunCurrent() const;
  uint8_t getIdleCurrent() const;
  bool setRunCurrent(uint8_t current);
  bool setIdleCurrent(uint8_t current);
  void stopStepper();
  void setStallguard(uint8_t threshold);
  int32_t readStallguard();

private:
  bool microStep(uint8_t mode);
  bool getFullRev(uint8_t mode, uint32_t &fullRev, uint8_t &sgAdj, uint8_t &serialNode);
  void setDirection(bool clockwise);
  uint32_t clampMoveFrequency(uint32_t requestedFrequency) const;
  uint32_t getStepperValue(uint32_t stepperFrequency) const;
  bool applyCurrentConfig();
  void setPulseCounter(uint32_t pulses);
  int32_t getPulseCount();
  void setPulsesToDo(uint32_t pulses);
  void startStepper();
  bool retract(uint32_t stepperValue, uint32_t startupLoops, uint32_t &retractTimeMs, int32_t &retractSteps);
  bool homing(uint32_t stepperValue, uint32_t stepperFrequency, uint32_t startupLoops, uint32_t retractTimeMs,
              int32_t retractSteps);
  static void execInstructionPair(PIO pio, uint sm, uint instrA, uint instrB);

  static void pioIrqHandler();
  static void diagIsr();

  RgbLed &rgbLed_;
  bool debug_;
  uint32_t maxFrequency_;
  uint32_t frequency_ = 5000000;
  Tmc2209 tmc_;

  uint32_t fullRev_ = 0;
  uint8_t sgAdj_ = 1;
  uint8_t serialNode_ = 0;
  uint32_t maxHomingRevs_ = 5;
  uint32_t maxSteps_ = 0;
  uint32_t lastMoveFrequency_ = 0;
  uint32_t travelSteps_ = 0;
  uint32_t currentPositionSteps_ = 0;
  bool calibrated_ = false;
  uint8_t runCurrent_ = 31;
  uint8_t idleCurrent_ = 0;
  uint8_t holdDelay_ = 8;
  volatile bool stepperSpinning_ = false;
  volatile bool stallguarded_ = false;

  PIO pio_ = pio1;
  uint smStep_ = 0;
  uint smStop_ = 1;
  uint smCount_ = 2;
  uint offsetStep_ = 0;
  uint offsetStop_ = 0;
  uint offsetCount_ = 0;

  static Stepper *instance_;

  static constexpr uint8_t kStepPin = 5;
  static constexpr uint8_t kDirPin = 6;
  static constexpr uint8_t kMs1Pin = 3;
  static constexpr uint8_t kMs2Pin = 4;
  static constexpr uint8_t kEnablePin = 2;
  static constexpr uint8_t kButtonPin = 9;
  static constexpr uint8_t kDiagPin = 11;
  static constexpr uint8_t kUartTxPin = 12;
  static constexpr uint8_t kUartRxPin = 13;
  static constexpr uint32_t kStepperSteps = 200;
  static constexpr uint32_t kPioVar = 2;
  static constexpr uint32_t kPioFix = 37;
};
