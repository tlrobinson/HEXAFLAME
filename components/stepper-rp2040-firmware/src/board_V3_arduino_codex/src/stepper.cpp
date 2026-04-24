#include "stepper.h"

#include <hardware/clocks.h>
#include <hardware/irq.h>

namespace {
constexpr Stepper::MicrostepSetting kMicrostepMap[] = {
    {"1/8", 0.125f, 0, 0, 1, 0},
    {"1/16", 0.0625f, 1, 1, 2, 3},
    {"1/32", 0.03125f, 1, 0, 4, 1},
    {"1/64", 0.015625f, 0, 1, 8, 2},
};
constexpr bool kUseDiagForHoming = false;

const uint16_t kStepProgramInstructions[] = {
    static_cast<uint16_t>(pio_encode_pull(false, false)),
    static_cast<uint16_t>(pio_encode_mov(pio_x, pio_osr)),
    static_cast<uint16_t>(pio_encode_mov(pio_y, pio_x)),
    static_cast<uint16_t>(pio_encode_set(pio_pins, 1) | pio_encode_delay(15)),
    static_cast<uint16_t>(pio_encode_set(pio_pins, 1) | pio_encode_delay(15)),
    static_cast<uint16_t>(pio_encode_set(pio_pins, 0)),
    static_cast<uint16_t>(pio_encode_jmp_y_dec(5)),
    static_cast<uint16_t>(pio_encode_jmp(0)),
};

const pio_program kStepProgram = {
    .instructions = kStepProgramInstructions,
    .length = sizeof(kStepProgramInstructions) / sizeof(kStepProgramInstructions[0]),
    .origin = -1,
    .pio_version = 0,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x0,
#endif
};

const uint16_t kStopProgramInstructions[] = {
    static_cast<uint16_t>(pio_encode_wait_pin(true, 0)),
    static_cast<uint16_t>(pio_encode_wait_pin(false, 0)),
    static_cast<uint16_t>(pio_encode_jmp_x_dec(0)),
    static_cast<uint16_t>(pio_encode_irq_wait(true, 0)),
};

const pio_program kStopProgram = {
    .instructions = kStopProgramInstructions,
    .length = sizeof(kStopProgramInstructions) / sizeof(kStopProgramInstructions[0]),
    .origin = -1,
    .pio_version = 0,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x0,
#endif
};

const uint16_t kCountProgramInstructions[] = {
    static_cast<uint16_t>(pio_encode_wait_pin(false, 0)),
    static_cast<uint16_t>(pio_encode_wait_pin(true, 0)),
    static_cast<uint16_t>(pio_encode_jmp_x_dec(0)),
};

const pio_program kCountProgram = {
    .instructions = kCountProgramInstructions,
    .length = sizeof(kCountProgramInstructions) / sizeof(kCountProgramInstructions[0]),
    .origin = -1,
    .pio_version = 0,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x0,
#endif
};
}

Stepper *Stepper::instance_ = nullptr;

Stepper::Stepper(RgbLed &rgbLed, uint32_t maxFrequency, bool debug)
    : rgbLed_(rgbLed), debug_(debug), maxFrequency_(maxFrequency), tmc_(Serial1, kUartRxPin, kUartTxPin, 0, 230400) {}

