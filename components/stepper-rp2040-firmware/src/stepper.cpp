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

void Stepper::setLogCallback(LogCallback callback) {
  logCallback_ = callback;
}

void Stepper::setServiceCallback(ServiceCallback callback) {
  serviceCallback_ = callback;
}

void Stepper::log(const char *level, const String &message) const {
  if (logCallback_ != nullptr) {
    logCallback_(level, message);
  }
  if (serviceCallback_ != nullptr) {
    serviceCallback_();
  }
}

bool Stepper::begin() {
  log("INFO", "uploading stepper_controller");

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
    tmc_.enableUartMode();
    const int32_t currentConfig = tmc_.readRegister(0x10);
    if (currentConfig >= 0) {
      holdDelay_ = static_cast<uint8_t>((currentConfig >> 16) & 0x0F);
    }
    idlePowerDownDelay_ = kDefaultIdlePowerDownDelay;
    applyCurrentConfig();
    setStallguard(0);
  }

  maxSteps_ = maxHomingRevs_ * fullRev_;
  stallguarded_ = true;
  return true;
}

bool Stepper::microStep(uint8_t mode) {
  if (mode >= (sizeof(kMicrostepMap) / sizeof(kMicrostepMap[0]))) {
    log("ERROR", "wrong parameter for micro_step");
    return false;
  }

  const MicrostepSetting &settings = kMicrostepMap[mode];
  digitalWrite(kMs1Pin, settings.ms1);
  digitalWrite(kMs2Pin, settings.ms2);
  String message = "microstepping set to ";
  message += settings.label;
  log("INFO", message);
  return true;
}

bool Stepper::getFullRev(uint8_t mode, uint32_t &fullRev, uint8_t &sgAdj, uint8_t &serialNode) {
  if (mode >= (sizeof(kMicrostepMap) / sizeof(kMicrostepMap[0]))) {
    log("ERROR", "wrong parameter for full revolution");
    return false;
  }

  const MicrostepSetting &settings = kMicrostepMap[mode];
  fullRev = static_cast<uint32_t>(kStepperSteps / settings.reduction);
  sgAdj = settings.sgAdjustment;
  serialNode = settings.serialPortNodeAddress;
  String message = "full revolution steps=";
  message += fullRev;
  log("INFO", message);
  return true;
}

uint32_t Stepper::clampMoveFrequency(uint32_t requestedFrequency) const {
  if (requestedFrequency == 0) {
    return maxMoveFrequency();
  }
  return constrain(requestedFrequency * sgAdj_, minMoveFrequency(), maxMoveFrequency());
}

uint32_t Stepper::minMoveFrequency() const {
  return 400UL * max<uint8_t>(sgAdj_, 1);
}

uint32_t Stepper::maxMoveFrequency() const {
  return safeMaxMoveFrequency_ * max<uint8_t>(sgAdj_, 1);
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
  const bool currentOk = tmc_.writeRegister(0x10, value);
  const bool idleDelayOk = tmc_.setIdlePowerDownDelay(idlePowerDownDelay_);
  return currentOk && idleDelayOk;
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
  moveCommandedSteps_ = 0;
  movePlannedSteps_ = 0;
  moveFrequency_ = 0;
  moveRampStartFrequency_ = 0;
  moveRampTargetFrequency_ = 0;
  moveRampStartMs_ = 0;
  moveRampDurationMs_ = 0;
  moveStartMs_ = 0;
  moveTimeoutMs_ = 0;
  lastMoveMinStallguard_ = moveMinStallguard_;
  lastMoveLowMargin_ = moveLowMarginWarned_;
  moveLastSgSampleMs_ = 0;
  moveSgWarningThreshold_ = 0;
  moveMinStallguard_ = -1;
  moveLowMarginSamples_ = 0;
  moveLowMarginWarned_ = false;
  resetHomeProbeState();
}

void Stepper::updateMoveProgress() {
  if (!moveInProgress_) {
    return;
  }

  const uint32_t completedSteps = getMoveCompletedSteps();
  if (moveDirectionPositive_) {
    currentPositionSteps_ =
        min<uint32_t>(travelSteps_, moveStartPositionSteps_ + completedSteps);
  } else if (completedSteps >= moveStartPositionSteps_) {
    currentPositionSteps_ = 0;
  } else {
    currentPositionSteps_ = moveStartPositionSteps_ - completedSteps;
  }
}

uint32_t Stepper::getMoveCompletedSteps() {
  int32_t pulseCount = getPulseCount();
  if (pulseCount < 0) {
    pulseCount = 0;
  }

  return min<uint32_t>(static_cast<uint32_t>(pulseCount), movePlannedSteps_);
}

void Stepper::resetHomeProbeState() {
  moveHomeProbeEnabled_ = false;
  moveHomeProbePositiveEnd_ = false;
  moveHomeProbeSamples_ = 0;
  moveHomeProbeSgThreshold_ = 0;
}

