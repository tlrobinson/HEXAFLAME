#include <Arduino.h>
#include <cctype>
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
constexpr size_t kSerialOutputQueueSize = 32;
constexpr uint32_t kDefaultChannel = 0;
constexpr uint32_t kChannelCount = 1;

RgbLed g_rgbLed;
Stepper g_stepper(g_rgbLed, 125000000, true);

bool g_motionInProgress = false;
bool g_homingRequested = false;
bool g_lastButtonLevel = true;
uint32_t g_lastDebounceMs = 0;
int g_lastIdx = 1;
String g_serialLine;
String g_currentRequestId = "";
bool g_currentRequestHasId = false;
uint32_t g_currentChannel = kDefaultChannel;
String g_serialOutputQueue[kSerialOutputQueueSize];
size_t g_serialOutputHead = 0;
size_t g_serialOutputTail = 0;
size_t g_serialOutputCount = 0;
uint32_t g_serialOutputDropped = 0;
bool g_serialOutputDraining = false;

struct CharacterizeEventContext {
  String requestId;
  uint32_t frequencyHz = 0;
  uint32_t stallguardThreshold = 0;
  uint32_t endpointToleranceSteps = 0;
};

enum class MotionState : uint8_t {
  Idle = 0,
  Homing,
  Jogging,
  Moving,
  Envelope,
  SpeedTesting,
  Fault,
};

MotionState g_motionState = MotionState::Idle;
MotionState g_lastReportedMotionState = MotionState::Idle;
bool g_lastReportedHomed = false;
bool g_haveReportedState = false;

String nextToken(const String &line, int &offset);
void clearEnvelope();
void emitStateChange(bool force = false);

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

int findJsonKey(const String &json, const char *key) {
  String needle = "\"";
  needle += key;
  needle += "\"";
  int pos = 0;
  while ((pos = json.indexOf(needle, pos)) >= 0) {
    int cursor = pos + needle.length();
    while (cursor < json.length() && isspace(json[cursor])) {
      ++cursor;
    }
    if (cursor < json.length() && json[cursor] == ':') {
      return cursor + 1;
    }
    pos += needle.length();
  }
  return -1;
}

String trimJsonValue(const String &raw) {
  String value = raw;
  value.trim();
  return value;
}

bool extractJsonValue(const String &json, const char *key, String &value) {
  int cursor = findJsonKey(json, key);
  if (cursor < 0) {
    return false;
  }
  while (cursor < json.length() && isspace(json[cursor])) {
    ++cursor;
  }
  if (cursor >= json.length()) {
    return false;
  }

  const int start = cursor;
  if (json[cursor] == '"') {
    ++cursor;
    bool escaped = false;
    while (cursor < json.length()) {
      const char ch = json[cursor++];
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        value = json.substring(start, cursor);
        return true;
      }
    }
    return false;
  }

  if (json[cursor] == '{' || json[cursor] == '[') {
    const char open = json[cursor];
    const char close = open == '{' ? '}' : ']';
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    while (cursor < json.length()) {
      const char ch = json[cursor++];
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if (ch == '"') {
          inString = false;
        }
        continue;
      }
      if (ch == '"') {
        inString = true;
      } else if (ch == open) {
        ++depth;
      } else if (ch == close) {
        --depth;
        if (depth == 0) {
          value = json.substring(start, cursor);
          return true;
        }
      }
    }
    return false;
  }

  while (cursor < json.length() && json[cursor] != ',' && json[cursor] != '}' && json[cursor] != ']') {
    ++cursor;
  }
  value = trimJsonValue(json.substring(start, cursor));
  return value.length() > 0;
}

bool decodeJsonString(const String &raw, String &value) {
  String input = trimJsonValue(raw);
  if (input.length() < 2 || input[0] != '"' || input[input.length() - 1] != '"') {
    return false;
  }
  value = "";
  for (int i = 1; i < input.length() - 1; ++i) {
    char ch = input[i];
    if (ch != '\\') {
      value += ch;
      continue;
    }
    if (++i >= input.length() - 1) {
      return false;
    }
    ch = input[i];
    switch (ch) {
    case '"':
    case '\\':
    case '/':
      value += ch;
      break;
    case 'b':
      value += '\b';
      break;
    case 'f':
      value += '\f';
      break;
    case 'n':
      value += '\n';
      break;
    case 'r':
      value += '\r';
      break;
    case 't':
      value += '\t';
      break;
    default:
      return false;
    }
  }
  return true;
}

bool jsonStringField(const String &json, const char *key, String &value) {
  String raw;
  return extractJsonValue(json, key, raw) && decodeJsonString(raw, value);
}

bool jsonUintField(const String &json, const char *key, uint32_t &value) {
  String raw;
  if (!extractJsonValue(json, key, raw)) {
    return false;
  }
  raw.trim();
  return parseUnsignedLongArg(raw, value);
}

bool jsonFloatField(const String &json, const char *key, float &value) {
  String raw;
  if (!extractJsonValue(json, key, raw)) {
    return false;
  }
  raw.trim();
  return parseFloatArg(raw, value);
}

bool jsonBoolField(const String &json, const char *key, bool &value) {
  String raw;
  if (!extractJsonValue(json, key, raw)) {
    return false;
  }
  raw.trim();
  if (raw == "true") {
    value = true;
    return true;
  }
  if (raw == "false") {
    value = false;
    return true;
  }
  return false;
}

bool parseJsonRpcRequest(const String &line, String &method, String &params, String &idRaw, bool &hasId) {
  String trimmed = line;
  trimmed.trim();
  if (!trimmed.startsWith("{") || !trimmed.endsWith("}")) {
    return false;
  }
  if (!jsonStringField(trimmed, "method", method) || method.length() == 0) {
    return false;
  }
  if (!extractJsonValue(trimmed, "params", params)) {
    params = "{}";
  }
  hasId = extractJsonValue(trimmed, "id", idRaw);
  if (!hasId) {
    idRaw = "null";
  }
  return true;
}

String hexByte(uint8_t value) {
  String out;
  if (value < 0x10) {
    out += "0";
  }
  out += String(value, HEX);
  out.toUpperCase();
  return out;
}

String hex32(uint32_t value) {
  String out = "0x";
  for (int shift = 28; shift >= 0; shift -= 4) {
    out += String((value >> shift) & 0x0F, HEX);
  }
  out.toUpperCase();
  return out;
}

const char *motionStateName(MotionState state) {
  switch (state) {
  case MotionState::Idle:
    return "Idle";
  case MotionState::Homing:
    return "Homing";
  case MotionState::Jogging:
    return "Jogging";
  case MotionState::Moving:
    return "Moving";
  case MotionState::Envelope:
    return "Envelope";
  case MotionState::SpeedTesting:
    return "SpeedTesting";
  case MotionState::Fault:
    return "Fault";
  }
  return "Unknown";
}

