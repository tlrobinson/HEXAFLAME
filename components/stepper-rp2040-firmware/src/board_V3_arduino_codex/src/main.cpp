#include <Arduino.h>
#include <cstdlib>

#include "rgb_led.h"
#include "stepper.h"

namespace {
constexpr uint8_t kButtonPin = 9;
constexpr uint32_t kDebounceMs = 10;
constexpr uint32_t kStepperFrequencies[2] = {400, 1200};
constexpr size_t kSerialLineMax = 64;

RgbLed g_rgbLed;
Stepper g_stepper(g_rgbLed, 125000000, true);

bool g_motionInProgress = false;
bool g_homingRequested = false;
bool g_lastButtonLevel = true;
uint32_t g_lastDebounceMs = 0;
int g_lastIdx = 1;
String g_serialLine;

bool parseUnsignedLongArg(const String &arg, uint32_t &value) {
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(arg.c_str(), &end, 10);
  if (end == arg.c_str() || *end != '\0') {
    return false;
  }
  value = static_cast<uint32_t>(parsed);
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
  Serial.println("  current status  - print TMC run/idle current settings");
  Serial.println("  current run <0-31>  - set TMC running current scale");
  Serial.println("  current idle <0-31> - set TMC holding current scale");
  Serial.println("  status          - print calibration status");
  Serial.println("  help            - show this help");
}

void printStatus() {
  Serial.print("Run current: ");
  Serial.println(g_stepper.getRunCurrent());
  Serial.print("Idle current: ");
  Serial.println(g_stepper.getIdleCurrent());
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
  const bool ok = g_stepper.moveToPercent(clampedPercent, requestedFrequency);
  if (ok) {
    Serial.print("Moving to ");
    Serial.print(clampedPercent, 1);
    Serial.println("%");
  } else {
    Serial.println("Move failed");
  }
}

void runMoveToStep(uint32_t targetStep, uint32_t requestedFrequency) {
  if (!g_stepper.isCalibrated()) {
    Serial.println("Move rejected: run 'home' first to calibrate the travel range.");
    return;
  }

  const bool ok = g_stepper.moveToStep(targetStep, requestedFrequency);
  if (ok) {
    Serial.print("Moving to step ");
    Serial.println(targetStep);
  } else {
    Serial.println("Move failed");
  }
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

  if (command.equalsIgnoreCase("current")) {
    if (arg1.equalsIgnoreCase("status") && arg2.length() == 0) {
      Serial.print("Run current: ");
      Serial.println(g_stepper.getRunCurrent());
      Serial.print("Idle current: ");
      Serial.println(g_stepper.getIdleCurrent());
      return;
    }
    uint32_t value = 0;
    if (!parseUnsignedLongArg(arg2, value) || value > 31) {
      Serial.println("Usage: current run <0-31> | current idle <0-31> | current status");
      return;
    }
    if (arg1.equalsIgnoreCase("run")) {
      if (g_stepper.setRunCurrent(static_cast<uint8_t>(value))) {
        Serial.print("Run current set to ");
        Serial.println(value);
      } else {
        Serial.println("Failed to set run current");
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
    Serial.println("Usage: current run <0-31> | current idle <0-31> | current status");
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
  if (moveUpdate == Stepper::MoveUpdate::Completed) {
    Serial.print("Moved to step ");
    Serial.println(g_stepper.getCurrentPositionSteps());
    Serial.print("Moved to ");
    Serial.print(g_stepper.getPositionPercent(), 1);
    Serial.println("%");
  } else if (moveUpdate == Stepper::MoveUpdate::Failed) {
    Serial.println("Move failed");
  }

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