bool Stepper::begin() {
  Serial.println();
  Serial.println("Uploading stepper_controller ...");

  instance_ = this;

  pinMode(kStepPin, OUTPUT);
  digitalWrite(kStepPin, LOW);
  pinMode(kDirPin, OUTPUT);
  pinMode(kMs1Pin, OUTPUT);
  pinMode(kMs2Pin, OUTPUT);
  pinMode(kDiagPin, INPUT_PULLDOWN);

  offsetStep_ = pio_add_program(pio_, &kStepProgram);
  offsetStop_ = pio_add_program(pio_, &kStopProgram);
  offsetCount_ = pio_add_program(pio_, &kCountProgram);

  pio_gpio_init(pio_, kStepPin);
  pio_sm_set_consecutive_pindirs(pio_, smStep_, kStepPin, 1, true);

  {
    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offsetStep_, offsetStep_ + kStepProgram.length - 1);
    sm_config_set_set_pins(&config, kStepPin, 1);
    sm_config_set_clkdiv(&config, static_cast<float>(clock_get_hz(clk_sys)) / static_cast<float>(frequency_));
    pio_sm_init(pio_, smStep_, offsetStep_, &config);
    pio_sm_put_blocking(pio_, smStep_, 65535);
    pio_sm_set_enabled(pio_, smStep_, false);
  }

  {
    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offsetStop_, offsetStop_ + kStopProgram.length - 1);
    sm_config_set_in_pins(&config, kStepPin);
    sm_config_set_clkdiv(&config, static_cast<float>(clock_get_hz(clk_sys)) / static_cast<float>(maxFrequency_));
    pio_sm_init(pio_, smStop_, offsetStop_, &config);
    pio_sm_set_enabled(pio_, smStop_, false);
  }

  {
    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offsetCount_, offsetCount_ + kCountProgram.length - 1);
    sm_config_set_in_pins(&config, kStepPin);
    sm_config_set_clkdiv(&config, static_cast<float>(clock_get_hz(clk_sys)) / static_cast<float>(maxFrequency_));
    pio_sm_init(pio_, smCount_, offsetCount_, &config);
    pio_sm_set_enabled(pio_, smCount_, true);
    setPulseCounter(0);
  }

  pio_set_irq0_source_enabled(pio_, pis_interrupt1, true);
  irq_set_exclusive_handler(PIO1_IRQ_0, pioIrqHandler);
  irq_set_enabled(PIO1_IRQ_0, true);

  attachInterrupt(digitalPinToInterrupt(kDiagPin), diagIsr, RISING);

  if (!microStep(0)) {
    return false;
  }

  if (!getFullRev(0, fullRev_, sgAdj_, serialNode_)) {
    return false;
  }

  tmc_.begin();
  tmc_.setMotorId(serialNode_);

  if (tmc_.test()) {
    const int32_t currentConfig = tmc_.readRegister(0x10);
    if (currentConfig >= 0) {
      holdDelay_ = static_cast<uint8_t>((currentConfig >> 16) & 0x0F);
    }
    applyCurrentConfig();
    setStallguard(0);
  }

  maxSteps_ = maxHomingRevs_ * fullRev_;
  stallguarded_ = true;
  return true;
}

bool Stepper::microStep(uint8_t mode) {
  if (mode >= (sizeof(kMicrostepMap) / sizeof(kMicrostepMap[0]))) {
    Serial.println("Wrong parameter for micro_step");
    return false;
  }

  const MicrostepSetting &settings = kMicrostepMap[mode];
  digitalWrite(kMs1Pin, settings.ms1);
  digitalWrite(kMs2Pin, settings.ms2);
  Serial.print("Microstepping set to ");
  Serial.println(settings.label);
  return true;
}

bool Stepper::getFullRev(uint8_t mode, uint32_t &fullRev, uint8_t &sgAdj, uint8_t &serialNode) {
  if (mode >= (sizeof(kMicrostepMap) / sizeof(kMicrostepMap[0]))) {
    Serial.println("Wrong parameter for full revolution");
    return false;
  }

  const MicrostepSetting &settings = kMicrostepMap[mode];
  fullRev = static_cast<uint32_t>(kStepperSteps / settings.reduction);
  sgAdj = settings.sgAdjustment;
  serialNode = settings.serialPortNodeAddress;
  Serial.print("Full revolution takes ");
  Serial.print(fullRev);
  Serial.println(" steps");
  return true;
}

uint32_t Stepper::clampMoveFrequency(uint32_t requestedFrequency) const {
  const uint32_t minFrequency = 400UL * sgAdj_;
  const uint32_t maxFrequency = 1200UL * sgAdj_;
  if (requestedFrequency == 0) {
    return maxFrequency;
  }
  return constrain(requestedFrequency * sgAdj_, minFrequency, maxFrequency);
}

void Stepper::setDirection(bool clockwise) {
  digitalWrite(kDirPin, clockwise ? HIGH : LOW);
}

uint32_t Stepper::getStepperValue(uint32_t stepperFrequency) const {
  if (stepperFrequency == 0) {
    return 0;
  }

  int64_t value = (static_cast<int64_t>(frequency_) - static_cast<int64_t>(stepperFrequency) * static_cast<int64_t>(kPioFix)) /
                  (static_cast<int64_t>(stepperFrequency) * static_cast<int64_t>(kPioVar));
  if (value < 0) {
    value = 0;
  }
  return static_cast<uint32_t>(value);
}

bool Stepper::applyCurrentConfig() {
  const uint32_t value = (static_cast<uint32_t>(holdDelay_ & 0x0F) << 16) |
                         (static_cast<uint32_t>(runCurrent_ & 0x1F) << 8) |
                         static_cast<uint32_t>(idleCurrent_ & 0x1F);
  return tmc_.writeRegister(0x10, value);
}

void Stepper::execInstructionPair(PIO pio, uint sm, uint instrA, uint instrB) {
  pio_sm_exec_wait_blocking(pio, sm, instrA);
  pio_sm_exec_wait_blocking(pio, sm, instrB);
}