void appendJsonString(String &out, const String &value) {
  out += "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    const char ch = value[i];
    switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<uint8_t>(ch) < 0x20) {
        out += "\\u00";
        if (static_cast<uint8_t>(ch) < 0x10) {
          out += "0";
        }
        out += String(static_cast<uint8_t>(ch), HEX);
      } else {
        out += ch;
      }
      break;
    }
  }
  out += "\"";
}

String jsonPair(const char *key, const String &value) {
  String out = "\"";
  out += key;
  out += "\":";
  appendJsonString(out, value);
  return out;
}

String jsonPair(const char *key, const char *value) {
  return jsonPair(key, String(value));
}

String jsonPair(const char *key, uint32_t value) {
  String out = "\"";
  out += key;
  out += "\":";
  out += value;
  return out;
}

String jsonPair(const char *key, int32_t value) {
  String out = "\"";
  out += key;
  out += "\":";
  out += value;
  return out;
}

String jsonPair(const char *key, bool value) {
  String out = "\"";
  out += key;
  out += "\":";
  out += value ? "true" : "false";
  return out;
}

String jsonPairFloat(const char *key, float value, uint8_t digits = 1) {
  String out = "\"";
  out += key;
  out += "\":";
  out += String(value, digits);
  return out;
}

String jsonObjectWithChannel(const String &objectJson) {
  String body = objectJson;
  body.trim();
  if (body.length() >= 2 && body[0] == '{' && body[body.length() - 1] == '}') {
    body = body.substring(1, body.length() - 1);
    body.trim();
  } else {
    body = "";
  }

  String out = "{";
  out += jsonPair("channel", g_currentChannel);
  if (body.length() > 0) {
    out += ",";
    out += body;
  }
  out += "}";
  return out;
}

String channelStateJson() {
  String state = "{";
  state += jsonPair("motion_state", motionStateName(g_motionState));
  state += ",";
  state += jsonPair("homed", g_stepper.isCalibrated());
  if (g_stepper.isCalibrated()) {
    state += ",";
    state += jsonPair("travel_steps", g_stepper.getTravelSteps());
    state += ",";
    state += jsonPair("current_step", g_stepper.getCurrentPositionSteps());
    state += ",";
    state += jsonPairFloat("position_percent", g_stepper.getPositionPercent());
  }
  state += "}";
  return state;
}

bool enqueueJsonLine(const String &line) {
  if (g_serialOutputCount >= kSerialOutputQueueSize) {
    ++g_serialOutputDropped;
    return false;
  }
  g_serialOutputQueue[g_serialOutputTail] = line;
  g_serialOutputTail = (g_serialOutputTail + 1) % kSerialOutputQueueSize;
  ++g_serialOutputCount;
  return true;
}

void writeSerialByteBlocking(uint8_t value) {
  while (true) {
    while (Serial.availableForWrite() <= 0) {
      delay(1);
    }
    if (Serial.write(value) == 1) {
      return;
    }
    delay(1);
  }
}

void writeSerialLineBlocking(const String &line) {
  for (size_t i = 0; i < line.length(); ++i) {
    writeSerialByteBlocking(static_cast<uint8_t>(line[i]));
  }
  writeSerialByteBlocking('\n');
}

void serviceSerialOutput() {
  if (g_serialOutputDraining) {
    return;
  }
  g_serialOutputDraining = true;

  if (g_serialOutputDropped > 0 && g_serialOutputCount < kSerialOutputQueueSize) {
    const uint32_t dropped = g_serialOutputDropped;
    g_serialOutputDropped = 0;
    String line = "{\"jsonrpc\":\"2.0\",\"method\":\"log\",\"params\":{\"channel\":";
    line += g_currentChannel;
    line += ",\"level\":\"WARN\",\"message\":\"serial output queue dropped messages\",\"dropped\":";
    line += dropped;
    line += "}}";
    enqueueJsonLine(line);
  }

  if (g_serialOutputCount == 0) {
    g_serialOutputDraining = false;
    return;
  }
  String line = g_serialOutputQueue[g_serialOutputHead];
  g_serialOutputQueue[g_serialOutputHead] = "";
  g_serialOutputHead = (g_serialOutputHead + 1) % kSerialOutputQueueSize;
  --g_serialOutputCount;
  writeSerialLineBlocking(line);
  Serial.flush();
  g_serialOutputDraining = false;
}

void writeJsonLine(const String &line) {
  enqueueJsonLine(line);
}

void emitNotification(const char *method, const String &paramsJson) {
  String line = "{\"jsonrpc\":\"2.0\",\"method\":";
  appendJsonString(line, method);
  line += ",\"params\":";
  line += paramsJson.length() > 0 ? paramsJson : "{}";
  line += "}";
  writeJsonLine(line);
}

void logLine(const char *level, const String &message) {
  String params = "{";
  params += jsonPair("channel", g_currentChannel);
  params += ",";
  params += jsonPair("level", level);
  params += ",";
  params += jsonPair("message", message);
  params += "}";
  emitNotification("log", params);
}

void emitEvent(const char *event, const String &dataJson = "{}") {
  String params = "{";
  params += jsonPair("event", event);
  params += ",\"data\":";
  params += jsonObjectWithChannel(dataJson.length() > 0 ? dataJson : "{}");
  params += "}";
  emitNotification("event", params);
}

void emitStateChange(bool force) {
  const bool homed = g_stepper.isCalibrated();
  if (!force && g_haveReportedState && g_lastReportedMotionState == g_motionState && g_lastReportedHomed == homed) {
    return;
  }
  g_lastReportedMotionState = g_motionState;
  g_lastReportedHomed = homed;
  g_haveReportedState = true;
  emitEvent("state-change", channelStateJson());
}

void emitRpcResult(const String &resultJson) {
  if (!g_currentRequestHasId) {
    return;
  }
  String line = "{\"jsonrpc\":\"2.0\",\"id\":";
  line += g_currentRequestId;
  line += ",\"result\":";
  line += jsonObjectWithChannel(resultJson.length() > 0 ? resultJson : "{}");
  line += "}";
  writeJsonLine(line);
}

