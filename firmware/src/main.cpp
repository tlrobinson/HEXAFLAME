#include <Arduino.h>
#include <cctype>
#include <cstdlib>

#include "board_config.h"
#include "status_led.h"
#include "stepper.h"

#if defined(BOARD_OCTOPUS)
#include "stm32/bootloader.h"
#include "stm32/firmware_update.h"
#include "stm32/self_flash.h"
#endif

// HEXAFLAME stepper firmware - shared application logic.
//
// One JSON-RPC connection over USB serial controls every channel; each
// request names a `channel` (defaulting to 0) and the dispatcher routes it to
// that channel's independent state machine. Non-blocking work - timed moves,
// absolute moves and ADSR envelopes - is serviced for all channels every loop
// iteration, so every channel can move at the same time. Blocking operations
// (homing, jog, speed-test, characterization) run to completion on their
// channel before the next request is handled.

namespace {
constexpr uint32_t kStepperFrequencies[2] = {400, 1200};
constexpr size_t kSerialLineMax = 256;
constexpr size_t kTmcRawMaxBytes = 32;
constexpr size_t kEnvelopeMaxPhases = 4;
constexpr uint32_t kTmcRawReadTimeoutMs = 50;
constexpr size_t kSerialOutputQueueSize = 64;
constexpr size_t kMaxChannels = 8;

#if defined(BOARD_RP2040)
constexpr uint8_t kButtonPin = 9;
constexpr uint32_t kDebounceMs = 10;
#endif

StatusLed g_statusLed;

enum class MotionState : uint8_t {
  Idle = 0,
  Homing,
  Jogging,
  Moving,
  Envelope,
  SpeedTesting,
  Fault,
};

struct EnvelopePhase {
  const char *name = "";
  float targetPercent = 0.0f;
  uint32_t durationMs = 0;
  uint32_t holdMs = 0;
  bool isHold = false;
};

// All per-channel state. Steppers themselves are allocated in setup() once the
// board has published its channel configuration.
struct ChannelState {
  uint8_t index = 0;
  Stepper *stepper = nullptr;
  MotionState motionState = MotionState::Idle;
  MotionState lastReportedMotionState = MotionState::Idle;
  bool lastReportedHomed = false;
  bool haveReportedState = false;
  bool motionInProgress = false;
  EnvelopePhase envelopePhases[kEnvelopeMaxPhases];
  size_t envelopePhaseCount = 0;
  size_t envelopePhaseIndex = 0;
  bool envelopeActive = false;
  bool envelopeWaitingForMove = false;
  bool envelopeHolding = false;
  uint32_t envelopeHoldUntilMs = 0;
  int lastDemoIdx = 1;
};

ChannelState g_channels[kMaxChannels];
size_t g_activeChannels = 1;

String g_serialLine;
String g_currentRequestId = "";
bool g_currentRequestHasId = false;
uint32_t g_currentChannel = 0;

String g_serialOutputQueue[kSerialOutputQueueSize];
size_t g_serialOutputHead = 0;
size_t g_serialOutputTail = 0;
size_t g_serialOutputCount = 0;
uint32_t g_serialOutputDropped = 0;
bool g_serialOutputDraining = false;

struct CharacterizeEventContext {
  String requestId;
  uint32_t channel = 0;
};

String nextToken(const String &line, int &offset);
void clearEnvelope(ChannelState &ch);
void emitStateChange(ChannelState &ch, bool force = false);

ChannelState &currentChannel() {
  return g_channels[g_currentChannel < g_activeChannels ? g_currentChannel : 0];
}

Stepper &currentStepper() {
  return *currentChannel().stepper;
}

// --------------------------------------------------------------------------
// Argument / JSON parsing helpers
// --------------------------------------------------------------------------

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

String nextToken(const String &line, int &offset) {
  while (offset < static_cast<int>(line.length()) &&
         (line[offset] == ' ' || line[offset] == '\t' || line[offset] == ',')) {
    ++offset;
  }
  const int start = offset;
  while (offset < static_cast<int>(line.length()) && line[offset] != ' ' && line[offset] != '\t' &&
         line[offset] != ',') {
    ++offset;
  }
  return line.substring(start, offset);
}

size_t parseByteList(const String &line, uint8_t *bytes, size_t maxBytes, bool &ok) {
  ok = true;
  size_t count = 0;
  int offset = 0;
  while (offset < static_cast<int>(line.length())) {
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
    while (cursor < static_cast<int>(json.length()) && isspace(json[cursor])) {
      ++cursor;
    }
    if (cursor < static_cast<int>(json.length()) && json[cursor] == ':') {
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
  while (cursor < static_cast<int>(json.length()) && isspace(json[cursor])) {
    ++cursor;
  }
  if (cursor >= static_cast<int>(json.length())) {
    return false;
  }

  const int start = cursor;
  if (json[cursor] == '"') {
    ++cursor;
    bool escaped = false;
    while (cursor < static_cast<int>(json.length())) {
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
    while (cursor < static_cast<int>(json.length())) {
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

  while (cursor < static_cast<int>(json.length()) && json[cursor] != ',' && json[cursor] != '}' &&
         json[cursor] != ']') {
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
  for (int i = 1; i < static_cast<int>(input.length()) - 1; ++i) {
    char ch = input[i];
    if (ch != '\\') {
      value += ch;
      continue;
    }
    if (++i >= static_cast<int>(input.length()) - 1) {
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

// --------------------------------------------------------------------------
// JSON encoding helpers
// --------------------------------------------------------------------------

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

String channelStateJson(ChannelState &ch) {
  Stepper &st = *ch.stepper;
  String state = "{";
  state += jsonPair("motion_state", motionStateName(ch.motionState));
  state += ",";
  state += jsonPair("homed", st.isCalibrated());
  if (st.isCalibrated()) {
    state += ",";
    state += jsonPair("travel_steps", st.getTravelSteps());
    state += ",";
    state += jsonPair("current_step", st.getCurrentPositionSteps());
    state += ",";
    state += jsonPairFloat("position_percent", st.getPositionPercent());
  }
  state += "}";
  return state;
}

// --------------------------------------------------------------------------
// Serial output queue
// --------------------------------------------------------------------------

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

// Stepper log callback - logs always carry the channel that produced them.
void logLine(uint8_t channel, const char *level, const String &message) {
  const uint32_t previousChannel = g_currentChannel;
  g_currentChannel = channel;
  String params = "{";
  params += jsonPair("channel", static_cast<uint32_t>(channel));
  params += ",";
  params += jsonPair("level", level);
  params += ",";
  params += jsonPair("message", message);
  params += "}";
  emitNotification("log", params);
  g_currentChannel = previousChannel;
}

void logCurrent(const char *level, const String &message) {
  logLine(static_cast<uint8_t>(g_currentChannel), level, message);
}

void emitEvent(const char *event, const String &dataJson = "{}") {
  String params = "{";
  params += jsonPair("event", event);
  params += ",\"data\":";
  params += jsonObjectWithChannel(dataJson.length() > 0 ? dataJson : "{}");
  params += "}";
  emitNotification("event", params);
}

void emitStateChange(ChannelState &ch, bool force) {
  const bool homed = ch.stepper->isCalibrated();
  if (!force && ch.haveReportedState && ch.lastReportedMotionState == ch.motionState &&
      ch.lastReportedHomed == homed) {
    return;
  }
  ch.lastReportedMotionState = ch.motionState;
  ch.lastReportedHomed = homed;
  ch.haveReportedState = true;
  emitEvent("state-change", channelStateJson(ch));
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
  data += jsonPair("ok", static_cast<bool>(String(status).equalsIgnoreCase("OK")));
  data += ",";
  data += jsonPair("status", status);
  data += ",";
  data += jsonPair("message", message);
  data += "}";
  emitEvent("response", data);
}

// --------------------------------------------------------------------------
// Per-channel motion helpers
// --------------------------------------------------------------------------

void setMotionState(ChannelState &ch, MotionState state, const char *reason) {
  if (ch.motionState == state) {
    return;
  }
  String message = "state ";
  message += motionStateName(ch.motionState);
  message += " -> ";
  message += motionStateName(state);
  if (reason != nullptr && reason[0] != '\0') {
    message += " reason=";
    message += reason;
  }
  logCurrent("INFO", message);
  ch.motionState = state;
  emitStateChange(ch);
}

bool isMotionIdle(ChannelState &ch) {
  return ch.motionState == MotionState::Idle && !ch.motionInProgress &&
         !ch.stepper->isMoveInProgress() && !ch.envelopeActive;
}

bool requireIdleForCommand(ChannelState &ch, const char *commandName) {
  if (isMotionIdle(ch)) {
    return true;
  }
  String message = "busy state=";
  message += motionStateName(ch.motionState);
  message += " rejected=";
  message += commandName;
  rspLine("ERR", message);
  return false;
}

bool requireHomedForCommand(ChannelState &ch, const char *commandName) {
  if (ch.stepper->isCalibrated()) {
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
    logCurrent("WARN", message);
  }
}

void clearEnvelope(ChannelState &ch) {
  ch.envelopePhaseCount = 0;
  ch.envelopePhaseIndex = 0;
  ch.envelopeActive = false;
  ch.envelopeWaitingForMove = false;
  ch.envelopeHolding = false;
  ch.envelopeHoldUntilMs = 0;
}

void stopAllMotion(ChannelState &ch, const char *reason) {
  clearEnvelope(ch);
  ch.stepper->stopStepper();
  ch.motionInProgress = false;
  setMotionState(ch, MotionState::Idle, reason);
  drainPendingSerialInput();
  rspLine("OK", "motion stopped");
}

void printMoveResult(ChannelState &ch, const Stepper::MoveCommandResult &result) {
  if (!result.accepted) {
    rspLine("ERR", "move rejected");
    return;
  }

  if (result.speedClampedHigh) {
    String message = "requested speed is faster than supported; clamped_hz=";
    message += result.actualFrequency;
    logCurrent("WARN", message);
  } else if (result.speedClampedLow) {
    String message = "requested speed is slower than supported; clamped_hz=";
    message += result.actualFrequency;
    logCurrent("WARN", message);
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

bool startEnvelopePhase(ChannelState &ch, const EnvelopePhase &phase) {
  if (phase.isHold) {
    String data = "{";
    data += jsonPair("phase", phase.name);
    data += ",";
    data += jsonPair("hold_ms", phase.holdMs);
    data += "}";
    emitEvent("envelope-hold", data);
    if (phase.holdMs == 0) {
      logCurrent("INFO", "envelope sustain hold until release");
    } else {
      String message = "envelope hold_ms=";
      message += phase.holdMs;
      logCurrent("INFO", message);
    }
    ch.envelopeHolding = true;
    ch.envelopeHoldUntilMs = millis() + phase.holdMs;
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

  Stepper::MoveCommandResult result = ch.stepper->moveToPercentInTime(phase.targetPercent, phase.durationMs);
  printMoveResult(ch, result);
  return result.accepted;
}

void startNextEnvelopePhase(ChannelState &ch) {
  ch.envelopeWaitingForMove = false;
  ch.envelopeHolding = false;

  while (ch.envelopeActive && ch.envelopePhaseIndex < ch.envelopePhaseCount) {
    EnvelopePhase &phase = ch.envelopePhases[ch.envelopePhaseIndex++];
    if (!startEnvelopePhase(ch, phase)) {
      clearEnvelope(ch);
      return;
    }
    if (phase.isHold) {
      return;
    }
    ch.envelopeWaitingForMove = true;
    return;
  }

  if (ch.envelopeActive) {
    rspLine("OK", "envelope complete");
    setMotionState(ch, MotionState::Idle, "envelope complete");
  }
  clearEnvelope(ch);
}

void serviceEnvelope(ChannelState &ch, Stepper::MoveUpdate moveUpdate) {
  if (!ch.envelopeActive) {
    return;
  }

  if (ch.envelopeWaitingForMove) {
    if (moveUpdate == Stepper::MoveUpdate::None) {
      return;
    }
    if (moveUpdate == Stepper::MoveUpdate::Failed) {
      rspLine("ERR", "envelope stopped: move failed");
      setMotionState(ch, MotionState::Fault, "envelope move failed");
      clearEnvelope(ch);
      return;
    }
    startNextEnvelopePhase(ch);
    return;
  }

  if (ch.envelopeHolding) {
    if (ch.envelopePhases[ch.envelopePhaseIndex - 1].holdMs == 0) {
      return;
    }
    if (static_cast<int32_t>(millis() - ch.envelopeHoldUntilMs) < 0) {
      return;
    }
    startNextEnvelopePhase(ch);
    return;
  }

  startNextEnvelopePhase(ch);
}

uint32_t nextDemoFrequency(ChannelState &ch) {
  const int idx = (ch.lastDemoIdx == 1) ? 0 : 1;
  ch.lastDemoIdx = idx;
  return kStepperFrequencies[idx];
}

// --------------------------------------------------------------------------
// Command implementations
// --------------------------------------------------------------------------

void printSerialHelp() {
  emitRpcResult(
      "{\"methods\":[\"help\",\"status\",\"board\",\"stop\",\"clear-fault\",\"home\",\"jog\",\"unstick\","
      "\"move-percent\",\"move-step\",\"move-percent-time\",\"move-percent-speed\",\"move-step-time\","
      "\"move-step-speed\",\"adsr\",\"release\",\"current.status\",\"current.set\",\"speed.status\","
      "\"speed.set-max\",\"ramp.status\",\"ramp.set\",\"speed-test\",\"stallguard.status\","
      "\"stallguard.set-threshold\",\"characterize.move-check\",\"tmc.test\",\"tmc.read\",\"tmc.write\","
      "\"tmc.raw\",\"bootsel\",\"dfu\",\"firmware.upload\"]}");
}

void printBoardInfo() {
  String result = "{";
  result += jsonPair("board", Board::name());
  result += ",";
  result += jsonPair("channel_count", static_cast<uint32_t>(g_activeChannels));
  result += "}";
  emitRpcResult(result);
}

void handleTmcCommand(Stepper &st, const String &subcommand, const String &args) {
  if (subcommand.equalsIgnoreCase("test")) {
    emitRpcResult(String("{") + jsonPair("ok", st.tmcTest()) + "}");
    return;
  }

  if (subcommand.equalsIgnoreCase("read")) {
    uint8_t reg = 0;
    if (!parseByteArg(args, reg)) {
      emitRpcError(-32602, "tmc.read requires reg");
      return;
    }
    uint32_t value = 0;
    if (!st.readTmcRegister(reg, value)) {
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
    if (!st.writeTmcRegister(reg, value, verify)) {
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
    const size_t rxCount = st.transferTmc(txBytes, txCount, rxBytes, sizeof(rxBytes), kTmcRawReadTimeoutMs);
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

void printStatus(ChannelState &ch) {
  Stepper &st = *ch.stepper;
  String result = "{";
  result += jsonPair("motion_state", motionStateName(ch.motionState));
  result += ",";
  result += jsonPair("run_current", static_cast<uint32_t>(st.getRunCurrent()));
  result += ",";
  result += jsonPair("recovery_current", static_cast<uint32_t>(st.getRecoveryRunCurrent()));
  if (st.getRecoveryRunCurrent() <= st.getRunCurrent()) {
    logCurrent("WARN", "recovery current has no headroom above normal run current");
  }
  result += ",";
  result += jsonPair("idle_current", static_cast<uint32_t>(st.getIdleCurrent()));
  result += ",";
  result += jsonPair("idle_delay", static_cast<uint32_t>(st.getIdlePowerDownDelay()));
  uint32_t drvStatus = 0;
  if (st.readTmcRegister(0x6F, drvStatus)) {
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
  result += jsonPair("homed", st.isCalibrated());
  if (st.isCalibrated()) {
    result += ",";
    result += jsonPair("travel_steps", st.getTravelSteps());
    result += ",";
    result += jsonPair("current_step", st.getCurrentPositionSteps());
    result += ",";
    result += jsonPairFloat("current_position_percent", st.getPositionPercent());
  }
  result += ",";
  result += jsonPair("minimum_valid_travel_steps", st.getMinValidTravelSteps());
  result += ",";
  result += jsonPair("min_move_frequency_hz", st.getMinMoveFrequency());
  result += ",";
  result += jsonPair("max_move_frequency_hz", st.getMaxMoveFrequency());
  result += ",";
  result += jsonPair("safe_max_speed_hz", st.getSafeMaxMoveFrequency());
  result += ",";
  result += jsonPair("move_ramp_duration_ms", st.getMoveRampDurationMs());
  if (st.getLastMoveMinStallguard() >= 0) {
    result += ",";
    result += jsonPair("last_move_min_stallguard", st.getLastMoveMinStallguard());
  }
  result += ",";
  result += jsonPair("last_move_low_margin", st.didLastMoveWarnLowMargin());
  result += "}";
  emitRpcResult(result);
}

void runCentering(ChannelState &ch, uint32_t requestedFrequency) {
  Stepper &st = *ch.stepper;
  clearEnvelope(ch);
  ch.motionInProgress = true;
  setMotionState(ch, MotionState::Homing, "home command");

  String message = "home requested_hz=";
  message += requestedFrequency;
  logCurrent("INFO", message);

  const bool ok = st.centering(requestedFrequency);
  if (ok) {
    setMotionState(ch, MotionState::Idle, "home complete");
    String result = "{";
    result += jsonPair("ok", true);
    result += ",";
    result += jsonPair("homed", st.isCalibrated());
    result += ",";
    result += jsonPair("travel_steps", st.getTravelSteps());
    result += ",";
    result += jsonPair("current_step", st.getCurrentPositionSteps());
    result += ",";
    result += jsonPairFloat("position_percent", st.getPositionPercent());
    result += ",";
    result += jsonPair("min_move_frequency_hz", st.getMinMoveFrequency());
    result += ",";
    result += jsonPair("max_move_frequency_hz", st.getMaxMoveFrequency());
    result += "}";
    emitRpcResult(result);
  } else {
    setMotionState(ch, MotionState::Fault, "home failed");
    emitRpcError(-32020, "home failed");
  }

  ch.motionInProgress = false;
  drainPendingSerialInput();
}

void runJogCommand(ChannelState &ch, bool positiveDirection, uint32_t steps, uint32_t frequency,
                   bool useRecoveryCurrent) {
  Stepper &st = *ch.stepper;
  clearEnvelope(ch);
  ch.motionInProgress = true;
  setMotionState(ch, MotionState::Jogging, "jog command");
  String message = "jog direction=";
  message += positiveDirection ? "+" : "-";
  message += " steps=";
  message += steps;
  message += " hz=";
  message += frequency == 0 ? st.getMaxMoveFrequency() : frequency;
  if (useRecoveryCurrent) {
    message += " recovery_current=yes";
  }
  logCurrent("INFO", message);

  const bool ok = st.jog(positiveDirection, steps, frequency, useRecoveryCurrent);
  setMotionState(ch, ok ? MotionState::Idle : MotionState::Fault, ok ? "jog complete" : "jog failed");
  ch.motionInProgress = false;
  drainPendingSerialInput();
  if (ok) {
    emitRpcResult(String("{") + jsonPair("ok", true) + "}");
  } else {
    emitRpcError(-32021, "jog failed");
  }
}

void runMoveToPercent(ChannelState &ch, float percent, uint32_t requestedFrequency) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }
  const float clampedPercent = constrain(percent, 0.0f, 100.0f);
  clearEnvelope(ch);
  const Stepper::MoveCommandResult result = st.moveToPercentAtFrequency(clampedPercent, requestedFrequency);
  if (result.accepted) {
    setMotionState(ch, MotionState::Moving, "pos command");
    printMoveResult(ch, result);
    emitRpcResult(String("{") + jsonPair("accepted", true) + "}");
  } else {
    emitRpcError(-32031, "move failed");
  }
}

void runMoveToStep(ChannelState &ch, uint32_t targetStep, uint32_t requestedFrequency) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }
  clearEnvelope(ch);
  const Stepper::MoveCommandResult result = st.moveToStepAtFrequency(targetStep, requestedFrequency);
  if (result.accepted) {
    setMotionState(ch, MotionState::Moving, "step command");
    printMoveResult(ch, result);
    emitRpcResult(String("{") + jsonPair("accepted", true) + "}");
  } else {
    emitRpcError(-32031, "move failed");
  }
}

void runMoveToStepInTime(ChannelState &ch, uint32_t targetStep, uint32_t durationMs) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }
  clearEnvelope(ch);
  const Stepper::MoveCommandResult result = st.moveToStepInTime(targetStep, durationMs);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(ch, MotionState::Moving, "step-time command");
  }
  printMoveResult(ch, result);
  emitRpcResult(String("{") + jsonPair("accepted", result.accepted) + "}");
}

void runMoveToStepAtSpeed(ChannelState &ch, uint32_t targetStep, uint32_t frequency) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }
  clearEnvelope(ch);
  const Stepper::MoveCommandResult result = st.moveToStepAtFrequency(targetStep, frequency);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(ch, MotionState::Moving, "step-speed command");
  }
  printMoveResult(ch, result);
  emitRpcResult(String("{") + jsonPair("accepted", result.accepted) + "}");
}

void runMoveToPercentInTime(ChannelState &ch, float percent, uint32_t durationMs) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }
  clearEnvelope(ch);
  const Stepper::MoveCommandResult result = st.moveToPercentInTime(percent, durationMs);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(ch, MotionState::Moving, "pos-time command");
  }
  printMoveResult(ch, result);
  emitRpcResult(String("{") + jsonPair("accepted", result.accepted) + "}");
}

void runMoveToPercentAtSpeed(ChannelState &ch, float percent, uint32_t frequency) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32030, "move rejected: run home first");
    return;
  }
  clearEnvelope(ch);
  const Stepper::MoveCommandResult result = st.moveToPercentAtFrequency(percent, frequency);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(ch, MotionState::Moving, "pos-speed command");
  }
  printMoveResult(ch, result);
  emitRpcResult(String("{") + jsonPair("accepted", result.accepted) + "}");
}

void printSpeedStatus(ChannelState &ch) {
  Stepper &st = *ch.stepper;
  String result = "{";
  result += jsonPair("min_move_frequency_hz", st.getMinMoveFrequency());
  result += ",";
  result += jsonPair("max_move_frequency_hz", st.getMaxMoveFrequency());
  result += ",";
  result += jsonPair("safe_max_speed_hz", st.getSafeMaxMoveFrequency());
  result += ",";
  result += jsonPair("move_ramp_duration_ms", st.getMoveRampDurationMs());
  if (st.getLastMoveMinStallguard() >= 0) {
    result += ",";
    result += jsonPair("last_move_min_stallguard", st.getLastMoveMinStallguard());
  }
  result += ",";
  result += jsonPair("last_move_low_margin", st.didLastMoveWarnLowMargin());
  result += "}";
  emitRpcResult(result);
}

void runSpeedTestCommand(ChannelState &ch, uint32_t startHz, uint32_t endHz, uint32_t stepHz,
                         uint32_t repeats) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32040, "speed-test rejected: run home first");
    return;
  }
  if (startHz == 0 || endHz == 0 || stepHz == 0) {
    emitRpcError(-32602, "speed-test requires start_hz, end_hz, and step_hz");
    return;
  }
  repeats = constrain(repeats == 0 ? 2UL : repeats, 1UL, 10UL);

  clearEnvelope(ch);
  setMotionState(ch, MotionState::SpeedTesting, "speed-test command");
  String message = "speed-test start_hz=";
  message += startHz;
  message += " end_hz=";
  message += endHz;
  message += " step_hz=";
  message += stepHz;
  message += " repeats=";
  message += repeats;
  logCurrent("INFO", message);

  const Stepper::SpeedTestResult result =
      st.runSpeedTest(startHz, endHz, stepHz, static_cast<uint8_t>(repeats));
  if (!result.accepted) {
    setMotionState(ch, MotionState::Idle, "speed-test rejected");
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
  response += jsonPair("safe_max_speed_hz", st.getSafeMaxMoveFrequency());
  response += "}";
  if (result.highestPassedFrequency > 0) {
    String data = "{";
    data += jsonPair("safe_max_speed_hz", result.highestPassedFrequency);
    data += "}";
    emitEvent("safe-max-speed-updated", data);
  }
  setMotionState(ch, MotionState::Idle, "speed-test complete");
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

void runCharacterizeMoveCheck(ChannelState &ch, uint32_t frequency, uint32_t threshold, uint32_t cycles,
                              float travelPercent, uint32_t endpointToleranceSteps) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32070, "characterize.move-check rejected: run home first");
    return;
  }
  if (frequency == 0 || threshold > 255 || cycles == 0) {
    emitRpcError(-32602, "characterize.move-check requires hz, threshold 0-255, and cycles");
    return;
  }

  clearEnvelope(ch);
  setMotionState(ch, MotionState::SpeedTesting, "characterize.move-check command");
  CharacterizeEventContext eventContext;
  eventContext.requestId = g_currentRequestId;
  eventContext.channel = g_currentChannel;
  Stepper::CharacterizeResult result =
      st.runCharacterization(frequency, static_cast<uint8_t>(threshold), cycles, travelPercent,
                             endpointToleranceSteps, emitCharacterizeCycle, &eventContext);
  setMotionState(ch, MotionState::Idle, "characterize.move-check complete");
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