uint32_t Stepper::getMaxHomeProbeDeltaSteps() const {
  if (travelSteps_ == 0) {
    return 8;
  }

  return constrain(travelSteps_ / 50UL, 8UL, 250UL);
}

bool Stepper::shouldProbeEndpoint(uint32_t targetStep, uint32_t moveFrequency, uint32_t moveSteps) const {
  if (!calibrated_ || travelSteps_ == 0 || moveSteps == 0) {
    return false;
  }

  if (targetStep != 0 && targetStep != travelSteps_) {
    return false;
  }

  const uint32_t effectiveFrequency = moveFrequency / max<uint8_t>(sgAdj_, 1);
  if (effectiveFrequency < 400) {
    return false;
  }

  const uint32_t moveDurationMs = (moveSteps * 1000UL) / max<uint32_t>(1, moveFrequency);
  return moveDurationMs > kHomeProbeStartupIgnoreMs;
}

bool Stepper::sampleEndpointHomeProbe() {
  if (!moveHomeProbeEnabled_ || !stepperSpinning_) {
    return false;
  }

  if ((millis() - moveStartMs_) < kHomeProbeStartupIgnoreMs) {
    return false;
  }

  const int32_t sg = readStallguard();
  if (sg >= moveHomeProbeSgThreshold_) {
    moveHomeProbeSamples_ = 0;
    return false;
  }

  if (moveHomeProbeSamples_ < 255) {
    ++moveHomeProbeSamples_;
  }

  return moveHomeProbeSamples_ >= kHomeProbeMinSamples;
}

bool Stepper::handleEndpointHomeProbeStall(uint32_t completedSteps) {
  int32_t rawPositionSteps = static_cast<int32_t>(moveStartPositionSteps_);
  if (moveDirectionPositive_) {
    rawPositionSteps += static_cast<int32_t>(completedSteps);
  } else {
    rawPositionSteps -= static_cast<int32_t>(completedSteps);
  }

  const int32_t deltaSteps = moveHomeProbePositiveEnd_
                                 ? rawPositionSteps - static_cast<int32_t>(travelSteps_)
                                 : rawPositionSteps;
  const int32_t absDeltaSteps = abs(deltaSteps);
  const uint32_t maxDeltaSteps = getMaxHomeProbeDeltaSteps();

  currentPositionSteps_ = moveHomeProbePositiveEnd_ ? travelSteps_ : 0;
  lastEndpointProbeValid_ = true;
  lastEndpointProbePositiveEnd_ = moveHomeProbePositiveEnd_;
  lastEndpointDeltaSteps_ = deltaSteps;

  if (!endpointCalibrationAdjustmentEnabled_) {
    String message = "endpoint home measured limit=";
    message += moveHomeProbePositiveEnd_ ? "upper" : "lower";
    message += " delta_steps=";
    message += deltaSteps;
    log(absDeltaSteps == 0 ? "INFO" : "WARN", message);
    pendingHomeCandidateValid_ = false;
    return false;
  }

  if (absDeltaSteps == 0) {
    log("WARN", "endpoint stall detected at expected home; calibration unchanged");
    pendingHomeCandidateValid_ = false;
    return false;
  }

  if (static_cast<uint32_t>(absDeltaSteps) > maxDeltaSteps) {
    String message = "endpoint stall ignored delta_steps=";
    message += deltaSteps;
    message += " expected_home=";
    message += moveHomeProbePositiveEnd_ ? "upper" : "lower";
    log("WARN", message);
    return false;
  }

  return recordEndpointHomeCandidate(moveHomeProbePositiveEnd_, deltaSteps);
}

bool Stepper::recordEndpointHomeCandidate(bool positiveEnd, int32_t deltaSteps) {
  const uint32_t repeatTolerance =
      max<uint32_t>(kMinHomeProbeRepeatToleranceSteps, travelSteps_ / 500UL);

  if (!pendingHomeCandidateValid_ || pendingHomeCandidatePositiveEnd_ != positiveEnd ||
      static_cast<uint32_t>(abs(deltaSteps - pendingHomeCandidateDeltaSteps_)) > repeatTolerance) {
    pendingHomeCandidateValid_ = true;
    pendingHomeCandidatePositiveEnd_ = positiveEnd;
    pendingHomeCandidateDeltaSteps_ = deltaSteps;
    String message = "endpoint home candidate limit=";
    message += positiveEnd ? "upper" : "lower";
    message += " delta_steps=";
    message += deltaSteps;
    message += " waiting_for_repeat=true";
    log("WARN", message);
    return false;
  }

  const int32_t averageDelta = (pendingHomeCandidateDeltaSteps_ + deltaSteps) / 2;
  pendingHomeCandidateValid_ = false;

  int32_t newTravelSteps = static_cast<int32_t>(travelSteps_);
  if (positiveEnd) {
    newTravelSteps += averageDelta;
  } else {
    newTravelSteps -= averageDelta;
  }

  if (newTravelSteps < static_cast<int32_t>(kMinValidTravelSteps)) {
    travelSteps_ = 0;
    currentPositionSteps_ = 0;
    calibrated_ = false;
    log("WARN", "repeated endpoint stalls invalidated calibration; run home again");
    return true;
  }

  travelSteps_ = static_cast<uint32_t>(newTravelSteps);
  currentPositionSteps_ = positiveEnd ? travelSteps_ : 0;
  calibrated_ = true;
  String message = "endpoint home adjusted limit=";
  message += positiveEnd ? "upper" : "lower";
  message += " delta_steps=";
  message += averageDelta;
  message += " travel_steps=";
  message += travelSteps_;
  log("WARN", message);
  return true;
}