void emitRpcError(int code, const String &message) {
  if (!g_currentRequestHasId) {
    String params = "{";
    params += jsonPair("channel", g_currentChannel);
    params += ",";
    params += jsonPair("level", "ERROR");
    params += ",";
    params += jsonPair("message", message);
    params += ",";
    params += jsonPair("code", static_cast<int32_t>(code));
    params += "}";
    emitNotification("log", params);
    return;
  }
  String line = "{\"jsonrpc\":\"2.0\",\"id\":";
  line += g_currentRequestId;
  line += ",\"error\":{\"code\":";
  line += code;
  line += ",\"message\":";
  appendJsonString(line, message);
  line += ",\"data\":";
  line += jsonObjectWithChannel("{}");
  line += "}}";
  writeJsonLine(line);
}

void rspLine(const char *status, const String &message) {
  String data = "{";
  data += jsonPair("ok", String(status).equalsIgnoreCase("OK"));
  data += ",";
  data += jsonPair("status", status);
  data += ",";
  data += jsonPair("message", message);
  data += "}";
  emitEvent("response", data);
}

void setMotionState(MotionState state, const char *reason) {
  if (g_motionState == state) {
    return;
  }
  String message = "state ";
  message += motionStateName(g_motionState);
  message += " -> ";
  message += motionStateName(state);
  if (reason != nullptr && reason[0] != '\0') {
    message += " reason=";
    message += reason;
  }
  logLine("INFO", message);
  g_motionState = state;
  emitStateChange();
}

bool isMotionIdle() {
  return g_motionState == MotionState::Idle && !g_motionInProgress && !g_stepper.isMoveInProgress() && !g_envelopeActive;
}

bool requireIdleForCommand(const char *commandName) {
  if (isMotionIdle()) {
    return true;
  }
  String message = "busy state=";
  message += motionStateName(g_motionState);
  message += " rejected=";
  message += commandName;
  rspLine("ERR", message);
  return false;
}

bool requireHomedForCommand(const char *commandName) {
  if (g_stepper.isCalibrated()) {
    return true;
  }
  String message = "unhomed rejected=";
  message += commandName;
  message += " run home first";
  rspLine("ERR", message);
  return false;
}

void drainPendingSerialInput() {
  size_t drained = 0;
  while (Serial.available() > 0) {
    Serial.read();
    ++drained;
  }
  g_serialLine = "";
  if (drained > 0) {
    String message = "discarded ";
    message += drained;
    message += " queued serial bytes after blocking motion";
    logLine("WARN", message);
  }
}

void stopAllMotion(const char *reason) {
  clearEnvelope();
  g_stepper.stopStepper();
  g_motionInProgress = false;
  setMotionState(MotionState::Idle, reason);
  drainPendingSerialInput();
  rspLine("OK", "motion stopped");
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
    rspLine("ERR", "move rejected");
    return;
  }

  if (result.speedClampedHigh) {
    String message = "requested speed is faster than supported; clamped_hz=";
    message += result.actualFrequency;
    logLine("WARN", message);
  } else if (result.speedClampedLow) {
    String message = "requested speed is slower than supported; clamped_hz=";
    message += result.actualFrequency;
    logLine("WARN", message);
  }

  if (result.completedImmediately) {
    rspLine("OK", "already at target");
    return;
  }

  String data = "{";
  data += jsonPair("target_step", result.targetStep);
  data += ",";
  data += jsonPair("actual_frequency_hz", result.actualFrequency);
  if (result.estimatedDurationMs > 0) {
    data += ",";
    data += jsonPair("estimated_duration_ms", result.estimatedDurationMs);
  }
  data += "}";
  emitEvent("move-started", data);
}

bool startEnvelopePhase(const EnvelopePhase &phase) {
  if (phase.isHold) {
    String data = "{";
    data += jsonPair("phase", phase.name);
    data += ",";
    data += jsonPair("hold_ms", phase.holdMs);
    data += "}";
    emitEvent("envelope-hold", data);
    if (phase.holdMs == 0) {
      logLine("INFO", "envelope sustain hold until release");
    } else {
      String message = "envelope hold_ms=";
      message += phase.holdMs;
      logLine("INFO", message);
    }
    g_envelopeHolding = true;
    g_envelopeHoldUntilMs = millis() + phase.holdMs;
    return true;
  }

  String data = "{";
  data += jsonPair("phase", phase.name);
  data += ",";
  data += jsonPairFloat("target_percent", phase.targetPercent);
  data += ",";
  data += jsonPair("duration_ms", phase.durationMs);
  data += "}";
  emitEvent("envelope-phase", data);

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
    rspLine("OK", "envelope complete");
    setMotionState(MotionState::Idle, "envelope complete");
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
      rspLine("ERR", "envelope stopped: move failed");
      setMotionState(MotionState::Fault, "envelope move failed");
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
  emitRpcResult("{\"methods\":[\"help\",\"status\",\"stop\",\"clear-fault\",\"home\",\"jog\",\"unstick\",\"move-percent\",\"move-step\",\"move-percent-time\",\"move-percent-speed\",\"move-step-time\",\"move-step-speed\",\"adsr\",\"release\",\"current.status\",\"current.set\",\"speed.status\",\"speed.set-max\",\"ramp.status\",\"ramp.set\",\"speed-test\",\"stallguard.status\",\"stallguard.set-threshold\",\"characterize.move-check\",\"tmc.test\",\"tmc.read\",\"tmc.write\",\"tmc.raw\",\"bootsel\"]}");
}

void handleTmcCommand(const String &subcommand, const String &args) {
  if (subcommand.equalsIgnoreCase("test")) {
    emitRpcResult(String("{") + jsonPair("ok", g_stepper.tmcTest()) + "}");
    return;
  }

  if (subcommand.equalsIgnoreCase("read")) {
    uint8_t reg = 0;
    if (!parseByteArg(args, reg)) {
      emitRpcError(-32602, "tmc.read requires reg");
      return;
    }

    uint32_t value = 0;
    if (!g_stepper.readTmcRegister(reg, value)) {
      emitRpcError(-32010, "TMC read failed");
      return;
    }

    String result = "{";
    result += jsonPair("reg", static_cast<uint32_t>(reg));
    result += ",";
    result += jsonPair("reg_hex", String("0x") + hexByte(reg));
    result += ",";
    result += jsonPair("value", value);
    result += ",";
    result += jsonPair("value_hex", hex32(value));
    result += "}";
    emitRpcResult(result);
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
      emitRpcError(-32602, "tmc.write requires reg and value");
      return;
    }

    const bool verify = !verifyToken.equalsIgnoreCase("noverify");
    if (!g_stepper.writeTmcRegister(reg, value, verify)) {
      emitRpcError(-32011, "TMC write failed");
      return;
    }

    String result = "{";
    result += jsonPair("reg", static_cast<uint32_t>(reg));
    result += ",";
    result += jsonPair("reg_hex", String("0x") + hexByte(reg));
    result += ",";
    result += jsonPair("value", value);
    result += ",";
    result += jsonPair("value_hex", hex32(value));
    result += ",";
    result += jsonPair("verified", verify);
    result += "}";
    emitRpcResult(result);
    return;
  }

  if (subcommand.equalsIgnoreCase("raw")) {
    uint8_t txBytes[kTmcRawMaxBytes] = {};
    uint8_t rxBytes[kTmcRawMaxBytes] = {};
    bool ok = false;
    const size_t txCount = parseByteList(args, txBytes, sizeof(txBytes), ok);
    if (!ok || txCount == 0) {
      emitRpcError(-32602, "tmc.raw requires bytes");
      return;
    }

    const size_t rxCount = g_stepper.transferTmc(txBytes, txCount, rxBytes, sizeof(rxBytes), kTmcRawReadTimeoutMs);
    String result = "{\"tx\":[";
    for (size_t i = 0; i < txCount; ++i) {
      if (i > 0) {
        result += ",";
      }
      result += static_cast<uint32_t>(txBytes[i]);
    }

    result += "],\"rx\":[";
    for (size_t i = 0; i < rxCount; ++i) {
      if (i > 0) {
        result += ",";
      }
      result += static_cast<uint32_t>(rxBytes[i]);
    }
    result += "]}";
    emitRpcResult(result);
    return;
  }

  emitRpcError(-32601, "unknown tmc method");
}

