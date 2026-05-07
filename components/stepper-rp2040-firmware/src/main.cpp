#include <Arduino.h>
#include <cstdlib>

#include "rgb_led.h"
#include "stepper.h"

namespace {
constexpr uint8_t kButtonPin = 9;
constexpr uint32_t kDebounceMs = 10;
constexpr uint32_t kStepperFrequencies[2] = {400, 1200};
constexpr size_t kSerialLineMax = 256;
constexpr size_t kTmcRawMaxBytes = 32;
constexpr size_t kEnvelopeMaxPhases = 4;
constexpr uint32_t kTmcRawReadTimeoutMs = 50;

RgbLed g_rgbLed;
Stepper g_stepper(g_rgbLed, 125000000, true);

bool g_motionInProgress = false;
bool g_homingRequested = false;
bool g_lastButtonLevel = true;
uint32_t g_lastDebounceMs = 0;
int g_lastIdx = 1;
String g_serialLine;

String nextToken(const String &line, int &offset);

struct EnvelopePhase {
  const char *name = "";
  float targetPercent = 0.0f;
  uint32_t durationMs = 0;
  uint32_t holdMs = 0;
  bool isHold = false;
};

EnvelopePhase g_envelopePhases[kEnvelopeMaxPhases];
size_t g_envelopePhaseCount = 0;
size_t g_envelopePhaseIndex = 0;
bool g_envelopeActive = false;
bool g_envelopeWaitingForMove = false;
bool g_envelopeHolding = false;
uint32_t g_envelopeHoldUntilMs = 0;

bool parseUnsignedLongArg(const String &arg, uint32_t &value) {
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(arg.c_str(), &end, 10);
  if (end == arg.c_str() || *end != '\0') {
    return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseNumberArg(const String &arg, uint32_t &value) {
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(arg.c_str(), &end, 0);
  if (end == arg.c_str() || *end != '\0') {
    return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseByteArg(const String &arg, uint8_t &value) {
  uint32_t parsed = 0;
  if (!parseNumberArg(arg, parsed) || parsed > 0xFF) {
    return false;
  }
  value = static_cast<uint8_t>(parsed);
  return true;
}

bool parseFloatArg(const String &arg, float &value) {
  char *end = nullptr;
  value = std::strtof(arg.c_str(), &end);
  if (end == arg.c_str() || *end != '\0') {
    return false;
  }
  return true;
}

bool parseFloatToken(const String &line, int &offset, float &value) {
  const String token = nextToken(line, offset);
  return token.length() > 0 && parseFloatArg(token, value);
}

bool parseUnsignedLongToken(const String &line, int &offset, uint32_t &value) {
  const String token = nextToken(line, offset);
  return token.length() > 0 && parseUnsignedLongArg(token, value);
}

bool splitCommandArgs(const String &line, String &command, String &arg1, String &arg2) {
  command = line;
  arg1 = "";
  arg2 = "";

  const int firstSpace = line.indexOf(' ');
  if (firstSpace < 0) {
    command.trim();
    return command.length() > 0;
  }

  command = line.substring(0, firstSpace);
  command.trim();

  String rest = line.substring(firstSpace + 1);
  rest.trim();
  if (rest.length() == 0) {
    return command.length() > 0;
  }

  const int secondSpace = rest.indexOf(' ');
  if (secondSpace < 0) {
    arg1 = rest;
    arg1.trim();
    return command.length() > 0;
  }

  arg1 = rest.substring(0, secondSpace);
  arg1.trim();
  arg2 = rest.substring(secondSpace + 1);
  arg2.trim();
  return command.length() > 0;
}

String nextToken(const String &line, int &offset) {
  while (offset < line.length() && (line[offset] == ' ' || line[offset] == '\t' || line[offset] == ',')) {
    ++offset;
  }

  const int start = offset;
  while (offset < line.length() && line[offset] != ' ' && line[offset] != '\t' && line[offset] != ',') {
    ++offset;
  }

  return line.substring(start, offset);
}

size_t parseByteList(const String &line, uint8_t *bytes, size_t maxBytes, bool &ok) {
  ok = true;
  size_t count = 0;
  int offset = 0;
  while (offset < line.length()) {
    const String token = nextToken(line, offset);
    if (token.length() == 0) {
      continue;
    }
    if (count >= maxBytes || !parseByteArg(token, bytes[count])) {
      ok = false;
      return count;
    }
    ++count;
  }
  return count;
}

void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void printHex32(uint32_t value) {
  Serial.print("0x");
  for (int shift = 28; shift >= 0; shift -= 4) {
    Serial.print((value >> shift) & 0x0F, HEX);
  }
}

void clearEnvelope() {
  g_envelopePhaseCount = 0;
  g_envelopePhaseIndex = 0;
  g_envelopeActive = false;
  g_envelopeWaitingForMove = false;
  g_envelopeHolding = false;
  g_envelopeHoldUntilMs = 0;
}

void printMoveResult(const Stepper::MoveCommandResult &result) {
  if (!result.accepted) {
    Serial.println("Move rejected");
    return;
  }

  if (result.speedClampedHigh) {
    Serial.print("WARNING: requested speed is faster than supported; clamped to ");
    Serial.print(result.actualFrequency);
    Serial.println("Hz");
  } else if (result.speedClampedLow) {
    Serial.print("WARNING: requested speed is slower than supported; clamped to ");
    Serial.print(result.actualFrequency);
    Serial.println("Hz");
  }

  if (result.completedImmediately) {
    Serial.println("Already at target");
    return;
  }

  Serial.print("Moving to step ");
  Serial.print(result.targetStep);
  Serial.print(" at ");
  Serial.print(result.actualFrequency);
  Serial.print("Hz");
  if (result.estimatedDurationMs > 0) {
    Serial.print(" (~");
    Serial.print(result.estimatedDurationMs);
    Serial.print("ms)");
  }
  Serial.println();
}

bool startEnvelopePhase(const EnvelopePhase &phase) {
  if (phase.isHold) {
    if (phase.holdMs == 0) {
      Serial.println("Envelope sustain hold until release");
    } else {
      Serial.print("Envelope hold ");
      Serial.print(phase.holdMs);
      Serial.println("ms");
    }
    g_envelopeHolding = true;
    g_envelopeHoldUntilMs = millis() + phase.holdMs;
    return true;
  }

  Serial.print("Envelope ");
  Serial.print(phase.name);
  Serial.print(": ");
  Serial.print(phase.targetPercent, 1);
  Serial.print("% in ");
  Serial.print(phase.durationMs);
  Serial.println("ms");

  Stepper::MoveCommandResult result = g_stepper.moveToPercentInTime(phase.targetPercent, phase.durationMs);
  printMoveResult(result);
  return result.accepted;
}

void startNextEnvelopePhase() {
  g_envelopeWaitingForMove = false;
  g_envelopeHolding = false;

  while (g_envelopeActive && g_envelopePhaseIndex < g_envelopePhaseCount) {
    EnvelopePhase &phase = g_envelopePhases[g_envelopePhaseIndex++];
    if (!startEnvelopePhase(phase)) {
      clearEnvelope();
      return;
    }

    if (phase.isHold) {
      return;
    }

    g_envelopeWaitingForMove = true;
    return;
  }

  if (g_envelopeActive) {
    Serial.println("Envelope complete");
  }
  clearEnvelope();
}

void serviceEnvelope(Stepper::MoveUpdate moveUpdate) {
  if (!g_envelopeActive) {
    return;
  }

  if (g_envelopeWaitingForMove) {
    if (moveUpdate == Stepper::MoveUpdate::None) {
      return;
    }
    if (moveUpdate == Stepper::MoveUpdate::Failed) {
      Serial.println("Envelope stopped: move failed");
      clearEnvelope();
      return;
    }
    startNextEnvelopePhase();
    return;
  }

  if (g_envelopeHolding) {
    if (g_envelopePhases[g_envelopePhaseIndex - 1].holdMs == 0) {
      return;
    }
    if (static_cast<int32_t>(millis() - g_envelopeHoldUntilMs) < 0) {
      return;
    }
    startNextEnvelopePhase();
    return;
  }

  startNextEnvelopePhase();
}

uint32_t nextDemoFrequency() {
  const int idx = (g_lastIdx == 1) ? 0 : 1;
  g_lastIdx = idx;
  return kStepperFrequencies[idx];
}

void printSerialHelp() {
  Serial.println("Serial commands:");
  Serial.println("  home            - run homing at alternating demo speed");
  Serial.println("  home <hz>       - run homing at a specific frequency (e.g. 400 or 1200)");
  Serial.println("  pos <percent> [hz] - move to a calibrated position from 0 to 100 (default 1200Hz)");
  Serial.println("  step <n> [hz]   - move to an absolute step from 0 to total travel (default 1200Hz)");
  Serial.println("  pos-time <percent> <ms> - move to a position by a target time");
  Serial.println("  pos-speed <percent> <hz> - move to a position at a target speed");
  Serial.println("  step-time <n> <ms> - move to an absolute step by a target time");
  Serial.println("  step-speed <n> <hz> - move to an absolute step at a target speed");
  Serial.println("  adsr <attack%> <attack_ms> <decay%> <decay_ms> <sustain_ms> <release%> <release_ms>");
  Serial.println("  release <percent> <ms> - preempt current envelope/move with release segment");
  Serial.println("  current status  - print TMC run/idle current settings");
  Serial.println("  current run <0-31>  - set normal TMC running current scale");
  Serial.println("  current normal <0-31> - alias for current run");
  Serial.println("  current recovery <0-31> - set homing/recovery current scale");
  Serial.println("  current idle <0-31> - set TMC holding current scale");
  Serial.println("  current idle-delay <0-255> - set delay before holding current");
  Serial.println("  tmc test        - test TMC2209 UART response");
  Serial.println("  tmc read <reg>  - read a TMC register, e.g. tmc read 0x6F");
  Serial.println("  tmc write <reg> <value> [noverify] - write a TMC register");
  Serial.println("  tmc raw <bytes> - send raw bytes to the TMC UART, e.g. tmc raw 0x55 0 0x06 0xE8");
  Serial.println("  status          - print calibration status");
  Serial.println("  bootsel         - reboot into BOOTSEL firmware update mode");
  Serial.println("  help            - show this help");
}

void handleTmcCommand(const String &subcommand, const String &args) {
  if (subcommand.equalsIgnoreCase("test")) {
    Serial.print("TMC UART test: ");
    Serial.println(g_stepper.tmcTest() ? "ok" : "failed");
    return;
  }

  if (subcommand.equalsIgnoreCase("read")) {
    uint8_t reg = 0;
    if (!parseByteArg(args, reg)) {
      Serial.println("Usage: tmc read <reg>");
      return;
    }

    uint32_t value = 0;
    if (!g_stepper.readTmcRegister(reg, value)) {
      Serial.println("TMC read failed");
      return;
    }

    Serial.print("TMC[0x");
    printHexByte(reg);
    Serial.print("] = ");
    printHex32(value);
    Serial.println();
    return;
  }

  if (subcommand.equalsIgnoreCase("write")) {
    int offset = 0;
    const String regToken = nextToken(args, offset);
    const String valueToken = nextToken(args, offset);
    const String verifyToken = nextToken(args, offset);

    uint8_t reg = 0;
    uint32_t value = 0;
    if (!parseByteArg(regToken, reg) || !parseNumberArg(valueToken, value)) {
      Serial.println("Usage: tmc write <reg> <value> [noverify]");
      return;
    }

    const bool verify = !verifyToken.equalsIgnoreCase("noverify");
    if (!g_stepper.writeTmcRegister(reg, value, verify)) {
      Serial.println("TMC write failed");
      return;
    }

    Serial.print("TMC[0x");
    printHexByte(reg);
    Serial.print("] <= ");
    printHex32(value);
    Serial.println(verify ? " verified" : " sent");
    return;
  }

  if (subcommand.equalsIgnoreCase("raw")) {
    uint8_t txBytes[kTmcRawMaxBytes] = {};
    uint8_t rxBytes[kTmcRawMaxBytes] = {};
    bool ok = false;
    const size_t txCount = parseByteList(args, txBytes, sizeof(txBytes), ok);
    if (!ok || txCount == 0) {
      Serial.println("Usage: tmc raw <byte> [byte...]");
      return;
    }

    const size_t rxCount = g_stepper.transferTmc(txBytes, txCount, rxBytes, sizeof(rxBytes), kTmcRawReadTimeoutMs);
    Serial.print("TMC raw tx:");
    for (size_t i = 0; i < txCount; ++i) {
      Serial.print(" 0x");
      printHexByte(txBytes[i]);
    }
    Serial.println();

    Serial.print("TMC raw rx:");
    if (rxCount == 0) {
      Serial.print(" [none]");
    }
    for (size_t i = 0; i < rxCount; ++i) {
      Serial.print(" 0x");
      printHexByte(rxBytes[i]);
    }
    Serial.println();
    return;
  }

  Serial.println("Usage: tmc test | tmc read <reg> | tmc write <reg> <value> [noverify] | tmc raw <bytes>");
}

void printStatus() {
  Serial.print("Run current: ");
  Serial.println(g_stepper.getRunCurrent());
  Serial.print("Recovery current: ");
  Serial.println(g_stepper.getRecoveryRunCurrent());
  if (g_stepper.getRecoveryRunCurrent() <= g_stepper.getRunCurrent()) {
    Serial.println("WARNING: recovery current has no headroom above normal run current");
  }
  Serial.print("Idle current: ");
  Serial.println(g_stepper.getIdleCurrent());
  Serial.print("Idle delay: ");
  Serial.println(g_stepper.getIdlePowerDownDelay());
  uint32_t drvStatus = 0;
  if (g_stepper.readTmcRegister(0x6F, drvStatus)) {
    Serial.print("TMC DRV_STATUS: ");
    printHex32(drvStatus);
    Serial.print(" cs_actual=");
    Serial.print((drvStatus >> 16) & 0x1F);
    Serial.print(" standstill=");
    Serial.println((drvStatus & (1UL << 31)) ? "yes" : "no");
  }
  Serial.print("Calibrated: ");
  Serial.println(g_stepper.isCalibrated() ? "yes" : "no");
  if (g_stepper.isCalibrated()) {
    Serial.print("Travel steps: ");
    Serial.println(g_stepper.getTravelSteps());
    Serial.print("Current step: ");
    Serial.println(g_stepper.getCurrentPositionSteps());
    Serial.print("Current position: ");
    Serial.print(g_stepper.getPositionPercent(), 1);
    Serial.println("%");
  }
}

void runCentering(uint32_t requestedFrequency) {
  clearEnvelope();
  g_motionInProgress = true;

  Serial.println();
  Serial.println();
  Serial.println("##############################################################################");
  Serial.println("############  Stepper centering via SENSORLESS homing function  ############");
  Serial.println("##############################################################################");
  Serial.print("Requested frequency: ");
  Serial.print(requestedFrequency);
  Serial.println("Hz");

  const bool ok = g_stepper.centering(requestedFrequency);
  if (ok) {
    Serial.println();
    Serial.println("Stepper is centered");
    Serial.println();
    printStatus();
  } else {
    Serial.println();
    Serial.println("Failed to center the stepper");
    Serial.println();
  }

  g_motionInProgress = false;
}

void runMoveToPercent(float percent, uint32_t requestedFrequency) {
  if (!g_stepper.isCalibrated()) {
    Serial.println("Move rejected: run 'home' first to calibrate the travel range.");
    return;
  }

  const float clampedPercent = constrain(percent, 0.0f, 100.0f);
  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToPercentAtFrequency(clampedPercent, requestedFrequency);
  if (result.accepted) {
    Serial.print("Moving to ");
    Serial.print(clampedPercent, 1);
    Serial.println("%");
    printMoveResult(result);
  } else {
    Serial.println("Move failed");
  }
}

void runMoveToStep(uint32_t targetStep, uint32_t requestedFrequency) {
  if (!g_stepper.isCalibrated()) {
    Serial.println("Move rejected: run 'home' first to calibrate the travel range.");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToStepAtFrequency(targetStep, requestedFrequency);
  if (result.accepted) {
    Serial.print("Moving to step ");
    Serial.println(targetStep);
    printMoveResult(result);
  } else {
    Serial.println("Move failed");
  }
}

void runMoveToStepInTime(uint32_t targetStep, uint32_t durationMs) {
  if (!g_stepper.isCalibrated()) {
    Serial.println("Move rejected: run 'home' first to calibrate the travel range.");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToStepInTime(targetStep, durationMs);
  printMoveResult(result);
}

void runMoveToStepAtSpeed(uint32_t targetStep, uint32_t frequency) {
  if (!g_stepper.isCalibrated()) {
    Serial.println("Move rejected: run 'home' first to calibrate the travel range.");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToStepAtFrequency(targetStep, frequency);
  printMoveResult(result);
}

void runMoveToPercentInTime(float percent, uint32_t durationMs) {
  if (!g_stepper.isCalibrated()) {
    Serial.println("Move rejected: run 'home' first to calibrate the travel range.");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToPercentInTime(percent, durationMs);
  printMoveResult(result);
}

void runMoveToPercentAtSpeed(float percent, uint32_t frequency) {
  if (!g_stepper.isCalibrated()) {
    Serial.println("Move rejected: run 'home' first to calibrate the travel range.");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToPercentAtFrequency(percent, frequency);
  printMoveResult(result);
}

void startAdsrEnvelope(const String &args) {
  if (!g_stepper.isCalibrated()) {
    Serial.println("Envelope rejected: run 'home' first to calibrate the travel range.");
    return;
  }

  int offset = 0;
  float attackPercent = 0.0f;
  uint32_t attackMs = 0;
  float decayPercent = 0.0f;
  uint32_t decayMs = 0;
  uint32_t sustainMs = 0;
  float releasePercent = 0.0f;
  uint32_t releaseMs = 0;
  if (!parseFloatToken(args, offset, attackPercent) || !parseUnsignedLongToken(args, offset, attackMs) ||
      !parseFloatToken(args, offset, decayPercent) || !parseUnsignedLongToken(args, offset, decayMs) ||
      !parseUnsignedLongToken(args, offset, sustainMs) || !parseFloatToken(args, offset, releasePercent) ||
      !parseUnsignedLongToken(args, offset, releaseMs)) {
    Serial.println("Usage: adsr <attack%> <attack_ms> <decay%> <decay_ms> <sustain_ms> <release%> <release_ms>");
    Serial.println("Use sustain_ms=0 to hold until a release command.");
    return;
  }

  clearEnvelope();
  g_stepper.stopStepper();
  g_envelopePhases[0] = {"attack", attackPercent, attackMs, 0, false};
  g_envelopePhases[1] = {"decay", decayPercent, decayMs, 0, false};
  g_envelopePhases[2] = {"sustain", decayPercent, 0, sustainMs, true};
  g_envelopePhases[3] = {"release", releasePercent, releaseMs, 0, false};
  g_envelopePhaseCount = sustainMs == 0 ? 3 : 4;
  g_envelopePhaseIndex = 0;
  g_envelopeActive = true;
  Serial.println("Envelope started");
  startNextEnvelopePhase();
}

void startReleaseMove(const String &args) {
  if (!g_stepper.isCalibrated()) {
    Serial.println("Release rejected: run 'home' first to calibrate the travel range.");
    return;
  }

  int offset = 0;
  float releasePercent = 0.0f;
  uint32_t releaseMs = 0;
  if (!parseFloatToken(args, offset, releasePercent) || !parseUnsignedLongToken(args, offset, releaseMs)) {
    Serial.println("Usage: release <percent> <ms>");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToPercentInTime(releasePercent, releaseMs);
  Serial.println("Release started");
  printMoveResult(result);
}

void handleSerialCommand(const String &rawLine) {
  String line = rawLine;
  line.trim();
  if (line.length() == 0) {
    return;
  }

  String command;
  String arg1;
  String arg2;
  if (!splitCommandArgs(line, command, arg1, arg2)) {
    return;
  }

  if (command.equalsIgnoreCase("help")) {
    printSerialHelp();
    return;
  }

  if (command.equalsIgnoreCase("status")) {
    printStatus();
    return;
  }

  if (command.equalsIgnoreCase("tmc")) {
    handleTmcCommand(arg1, arg2);
    return;
  }

  if (command.equalsIgnoreCase("bootsel") || command.equalsIgnoreCase("bootloader")) {
    Serial.println("Rebooting into BOOTSEL firmware update mode...");
    Serial.flush();
    delay(100);
    rp2040.rebootToBootloader();
    return;
  }

  if (command.equalsIgnoreCase("current")) {
    if (arg1.equalsIgnoreCase("status") && arg2.length() == 0) {
      Serial.print("Run current: ");
      Serial.println(g_stepper.getRunCurrent());
      Serial.print("Recovery current: ");
      Serial.println(g_stepper.getRecoveryRunCurrent());
      if (g_stepper.getRecoveryRunCurrent() <= g_stepper.getRunCurrent()) {
        Serial.println("WARNING: recovery current has no headroom above normal run current");
      }
      Serial.print("Idle current: ");
      Serial.println(g_stepper.getIdleCurrent());
      Serial.print("Idle delay: ");
      Serial.println(g_stepper.getIdlePowerDownDelay());
      return;
    }
    uint32_t value = 0;
    const bool isIdleDelay = arg1.equalsIgnoreCase("idle-delay");
    const uint32_t maxValue = isIdleDelay ? 255 : 31;
    if (!parseUnsignedLongArg(arg2, value) || value > maxValue) {
      Serial.println("Usage: current run|normal <0-31> | current recovery <0-31> | current idle <0-31> | current idle-delay <0-255> | current status");
      return;
    }
    if (arg1.equalsIgnoreCase("run") || arg1.equalsIgnoreCase("normal")) {
      if (g_stepper.setRunCurrent(static_cast<uint8_t>(value))) {
        Serial.print("Run current set to ");
        Serial.println(value);
      } else {
        Serial.println("Failed to set run current");
      }
      return;
    }
    if (arg1.equalsIgnoreCase("recovery")) {
      if (g_stepper.setRecoveryRunCurrent(static_cast<uint8_t>(value))) {
        Serial.print("Recovery current set to ");
        Serial.println(value);
        if (value <= g_stepper.getRunCurrent()) {
          Serial.println("WARNING: recovery current has no headroom above normal run current");
        }
      } else {
        Serial.println("Failed to set recovery current");
      }
      return;
    }
    if (arg1.equalsIgnoreCase("idle")) {
      if (g_stepper.setIdleCurrent(static_cast<uint8_t>(value))) {
        Serial.print("Idle current set to ");
        Serial.println(value);
      } else {
        Serial.println("Failed to set idle current");
      }
      return;
    }
    if (isIdleDelay) {
      if (g_stepper.setIdlePowerDownDelay(static_cast<uint8_t>(value))) {
        Serial.print("Idle delay set to ");
        Serial.println(value);
      } else {
        Serial.println("Failed to set idle delay");
      }
      return;
    }
    Serial.println("Usage: current run|normal <0-31> | current recovery <0-31> | current idle <0-31> | current idle-delay <0-255> | current status");
    return;
  }

  if (command.equalsIgnoreCase("adsr") || command.equalsIgnoreCase("asdr")) {
    startAdsrEnvelope(line.substring(command.length()));
    return;
  }

  if (command.equalsIgnoreCase("release")) {
    startReleaseMove(line.substring(command.length()));
    return;
  }

  if (command.equalsIgnoreCase("home") && arg1.length() == 0) {
    if (g_motionInProgress || g_stepper.isMoveInProgress()) {
      Serial.println("Busy");
      return;
    }
    runCentering(nextDemoFrequency());
    return;
  }

  if (command.equalsIgnoreCase("home")) {
    if (g_motionInProgress || g_stepper.isMoveInProgress()) {
      Serial.println("Busy");
      return;
    }
    uint32_t frequency = 0;
    if (!parseUnsignedLongArg(arg1, frequency) || frequency == 0 || arg2.length() != 0) {
      Serial.println("Invalid frequency. Example: home 1200");
      return;
    }
    runCentering(frequency);
    return;
  }

  if (command.equalsIgnoreCase("pos") || command.equalsIgnoreCase("move")) {
    float percent = 0.0f;
    uint32_t frequency = 0;
    if (!parseFloatArg(arg1, percent)) {
      Serial.println("Invalid position. Example: pos 50  or  pos 50 1200");
      return;
    }
    if (arg2.length() > 0 && !parseUnsignedLongArg(arg2, frequency)) {
      Serial.println("Invalid frequency. Example: pos 50 1200");
      return;
    }
    runMoveToPercent(percent, frequency);
    return;
  }

  if (command.equalsIgnoreCase("pos-time") || command.equalsIgnoreCase("move-time")) {
    float percent = 0.0f;
    uint32_t durationMs = 0;
    if (!parseFloatArg(arg1, percent) || !parseUnsignedLongArg(arg2, durationMs) || durationMs == 0) {
      Serial.println("Invalid timed move. Example: pos-time 75 250");
      return;
    }
    runMoveToPercentInTime(percent, durationMs);
    return;
  }

  if (command.equalsIgnoreCase("pos-speed") || command.equalsIgnoreCase("move-speed")) {
    float percent = 0.0f;
    uint32_t frequency = 0;
    if (!parseFloatArg(arg1, percent) || !parseUnsignedLongArg(arg2, frequency) || frequency == 0) {
      Serial.println("Invalid speed move. Example: pos-speed 75 900");
      return;
    }
    runMoveToPercentAtSpeed(percent, frequency);
    return;
  }

  if (command.equalsIgnoreCase("step")) {
    uint32_t targetStep = 0;
    uint32_t frequency = 0;
    if (!parseUnsignedLongArg(arg1, targetStep)) {
      Serial.println("Invalid step value. Example: step 1000  or  step 1000 1200");
      return;
    }
    if (arg2.length() > 0 && !parseUnsignedLongArg(arg2, frequency)) {
      Serial.println("Invalid frequency. Example: step 1000 1200");
      return;
    }
    runMoveToStep(targetStep, frequency);
    return;
  }

  if (command.equalsIgnoreCase("step-time")) {
    uint32_t targetStep = 0;
    uint32_t durationMs = 0;
    if (!parseUnsignedLongArg(arg1, targetStep) || !parseUnsignedLongArg(arg2, durationMs) || durationMs == 0) {
      Serial.println("Invalid timed step move. Example: step-time 1000 250");
      return;
    }
    runMoveToStepInTime(targetStep, durationMs);
    return;
  }

  if (command.equalsIgnoreCase("step-speed")) {
    uint32_t targetStep = 0;
    uint32_t frequency = 0;
    if (!parseUnsignedLongArg(arg1, targetStep) || !parseUnsignedLongArg(arg2, frequency) || frequency == 0) {
      Serial.println("Invalid speed step move. Example: step-speed 1000 900");
      return;
    }
    runMoveToStepAtSpeed(targetStep, frequency);
    return;
  }

  Serial.print("Unknown command: ");
  Serial.println(line);
  printSerialHelp();
}

void processSerialInput() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      if (g_serialLine.length() > 0) {
        Serial.print("> ");
        Serial.println(g_serialLine);
      }
      handleSerialCommand(g_serialLine);
      g_serialLine = "";
      continue;
    }

    if (g_serialLine.length() < kSerialLineMax) {
      g_serialLine += ch;
    }
  }
}
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(kButtonPin, INPUT_PULLUP);

  Serial.println("waiting time to eventually stop the code before further imports ...");
  g_rgbLed.heartBeat(10, 1000);

  if (!g_stepper.begin()) {
    Serial.println("Failed to initialize stepper");
    return;
  }

  if (g_stepper.tmcTest()) {
    Serial.println();
    Serial.println("Code running on RP2040 (Arduino/PlatformIO)");
    Serial.println("Sensorless homing example");
    Serial.println();
    Serial.println("Press the push button for SENSORLESS homing demo");
    printSerialHelp();
  } else {
    Serial.println();
    Serial.println("###################################################################");
    Serial.println("#   The TMC driver UART does not react: IS THE DRIVER POWERED ?   #");
    Serial.println("###################################################################");
  }
}

void loop() {
  processSerialInput();

  const Stepper::MoveUpdate moveUpdate = g_stepper.serviceMove();
  if (moveUpdate == Stepper::MoveUpdate::Completed || moveUpdate == Stepper::MoveUpdate::HomeAdjusted) {
    Serial.print("Moved to step ");
    Serial.println(g_stepper.getCurrentPositionSteps());
    Serial.print("Moved to ");
    Serial.print(g_stepper.getPositionPercent(), 1);
    Serial.println("%");
    if (moveUpdate == Stepper::MoveUpdate::HomeAdjusted) {
      printStatus();
    }
  } else if (moveUpdate == Stepper::MoveUpdate::Failed) {
    Serial.println("Move failed");
  }
  serviceEnvelope(moveUpdate);

  const bool buttonLevel = digitalRead(kButtonPin);
  const uint32_t now = millis();

  if (buttonLevel != g_lastButtonLevel) {
    g_lastDebounceMs = now;
    g_lastButtonLevel = buttonLevel;
  }

  if (!g_motionInProgress && !g_stepper.isMoveInProgress() && !buttonLevel &&
      (now - g_lastDebounceMs) >= kDebounceMs) {
    g_homingRequested = true;
  }

  if (g_homingRequested) {
    g_homingRequested = false;
    runCentering(nextDemoFrequency());
    while (digitalRead(kButtonPin) == LOW) {
      delay(5);
    }
    g_lastButtonLevel = true;
    g_lastDebounceMs = millis();
  }

  delay(10);
}