void Stepper::configureMoveStallguardMonitor(uint32_t moveFrequency) {
  const int32_t minExpectedSg =
      static_cast<int32_t>(0.15f * static_cast<float>(moveFrequency) / static_cast<float>(max<uint8_t>(sgAdj_, 1)));
  moveSgWarningThreshold_ = static_cast<int32_t>(0.8f * static_cast<float>(minExpectedSg));
  moveMinStallguard_ = -1;
  moveLowMarginSamples_ = 0;
  moveLowMarginWarned_ = false;
  moveLastSgSampleMs_ = 0;
}

void Stepper::sampleMoveStallguardMargin() {
  if (!moveInProgress_ || !stepperSpinning_) {
    return;
  }

  if ((millis() - moveStartMs_) < kHomeProbeStartupIgnoreMs) {
    return;
  }

  if (moveHomeProbeEnabled_ && getMoveCompletedSteps() + kHomeProbeExtraPaddingSteps >= moveCommandedSteps_) {
    return;
  }

  const uint32_t now = millis();
  if (moveLastSgSampleMs_ != 0 && (now - moveLastSgSampleMs_) < kMoveSgSampleIntervalMs) {
    return;
  }
  moveLastSgSampleMs_ = now;

  const int32_t sg = readStallguard();
  if (sg < 0) {
    return;
  }

  if (moveMinStallguard_ < 0 || sg < moveMinStallguard_) {
    moveMinStallguard_ = sg;
  }

  if (sg >= moveSgWarningThreshold_) {
    moveLowMarginSamples_ = 0;
    return;
  }

  if (moveLowMarginSamples_ < 255) {
    ++moveLowMarginSamples_;
  }

  if (!moveLowMarginWarned_ && moveLowMarginSamples_ >= kMoveLowMarginMinSamples) {
    moveLowMarginWarned_ = true;
    String message = "low StallGuard margin during move sg_result=";
    message += sg;
    message += " threshold=";
    message += moveSgWarningThreshold_;
    log("WARN", message);
  }
}

void Stepper::serviceMoveRamp() {
  if (!moveInProgress_ || !stepperSpinning_ || moveRampDurationMs_ == 0 ||
      moveRampStartFrequency_ >= moveRampTargetFrequency_) {
    return;
  }

  const uint32_t elapsedMs = millis() - moveRampStartMs_;
  uint32_t nextFrequency = moveRampTargetFrequency_;
  if (elapsedMs < moveRampDurationMs_) {
    nextFrequency = moveRampStartFrequency_ +
                    ((moveRampTargetFrequency_ - moveRampStartFrequency_) * elapsedMs) / moveRampDurationMs_;
  }

  nextFrequency = constrain(nextFrequency, moveRampStartFrequency_, moveRampTargetFrequency_);
  if (nextFrequency == 0 || nextFrequency == moveFrequency_) {
    return;
  }

  if (!pio_sm_is_tx_fifo_full(pio_, smStep_)) {
    pio_sm_put(pio_, smStep_, getStepperValue(nextFrequency));
    moveFrequency_ = nextFrequency;
  }
}

bool Stepper::waitForBlockingMove(uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  while (millis() - startMs <= timeoutMs) {
    if (serviceCallback_ != nullptr) {
      serviceCallback_();
    }
    const MoveUpdate update = serviceMove();
    if (update == MoveUpdate::Completed || update == MoveUpdate::HomeAdjusted) {
      return true;
    }
    if (update == MoveUpdate::Failed) {
      return false;
    }
    delay(5);
  }

  stopStepper();
  clearMoveState();
  return false;
}

void Stepper::setStallguard(uint8_t threshold) {
  const uint8_t clamped = min<uint8_t>(threshold, 255);
  tmc_.setStallguardThreshold(clamped);
  tmc_.setCoolStepThreshold();

  if (debug_) {
    String message = "setting StallGuard irq_gpio=11 threshold=";
    message += clamped;
    log("DEBUG", message);
  }
}

int32_t Stepper::readStallguard() {
  return tmc_.getStallguardResult();
}