void printStatus() {
  String result = "{";
  result += jsonPair("motion_state", motionStateName(g_motionState));
  result += ",";
  result += jsonPair("run_current", static_cast<uint32_t>(g_stepper.getRunCurrent()));
  result += ",";
  result += jsonPair("recovery_current", static_cast<uint32_t>(g_stepper.getRecoveryRunCurrent()));
  if (g_stepper.getRecoveryRunCurrent() <= g_stepper.getRunCurrent()) {
    logLine("WARN", "recovery current has no headroom above normal run current");
  }
  result += ",";
  result += jsonPair("idle_current", static_cast<uint32_t>(g_stepper.getIdleCurrent()));
  result += ",";
  result += jsonPair("idle_delay", static_cast<uint32_t>(g_stepper.getIdlePowerDownDelay()));
  uint32_t drvStatus = 0;
  if (g_stepper.readTmcRegister(0x6F, drvStatus)) {
    result += ",\"drv_status\":{";
    result += jsonPair("value", drvStatus);
    result += ",";
    result += jsonPair("hex", hex32(drvStatus));
    result += ",";
    result += jsonPair("cs_actual", static_cast<uint32_t>((drvStatus >> 16) & 0x1F));
    result += ",";
    result += jsonPair("standstill", (drvStatus & (1UL << 31)) != 0);
    result += "}";
  }
  result += ",";
  result += jsonPair("homed", g_stepper.isCalibrated());
  if (g_stepper.isCalibrated()) {
    result += ",";
    result += jsonPair("travel_steps", g_stepper.getTravelSteps());
    result += ",";
    result += jsonPair("current_step", g_stepper.getCurrentPositionSteps());
    result += ",";
    result += jsonPairFloat("current_position_percent", g_stepper.getPositionPercent());
  }
  result += ",";
  result += jsonPair("minimum_valid_travel_steps", g_stepper.getMinValidTravelSteps());
  result += ",";
  result += jsonPair("min_move_frequency_hz", g_stepper.getMinMoveFrequency());
  result += ",";
  result += jsonPair("max_move_frequency_hz", g_stepper.getMaxMoveFrequency());
  result += ",";
  result += jsonPair("safe_max_speed_hz", g_stepper.getSafeMaxMoveFrequency());
  result += ",";
  result += jsonPair("move_ramp_duration_ms", g_stepper.getMoveRampDurationMs());
  if (g_stepper.getLastMoveMinStallguard() >= 0) {
    result += ",";
    result += jsonPair("last_move_min_stallguard", g_stepper.getLastMoveMinStallguard());
  }
  result += ",";
  result += jsonPair("last_move_low_margin", g_stepper.didLastMoveWarnLowMargin());
  result += "}";
  emitRpcResult(result);
}

void runCentering(uint32_t requestedFrequency) {
  clearEnvelope();
  g_motionInProgress = true;
  setMotionState(MotionState::Homing, "home command");

  String message = "home requested_hz=";
  message += requestedFrequency;
  logLine("INFO", message);

  const bool ok = g_stepper.centering(requestedFrequency);
  if (ok) {
    setMotionState(MotionState::Idle, "home complete");
    String result = "{";
    result += jsonPair("ok", true);
    result += ",";
    result += jsonPair("homed", g_stepper.isCalibrated());
    result += ",";
    result += jsonPair("travel_steps", g_stepper.getTravelSteps());
    result += ",";
    result += jsonPair("current_step", g_stepper.getCurrentPositionSteps());
    result += ",";
    result += jsonPairFloat("position_percent", g_stepper.getPositionPercent());
    result += ",";
    result += jsonPair("min_move_frequency_hz", g_stepper.getMinMoveFrequency());
    result += ",";
    result += jsonPair("max_move_frequency_hz", g_stepper.getMaxMoveFrequency());
    result += "}";
    emitRpcResult(result);
  } else {
    setMotionState(MotionState::Fault, "home failed");
    emitRpcError(-32020, "home failed");
  }

  g_motionInProgress = false;
  drainPendingSerialInput();
}

void runJogCommand(bool positiveDirection, uint32_t steps, uint32_t frequency, bool useRecoveryCurrent) {
  clearEnvelope();
  g_motionInProgress = true;
  setMotionState(MotionState::Jogging, "jog command");
  String message = "jog direction=";
  message += positiveDirection ? "+" : "-";
  message += " steps=";
  message += steps;
  message += " hz=";
  message += frequency == 0 ? g_stepper.getMaxMoveFrequency() : frequency;
  if (useRecoveryCurrent) {
    message += " recovery_current=yes";
  }
  logLine("INFO", message);

  const bool ok = g_stepper.jog(positiveDirection, steps, frequency, useRecoveryCurrent);
  setMotionState(ok ? MotionState::Idle : MotionState::Fault, ok ? "jog complete" : "jog failed");
  g_motionInProgress = false;
  drainPendingSerialInput();
  if (ok) {
    emitRpcResult(String("{") + jsonPair("ok", true) + "}");
  } else {
    emitRpcError(-32021, "jog failed");
  }
}

