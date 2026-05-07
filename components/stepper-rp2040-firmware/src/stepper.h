#pragma once

#include <Arduino.h>
#include <hardware/pio.h>

#include "rgb_led.h"
#include "tmc2209.h"

class Stepper {
public:
  enum class MoveUpdate : uint8_t {
    None = 0,
    Completed,
    HomeAdjusted,
    Failed,
  };

  struct MicrostepSetting {
    const char *label;
    float reduction;
    uint8_t ms1;
    uint8_t ms2;
    uint8_t sgAdjustment;
    uint8_t serialPortNodeAddress;
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

  Stepper(RgbLed &rgbLed, uint32_t maxFrequency = 125000000, bool debug = false);

  bool begin();
  bool tmcTest();
  bool centering(uint32_t requestedFrequency);
  MoveUpdate serviceMove();
  bool moveToPercent(float percent, uint32_t requestedFrequency = 0);
  bool moveToStep(uint32_t targetStep, uint32_t requestedFrequency = 0);
  MoveCommandResult moveToPercentInTime(float percent, uint32_t durationMs);
  MoveCommandResult moveToPercentAtFrequency(float percent, uint32_t requestedFrequency);
  MoveCommandResult moveToStepInTime(uint32_t targetStep, uint32_t durationMs);
  MoveCommandResult moveToStepAtFrequency(uint32_t targetStep, uint32_t requestedFrequency);
  bool isMoveInProgress() const;
  bool isCalibrated() const;
  uint32_t getTravelSteps() const;
  uint32_t getCurrentPositionSteps() const;
  float getPositionPercent() const;
  uint32_t getMinMoveFrequency() const;
  uint32_t getMaxMoveFrequency() const;
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
  bool microStep(uint8_t mode);
  bool getFullRev(uint8_t mode, uint32_t &fullRev, uint8_t &sgAdj, uint8_t &serialNode);
  void clearMoveState();
  void updateMoveProgress();
  uint32_t getMoveCompletedSteps();
  void resetHomeProbeState();
  bool shouldProbeEndpoint(uint32_t targetStep, uint32_t moveFrequency, uint32_t moveSteps) const;
  uint32_t getMaxHomeProbeDeltaSteps() const;
  bool sampleEndpointHomeProbe();
  bool handleEndpointHomeProbeStall(uint32_t completedSteps);
  bool recordEndpointHomeCandidate(bool positiveEnd, int32_t deltaSteps);
  void setDirection(bool clockwise);
  uint32_t clampMoveFrequency(uint32_t requestedFrequency) const;
  uint32_t minMoveFrequency() const;
  uint32_t maxMoveFrequency() const;
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
  uint8_t runCurrent_ = 24;
  uint8_t recoveryRunCurrent_ = 31;
  uint8_t idleCurrent_ = 0;
  uint8_t holdDelay_ = 8;
  uint8_t idlePowerDownDelay_ = 1;
  bool moveInProgress_ = false;
  bool moveDirectionPositive_ = true;
  uint32_t moveStartPositionSteps_ = 0;
  uint32_t moveTargetPositionSteps_ = 0;
  uint32_t movePlannedSteps_ = 0;
  uint32_t moveFrequency_ = 0;
  uint32_t moveStartMs_ = 0;
  uint32_t moveTimeoutMs_ = 0;
  bool moveHomeProbeEnabled_ = false;
  bool moveHomeProbePositiveEnd_ = false;
  uint8_t moveHomeProbeSamples_ = 0;
  int32_t moveHomeProbeSgThreshold_ = 0;
  bool pendingHomeCandidateValid_ = false;
  bool pendingHomeCandidatePositiveEnd_ = false;
  int32_t pendingHomeCandidateDeltaSteps_ = 0;
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
  static constexpr uint8_t kDefaultIdlePowerDownDelay = 1;
  static constexpr uint32_t kHomeProbeStartupIgnoreMs = 150;
  static constexpr uint8_t kHomeProbeMinSamples = 3;
  static constexpr uint32_t kHomeProbeExtraPaddingSteps = 16;
  static constexpr uint32_t kMinValidTravelSteps = 16;
  static constexpr uint32_t kMinHomeProbeRepeatToleranceSteps = 8;
};
