#pragma once

#include <Arduino.h>
#include <hardware/pio.h>

#include "../step_engine.h"

// RP2040 step generator. Three PIO state machines cooperate exactly as in the
// original single-channel firmware: one emits a programmed number of STEP
// pulses, one counts down the remaining pulses and raises an IRQ when the
// burst finishes, and one counts emitted pulses for progress reporting.
//
// The RP2040 board carries a single channel, so a single PIO instance and a
// single static back-pointer (for the IRQ handler) are sufficient.
class PioStepEngine : public StepEngine {
public:
  PioStepEngine(uint8_t stepPin, uint8_t dirPin);

  bool begin() override;
  void setDirection(bool positive) override;
  void prepare(uint32_t pulses, uint32_t initialHz) override;
  void start() override;
  void updateSpeed(uint32_t hz) override;
  void stop() override;
  bool isSpinning() override;
  uint32_t completedPulses() override;

private:
  uint32_t hzToPioValue(uint32_t hz) const;
  void setPulseCounter(uint32_t pulses);
  int32_t getPulseCount();
  void setPulsesToDo(uint32_t pulses);
  static void execInstructionPair(PIO pio, uint sm, uint instrA, uint instrB);
  static void pioIrqHandler();

  uint8_t stepPin_;
  uint8_t dirPin_;
  volatile bool spinning_ = false;

  PIO pio_ = pio1;
  uint smStep_ = 0;
  uint smStop_ = 1;
  uint smCount_ = 2;
  uint offsetStep_ = 0;
  uint offsetStop_ = 0;
  uint offsetCount_ = 0;

  uint32_t pioClockHz_ = 5000000;
  uint32_t sampleClockHz_ = 125000000;

  static PioStepEngine *instance_;

  static constexpr uint32_t kPioVar = 2;
  static constexpr uint32_t kPioFix = 37;
};