void runMoveToPercent(float percent, uint32_t requestedFrequency) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }

  const float clampedPercent = constrain(percent, 0.0f, 100.0f);
  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToPercentAtFrequency(clampedPercent, requestedFrequency);
  if (result.accepted) {
    setMotionState(MotionState::Moving, "pos command");
    printMoveResult(result);
    emitRpcResult(String("{") + jsonPair("accepted", true) + "}");
  } else {
    emitRpcError(-32031, "move failed");
  }
}

void runMoveToStep(uint32_t targetStep, uint32_t requestedFrequency) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToStepAtFrequency(targetStep, requestedFrequency);
  if (result.accepted) {
    setMotionState(MotionState::Moving, "step command");
    printMoveResult(result);
    emitRpcResult(String("{") + jsonPair("accepted", true) + "}");
  } else {
    emitRpcError(-32031, "move failed");
  }
}

void runMoveToStepInTime(uint32_t targetStep, uint32_t durationMs) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToStepInTime(targetStep, durationMs);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(MotionState::Moving, "step-time command");
  }
  printMoveResult(result);
  emitRpcResult(String("{") + jsonPair("accepted", result.accepted) + "}");
}

void runMoveToStepAtSpeed(uint32_t targetStep, uint32_t frequency) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToStepAtFrequency(targetStep, frequency);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(MotionState::Moving, "step-speed command");
  }
  printMoveResult(result);
  emitRpcResult(String("{") + jsonPair("accepted", result.accepted) + "}");
}

void runMoveToPercentInTime(float percent, uint32_t durationMs) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToPercentInTime(percent, durationMs);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(MotionState::Moving, "pos-time command");
  }
  printMoveResult(result);
  emitRpcResult(String("{") + jsonPair("accepted", result.accepted) + "}");
}

void runMoveToPercentAtSpeed(float percent, uint32_t frequency) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToPercentAtFrequency(percent, frequency);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(MotionState::Moving, "pos-speed command");
  }
  printMoveResult(result);
  emitRpcResult(String("{") + jsonPair("accepted", result.accepted) + "}");
}

void printSpeedStatus() {
  String result = "{";
  result += jsonPair("min_move_frequency_hz", g_stepper.getMinMoveFrequency());
  result += ",";
  result += jsonPair("max_move_frequency_hz", g_stepper.getMaxMoveFrequency());
  result += ",";
  result += jsonPair("safe_max_speed_hz", g_stepper.getSafeMaxMoveFrequency());
  result += ",";
  result += jsonPair("move_ramp_duration_ms", g_stepper.getMoveRampDurationMs());
  if (g_stepper.getLastMoveMinStallguard() >= 0) {
    result += ",";
    result += jsonPair("last_move_min_stallguard", g_stepper.getLastMoveMinStallguard());
  }
  result += ",";
  result += jsonPair("last_move_low_margin", g_stepper.didLastMoveWarnLowMargin());
  result += "}";
  emitRpcResult(result);
}

void runSpeedTestCommand(uint32_t startHz, uint32_t endHz, uint32_t stepHz, uint32_t repeats) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32040, "speed-test rejected: run home first");
    return;
  }

  if (startHz == 0 || endHz == 0 || stepHz == 0) {
    emitRpcError(-32602, "speed-test requires start_hz, end_hz, and step_hz");
    return;
  }
  repeats = constrain(repeats == 0 ? 2UL : repeats, 1UL, 10UL);

  clearEnvelope();
  setMotionState(MotionState::SpeedTesting, "speed-test command");
  String message = "speed-test start_hz=";
  message += startHz;
  message += " end_hz=";
  message += endHz;
  message += " step_hz=";
  message += stepHz;
  message += " repeats=";
  message += repeats;
  logLine("INFO", message);

  const Stepper::SpeedTestResult result =
      g_stepper.runSpeedTest(startHz, endHz, stepHz, static_cast<uint8_t>(repeats));
  if (!result.accepted) {
    setMotionState(MotionState::Idle, "speed-test rejected");
    emitRpcError(-32041, "speed-test rejected");
    return;
  }

  String response = "{";
  response += jsonPair("accepted", true);
  response += ",";
  response += jsonPair("tested_count", result.testedCount);
  response += ",";
  response += jsonPair("passed_count", result.passedCount);
  response += ",";
  response += jsonPair("highest_passed_frequency_hz", result.highestPassedFrequency);
  response += ",";
  response += jsonPair("first_failed_frequency_hz", result.firstFailedFrequency);
  response += ",";
  response += jsonPair("safe_max_speed_hz", g_stepper.getSafeMaxMoveFrequency());
  response += "}";
  if (result.highestPassedFrequency > 0) {
    String data = "{";
    data += jsonPair("safe_max_speed_hz", result.highestPassedFrequency);
    data += "}";
    emitEvent("safe-max-speed-updated", data);
  }
  setMotionState(MotionState::Idle, "speed-test complete");
  drainPendingSerialInput();
  emitRpcResult(response);
}

void emitCharacterizeCycle(const Stepper::CharacterizeCycle &cycle, void *context) {
  CharacterizeEventContext *eventContext = static_cast<CharacterizeEventContext *>(context);
  String data = "{";
  if (eventContext != nullptr) {
    data += jsonPair("request_id", eventContext->requestId);
    data += ",";
  }
  data += jsonPair("cycle", cycle.cycle);
  data += ",";
  data += jsonPair("frequency_hz", cycle.frequencyHz);
  data += ",";
  data += jsonPair("stallguard_threshold", static_cast<uint32_t>(cycle.stallguardThreshold));
  data += ",";
  data += jsonPair("endpoint_tolerance_steps", cycle.endpointToleranceSteps);
  data += ",";
  data += jsonPair("target_step", cycle.targetStep);
  data += ",";
  data += jsonPair("move_accepted", cycle.moveAccepted);
  data += ",";
  data += jsonPair("move_completed", cycle.moveCompleted);
  data += ",";
  data += jsonPair("home_accepted", cycle.homeAccepted);
  data += ",";
  data += jsonPair("home_completed", cycle.homeCompleted);
  data += ",";
  data += jsonPair("passed", cycle.passed);
  data += ",";
  data += jsonPair("low_margin", cycle.lowMargin);
  data += ",";
  data += jsonPair("threshold_failed", cycle.thresholdFailed);
  data += ",";
  data += jsonPair("move_min_stallguard", cycle.moveMinStallguard);
  data += ",";
  data += jsonPair("home_min_stallguard", cycle.homeMinStallguard);
  data += ",";
  data += jsonPair("endpoint_delta_steps", cycle.endpointDeltaSteps);
  data += "}";
  emitEvent("characterize-cycle", data);
}