void Stepper::setPulseCounter(uint32_t pulses) {
  pio_sm_put_blocking(pio_, smCount_, pulses);
  execInstructionPair(pio_, smCount_, pio_encode_pull(false, false), pio_encode_mov(pio_x, pio_osr));
}

int32_t Stepper::getPulseCount() {
  execInstructionPair(pio_, smCount_, pio_encode_mov(pio_isr, pio_x), pio_encode_push(false, false));
  if (pio_sm_is_rx_fifo_empty(pio_, smCount_)) {
    return -1;
  }

  return static_cast<int32_t>(-static_cast<int32_t>(pio_sm_get(pio_, smCount_)));
}

void Stepper::setPulsesToDo(uint32_t pulses) {
  pio_sm_set_enabled(pio_, smStop_, true);
  pio_sm_put_blocking(pio_, smStop_, pulses);
  execInstructionPair(pio_, smStop_, pio_encode_pull(false, false), pio_encode_mov(pio_x, pio_osr));
}

void Stepper::startStepper() {
  stepperSpinning_ = true;
  pio_sm_set_enabled(pio_, smStep_, true);
}

void Stepper::stopStepper() {
  pio_sm_set_enabled(pio_, smStep_, false);
  stepperSpinning_ = false;
}

void Stepper::clearMoveState() {
  moveInProgress_ = false;
  moveDirectionPositive_ = true;
  moveStartPositionSteps_ = currentPositionSteps_;
  moveTargetPositionSteps_ = currentPositionSteps_;
  movePlannedSteps_ = 0;
  moveFrequency_ = 0;
  moveStartMs_ = 0;
  moveTimeoutMs_ = 0;
}

void Stepper::updateMoveProgress() {
  if (!moveInProgress_) {
    return;
  }

  int32_t pulseCount = getPulseCount();
  if (pulseCount < 0) {
    pulseCount = 0;
  }

  const uint32_t completedSteps =
      min<uint32_t>(static_cast<uint32_t>(pulseCount), movePlannedSteps_);
  if (moveDirectionPositive_) {
    currentPositionSteps_ =
        min<uint32_t>(travelSteps_, moveStartPositionSteps_ + completedSteps);
  } else if (completedSteps >= moveStartPositionSteps_) {
    currentPositionSteps_ = 0;
  } else {
    currentPositionSteps_ = moveStartPositionSteps_ - completedSteps;
  }
}

void Stepper::setStallguard(uint8_t threshold) {
  const uint8_t clamped = min<uint8_t>(threshold, 255);
  tmc_.setStallguardThreshold(clamped);
  tmc_.setCoolStepThreshold();

  if (debug_) {
    Serial.print("Setting StallGuard (irq to GPIO 11) to ");
    Serial.println(clamped);
  }
}

int32_t Stepper::readStallguard() {
  return tmc_.getStallguardResult();
}

bool Stepper::tmcTest() {
  return tmc_.test();
}

bool Stepper::isCalibrated() const {
  return calibrated_;
}

bool Stepper::isMoveInProgress() const {
  return moveInProgress_;
}

uint32_t Stepper::getTravelSteps() const {
  return travelSteps_;
}

uint32_t Stepper::getCurrentPositionSteps() const {
  return currentPositionSteps_;
}

float Stepper::getPositionPercent() const {
  if (!calibrated_ || travelSteps_ == 0) {
    return 0.0f;
  }

  return (100.0f * static_cast<float>(currentPositionSteps_)) / static_cast<float>(travelSteps_);
}

uint8_t Stepper::getRunCurrent() const {
  return runCurrent_;
}

uint8_t Stepper::getIdleCurrent() const {
  return idleCurrent_;
}

bool Stepper::setRunCurrent(uint8_t current) {
  runCurrent_ = min<uint8_t>(current, 31);
  return applyCurrentConfig();
}

bool Stepper::setIdleCurrent(uint8_t current) {
  idleCurrent_ = min<uint8_t>(current, 31);
  return applyCurrentConfig();
}

bool Stepper::retract(uint32_t stepperValue, uint32_t startupLoops, uint32_t &retractTimeMs, int32_t &retractSteps) {
  setStallguard(0);
  stopStepper();
  setPulseCounter(0);
  setPulsesToDo(maxSteps_);
  pio_sm_put_blocking(pio_, smStep_, stepperValue);
  startStepper();

  if (debug_) {
    Serial.println("Retract the stepper prior the first home search");
  }

  const uint32_t startMs = millis();
  for (uint32_t i = 0; i < startupLoops; ++i) {
    (void)readStallguard();
  }

  stopStepper();
  retractTimeMs = millis() - startMs;
  retractSteps = getPulseCount();
  return true;
}

