#pragma once

#include <Arduino.h>

#include "board_config.h"
#include "status_led.h"
#include "step_engine.h"
#include "tmc2209.h"

// One stepper channel: sensorless homing, calibrated absolute moves, motion
// ramps, ADSR-style envelopes and the StallGuard characterization sweeps.
//
// Every Stepper is fully self-contained - it owns no static state - so a board
// can instantiate as many as it has channels and run them all independently.
// Hardware differences are hidden behind the StepEngine (pulse generation) and
// Tmc2209 (driver UART) it is handed at construction.
class Stepper {
public:
  using LogCallback = void (*)(uint8_t channel, const char *level, const String &message);
  using ServiceCallback = void (*)();

  enum class MoveUpdate : uint8_t {
    None = 0,
    Completed,
    HomeAdjusted,
    Failed,
  };

  struct MoveCommandResult {
    bool accepted = false;
    bool completedImmediately = false;
    bool speedClampedHigh = false;
    bool speedClampedLow = false;
    uint32_t targetStep = 0;
    uint32_t moveSteps = 0;
    uint32_t requestedFrequency = 0;
    uint32_t actualFrequency = 0;
    uint32_t requestedDurationMs = 0;
    uint32_t estimatedDurationMs = 0;
  };

  struct SpeedTestResult {
    bool accepted = false;
    uint32_t testedCount = 0;
    uint32_t passedCount = 0;
    uint32_t highestPassedFrequency = 0;
    uint32_t firstFailedFrequency = 0;
  };

  struct CharacterizeCycle {
    uint32_t cycle = 0;
    uint32_t frequencyHz = 0;
    uint8_t stallguardThreshold = 0;
    uint32_t endpointToleranceSteps = 0;
    bool moveAccepted = false;
    bool moveCompleted = false;
    bool homeAccepted = false;
    bool homeCompleted = false;
    bool passed = false;
    bool lowMargin = false;
    bool thresholdFailed = false;
    int32_t moveMinStallguard = -1;
    int32_t homeMinStallguard = -1;
    int32_t endpointDeltaSteps = 0;
    uint32_t targetStep = 0;
  };

  struct CharacterizeResult {
    bool accepted = false;
    uint32_t cyclesRequested = 0;
    uint32_t cyclesCompleted = 0;
    uint32_t cyclesPassed = 0;
    uint32_t frequencyHz = 0;
    uint8_t stallguardThreshold = 0;
    int32_t minStallguard = -1;
    int32_t maxAbsEndpointDeltaSteps = 0;
    uint32_t lowMarginFailures = 0;
    uint32_t thresholdFailures = 0;
    uint32_t endpointFailures = 0;
    uint32_t motionFailures = 0;
  };

  using CharacterizeCycleCallback = void (*)(const CharacterizeCycle &cycle, void *context);

  Stepper(uint8_t channelIndex, ChannelConfig &config, StatusLed &statusLed, bool debug = false);

  uint8_t channelIndex() const { return channelIndex_; }

  bool begin();
  void setLogCallback(LogCallback callback);
  void setServiceCallback(ServiceCallback callback);
  bool tmcTest();
  bool centering(uint32_t requestedFrequency);
  bool jog(bool positiveDirection, uint32_t steps, uint32_t requestedFrequency = 0, bool useRecoveryCurrent = false);
  MoveUpdate serviceMove();
  bool moveToPercent(float percent, uint32_t requestedFrequency = 0);
  bool moveToStep(uint32_t targetStep, uint32_t requestedFrequency = 0);
  MoveCommandResult moveToPercentInTime(float percent, uint32_t durationMs);
  MoveCommandResult moveToPercentAtFrequency(float percent, uint32_t requestedFrequency);
  MoveCommandResult moveToStepInTime(uint32_t targetStep, uint32_t durationMs);
  MoveCommandResult moveToStepAtFrequency(uint32_t targetStep, uint32_t requestedFrequency);
  bool isMoveInProgress() const;
  bool isCalibrated() const;
  uint32_t getMinValidTravelSteps() const;
  uint32_t getTravelSteps() const;
  uint32_t getCurrentPositionSteps() const;
  float getPositionPercent() const;
  uint32_t getMinMoveFrequency() const;
  uint32_t getMaxMoveFrequency() const;
  uint32_t getSafeMaxMoveFrequency() const;
  bool setSafeMaxMoveFrequency(uint32_t frequency);
  uint32_t getMoveRampDurationMs() const;
  bool setMoveRampDurationMs(uint32_t durationMs);
  int32_t getLastMoveMinStallguard() const;
  bool didLastMoveWarnLowMargin() const;
  SpeedTestResult runSpeedTest(uint32_t startFrequency, uint32_t endFrequency, uint32_t stepFrequency,
                               uint8_t repeats);
  CharacterizeResult runCharacterization(uint32_t frequency, uint8_t stallguardThreshold, uint32_t cycles,
                                         float travelPercent, uint32_t endpointToleranceSteps,
                                         CharacterizeCycleCallback callback = nullptr, void *callbackContext = nullptr);
  bool didLastMoveProbeEndpoint() const;
  bool wasLastEndpointProbeUpper() const;
  int32_t getLastEndpointDeltaSteps() const;
  uint8_t getRunCurrent() const;
  uint8_t getRecoveryRunCurrent() const;
  uint8_t getIdleCurrent() const;
  uint8_t getIdlePowerDownDelay() const;
  bool setRunCurrent(uint8_t current);
  bool setRecoveryRunCurrent(uint8_t current);
  bool setIdleCurrent(uint8_t current);
  bool setIdlePowerDownDelay(uint8_t delay);
  void stopStepper();
  void setStallguard(uint8_t threshold);
  int32_t readStallguard();
  bool readTmcRegister(uint8_t reg, uint32_t &value);
  bool writeTmcRegister(uint8_t reg, uint32_t value, bool verify = true);
  size_t transferTmc(const uint8_t *txData, size_t txLength, uint8_t *rxData, size_t rxMaxLength,
                     uint32_t timeoutMs);

private:
  bool applyMicrostep();
  void clearMoveState();
  void updateMoveProgress();
  uint32_t getMoveCompletedSteps();
  void resetHomeProbeState();
  bool shouldProbeEndpoint(uint32_t targetStep, uint32_t moveFrequency, uint32_t moveSteps) const;
  uint32_t getMaxHomeProbeDeltaSteps() const;
  bool sampleEndpointHomeProbe();
  bool handleEndpointHomeProbeStall(uint32_t completedSteps);
  bool recordEndpointHomeCandidate(bool positiveEnd, int32_t deltaSteps);
  void configureMoveStallguardMonitor(uint32_t moveFrequency);
  void sampleMoveStallguardMargin();
  void serviceMoveRamp();
  bool waitForBlockingMove(uint32_t timeoutMs);
  uint32_t clampMoveFrequency(uint32_t requestedFrequency) const;
  uint32_t minMoveFrequency() const;
  uint32_t maxMoveFrequency() const;
  bool applyCurrentConfig();
  bool retract(uint32_t stepperFrequency, uint32_t startupLoops, uint32_t &retractTimeMs, int32_t &retractSteps);
  bool homing(uint32_t stepperFrequency, uint32_t startupLoops, uint32_t retractTimeMs, int32_t retractSteps);
  bool centeringAttempt(uint32_t stepperFrequency, uint32_t startupLoops, bool &stuckDetected);
  void log(const char *level, const String &message) const;