void runCharacterizeMoveCheck(uint32_t frequency, uint32_t threshold, uint32_t cycles, float travelPercent,
                              uint32_t endpointToleranceSteps) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32070, "characterize.move-check rejected: run home first");
    return;
  }
  if (frequency == 0 || threshold > 255 || cycles == 0) {
    emitRpcError(-32602, "characterize.move-check requires hz, threshold 0-255, and cycles");
    return;
  }

  clearEnvelope();
  setMotionState(MotionState::SpeedTesting, "characterize.move-check command");
  CharacterizeEventContext eventContext;
  eventContext.requestId = g_currentRequestId;
  eventContext.frequencyHz = frequency;
  eventContext.stallguardThreshold = threshold;
  eventContext.endpointToleranceSteps = endpointToleranceSteps;
  Stepper::CharacterizeResult result =
      g_stepper.runCharacterization(frequency, static_cast<uint8_t>(threshold), cycles, travelPercent,
                                    endpointToleranceSteps, emitCharacterizeCycle, &eventContext);
  setMotionState(MotionState::Idle, "characterize.move-check complete");
  drainPendingSerialInput();

  if (!result.accepted) {
    emitRpcError(-32071, "characterize.move-check rejected");
    return;
  }

  String response = "{";
  response += jsonPair("accepted", true);
  response += ",";
  response += jsonPair("frequency_hz", result.frequencyHz);
  response += ",";
  response += jsonPair("stallguard_threshold", static_cast<uint32_t>(result.stallguardThreshold));
  response += ",";
  response += jsonPair("cycles_requested", result.cyclesRequested);
  response += ",";
  response += jsonPair("cycles_completed", result.cyclesCompleted);
  response += ",";
  response += jsonPair("cycles_passed", result.cyclesPassed);
  response += ",";
  response += jsonPair("min_stallguard", result.minStallguard);
  response += ",";
  response += jsonPair("max_abs_endpoint_delta_steps", result.maxAbsEndpointDeltaSteps);
  response += ",";
  response += jsonPair("low_margin_failures", result.lowMarginFailures);
  response += ",";
  response += jsonPair("threshold_failures", result.thresholdFailures);
  response += ",";
  response += jsonPair("endpoint_failures", result.endpointFailures);
  response += ",";
  response += jsonPair("motion_failures", result.motionFailures);
  response += "}";
  emitRpcResult(response);
}

void startAdsrEnvelope(float attackPercent, uint32_t attackMs, float decayPercent, uint32_t decayMs,
                       uint32_t sustainMs, float releasePercent, uint32_t releaseMs) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32050, "envelope rejected: run home first");
    return;
  }

  if (attackMs == 0 || decayMs == 0 || releaseMs == 0) {
    emitRpcError(-32602, "adsr requires nonzero attack_ms, decay_ms, and release_ms");
    return;
  }

  clearEnvelope();
  g_stepper.stopStepper();
  setMotionState(MotionState::Envelope, "adsr command");
  g_envelopePhases[0] = {"attack", attackPercent, attackMs, 0, false};
  g_envelopePhases[1] = {"decay", decayPercent, decayMs, 0, false};
  g_envelopePhases[2] = {"sustain", decayPercent, 0, sustainMs, true};
  g_envelopePhases[3] = {"release", releasePercent, releaseMs, 0, false};
  g_envelopePhaseCount = sustainMs == 0 ? 3 : 4;
  g_envelopePhaseIndex = 0;
  g_envelopeActive = true;
  emitRpcResult(String("{") + jsonPair("accepted", true) + "}");
  startNextEnvelopePhase();
}

void startReleaseMove(float releasePercent, uint32_t releaseMs) {
  if (!g_stepper.isCalibrated()) {
    emitRpcError(-32051, "release rejected: run home first");
    return;
  }

  if (releaseMs == 0) {
    emitRpcError(-32602, "release requires percent and ms");
    return;
  }

  clearEnvelope();
  const Stepper::MoveCommandResult result = g_stepper.moveToPercentInTime(releasePercent, releaseMs);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(MotionState::Moving, "release command");
  }
  printMoveResult(result);
  emitRpcResult(String("{") + jsonPair("accepted", result.accepted) + "}");
}

bool requireParam(bool condition, const char *message) {
  if (condition) {
    return true;
  }
  emitRpcError(-32602, message);
  return false;
}

bool selectCommandChannel(const String &params) {
  g_currentChannel = kDefaultChannel;

  String rawChannel;
  if (!extractJsonValue(params, "channel", rawChannel)) {
    return true;
  }

  uint32_t channel = 0;
  if (!jsonUintField(params, "channel", channel) || channel >= kChannelCount) {
    emitRpcError(-32602, "unsupported channel");
    return false;
  }

  g_currentChannel = channel;
  return true;
}

