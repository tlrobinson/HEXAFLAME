#pragma once

#include <Arduino.h>
#include <HardwareTimer.h>

#include "../step_engine.h"

// STM32 step generator. The Octopus has no PIO, so each channel owns a
// dedicated hardware timer whose update interrupt toggles the STEP pin. The
// ISR runs at twice the requested step frequency (one edge per tick) and
// stops itself once the programmed pulse count has been emitted, giving the
// same CPU-independent cadence the RP2040 gets from the PIO. Eight channels
// therefore step simultaneously and independently.
class TimerStepEngine : public StepEngine {
public:
  TimerStepEngine(uint32_t stepPin, uint32_t dirPin, TIM_TypeDef *timerInstance);

  bool begin() override;
  void setDirection(bool positive) override;
  void prepare(uint32_t pulses, uint32_t initialHz) override;
  void start() override;
  void updateSpeed(uint32_t hz) override;
  void stop() override;
  bool isSpinning() override;
  uint32_t completedPulses() override;

private:
  void onTick();
  void applyFrequency(uint32_t hz);

  uint32_t stepPin_;
  uint32_t dirPin_;
  TIM_TypeDef *timerInstance_;
  HardwareTimer *timer_ = nullptr;

  volatile bool spinning_ = false;
  volatile bool pinHigh_ = false;
  volatile uint32_t pulsesTarget_ = 0;
  volatile uint32_t pulsesDone_ = 0;
  uint32_t currentHz_ = 0;
};