  uint8_t channelIndex_;
  ChannelConfig &config_;
  StatusLed &statusLed_;
  StepEngine &engine_;
  Tmc2209 &tmc_;
  LogCallback logCallback_ = nullptr;
  ServiceCallback serviceCallback_ = nullptr;
  bool debug_;

  uint32_t fullRev_ = 0;
  uint8_t sgAdj_ = 1;
  uint32_t maxHomingRevs_ = 5;
  uint32_t maxSteps_ = 0;
  uint32_t safeMaxMoveFrequency_ = 1200;
  uint32_t lastMoveFrequency_ = 0;
  int32_t lastMoveMinStallguard_ = -1;
  bool lastMoveLowMargin_ = false;
  uint32_t travelSteps_ = 0;
  uint32_t currentPositionSteps_ = 0;
  bool calibrated_ = false;
  uint8_t runCurrent_ = 24;
  uint8_t recoveryRunCurrent_ = 31;
  uint8_t idleCurrent_ = 0;
  uint8_t holdDelay_ = 8;
  uint8_t idlePowerDownDelay_ = 1;
  bool moveInProgress_ = false;
  bool moveDirectionPositive_ = true;
  uint32_t moveStartPositionSteps_ = 0;
  uint32_t moveTargetPositionSteps_ = 0;
  uint32_t moveCommandedSteps_ = 0;
  uint32_t movePlannedSteps_ = 0;
  uint32_t moveFrequency_ = 0;
  uint32_t moveRampStartFrequency_ = 0;
  uint32_t moveRampTargetFrequency_ = 0;
  uint32_t moveRampStartMs_ = 0;
  uint32_t moveRampDurationMs_ = 0;
  uint32_t moveRampDurationMsSetting_ = 80;
  uint32_t moveStartMs_ = 0;
  uint32_t moveTimeoutMs_ = 0;
  bool moveHomeProbeEnabled_ = false;
  bool moveHomeProbePositiveEnd_ = false;
  uint8_t moveHomeProbeSamples_ = 0;
  int32_t moveHomeProbeSgThreshold_ = 0;
  uint32_t moveLastSgSampleMs_ = 0;
  int32_t moveSgWarningThreshold_ = 0;
  int32_t moveMinStallguard_ = -1;
  uint8_t moveLowMarginSamples_ = 0;
  bool moveLowMarginWarned_ = false;
  bool pendingHomeCandidateValid_ = false;
  bool pendingHomeCandidatePositiveEnd_ = false;
  int32_t pendingHomeCandidateDeltaSteps_ = 0;
  bool endpointCalibrationAdjustmentEnabled_ = true;
  bool lastEndpointProbeValid_ = false;
  bool lastEndpointProbePositiveEnd_ = false;
  int32_t lastEndpointDeltaSteps_ = 0;

  static constexpr uint32_t kStepperSteps = 200;
  static constexpr uint8_t kDefaultIdlePowerDownDelay = 1;
  static constexpr uint32_t kHomeProbeStartupIgnoreMs = 150;
  static constexpr uint8_t kHomeProbeMinSamples = 3;
  static constexpr uint32_t kHomeProbeExtraPaddingSteps = 16;
  static constexpr uint32_t kMinValidTravelSteps = 1500;
  static constexpr uint32_t kMinHomeProbeRepeatToleranceSteps = 8;
  static constexpr uint32_t kMaxAllowedMoveFrequency = 8000;
  static constexpr uint8_t kHomeRecoveryRetries = 2;
  static constexpr uint32_t kHomeRecoveryJogSteps = 500;
  static constexpr uint32_t kMinMoveRampDurationMs = 0;
  static constexpr uint32_t kMaxMoveRampDurationMs = 500;
  static constexpr uint32_t kMoveSgSampleIntervalMs = 20;
  static constexpr uint8_t kMoveLowMarginMinSamples = 3;
};