void handleJsonRpcCommand(const String &rawLine) {
  String method;
  String params;
  String idRaw;
  bool hasId = false;
  if (!parseJsonRpcRequest(rawLine, method, params, idRaw, hasId)) {
    g_currentChannel = kDefaultChannel;
    g_currentRequestId = "null";
    g_currentRequestHasId = true;
    emitRpcError(-32700, "expected newline-delimited JSON-RPC request");
    g_currentRequestHasId = false;
    return;
  }

  g_currentRequestId = idRaw;
  g_currentRequestHasId = hasId;
  if (!selectCommandChannel(params)) {
    g_currentRequestHasId = false;
    return;
  }
  emitEvent("command-received", String("{") + jsonPair("method", method) + "}");

  if (method == "help") {
    printSerialHelp();
  } else if (method == "status") {
    printStatus();
  } else if (method == "stop" || method == "abort") {
    stopAllMotion(method.c_str());
    emitRpcResult(String("{") + jsonPair("ok", true) + "}");
  } else if (method == "clear-fault") {
    const bool wasFault = g_motionState == MotionState::Fault;
    if (wasFault) {
      setMotionState(MotionState::Idle, "clear-fault command");
    }
    String result = "{";
    result += jsonPair("ok", true);
    result += ",";
    result += jsonPair("was_fault", wasFault);
    result += "}";
    emitRpcResult(result);
  } else if (method == "bootsel" || method == "bootloader") {
    emitRpcResult(String("{") + jsonPair("rebooting", true) + "}");
    Serial.flush();
    delay(100);
    rp2040.rebootToBootloader();
  } else if (method == "tmc.test") {
    emitRpcResult(String("{") + jsonPair("ok", g_stepper.tmcTest()) + "}");
  } else if (method == "tmc.read") {
    uint32_t regValue = 0;
    if (requireParam(jsonUintField(params, "reg", regValue) && regValue <= 0xFF, "tmc.read requires reg 0-255")) {
      handleTmcCommand("read", String(regValue));
    }
  } else if (method == "tmc.write") {
    uint32_t regValue = 0;
    uint32_t value = 0;
    bool verify = true;
    (void)jsonBoolField(params, "verify", verify);
    if (requireParam(jsonUintField(params, "reg", regValue) && regValue <= 0xFF && jsonUintField(params, "value", value),
                     "tmc.write requires reg 0-255 and value")) {
      String args = String(regValue) + " " + String(value) + (verify ? "" : " noverify");
      handleTmcCommand("write", args);
    }
  } else if (method == "tmc.raw") {
    String bytes;
    if (requireParam(extractJsonValue(params, "bytes", bytes), "tmc.raw requires bytes")) {
      bytes.replace("[", "");
      bytes.replace("]", "");
      handleTmcCommand("raw", bytes);
    }
  } else if (method == "current.status") {
    String result = "{";
    result += jsonPair("run", static_cast<uint32_t>(g_stepper.getRunCurrent()));
    result += ",";
    result += jsonPair("recovery", static_cast<uint32_t>(g_stepper.getRecoveryRunCurrent()));
    result += ",";
    result += jsonPair("idle", static_cast<uint32_t>(g_stepper.getIdleCurrent()));
    result += ",";
    result += jsonPair("idle_delay", static_cast<uint32_t>(g_stepper.getIdlePowerDownDelay()));
    result += "}";
    emitRpcResult(result);
  } else if (method == "current.set") {
    String which;
    uint32_t value = 0;
    if (!requireParam(jsonStringField(params, "which", which) && jsonUintField(params, "value", value),
                      "current.set requires which and value")) {
      g_currentRequestHasId = false;
      return;
    }
    bool ok = false;
    if ((which == "run" || which == "normal") && value <= 31) {
      ok = g_stepper.setRunCurrent(static_cast<uint8_t>(value));
    } else if (which == "recovery" && value <= 31) {
      ok = g_stepper.setRecoveryRunCurrent(static_cast<uint8_t>(value));
    } else if (which == "idle" && value <= 31) {
      ok = g_stepper.setIdleCurrent(static_cast<uint8_t>(value));
    } else if (which == "idle-delay" && value <= 255) {
      ok = g_stepper.setIdlePowerDownDelay(static_cast<uint8_t>(value));
    } else {
      emitRpcError(-32602, "invalid current field or value");
      g_currentRequestHasId = false;
      return;
    }
    if (ok) {
      emitRpcResult(String("{") + jsonPair("ok", true) + "}");
    } else {
      emitRpcError(-32060, "failed to set current configuration");
    }
  } else if (method == "speed.status") {
    printSpeedStatus();
  } else if (method == "speed.set-max") {
    uint32_t value = 0;
    if (requireParam(jsonUintField(params, "hz", value), "speed.set-max requires hz")) {
      if (g_stepper.setSafeMaxMoveFrequency(value)) {
        emitRpcResult(String("{") + jsonPair("safe_max_speed_hz", g_stepper.getSafeMaxMoveFrequency()) + "}");
      } else {
        emitRpcError(-32602, "invalid safe max speed");
      }
    }
  } else if (method == "ramp.status") {
    emitRpcResult(String("{") + jsonPair("duration_ms", g_stepper.getMoveRampDurationMs()) + "}");
  } else if (method == "ramp.set") {
    uint32_t value = 0;
    if (requireParam(jsonUintField(params, "duration_ms", value), "ramp.set requires duration_ms")) {
      if (g_stepper.setMoveRampDurationMs(value)) {
        emitRpcResult(String("{") + jsonPair("duration_ms", g_stepper.getMoveRampDurationMs()) + "}");
      } else {
        emitRpcError(-32602, "invalid ramp duration");
      }
    }
  } else if (method == "stallguard.status") {
    int32_t sg = g_stepper.readStallguard();
    String result = "{";
    result += jsonPair("sg_result", sg);
    result += "}";
    emitRpcResult(result);
  } else if (method == "stallguard.set-threshold") {
    uint32_t threshold = 0;
    if (requireParam(jsonUintField(params, "threshold", threshold) && threshold <= 255,
                     "stallguard.set-threshold requires threshold 0-255")) {
      g_stepper.setStallguard(static_cast<uint8_t>(threshold));
      emitRpcResult(String("{") + jsonPair("threshold", threshold) + "}");
    }
  } else if (method == "speed-test") {
    uint32_t startHz = 0;
    uint32_t endHz = 0;
    uint32_t stepHz = 0;
    uint32_t repeats = 2;
    if (requireIdleForCommand("speed-test") &&
        requireParam(jsonUintField(params, "start_hz", startHz) && jsonUintField(params, "end_hz", endHz) &&
                         jsonUintField(params, "step_hz", stepHz),
                     "speed-test requires start_hz, end_hz, and step_hz")) {
      (void)jsonUintField(params, "repeats", repeats);
      runSpeedTestCommand(startHz, endHz, stepHz, repeats);
    }
  } else if (method == "characterize.move-check") {
    uint32_t frequency = 0;
    uint32_t threshold = 0;
    uint32_t cycles = 0;
    uint32_t endpointToleranceSteps = 8;
    float travelPercent = 100.0f;
    (void)jsonUintField(params, "endpoint_tolerance_steps", endpointToleranceSteps);
    (void)jsonFloatField(params, "travel_percent", travelPercent);
    if (requireIdleForCommand("characterize.move-check") &&
        requireParam(jsonUintField(params, "hz", frequency) && jsonUintField(params, "threshold", threshold) &&
                         jsonUintField(params, "cycles", cycles),
                     "characterize.move-check requires hz, threshold, and cycles")) {
      runCharacterizeMoveCheck(frequency, threshold, cycles, travelPercent, endpointToleranceSteps);
    }
  } else if (method == "home") {
    uint32_t frequency = 0;
    (void)jsonUintField(params, "hz", frequency);
    if (requireIdleForCommand("home")) {
      runCentering(frequency == 0 ? nextDemoFrequency() : frequency);
    }
  } else if (method == "jog") {
    String direction;
    uint32_t steps = 0;
    uint32_t frequency = 0;
    bool recovery = false;
    (void)jsonUintField(params, "hz", frequency);
    (void)jsonBoolField(params, "recovery", recovery);
    if (requireIdleForCommand("jog") &&
        requireParam(jsonStringField(params, "direction", direction) && jsonUintField(params, "steps", steps) && steps > 0,
                     "jog requires direction and steps")) {
      const bool positive = direction == "+" || direction == "pos" || direction == "positive";
      const bool negative = direction == "-" || direction == "neg" || direction == "negative";
      if (requireParam(positive || negative, "jog direction must be + or -")) {
        runJogCommand(positive, steps, frequency, recovery);
      }
    }
  } else if (method == "unstick") {
    uint32_t steps = 500;
    uint32_t frequency = 400;
    (void)jsonUintField(params, "steps", steps);
    (void)jsonUintField(params, "hz", frequency);
    if (requireIdleForCommand("unstick")) {
      runJogCommand(true, steps, frequency, true);
    }
  } else if (method == "move-percent") {
    float percent = 0.0f;
    uint32_t frequency = 0;
    (void)jsonUintField(params, "hz", frequency);
    if (requireIdleForCommand("move-percent") && requireHomedForCommand("move-percent") &&
        requireParam(jsonFloatField(params, "percent", percent), "move-percent requires percent")) {
      runMoveToPercent(percent, frequency);
    }
  } else if (method == "move-step") {
    uint32_t step = 0;
    uint32_t frequency = 0;
    (void)jsonUintField(params, "hz", frequency);
    if (requireIdleForCommand("move-step") && requireHomedForCommand("move-step") &&
        requireParam(jsonUintField(params, "step", step), "move-step requires step")) {
      runMoveToStep(step, frequency);
    }
  } else if (method == "move-percent-time") {
    float percent = 0.0f;
    uint32_t durationMs = 0;
    if (requireIdleForCommand("move-percent-time") && requireHomedForCommand("move-percent-time") &&
        requireParam(jsonFloatField(params, "percent", percent) && jsonUintField(params, "duration_ms", durationMs) && durationMs > 0,
                     "move-percent-time requires percent and duration_ms")) {
      runMoveToPercentInTime(percent, durationMs);
    }
  } else if (method == "move-percent-speed") {
    float percent = 0.0f;
    uint32_t frequency = 0;
    if (requireIdleForCommand("move-percent-speed") && requireHomedForCommand("move-percent-speed") &&
        requireParam(jsonFloatField(params, "percent", percent) && jsonUintField(params, "hz", frequency) && frequency > 0,
                     "move-percent-speed requires percent and hz")) {
      runMoveToPercentAtSpeed(percent, frequency);
    }
  } else if (method == "move-step-time") {
    uint32_t step = 0;
    uint32_t durationMs = 0;
    if (requireIdleForCommand("move-step-time") && requireHomedForCommand("move-step-time") &&
        requireParam(jsonUintField(params, "step", step) && jsonUintField(params, "duration_ms", durationMs) && durationMs > 0,
                     "move-step-time requires step and duration_ms")) {
      runMoveToStepInTime(step, durationMs);
    }
  } else if (method == "move-step-speed") {
    uint32_t step = 0;
    uint32_t frequency = 0;
    if (requireIdleForCommand("move-step-speed") && requireHomedForCommand("move-step-speed") &&
        requireParam(jsonUintField(params, "step", step) && jsonUintField(params, "hz", frequency) && frequency > 0,
                     "move-step-speed requires step and hz")) {
      runMoveToStepAtSpeed(step, frequency);
    }
  } else if (method == "adsr") {
    float attackPercent = 0.0f;
    float decayPercent = 0.0f;
    float releasePercent = 0.0f;
    uint32_t attackMs = 0;
    uint32_t decayMs = 0;
    uint32_t sustainMs = 0;
    uint32_t releaseMs = 0;
    if (requireIdleForCommand("adsr") && requireHomedForCommand("adsr") &&
        requireParam(jsonFloatField(params, "attack_percent", attackPercent) && jsonUintField(params, "attack_ms", attackMs) &&
                         jsonFloatField(params, "decay_percent", decayPercent) && jsonUintField(params, "decay_ms", decayMs) &&
                         jsonUintField(params, "sustain_ms", sustainMs) && jsonFloatField(params, "release_percent", releasePercent) &&
                         jsonUintField(params, "release_ms", releaseMs),
                     "adsr requires attack_percent, attack_ms, decay_percent, decay_ms, sustain_ms, release_percent, release_ms")) {
      startAdsrEnvelope(attackPercent, attackMs, decayPercent, decayMs, sustainMs, releasePercent, releaseMs);
    }
  } else if (method == "release") {
    float percent = 0.0f;
    uint32_t durationMs = 0;
    if (requireIdleForCommand("release") && requireHomedForCommand("release") &&
        requireParam(jsonFloatField(params, "percent", percent) && jsonUintField(params, "duration_ms", durationMs),
                     "release requires percent and duration_ms")) {
      startReleaseMove(percent, durationMs);
    }
  } else {
    emitRpcError(-32601, "method not found");
  }

  g_currentRequestHasId = false;
}