bool Stepper::readTmcRegister(uint8_t reg, uint32_t &value) {
  return tmc_.readRegister(reg, value);
}

bool Stepper::writeTmcRegister(uint8_t reg, uint32_t value, bool verify) {
  return tmc_.writeRegister(reg, value, verify);
}

size_t Stepper::transferTmc(const uint8_t *txData, size_t txLength, uint8_t *rxData, size_t rxMaxLength,
                            uint32_t timeoutMs) {
  return tmc_.transfer(txData, txLength, rxData, rxMaxLength, timeoutMs);
}

bool Stepper::tmcTest() {
  return tmc_.test();
}

bool Stepper::isCalibrated() const {
  return calibrated_;
}

uint32_t Stepper::getMinValidTravelSteps() const {
  return kMinValidTravelSteps;
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

uint8_t Stepper::getRecoveryRunCurrent() const {
  return recoveryRunCurrent_;
}

uint8_t Stepper::getIdleCurrent() const {
  return idleCurrent_;
}

uint8_t Stepper::getIdlePowerDownDelay() const {
  return idlePowerDownDelay_;
}

bool Stepper::setRunCurrent(uint8_t current) {
  runCurrent_ = min<uint8_t>(current, 31);
  if (recoveryRunCurrent_ < runCurrent_) {
    recoveryRunCurrent_ = runCurrent_;
  }
  return applyCurrentConfig();
}

bool Stepper::setRecoveryRunCurrent(uint8_t current) {
  recoveryRunCurrent_ = min<uint8_t>(current, 31);
  return true;
}

bool Stepper::setIdleCurrent(uint8_t current) {
  idleCurrent_ = min<uint8_t>(current, 31);
  return applyCurrentConfig();
}

bool Stepper::setIdlePowerDownDelay(uint8_t delay) {
  idlePowerDownDelay_ = delay;
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
    log("DEBUG", "retracting stepper before first home search");
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
    String message = "homing frequency_hz=";
    message += stepperFrequency;
    message += " sg_threshold=";
    message += sgThreshold;
    log("DEBUG", message);
  }

  const uint32_t startMs = millis();
  uint32_t loops = 0;
  while (millis() - startMs < maxHomingMs) {
    if (serviceCallback_ != nullptr) {
      serviceCallback_();
    }
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
              log("DEBUG", "StallGuard detection via DIAG pin");
            }
            String message = "homing reached loops=";
            message += loops;
            message += " elapsed_ms=";
            message += millis() - startMs;
            log("DEBUG", message);
          }
          return true;
        }
      }
    }
  }

  setStallguard(0);
  stopStepper();
  log("ERROR", "homing failed");
  return false;
}

bool Stepper::centering(uint32_t requestedFrequency) {
  if (moveInProgress_) {
    stopStepper();
  }
  clearMoveState();

  const uint8_t normalRunCurrent = runCurrent_;
  const uint8_t recoveryRunCurrent = max<uint8_t>(normalRunCurrent, recoveryRunCurrent_);
  if (recoveryRunCurrent > normalRunCurrent) {
    setRunCurrent(recoveryRunCurrent);
    String message = "using recovery current during homing current=";
    message += recoveryRunCurrent;
    log("WARN", message);
  }

  const uint32_t stepperFrequency = clampMoveFrequency(requestedFrequency);
  const uint32_t stepperValue = getStepperValue(stepperFrequency);
  const uint32_t startupLoops = 10;

  bool ok = false;
  for (uint8_t attempt = 0; attempt <= kHomeRecoveryRetries; ++attempt) {
    bool stuckDetected = false;
    if (attempt > 0) {
      String message = "retrying homing after recovery jog attempt=";
      message += attempt + 1;
      message += " max_attempts=";
      message += kHomeRecoveryRetries + 1;
      log("WARN", message);
    }

    ok = centeringAttempt(stepperFrequency, stepperValue, startupLoops, stuckDetected);
    if (ok) {
      break;
    }

    if (!stuckDetected || attempt >= kHomeRecoveryRetries) {
      break;
    }

    String message = "running automatic lower-limit recovery jog direction=+ steps=";
    message += kHomeRecoveryJogSteps;
    message += " frequency_hz=";
    message += stepperFrequency;
    log("WARN", message);
    if (!jog(true, kHomeRecoveryJogSteps, stepperFrequency, true)) {
      log("ERROR", "automatic recovery jog failed");
      break;
    }
  }

  setRunCurrent(normalRunCurrent);
  if (!ok) {
    rgbLed_.flashColor("blue", 0.1f, 10, 50);
  }
  return ok;
}