bool Stepper::homing(uint32_t stepperValue, uint32_t stepperFrequency, uint32_t startupLoops, uint32_t retractTimeMs,
                     int32_t retractSteps) {
  setStallguard(0);
  const uint32_t maxHomingMs = retractTimeMs + (maxHomingRevs_ * 1000UL * fullRev_) / max<uint32_t>(1, stepperFrequency);
  bool doOnce = true;

  stallguarded_ = false;
  stopStepper();
  setPulseCounter(0);
  setPulsesToDo(static_cast<uint32_t>(max<int32_t>(0, retractSteps)) + maxSteps_);
  pio_sm_put_blocking(pio_, smStep_, stepperValue);
  startStepper();

  const int32_t minExpectedSg = static_cast<int32_t>(0.15f * static_cast<float>(stepperFrequency) / static_cast<float>(sgAdj_));
  const int32_t sgThreshold = static_cast<int32_t>(0.8f * static_cast<float>(minExpectedSg));
  const int32_t sgThresholdDiag = static_cast<int32_t>(0.45f * static_cast<float>(minExpectedSg));

  if (debug_) {
    Serial.print("Homing with stepper speed of ");
    Serial.print(stepperFrequency);
    Serial.print("Hz and UART StallGuard threshold of ");
    Serial.println(sgThreshold);
  }

  const uint32_t startMs = millis();
  uint32_t loops = 0;
  while (millis() - startMs < maxHomingMs) {
    const int32_t sg = readStallguard();
    ++loops;

    if (loops > startupLoops) {
      if (doOnce && kUseDiagForHoming) {
        setStallguard(static_cast<uint8_t>(max<int32_t>(0, min<int32_t>(255, sgThresholdDiag))));
        doOnce = false;
      } else if (doOnce) {
        doOnce = false;
      }

      bool diagTriggered = false;
      if (kUseDiagForHoming && stallguarded_) {
        const bool diagHigh = digitalRead(kDiagPin) == HIGH;
        const bool diagMatchesSg = sg < (sgThresholdDiag * 2);
        diagTriggered = diagHigh && diagMatchesSg;

        if (!diagTriggered) {
          stallguarded_ = false;
        }
      }

      if (sg < sgThreshold || diagTriggered) {
        stopStepper();

        if (getPulseCount() < static_cast<int32_t>(0.95f * static_cast<float>(max<int32_t>(0, retractSteps) + maxSteps_))) {
          if (diagTriggered) {
            rgbLed_.flashColor("red", 0.8f, 1, 10);
          } else {
            rgbLed_.flashColor("red", 0.1f, 3, 50);
          }

          if (debug_) {
            if (diagTriggered) {
              Serial.println("StallGuard detection via DIAG pin");
            }
            Serial.print("Homing reached after ");
            Serial.print(loops);
            Serial.print(" iterations in ");
            Serial.print(millis() - startMs);
            Serial.println(" ms");
          }
          return true;
        }
      }
    }
  }

  setStallguard(0);
  stopStepper();
  Serial.println("Failed homing");
  return false;
}

bool Stepper::centering(uint32_t requestedFrequency) {
  if (moveInProgress_) {
    stopStepper();
  }
  clearMoveState();
  const uint32_t stepperFrequency = clampMoveFrequency(requestedFrequency);
  const uint32_t stepperValue = getStepperValue(stepperFrequency);
  const uint32_t startupLoops = 10;

  uint32_t retractTimeMs = 0;
  int32_t retractSteps = 0;

  setDirection(false);
  retract(stepperValue, startupLoops, retractTimeMs, retractSteps);

  setDirection(true);
  if (!homing(stepperValue, stepperFrequency, startupLoops, retractTimeMs, retractSteps)) {
    stopStepper();
    travelSteps_ = 0;
    currentPositionSteps_ = 0;
    calibrated_ = false;
    rgbLed_.flashColor("blue", 0.1f, 10, 50);
    return false;
  }

  setDirection(false);
  if (!homing(stepperValue, stepperFrequency, startupLoops, retractTimeMs, retractSteps)) {
    stopStepper();
    travelSteps_ = 0;
    currentPositionSteps_ = 0;
    calibrated_ = false;
    rgbLed_.flashColor("blue", 0.1f, 10, 50);
    return false;
  }

  int32_t stepsRange = getPulseCount();
  if (stepsRange < 0) {
    stepsRange = 0;
  }
  const uint32_t halfRange = static_cast<uint32_t>(stepsRange / 2);

  setDirection(true);
  setPulsesToDo(halfRange);
  pio_sm_put_blocking(pio_, smStep_, stepperValue);
  startStepper();

  const uint32_t centeringTimeMs = 100UL + (halfRange * 1000UL) / max<uint32_t>(1, stepperFrequency);
  if (debug_) {
    Serial.print("Counted ");
    Serial.print(stepsRange);
    Serial.println(" steps between homes");
    Serial.print("Positioning the stepper at ");
    Serial.print(halfRange);
    Serial.println(" from the last detected home");
  }

  delay(centeringTimeMs);
  if (!stepperSpinning_) {
    travelSteps_ = static_cast<uint32_t>(stepsRange);
    currentPositionSteps_ = halfRange;
    lastMoveFrequency_ = stepperFrequency;
    calibrated_ = true;
    rgbLed_.flashColor("green", 0.2f, 3, 50);
    return true;
  }

  stopStepper();
  calibrated_ = false;
  return false;
}

