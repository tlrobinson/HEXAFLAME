#include "stepper.h"

#include "compat.h"

// Microstepping table shared by every board. The MS1/MS2 columns are the
// strap levels used on the RP2040 (where those pins also pick the TMC2209's
// UART address); `mres` is the CHOPCONF code used when the Octopus programs
// microstepping over UART instead.
const MicrostepSetting kMicrostepMap[] = {
    {"1/8", 0.125f, 0, 0, 1, 5},
    {"1/16", 0.0625f, 1, 1, 2, 4},
    {"1/32", 0.03125f, 1, 0, 4, 3},
    {"1/64", 0.015625f, 0, 1, 8, 2},
};
const size_t kMicrostepMapCount = sizeof(kMicrostepMap) / sizeof(kMicrostepMap[0]);

Stepper::Stepper(uint8_t channelIndex, ChannelConfig &config, StatusLed &statusLed, bool debug)
    : channelIndex_(channelIndex),
      config_(config),
      statusLed_(statusLed),
      engine_(*config.engine),
      tmc_(*config.tmc),
      debug_(debug) {}

void Stepper::setLogCallback(LogCallback callback) {
  logCallback_ = callback;
}

void Stepper::setServiceCallback(ServiceCallback callback) {
  serviceCallback_ = callback;
}

void Stepper::log(const char *level, const String &message) const {
  if (logCallback_ != nullptr) {
    logCallback_(channelIndex_, level, message);
  }
  if (serviceCallback_ != nullptr) {
    serviceCallback_();
  }
}

bool Stepper::applyMicrostep() {
  uint8_t mode = config_.microstepMode;
  if (mode >= kMicrostepMapCount) {
    log("ERROR", "invalid microstep mode");
    mode = 0;
  }

  const MicrostepSetting &settings = kMicrostepMap[mode];
  if (!config_.microstepViaUart) {
    if (config_.ms1Pin >= 0) {
      digitalWrite(config_.ms1Pin, settings.ms1);
    }
    if (config_.ms2Pin >= 0) {
      digitalWrite(config_.ms2Pin, settings.ms2);
    }
  }

  fullRev_ = static_cast<uint32_t>(kStepperSteps / settings.reduction);
  sgAdj_ = settings.sgAdjustment;

  String message = "microstepping=";
  message += settings.label;
  message += " full_revolution_steps=";
  message += fullRev_;
  log("INFO", message);
  return true;
}

bool Stepper::begin() {
  String startup = "initializing channel ";
  startup += channelIndex_;
  if (config_.name != nullptr) {
    startup += " (";
    startup += config_.name;
    startup += ")";
  }
  log("INFO", startup);

  if (config_.ms1Pin >= 0) {
    pinMode(config_.ms1Pin, OUTPUT);
  }
  if (config_.ms2Pin >= 0) {
    pinMode(config_.ms2Pin, OUTPUT);
  }
  if (config_.enablePin >= 0) {
    pinMode(config_.enablePin, OUTPUT);
    digitalWrite(config_.enablePin, LOW);  // active LOW: driver enabled
  }
  if (config_.diagPin >= 0) {
    pinMode(config_.diagPin, INPUT_PULLDOWN);
  }

  applyMicrostep();

  if (!engine_.begin()) {
    log("ERROR", "step engine init failed");
    return false;
  }

  tmc_.begin();
  tmc_.setAddress(config_.tmcAddress);

  if (tmc_.test()) {
    tmc_.enableUartMode(config_.microstepViaUart);
    if (config_.microstepViaUart) {
      const uint8_t mode = config_.microstepMode < kMicrostepMapCount ? config_.microstepMode : 0;
      tmc_.setMicrostepResolution(kMicrostepMap[mode].mres);
    }
    const int32_t currentConfig = tmc_.readRegister(0x10);
    if (currentConfig >= 0) {
      holdDelay_ = static_cast<uint8_t>((currentConfig >> 16) & 0x0F);
    }
    idlePowerDownDelay_ = kDefaultIdlePowerDownDelay;
    applyCurrentConfig();
    setStallguard(0);
  }

  maxSteps_ = maxHomingRevs_ * fullRev_;
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

bool Stepper::applyCurrentConfig() {
  const uint32_t value = (static_cast<uint32_t>(holdDelay_ & 0x0F) << 16) |
                         (static_cast<uint32_t>(runCurrent_ & 0x1F) << 8) |
                         static_cast<uint32_t>(idleCurrent_ & 0x1F);
  const bool currentOk = tmc_.writeRegister(0x10, value);
  const bool idleDelayOk = tmc_.setIdlePowerDownDelay(idlePowerDownDelay_);
  return currentOk && idleDelayOk;
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
    currentPositionSteps_ = min<uint32_t>(travelSteps_, moveStartPositionSteps_ + completedSteps);
  } else if (completedSteps >= moveStartPositionSteps_) {
    currentPositionSteps_ = 0;
  } else {
    currentPositionSteps_ = moveStartPositionSteps_ - completedSteps;
  }
}

