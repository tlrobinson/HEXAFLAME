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
constexpr uint32_t kTmcRawReadTimeoutMs = 50;

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
      Serial.println("Usage: current run <0-31> | current idle <0-31> | current idle-delay <0-255> | current status");
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
    if (isIdleDelay) {
      if (g_stepper.setIdlePowerDownDelay(static_cast<uint8_t>(value))) {
        Serial.print("Idle delay set to ");
        Serial.println(value);
      } else {
        Serial.println("Failed to set idle delay");
      }
      return;
    }
    Serial.println("Usage: current run <0-31> | current idle <0-31> | current idle-delay <0-255> | current status");
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