bool Stepper::centeringAttempt(uint32_t stepperFrequency, uint32_t stepperValue, uint32_t startupLoops,
                               bool &stuckDetected) {
  stuckDetected = false;
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
    return false;
  }

  setDirection(false);
  if (!homing(stepperValue, stepperFrequency, startupLoops, retractTimeMs, retractSteps)) {
    stopStepper();
    travelSteps_ = 0;
    currentPositionSteps_ = 0;
    calibrated_ = false;
    return false;
  }

  int32_t stepsRange = getPulseCount();
  if (stepsRange < 0) {
    stepsRange = 0;
  }

  if (stepsRange < static_cast<int32_t>(kMinValidTravelSteps)) {
    stopStepper();
    travelSteps_ = 0;
    currentPositionSteps_ = 0;
    calibrated_ = false;
    stuckDetected = true;
    String message = "measured travel below minimum valid travel measured_steps=";
    message += stepsRange;
    message += " minimum_steps=";
    message += kMinValidTravelSteps;
    message += " valve_may_be_stuck=true";
    log("WARN", message);
    return false;
  }

  const uint32_t halfRange = static_cast<uint32_t>(stepsRange / 2);

  setDirection(true);
  setPulsesToDo(halfRange);
  pio_sm_put_blocking(pio_, smStep_, stepperValue);
  startStepper();

  const uint32_t centeringTimeMs = 100UL + (halfRange * 1000UL) / max<uint32_t>(1, stepperFrequency);
  if (debug_) {
    String message = "centering counted_steps=";
    message += stepsRange;
    message += " half_range_steps=";
    message += halfRange;
    log("DEBUG", message);
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

bool Stepper::jog(bool positiveDirection, uint32_t steps, uint32_t requestedFrequency, bool useRecoveryCurrent) {
  if (steps == 0) {
    return false;
  }

  if (moveInProgress_) {
    stopStepper();
  }
  clearMoveState();

  const uint8_t normalRunCurrent = runCurrent_;
  if (useRecoveryCurrent && recoveryRunCurrent_ > runCurrent_) {
    setRunCurrent(recoveryRunCurrent_);
  }

  const uint32_t moveFrequency = clampMoveFrequency(requestedFrequency);
  const uint32_t stepperValue = getStepperValue(moveFrequency);
  stopStepper();
  setPulseCounter(0);
  setDirection(positiveDirection);
  setPulsesToDo(steps);
  pio_sm_put_blocking(pio_, smStep_, stepperValue);
  startStepper();

  const uint32_t timeoutMs = 250UL + (steps * 1000UL) / max<uint32_t>(1, moveFrequency);
  const uint32_t startMs = millis();
  while (stepperSpinning_ && (millis() - startMs) <= timeoutMs) {
    delay(5);
  }
  const bool completed = !stepperSpinning_;
  stopStepper();

  if (useRecoveryCurrent && recoveryRunCurrent_ > normalRunCurrent) {
    setRunCurrent(normalRunCurrent);
  }

  return completed;
}

Stepper::MoveUpdate Stepper::serviceMove() {
  if (!moveInProgress_) {
    return MoveUpdate::None;
  }

  serviceMoveRamp();
  updateMoveProgress();
  sampleMoveStallguardMargin();
  if (sampleEndpointHomeProbe()) {
    const uint32_t completedSteps = getMoveCompletedSteps();
    stopStepper();
    const bool adjusted = handleEndpointHomeProbeStall(completedSteps);
    lastMoveFrequency_ = moveFrequency_;
    clearMoveState();
    return adjusted ? MoveUpdate::HomeAdjusted : MoveUpdate::Completed;
  }

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
  return moveToPercentAtFrequency(percent, requestedFrequency).accepted;
}

Stepper::MoveCommandResult Stepper::moveToPercentAtFrequency(float percent, uint32_t requestedFrequency) {
  if (!calibrated_ || travelSteps_ == 0) {
    return {};
  }

  const float clampedPercent = constrain(percent, 0.0f, 100.0f);
  uint32_t targetSteps = static_cast<uint32_t>((static_cast<float>(travelSteps_) * clampedPercent / 100.0f) + 0.5f);
  if (targetSteps > travelSteps_) {
    targetSteps = travelSteps_;
  }

  return moveToStepAtFrequency(targetSteps, requestedFrequency);
}

Stepper::MoveCommandResult Stepper::moveToPercentInTime(float percent, uint32_t durationMs) {
  if (!calibrated_ || travelSteps_ == 0) {
    return {};
  }

  const float clampedPercent = constrain(percent, 0.0f, 100.0f);
  uint32_t targetSteps = static_cast<uint32_t>((static_cast<float>(travelSteps_) * clampedPercent / 100.0f) + 0.5f);
  if (targetSteps > travelSteps_) {
    targetSteps = travelSteps_;
  }

  return moveToStepInTime(targetSteps, durationMs);
}

bool Stepper::moveToStep(uint32_t targetStep, uint32_t requestedFrequency) {
  return moveToStepAtFrequency(targetStep, requestedFrequency).accepted;
}

Stepper::MoveCommandResult Stepper::moveToStepInTime(uint32_t targetStep, uint32_t durationMs) {
  MoveCommandResult result;
  result.requestedDurationMs = durationMs;

  if (!calibrated_ || travelSteps_ == 0 || durationMs == 0) {
    return result;
  }

  if (targetStep > travelSteps_) {
    targetStep = travelSteps_;
  }

  if (moveInProgress_) {
    updateMoveProgress();
  }

  const int32_t delta = static_cast<int32_t>(targetStep) - static_cast<int32_t>(currentPositionSteps_);
  const uint32_t moveSteps = static_cast<uint32_t>(abs(delta));
  const uint32_t desiredFrequency = max<uint32_t>(1, (moveSteps * 1000UL + durationMs - 1UL) / durationMs);
  const uint32_t requestedFrequency = (desiredFrequency + max<uint8_t>(sgAdj_, 1) - 1UL) / max<uint8_t>(sgAdj_, 1);
  result = moveToStepAtFrequency(targetStep, requestedFrequency);
  result.requestedDurationMs = durationMs;
  return result;
}

Stepper::MoveCommandResult Stepper::moveToStepAtFrequency(uint32_t targetStep, uint32_t requestedFrequency) {
  MoveCommandResult result;
  result.requestedFrequency = requestedFrequency;

  if (!calibrated_ || travelSteps_ == 0) {
    return result;
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
  result.targetStep = targetStep;
  if (refreshedDelta == 0) {
    result.accepted = true;
    result.completedImmediately = true;
    result.actualFrequency = lastMoveFrequency_;
    return result;
  }

  const uint32_t moveFrequency = clampMoveFrequency(requestedFrequency);
  const uint32_t commandedMoveSteps = static_cast<uint32_t>(abs(refreshedDelta));
  const uint32_t requestedInternalFrequency = requestedFrequency * max<uint8_t>(sgAdj_, 1);
  const bool homeProbeEnabled = shouldProbeEndpoint(targetStep, moveFrequency, commandedMoveSteps);
  const uint32_t homeProbeExtraSteps =
      homeProbeEnabled ? getMaxHomeProbeDeltaSteps() + kHomeProbeExtraPaddingSteps : 0;
  const uint32_t moveSteps = commandedMoveSteps + homeProbeExtraSteps;
  const uint32_t moveTimeMs =
      100UL + moveRampDurationMsSetting_ + (moveSteps * 1000UL) / max<uint32_t>(1, moveFrequency);
  const uint32_t rampStartFrequency = min<uint32_t>(moveFrequency, minMoveFrequency());
  const uint32_t stepperValue = getStepperValue(rampStartFrequency);

  result.accepted = true;
  result.moveSteps = commandedMoveSteps;
  result.actualFrequency = moveFrequency;
  result.estimatedDurationMs = moveTimeMs;
  result.speedClampedHigh = requestedInternalFrequency > maxMoveFrequency();
  result.speedClampedLow = requestedFrequency != 0 && requestedInternalFrequency < minMoveFrequency();

  stopStepper();
  setPulseCounter(0);
  setDirection(refreshedDelta > 0);
  setPulsesToDo(moveSteps);
  pio_sm_put_blocking(pio_, smStep_, stepperValue);
  moveInProgress_ = true;
  moveDirectionPositive_ = refreshedDelta > 0;
  moveStartPositionSteps_ = currentPositionSteps_;
  moveTargetPositionSteps_ = targetStep;
  moveCommandedSteps_ = commandedMoveSteps;
  movePlannedSteps_ = moveSteps;
  moveFrequency_ = rampStartFrequency;
  moveRampStartFrequency_ = rampStartFrequency;
  moveRampTargetFrequency_ = moveFrequency;
  moveRampStartMs_ = millis();
  moveRampDurationMs_ = moveRampDurationMsSetting_;
  moveStartMs_ = millis();
  moveTimeoutMs_ = moveTimeMs;
  moveHomeProbeEnabled_ = homeProbeEnabled;
  moveHomeProbePositiveEnd_ = targetStep == travelSteps_;
  moveHomeProbeSamples_ = 0;
  const int32_t minExpectedSg =
      static_cast<int32_t>(0.15f * static_cast<float>(moveFrequency) / static_cast<float>(max<uint8_t>(sgAdj_, 1)));
  moveHomeProbeSgThreshold_ = static_cast<int32_t>(0.8f * static_cast<float>(minExpectedSg));
  configureMoveStallguardMonitor(moveFrequency);
  startStepper();

  if (debug_) {
    String message = "moving target_step=";
    message += targetStep;
    message += " frequency_hz=";
    message += moveFrequency / max<uint8_t>(sgAdj_, 1);
    message += " planned_steps=";
    message += moveSteps;
    if (homeProbeEnabled) {
      message += " endpoint_probe_extra_steps=";
      message += homeProbeExtraSteps;
    }
    log("DEBUG", message);
  }

  return result;
}

Stepper::SpeedTestResult Stepper::runSpeedTest(uint32_t startFrequency, uint32_t endFrequency, uint32_t stepFrequency,
                                               uint8_t repeats) {
  SpeedTestResult result;
  if (!calibrated_ || travelSteps_ == 0 || startFrequency == 0 || endFrequency == 0 || stepFrequency == 0 ||
      repeats == 0) {
    return result;
  }

  result.accepted = true;
  const uint32_t originalSafeMax = safeMaxMoveFrequency_;
  const uint32_t clampedStart = constrain(startFrequency, getMinMoveFrequency(), kMaxAllowedMoveFrequency);
  const uint32_t clampedEnd = constrain(endFrequency, getMinMoveFrequency(), kMaxAllowedMoveFrequency);
  const bool ascending = clampedEnd >= clampedStart;
  const uint32_t testStep = max<uint32_t>(1, stepFrequency);

  uint32_t frequency = clampedStart;
  bool stop = false;
  while (!stop) {
    if (serviceCallback_ != nullptr) {
      serviceCallback_();
    }
    ++result.testedCount;
    safeMaxMoveFrequency_ = frequency;
    bool passed = true;

    for (uint8_t repeat = 0; repeat < repeats && passed; ++repeat) {
      const uint32_t targetStep = (repeat % 2 == 0) ? travelSteps_ : 0;
      const MoveCommandResult move = moveToStepAtFrequency(targetStep, frequency);
      if (!move.accepted) {
        passed = false;
        break;
      }

      const uint32_t timeoutMs = max<uint32_t>(1000, move.estimatedDurationMs + 750);
      if (!waitForBlockingMove(timeoutMs) || didLastMoveWarnLowMargin()) {
        passed = false;
        break;
      }
    }

    String message = "speed-test frequency_hz=";
    message += frequency;
    message += " passed=";
    message += passed ? "true" : "false";
    if (getLastMoveMinStallguard() >= 0) {
      message += " min_sg=";
      message += getLastMoveMinStallguard();
    }
    log(passed ? "INFO" : "WARN", message);

    if (!passed) {
      result.firstFailedFrequency = frequency;
      break;
    }

    ++result.passedCount;
    result.highestPassedFrequency = frequency;

    if (frequency == clampedEnd) {
      break;
    }

    if (ascending) {
      if (frequency + testStep >= clampedEnd) {
        frequency = clampedEnd;
      } else {
        frequency += testStep;
      }
    } else {
      if (frequency <= clampedEnd + testStep) {
        frequency = clampedEnd;
      } else {
        frequency -= testStep;
      }
    }
    stop = false;
  }

  safeMaxMoveFrequency_ = result.highestPassedFrequency > 0 ? result.highestPassedFrequency : originalSafeMax;
  return result;
}

Stepper::CharacterizeResult Stepper::runCharacterization(uint32_t frequency, uint8_t stallguardThreshold,
                                                         uint32_t cycles, float travelPercent,
                                                         uint32_t endpointToleranceSteps,
                                                         CharacterizeCycleCallback callback, void *callbackContext) {
  CharacterizeResult result;
  result.cyclesRequested = cycles;
  result.frequencyHz = frequency;
  result.stallguardThreshold = stallguardThreshold;

  if (!calibrated_ || travelSteps_ == 0 || frequency == 0 || cycles == 0) {
    return result;
  }

  result.accepted = true;
  const uint32_t originalSafeMax = safeMaxMoveFrequency_;
  const bool originalEndpointAdjustment = endpointCalibrationAdjustmentEnabled_;
  endpointCalibrationAdjustmentEnabled_ = false;
  safeMaxMoveFrequency_ = max<uint32_t>(safeMaxMoveFrequency_, min<uint32_t>(frequency, kMaxAllowedMoveFrequency));
  setStallguard(stallguardThreshold);

  const float clampedPercent = constrain(travelPercent, 1.0f, 100.0f);
  uint32_t targetStep =
      static_cast<uint32_t>((static_cast<float>(travelSteps_) * clampedPercent / 100.0f) + 0.5f);
  targetStep = constrain(targetStep, 1UL, travelSteps_);
  const uint32_t tolerance = endpointToleranceSteps == 0 ? kMinHomeProbeRepeatToleranceSteps : endpointToleranceSteps;

  for (uint32_t cycleIndex = 0; cycleIndex < cycles; ++cycleIndex) {
    if (serviceCallback_ != nullptr) {
      serviceCallback_();
    }
    CharacterizeCycle cycle;
    cycle.cycle = cycleIndex + 1;
    cycle.frequencyHz = frequency;
    cycle.stallguardThreshold = stallguardThreshold;
    cycle.endpointToleranceSteps = tolerance;
    cycle.targetStep = targetStep;

    MoveCommandResult move = moveToStepAtFrequency(targetStep, frequency);
    cycle.moveAccepted = move.accepted;
    if (move.accepted) {
      const uint32_t timeoutMs = max<uint32_t>(1000, move.estimatedDurationMs + 1000);
      cycle.moveCompleted = waitForBlockingMove(timeoutMs);
      cycle.moveMinStallguard = getLastMoveMinStallguard();
      cycle.lowMargin = didLastMoveWarnLowMargin();
    }

    if (cycle.moveCompleted) {
      MoveCommandResult home = moveToStepAtFrequency(0, frequency);
      cycle.homeAccepted = home.accepted;
      if (home.accepted) {
        const uint32_t timeoutMs = max<uint32_t>(1000, home.estimatedDurationMs + 1000);
        cycle.homeCompleted = waitForBlockingMove(timeoutMs);
        cycle.homeMinStallguard = getLastMoveMinStallguard();
        cycle.lowMargin = cycle.lowMargin || didLastMoveWarnLowMargin();
        if (didLastMoveProbeEndpoint()) {
          cycle.endpointDeltaSteps = getLastEndpointDeltaSteps();
        } else {
          cycle.endpointDeltaSteps = static_cast<int32_t>(tolerance) + 1;
        }
      }
    }

    const int32_t moveSg = cycle.moveMinStallguard;
    const int32_t homeSg = cycle.homeMinStallguard;
    int32_t cycleMinSg = -1;
    if (moveSg >= 0 && homeSg >= 0) {
      cycleMinSg = min<int32_t>(moveSg, homeSg);
    } else if (moveSg >= 0) {
      cycleMinSg = moveSg;
    } else {
      cycleMinSg = homeSg;
    }
    if (cycleMinSg >= 0 && (result.minStallguard < 0 || cycleMinSg < result.minStallguard)) {
      result.minStallguard = cycleMinSg;
    }

    cycle.thresholdFailed = cycleMinSg >= 0 && cycleMinSg < static_cast<int32_t>(stallguardThreshold);
    const int32_t absDelta = abs(cycle.endpointDeltaSteps);
    result.maxAbsEndpointDeltaSteps = max<int32_t>(result.maxAbsEndpointDeltaSteps, absDelta);
    if (cycle.lowMargin) {
      ++result.lowMarginFailures;
    }
    if (cycle.thresholdFailed) {
      ++result.thresholdFailures;
    }
    if (static_cast<uint32_t>(absDelta) > tolerance) {
      ++result.endpointFailures;
    }
    if (!cycle.moveCompleted || !cycle.homeCompleted) {
      ++result.motionFailures;
    }

    cycle.passed = cycle.moveAccepted && cycle.moveCompleted && cycle.homeAccepted && cycle.homeCompleted &&
                   !cycle.lowMargin && !cycle.thresholdFailed && static_cast<uint32_t>(absDelta) <= tolerance;
    ++result.cyclesCompleted;
    if (cycle.passed) {
      ++result.cyclesPassed;
    }

    if (callback != nullptr) {
      callback(cycle, callbackContext);
      if (serviceCallback_ != nullptr) {
        serviceCallback_();
      }
    }

    if (!cycle.moveCompleted || !cycle.homeCompleted) {
      break;
    }
  }

  setStallguard(0);
  safeMaxMoveFrequency_ = originalSafeMax;
  endpointCalibrationAdjustmentEnabled_ = originalEndpointAdjustment;
  pendingHomeCandidateValid_ = false;
  return result;
}

bool Stepper::didLastMoveProbeEndpoint() const {
  return lastEndpointProbeValid_;
}

bool Stepper::wasLastEndpointProbeUpper() const {
  return lastEndpointProbePositiveEnd_;
}

int32_t Stepper::getLastEndpointDeltaSteps() const {
  return lastEndpointDeltaSteps_;
}

uint32_t Stepper::getMinMoveFrequency() const {
  return minMoveFrequency() / max<uint8_t>(sgAdj_, 1);
}

uint32_t Stepper::getMaxMoveFrequency() const {
  return maxMoveFrequency() / max<uint8_t>(sgAdj_, 1);
}

uint32_t Stepper::getSafeMaxMoveFrequency() const {
  return safeMaxMoveFrequency_;
}

bool Stepper::setSafeMaxMoveFrequency(uint32_t frequency) {
  if (frequency < getMinMoveFrequency() || frequency > kMaxAllowedMoveFrequency) {
    return false;
  }

  safeMaxMoveFrequency_ = frequency;
  return true;
}

uint32_t Stepper::getMoveRampDurationMs() const {
  return moveRampDurationMsSetting_;
}

bool Stepper::setMoveRampDurationMs(uint32_t durationMs) {
  if (durationMs < kMinMoveRampDurationMs || durationMs > kMaxMoveRampDurationMs) {
    return false;
  }

  moveRampDurationMsSetting_ = durationMs;
  return true;
}

int32_t Stepper::getLastMoveMinStallguard() const {
  return lastMoveMinStallguard_;
}

bool Stepper::didLastMoveWarnLowMargin() const {
  return lastMoveLowMargin_;
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