void startAdsrEnvelope(ChannelState &ch, float attackPercent, uint32_t attackMs, float decayPercent,
                       uint32_t decayMs, uint32_t sustainMs, float releasePercent, uint32_t releaseMs) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32050, "envelope rejected: run home first");
    return;
  }
  if (attackMs == 0 || decayMs == 0 || releaseMs == 0) {
    emitRpcError(-32602, "adsr requires nonzero attack_ms, decay_ms, and release_ms");
    return;
  }

  clearEnvelope(ch);
  st.stopStepper();
  setMotionState(ch, MotionState::Envelope, "adsr command");
  ch.envelopePhases[0] = {"attack", attackPercent, attackMs, 0, false};
  ch.envelopePhases[1] = {"decay", decayPercent, decayMs, 0, false};
  ch.envelopePhases[2] = {"sustain", decayPercent, 0, sustainMs, true};
  ch.envelopePhases[3] = {"release", releasePercent, releaseMs, 0, false};
  ch.envelopePhaseCount = sustainMs == 0 ? 3 : 4;
  ch.envelopePhaseIndex = 0;
  ch.envelopeActive = true;
  emitRpcResult(String("{") + jsonPair("accepted", true) + "}");
  startNextEnvelopePhase(ch);
}

void startReleaseMove(ChannelState &ch, float releasePercent, uint32_t releaseMs) {
  Stepper &st = *ch.stepper;
  if (!st.isCalibrated()) {
    emitRpcError(-32051, "release rejected: run home first");
    return;
  }
  if (releaseMs == 0) {
    emitRpcError(-32602, "release requires percent and ms");
    return;
  }

  clearEnvelope(ch);
  const Stepper::MoveCommandResult result = st.moveToPercentInTime(releasePercent, releaseMs);
  if (result.accepted && !result.completedImmediately) {
    setMotionState(ch, MotionState::Moving, "release command");
  }
  printMoveResult(ch, result);
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
  g_currentChannel = 0;
  String rawChannel;
  if (!extractJsonValue(params, "channel", rawChannel)) {
    return true;
  }
  uint32_t channel = 0;
  if (!jsonUintField(params, "channel", channel) || channel >= g_activeChannels) {
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
    g_currentChannel = 0;
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
  ChannelState &ch = currentChannel();
  Stepper &st = *ch.stepper;
  emitEvent("command-received", String("{") + jsonPair("method", method) + "}");

  if (method == "help") {
    printSerialHelp();
  } else if (method == "board") {
    printBoardInfo();
  } else if (method == "status") {
    printStatus(ch);
  } else if (method == "stop" || method == "abort") {
    stopAllMotion(ch, method.c_str());
    emitRpcResult(String("{") + jsonPair("ok", true) + "}");
  } else if (method == "clear-fault") {
    const bool wasFault = ch.motionState == MotionState::Fault;
    if (wasFault) {
      setMotionState(ch, MotionState::Idle, "clear-fault command");
    }
    String result = "{";
    result += jsonPair("ok", true);
    result += ",";
    result += jsonPair("was_fault", wasFault);
    result += "}";
    emitRpcResult(result);
  } else if (method == "bootsel" || method == "bootloader") {
#if defined(BOARD_RP2040)
    emitRpcResult(String("{") + jsonPair("rebooting", true) + "}");
    Serial.flush();
    delay(100);
    rp2040.rebootToBootloader();
#else
    emitRpcError(-32601, "bootsel not supported on this board");
#endif
  } else if (method == "dfu") {
#if defined(BOARD_OCTOPUS)
    if (Bootloader::dfuSupported()) {
      emitRpcResult(String("{") + jsonPair("entering_dfu", true) + "}");
      Serial.flush();
      delay(50);
      Bootloader::enterDfu();
    } else {
      emitRpcError(-32601, "dfu not supported on this board");
    }
#else
    emitRpcError(-32601, "dfu not supported on this board");
#endif
  } else if (method == "firmware.upload") {
#if defined(BOARD_OCTOPUS)
    uint32_t size = 0;
    uint32_t crc = 0;
    if (!SelfFlash::kSupported) {
      emitRpcError(-32601, "firmware.upload not supported on this board");
    } else if (requireIdleForCommand(ch, "firmware.upload") &&
               requireParam(jsonUintField(params, "size", size) && jsonUintField(params, "crc", crc),
                            "firmware.upload requires size and crc")) {
      // Acknowledge first, halt every channel, then hand the serial line
      // to the blocking image receiver. On success it self-flashes and
      // never returns; on failure it emits a firmware-upload error event.
      emitRpcResult(String("{") + jsonPair("accepted", true) + "}");
      for (size_t i = 0; i < g_activeChannels; ++i) {
        g_channels[i].stepper->stopStepper();
      }
      FirmwareUpdate::receive(size, crc);
    }
#else
    emitRpcError(-32601, "firmware.upload not supported on this board");
#endif
  } else if (method == "tmc.test") {
    emitRpcResult(String("{") + jsonPair("ok", st.tmcTest()) + "}");
  } else if (method == "tmc.read") {
    uint32_t regValue = 0;
    if (requireParam(jsonUintField(params, "reg", regValue) && regValue <= 0xFF, "tmc.read requires reg 0-255")) {
      handleTmcCommand(st, "read", String(regValue));
    }
  } else if (method == "tmc.write") {
    uint32_t regValue = 0;
    uint32_t value = 0;
    bool verify = true;
    (void)jsonBoolField(params, "verify", verify);
    if (requireParam(jsonUintField(params, "reg", regValue) && regValue <= 0xFF &&
                         jsonUintField(params, "value", value),
                     "tmc.write requires reg 0-255 and value")) {
      String args = String(regValue) + " " + String(value) + (verify ? "" : " noverify");
      handleTmcCommand(st, "write", args);
    }
  } else if (method == "tmc.raw") {
    String bytes;
    if (requireParam(extractJsonValue(params, "bytes", bytes), "tmc.raw requires bytes")) {
      bytes.replace("[", "");
      bytes.replace("]", "");
      handleTmcCommand(st, "raw", bytes);
    }
  } else if (method == "current.status") {
    String result = "{";
    result += jsonPair("run", static_cast<uint32_t>(st.getRunCurrent()));
    result += ",";
    result += jsonPair("recovery", static_cast<uint32_t>(st.getRecoveryRunCurrent()));
    result += ",";
    result += jsonPair("idle", static_cast<uint32_t>(st.getIdleCurrent()));
    result += ",";
    result += jsonPair("idle_delay", static_cast<uint32_t>(st.getIdlePowerDownDelay()));
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
      ok = st.setRunCurrent(static_cast<uint8_t>(value));
    } else if (which == "recovery" && value <= 31) {
      ok = st.setRecoveryRunCurrent(static_cast<uint8_t>(value));
    } else if (which == "idle" && value <= 31) {
      ok = st.setIdleCurrent(static_cast<uint8_t>(value));
    } else if (which == "idle-delay" && value <= 255) {
      ok = st.setIdlePowerDownDelay(static_cast<uint8_t>(value));
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
    printSpeedStatus(ch);
  } else if (method == "speed.set-max") {
    uint32_t value = 0;
    if (requireParam(jsonUintField(params, "hz", value), "speed.set-max requires hz")) {
      if (st.setSafeMaxMoveFrequency(value)) {
        emitRpcResult(String("{") + jsonPair("safe_max_speed_hz", st.getSafeMaxMoveFrequency()) + "}");
      } else {
        emitRpcError(-32602, "invalid safe max speed");
      }
    }
  } else if (method == "ramp.status") {
    emitRpcResult(String("{") + jsonPair("duration_ms", st.getMoveRampDurationMs()) + "}");
  } else if (method == "ramp.set") {
    uint32_t value = 0;
    if (requireParam(jsonUintField(params, "duration_ms", value), "ramp.set requires duration_ms")) {
      if (st.setMoveRampDurationMs(value)) {
        emitRpcResult(String("{") + jsonPair("duration_ms", st.getMoveRampDurationMs()) + "}");
      } else {
        emitRpcError(-32602, "invalid ramp duration");
      }
    }
  } else if (method == "stallguard.status") {
    int32_t sg = st.readStallguard();
    emitRpcResult(String("{") + jsonPair("sg_result", sg) + "}");
  } else if (method == "stallguard.set-threshold") {
    uint32_t threshold = 0;
    if (requireParam(jsonUintField(params, "threshold", threshold) && threshold <= 255,
                     "stallguard.set-threshold requires threshold 0-255")) {
      st.setStallguard(static_cast<uint8_t>(threshold));
      emitRpcResult(String("{") + jsonPair("threshold", threshold) + "}");
    }
  } else if (method == "speed-test") {
    uint32_t startHz = 0;
    uint32_t endHz = 0;
    uint32_t stepHz = 0;
    uint32_t repeats = 2;
    if (requireIdleForCommand(ch, "speed-test") &&
        requireParam(jsonUintField(params, "start_hz", startHz) && jsonUintField(params, "end_hz", endHz) &&
                         jsonUintField(params, "step_hz", stepHz),
                     "speed-test requires start_hz, end_hz, and step_hz")) {
      (void)jsonUintField(params, "repeats", repeats);
      runSpeedTestCommand(ch, startHz, endHz, stepHz, repeats);
    }
  } else if (method == "characterize.move-check") {
    uint32_t frequency = 0;
    uint32_t threshold = 0;
    uint32_t cycles = 0;
    uint32_t endpointToleranceSteps = 8;
    float travelPercent = 100.0f;
    (void)jsonUintField(params, "endpoint_tolerance_steps", endpointToleranceSteps);
    (void)jsonFloatField(params, "travel_percent", travelPercent);
    if (requireIdleForCommand(ch, "characterize.move-check") &&
        requireParam(jsonUintField(params, "hz", frequency) && jsonUintField(params, "threshold", threshold) &&
                         jsonUintField(params, "cycles", cycles),
                     "characterize.move-check requires hz, threshold, and cycles")) {
      runCharacterizeMoveCheck(ch, frequency, threshold, cycles, travelPercent, endpointToleranceSteps);
    }
  } else if (method == "home") {
    uint32_t frequency = 0;
    (void)jsonUintField(params, "hz", frequency);
    if (requireIdleForCommand(ch, "home")) {
      runCentering(ch, frequency == 0 ? nextDemoFrequency(ch) : frequency);
    }
  } else if (method == "jog") {
    String direction;
    uint32_t steps = 0;
    uint32_t frequency = 0;
    bool recovery = false;
    (void)jsonUintField(params, "hz", frequency);
    (void)jsonBoolField(params, "recovery", recovery);
    if (requireIdleForCommand(ch, "jog") &&
        requireParam(jsonStringField(params, "direction", direction) &&
                         jsonUintField(params, "steps", steps) && steps > 0,
                     "jog requires direction and steps")) {
      const bool positive = direction == "+" || direction == "pos" || direction == "positive";
      const bool negative = direction == "-" || direction == "neg" || direction == "negative";
      if (requireParam(positive || negative, "jog direction must be + or -")) {
        runJogCommand(ch, positive, steps, frequency, recovery);
      }
    }
  } else if (method == "unstick") {
    uint32_t steps = 500;
    uint32_t frequency = 400;
    (void)jsonUintField(params, "steps", steps);
    (void)jsonUintField(params, "hz", frequency);
    if (requireIdleForCommand(ch, "unstick")) {
      runJogCommand(ch, true, steps, frequency, true);
    }
  } else if (method == "move-percent") {
    float percent = 0.0f;
    uint32_t frequency = 0;
    (void)jsonUintField(params, "hz", frequency);
    if (requireIdleForCommand(ch, "move-percent") && requireHomedForCommand(ch, "move-percent") &&
        requireParam(jsonFloatField(params, "percent", percent), "move-percent requires percent")) {
      runMoveToPercent(ch, percent, frequency);
    }
  } else if (method == "move-step") {
    uint32_t step = 0;
    uint32_t frequency = 0;
    (void)jsonUintField(params, "hz", frequency);
    if (requireIdleForCommand(ch, "move-step") && requireHomedForCommand(ch, "move-step") &&
        requireParam(jsonUintField(params, "step", step), "move-step requires step")) {
      runMoveToStep(ch, step, frequency);
    }
  } else if (method == "move-percent-time") {
    float percent = 0.0f;
    uint32_t durationMs = 0;
    if (requireIdleForCommand(ch, "move-percent-time") && requireHomedForCommand(ch, "move-percent-time") &&
        requireParam(jsonFloatField(params, "percent", percent) &&
                         jsonUintField(params, "duration_ms", durationMs) && durationMs > 0,
                     "move-percent-time requires percent and duration_ms")) {
      runMoveToPercentInTime(ch, percent, durationMs);
    }
  } else if (method == "move-percent-speed") {
    float percent = 0.0f;
    uint32_t frequency = 0;
    if (requireIdleForCommand(ch, "move-percent-speed") && requireHomedForCommand(ch, "move-percent-speed") &&
        requireParam(jsonFloatField(params, "percent", percent) &&
                         jsonUintField(params, "hz", frequency) && frequency > 0,
                     "move-percent-speed requires percent and hz")) {
      runMoveToPercentAtSpeed(ch, percent, frequency);
    }
  } else if (method == "move-step-time") {
    uint32_t step = 0;
    uint32_t durationMs = 0;
    if (requireIdleForCommand(ch, "move-step-time") && requireHomedForCommand(ch, "move-step-time") &&
        requireParam(jsonUintField(params, "step", step) &&
                         jsonUintField(params, "duration_ms", durationMs) && durationMs > 0,
                     "move-step-time requires step and duration_ms")) {
      runMoveToStepInTime(ch, step, durationMs);
    }
  } else if (method == "move-step-speed") {
    uint32_t step = 0;
    uint32_t frequency = 0;
    if (requireIdleForCommand(ch, "move-step-speed") && requireHomedForCommand(ch, "move-step-speed") &&
        requireParam(jsonUintField(params, "step", step) && jsonUintField(params, "hz", frequency) &&
                         frequency > 0,
                     "move-step-speed requires step and hz")) {
      runMoveToStepAtSpeed(ch, step, frequency);
    }
  } else if (method == "adsr") {
    float attackPercent = 0.0f;
    float decayPercent = 0.0f;
    float releasePercent = 0.0f;
    uint32_t attackMs = 0;
    uint32_t decayMs = 0;
    uint32_t sustainMs = 0;
    uint32_t releaseMs = 0;
    if (requireIdleForCommand(ch, "adsr") && requireHomedForCommand(ch, "adsr") &&
        requireParam(jsonFloatField(params, "attack_percent", attackPercent) &&
                         jsonUintField(params, "attack_ms", attackMs) &&
                         jsonFloatField(params, "decay_percent", decayPercent) &&
                         jsonUintField(params, "decay_ms", decayMs) &&
                         jsonUintField(params, "sustain_ms", sustainMs) &&
                         jsonFloatField(params, "release_percent", releasePercent) &&
                         jsonUintField(params, "release_ms", releaseMs),
                     "adsr requires attack_percent, attack_ms, decay_percent, decay_ms, sustain_ms, "
                     "release_percent, release_ms")) {
      startAdsrEnvelope(ch, attackPercent, attackMs, decayPercent, decayMs, sustainMs, releasePercent,
                        releaseMs);
    }
  } else if (method == "release") {
    float percent = 0.0f;
    uint32_t durationMs = 0;
    if (requireIdleForCommand(ch, "release") && requireHomedForCommand(ch, "release") &&
        requireParam(jsonFloatField(params, "percent", percent) &&
                         jsonUintField(params, "duration_ms", durationMs),
                     "release requires percent and duration_ms")) {
      startReleaseMove(ch, percent, durationMs);
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

void serviceChannelMotion(ChannelState &ch) {
  g_currentChannel = ch.index;
  const Stepper::MoveUpdate moveUpdate = ch.stepper->serviceMove();
  if (moveUpdate == Stepper::MoveUpdate::Completed || moveUpdate == Stepper::MoveUpdate::HomeAdjusted) {
    String message = "move complete step=";
    message += ch.stepper->getCurrentPositionSteps();
    message += " percent=";
    message += String(ch.stepper->getPositionPercent(), 1);
    rspLine("OK", message);
    setMotionState(ch, MotionState::Idle, "move complete");
  } else if (moveUpdate == Stepper::MoveUpdate::Failed) {
    rspLine("ERR", "move failed");
    setMotionState(ch, MotionState::Fault, "move failed");
  }
  serviceEnvelope(ch, moveUpdate);
  emitStateChange(ch);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

#if defined(BOARD_RP2040)
  pinMode(kButtonPin, INPUT_PULLUP);
#endif

  g_statusLed.begin();
  Board::begin();
  g_activeChannels = Board::channelCount();
  if (g_activeChannels > kMaxChannels) {
    g_activeChannels = kMaxChannels;
  }

  for (size_t i = 0; i < g_activeChannels; ++i) {
    g_channels[i].index = static_cast<uint8_t>(i);
    g_channels[i].stepper =
        new Stepper(static_cast<uint8_t>(i), Board::channelConfig(i), g_statusLed, true);
    g_channels[i].stepper->setLogCallback(logLine);
    g_channels[i].stepper->setServiceCallback(serviceSerialOutput);
  }

  g_currentChannel = 0;
  logCurrent("INFO", "startup: initializing channels");
  g_statusLed.heartBeat(4, 400);

  bool anyTmc = false;
  for (size_t i = 0; i < g_activeChannels; ++i) {
    g_currentChannel = i;
    if (!g_channels[i].stepper->begin()) {
      logCurrent("ERROR", "failed to initialize channel");
      continue;
    }
    if (g_channels[i].stepper->tmcTest()) {
      anyTmc = true;
    } else {
      logCurrent("ERROR", "TMC driver UART did not respond; check stepper power");
    }
  }

  g_currentChannel = 0;
  String ready = "{\"protocol\":\"json-rpc\",\"transport\":\"newline-delimited-json\",\"firmware\":";
  ready += "\"hexaflame-stepper\",\"board\":";
  String boardName = Board::name();
  String boardEscaped;
  appendJsonString(boardEscaped, boardName);
  ready += boardEscaped;
  ready += ",\"channel_count\":";
  ready += static_cast<uint32_t>(g_activeChannels);
  ready += "}";
  emitEvent("ready", ready);

  for (size_t i = 0; i < g_activeChannels; ++i) {
    g_currentChannel = i;
    emitStateChange(g_channels[i], true);
  }
  (void)anyTmc;
}

void loop() {
  serviceSerialOutput();
  processSerialInput();

  for (size_t i = 0; i < g_activeChannels; ++i) {
    serviceChannelMotion(g_channels[i]);
  }

#if defined(BOARD_RP2040)
  static bool lastButtonLevel = true;
  static uint32_t lastDebounceMs = 0;
  static bool homingRequested = false;

  ChannelState &button_ch = g_channels[0];
  const bool buttonLevel = digitalRead(kButtonPin);
  const uint32_t now = millis();
  if (buttonLevel != lastButtonLevel) {
    lastDebounceMs = now;
    lastButtonLevel = buttonLevel;
  }
  if (isMotionIdle(button_ch) && !buttonLevel && (now - lastDebounceMs) >= kDebounceMs) {
    homingRequested = true;
  }
  if (homingRequested) {
    homingRequested = false;
    g_currentChannel = 0;
    runCentering(button_ch, nextDemoFrequency(button_ch));
    while (digitalRead(kButtonPin) == LOW) {
      delay(5);
    }
    lastButtonLevel = true;
    lastDebounceMs = millis();
  }
#endif

  delay(1);
}