Stepper::MoveUpdate Stepper::serviceMove() {
  if (!moveInProgress_) {
    return MoveUpdate::None;
  }

  updateMoveProgress();
  if (!stepperSpinning_) {
    currentPositionSteps_ = moveTargetPositionSteps_;
    lastMoveFrequency_ = moveFrequency_;
    clearMoveState();
    return MoveUpdate::Completed;
  }

  if ((millis() - moveStartMs_) <= moveTimeoutMs_) {
    return MoveUpdate::None;
  }

  stopStepper();
  travelSteps_ = 0;
  currentPositionSteps_ = 0;
  calibrated_ = false;
  clearMoveState();
  return MoveUpdate::Failed;
}

bool Stepper::moveToPercent(float percent, uint32_t requestedFrequency) {
  if (!calibrated_ || travelSteps_ == 0) {
    return false;
  }

  const float clampedPercent = constrain(percent, 0.0f, 100.0f);
  uint32_t targetSteps = static_cast<uint32_t>((static_cast<float>(travelSteps_) * clampedPercent / 100.0f) + 0.5f);
  if (targetSteps > travelSteps_) {
    targetSteps = travelSteps_;
  }

  return moveToStep(targetSteps, requestedFrequency);
}

bool Stepper::moveToStep(uint32_t targetStep, uint32_t requestedFrequency) {
  if (!calibrated_ || travelSteps_ == 0) {
    return false;
  }

  if (targetStep > travelSteps_) {
    targetStep = travelSteps_;
  }

  if (moveInProgress_) {
    updateMoveProgress();
    stopStepper();
    clearMoveState();
  }

  const int32_t refreshedDelta = static_cast<int32_t>(targetStep) - static_cast<int32_t>(currentPositionSteps_);
  if (refreshedDelta == 0) {
    return true;
  }

  const uint32_t moveFrequency = clampMoveFrequency(requestedFrequency);
  const uint32_t stepperValue = getStepperValue(moveFrequency);
  const uint32_t moveSteps = static_cast<uint32_t>(abs(refreshedDelta));
  const uint32_t moveTimeMs = 100UL + (moveSteps * 1000UL) / max<uint32_t>(1, moveFrequency);

  stopStepper();
  setPulseCounter(0);
  setDirection(refreshedDelta > 0);
  setPulsesToDo(moveSteps);
  pio_sm_put_blocking(pio_, smStep_, stepperValue);
  moveInProgress_ = true;
  moveDirectionPositive_ = refreshedDelta > 0;
  moveStartPositionSteps_ = currentPositionSteps_;
  moveTargetPositionSteps_ = targetStep;
  movePlannedSteps_ = moveSteps;
  moveFrequency_ = moveFrequency;
  moveStartMs_ = millis();
  moveTimeoutMs_ = moveTimeMs;
  startStepper();

  if (debug_) {
    Serial.print("Moving to step ");
    Serial.print(targetStep);
    Serial.print(" at ");
    Serial.print(moveFrequency / max<uint8_t>(sgAdj_, 1));
    Serial.print("Hz (");
    Serial.print(moveSteps);
    Serial.println(" steps)");
  }

  return true;
}

void Stepper::pioIrqHandler() {
  if (instance_ == nullptr) {
    return;
  }

  if ((pio1->irq & (1u << 1)) == 0) {
    return;
  }

  pio1->irq = (1u << 1);
  pio_sm_set_enabled(pio1, instance_->smStep_, false);
  instance_->stepperSpinning_ = false;
}

void Stepper::diagIsr() {
  if (instance_ == nullptr) {
    return;
  }

  instance_->stallguarded_ = true;
}