uint32_t Stepper::getMoveCompletedSteps() {
  const uint32_t completed = engine_.completedPulses();
  return min<uint32_t>(completed, movePlannedSteps_);
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
  if (!moveHomeProbeEnabled_ || !engine_.isSpinning()) {
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
  if (!moveInProgress_ || !engine_.isSpinning()) {
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
  if (!moveInProgress_ || !engine_.isSpinning() || moveRampDurationMs_ == 0 ||
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

  engine_.updateSpeed(nextFrequency);
  moveFrequency_ = nextFrequency;
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

  engine_.stop();
  clearMoveState();
  return false;
}

void Stepper::setStallguard(uint8_t threshold) {
  tmc_.setStallguardThreshold(threshold);
  tmc_.setCoolStepThreshold();

  if (debug_) {
    String message = "setting StallGuard threshold=";
    message += threshold;
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

void Stepper::stopStepper() {
  engine_.stop();
}

bool Stepper::retract(uint32_t stepperFrequency, uint32_t startupLoops, uint32_t &retractTimeMs,
                      int32_t &retractSteps) {
  setStallguard(0);
  engine_.stop();
  engine_.prepare(maxSteps_, stepperFrequency);
  engine_.start();

  if (debug_) {
    log("DEBUG", "retracting stepper before first home search");
  }

  const uint32_t startMs = millis();
  for (uint32_t i = 0; i < startupLoops; ++i) {
    (void)readStallguard();
  }

  engine_.stop();
  retractTimeMs = millis() - startMs;
  retractSteps = static_cast<int32_t>(engine_.completedPulses());
  return true;
}

bool Stepper::homing(uint32_t stepperFrequency, uint32_t startupLoops, uint32_t retractTimeMs,
                     int32_t retractSteps) {
  setStallguard(0);
  const uint32_t maxHomingMs =
      retractTimeMs + (maxHomingRevs_ * 1000UL * fullRev_) / max<uint32_t>(1, stepperFrequency);

  const uint32_t plannedSteps = static_cast<uint32_t>(max<int32_t>(0, retractSteps)) + maxSteps_;
  engine_.stop();
  engine_.prepare(plannedSteps, stepperFrequency);
  engine_.start();

  const int32_t minExpectedSg =
      static_cast<int32_t>(0.15f * static_cast<float>(stepperFrequency) / static_cast<float>(max<uint8_t>(sgAdj_, 1)));
  const int32_t sgThreshold = static_cast<int32_t>(0.8f * static_cast<float>(minExpectedSg));

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

    if (loops > startupLoops && sg < sgThreshold) {
      engine_.stop();
      if (engine_.completedPulses() < static_cast<uint32_t>(0.95f * static_cast<float>(plannedSteps))) {
        statusLed_.flashColor("red", 0.1f, 3, 50);
        if (debug_) {
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

  setStallguard(0);
  engine_.stop();
  log("ERROR", "homing failed");
  return false;
}

bool Stepper::centering(uint32_t requestedFrequency) {
  if (moveInProgress_) {
    engine_.stop();
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

    ok = centeringAttempt(stepperFrequency, startupLoops, stuckDetected);
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
    statusLed_.flashColor("blue", 0.1f, 10, 50);
  }
  return ok;
}

bool Stepper::centeringAttempt(uint32_t stepperFrequency, uint32_t startupLoops, bool &stuckDetected) {
  stuckDetected = false;
  uint32_t retractTimeMs = 0;
  int32_t retractSteps = 0;

  engine_.setDirection(false);
  retract(stepperFrequency, startupLoops, retractTimeMs, retractSteps);

  engine_.setDirection(true);
  if (!homing(stepperFrequency, startupLoops, retractTimeMs, retractSteps)) {
    engine_.stop();
    travelSteps_ = 0;
    currentPositionSteps_ = 0;
    calibrated_ = false;
    return false;
  }

  engine_.setDirection(false);
  if (!homing(stepperFrequency, startupLoops, retractTimeMs, retractSteps)) {
    engine_.stop();
    travelSteps_ = 0;
    currentPositionSteps_ = 0;
    calibrated_ = false;
    return false;
  }

  int32_t stepsRange = static_cast<int32_t>(engine_.completedPulses());
  if (stepsRange < 0) {
    stepsRange = 0;
  }

  if (stepsRange < static_cast<int32_t>(kMinValidTravelSteps)) {
    engine_.stop();
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

  engine_.setDirection(true);
  engine_.prepare(halfRange, stepperFrequency);
  engine_.start();

  const uint32_t centeringTimeMs = 100UL + (halfRange * 1000UL) / max<uint32_t>(1, stepperFrequency);
  if (debug_) {
    String message = "centering counted_steps=";
    message += stepsRange;
    message += " half_range_steps=";
    message += halfRange;
    log("DEBUG", message);
  }

  delay(centeringTimeMs);
  if (!engine_.isSpinning()) {
    travelSteps_ = static_cast<uint32_t>(stepsRange);
    currentPositionSteps_ = halfRange;
    lastMoveFrequency_ = stepperFrequency;
    calibrated_ = true;
    statusLed_.flashColor("green", 0.2f, 3, 50);
    return true;
  }

  engine_.stop();
  calibrated_ = false;
  return false;
}

bool Stepper::jog(bool positiveDirection, uint32_t steps, uint32_t requestedFrequency, bool useRecoveryCurrent) {
  if (steps == 0) {
    return false;
  }

  if (moveInProgress_) {
    engine_.stop();
  }
  clearMoveState();

  const uint8_t normalRunCurrent = runCurrent_;
  if (useRecoveryCurrent && recoveryRunCurrent_ > runCurrent_) {
    setRunCurrent(recoveryRunCurrent_);
  }

  const uint32_t moveFrequency = clampMoveFrequency(requestedFrequency);
  engine_.stop();
  engine_.setDirection(positiveDirection);
  engine_.prepare(steps, moveFrequency);
  engine_.start();

  const uint32_t timeoutMs = 250UL + (steps * 1000UL) / max<uint32_t>(1, moveFrequency);
  const uint32_t startMs = millis();
  while (engine_.isSpinning() && (millis() - startMs) <= timeoutMs) {
    if (serviceCallback_ != nullptr) {
      serviceCallback_();
    }
    delay(5);
  }
  const bool completed = !engine_.isSpinning();
  engine_.stop();

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
    engine_.stop();
    const bool adjusted = handleEndpointHomeProbeStall(completedSteps);
    lastMoveFrequency_ = moveFrequency_;
    clearMoveState();
    return adjusted ? MoveUpdate::HomeAdjusted : MoveUpdate::Completed;
  }

  if (!engine_.isSpinning()) {
    currentPositionSteps_ = moveTargetPositionSteps_;
    lastMoveFrequency_ = moveFrequency_;
    clearMoveState();
    return MoveUpdate::Completed;
  }

  if ((millis() - moveStartMs_) <= moveTimeoutMs_) {
    return MoveUpdate::None;
  }

  engine_.stop();
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
    engine_.stop();
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

  result.accepted = true;
  result.moveSteps = commandedMoveSteps;
  result.actualFrequency = moveFrequency;
  result.estimatedDurationMs = moveTimeMs;
  result.speedClampedHigh = requestedInternalFrequency > maxMoveFrequency();
  result.speedClampedLow = requestedFrequency != 0 && requestedInternalFrequency < minMoveFrequency();

  engine_.stop();
  engine_.setDirection(refreshedDelta > 0);
  engine_.prepare(moveSteps, rampStartFrequency);
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
  engine_.start();

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

Stepper::SpeedTestResult Stepper::runSpeedTest(uint32_t startFrequency, uint32_t endFrequency,
                                               uint32_t stepFrequency, uint8_t repeats) {
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
                                                         CharacterizeCycleCallback callback,
                                                         void *callbackContext) {
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
  const uint32_t tolerance =
      endpointToleranceSteps == 0 ? kMinHomeProbeRepeatToleranceSteps : endpointToleranceSteps;

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
