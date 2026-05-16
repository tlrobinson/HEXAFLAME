#include "step_engine_timer.h"

#include "../compat.h"

TimerStepEngine::TimerStepEngine(uint32_t stepPin, uint32_t dirPin, TIM_TypeDef *timerInstance)
    : stepPin_(stepPin), dirPin_(dirPin), timerInstance_(timerInstance) {}

bool TimerStepEngine::begin() {
  pinMode(stepPin_, OUTPUT);
  digitalWrite(stepPin_, LOW);
  pinMode(dirPin_, OUTPUT);
  digitalWrite(dirPin_, LOW);

  timer_ = new HardwareTimer(timerInstance_);
  timer_->setOverflow(2000, HERTZ_FORMAT);
  timer_->attachInterrupt([this]() { onTick(); });
  timer_->pause();
  return true;
}

void TimerStepEngine::applyFrequency(uint32_t hz) {
  currentHz_ = hz;
  const uint32_t edgeHz = max<uint32_t>(2, hz * 2UL);
  timer_->setOverflow(edgeHz, HERTZ_FORMAT);
}

void TimerStepEngine::setDirection(bool positive) {
  digitalWrite(dirPin_, positive ? HIGH : LOW);
}

void TimerStepEngine::prepare(uint32_t pulses, uint32_t initialHz) {
  timer_->pause();
  spinning_ = false;
  pulsesDone_ = 0;
  pulsesTarget_ = pulses;
  pinHigh_ = false;
  digitalWrite(stepPin_, LOW);
  applyFrequency(max<uint32_t>(1, initialHz));
}

void TimerStepEngine::start() {
  if (pulsesTarget_ == 0) {
    spinning_ = false;
    return;
  }
  spinning_ = true;
  timer_->refresh();
  timer_->resume();
}

void TimerStepEngine::updateSpeed(uint32_t hz) {
  hz = max<uint32_t>(1, hz);
  if (hz == currentHz_) {
    return;
  }
  applyFrequency(hz);
}

void TimerStepEngine::stop() {
  timer_->pause();
  spinning_ = false;
  pinHigh_ = false;
  digitalWrite(stepPin_, LOW);
}

bool TimerStepEngine::isSpinning() {
  return spinning_;
}

uint32_t TimerStepEngine::completedPulses() {
  return pulsesDone_;
}

void TimerStepEngine::onTick() {
  if (!spinning_) {
    return;
  }

  if (pinHigh_) {
    digitalWrite(stepPin_, LOW);
    pinHigh_ = false;
    return;
  }

  digitalWrite(stepPin_, HIGH);
  pinHigh_ = true;
  const uint32_t done = pulsesDone_ + 1;
  pulsesDone_ = done;
  if (done >= pulsesTarget_) {
    timer_->pause();
    spinning_ = false;
  }
}