void processSerialInput() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      handleJsonRpcCommand(g_serialLine);
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

  g_stepper.setLogCallback(logLine);
  g_stepper.setServiceCallback(serviceSerialOutput);
  logLine("INFO", "startup delay before stepper initialization");
  g_rgbLed.heartBeat(10, 1000);

  if (!g_stepper.begin()) {
    logLine("ERROR", "failed to initialize stepper");
    return;
  }

  if (g_stepper.tmcTest()) {
    emitEvent("ready", "{\"protocol\":\"json-rpc\",\"transport\":\"newline-delimited-json\",\"firmware\":\"stepper-rp2040\"}");
    emitStateChange(true);
  } else {
    logLine("ERROR", "TMC driver UART did not respond; check stepper power");
  }
}

void loop() {
  serviceSerialOutput();
  processSerialInput();

  const Stepper::MoveUpdate moveUpdate = g_stepper.serviceMove();
  if (moveUpdate == Stepper::MoveUpdate::Completed || moveUpdate == Stepper::MoveUpdate::HomeAdjusted) {
    String message = "move complete step=";
    message += g_stepper.getCurrentPositionSteps();
    message += " percent=";
    message += String(g_stepper.getPositionPercent(), 1);
    rspLine("OK", message);
    setMotionState(MotionState::Idle, "move complete");
    if (moveUpdate == Stepper::MoveUpdate::HomeAdjusted) {
      printStatus();
    }
  } else if (moveUpdate == Stepper::MoveUpdate::Failed) {
    rspLine("ERR", "move failed");
    setMotionState(MotionState::Fault, "move failed");
  }
  serviceEnvelope(moveUpdate);
  emitStateChange();

  const bool buttonLevel = digitalRead(kButtonPin);
  const uint32_t now = millis();

  if (buttonLevel != g_lastButtonLevel) {
    g_lastDebounceMs = now;
    g_lastButtonLevel = buttonLevel;
  }

  if (isMotionIdle() && !buttonLevel &&
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
