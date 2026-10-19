#include "output_controller.h"

namespace hexaflame {

OutputController::OutputController(OutputBank &bank) : bank_(bank) {}

uint16_t OutputController::channelCount() const {
  const uint16_t count = bank_.channelCount();
  return count < kMaxChannels ? count : kMaxChannels;
}

bool OutputController::get(uint16_t channel) const {
  return valid(channel) && bank_.get(channel);
}

bool OutputController::anyOn() const {
  const uint16_t count = channelCount();
  for (uint16_t channel = 0; channel < count; ++channel) {
    if (bank_.get(channel)) {
      return true;
    }
  }
  return false;
}

bool OutputController::set(uint16_t channel, bool on) {
  if (!valid(channel)) {
    return false;
  }
  timers_[channel].active = false;
  bank_.set(channel, on);
  return true;
}

bool OutputController::toggle(uint16_t channel) {
  if (!valid(channel)) {
    return false;
  }
  return set(channel, !bank_.get(channel));
}

void OutputController::setAll(bool on) {
  const uint16_t count = channelCount();
  for (uint16_t channel = 0; channel < count; ++channel) {
    set(channel, on);
  }
}

void OutputController::toggleAll() {
  const uint16_t count = channelCount();
  for (uint16_t channel = 0; channel < count; ++channel) {
    toggle(channel);
  }
}

bool OutputController::pulse(uint16_t channel, bool stateNow, uint32_t durationMs, uint32_t nowMs) {
  if (!set(channel, stateNow)) {
    return false;
  }
  if (durationMs == 0) {
    return true;
  }
  PulseTimer &timer = timers_[channel];
  timer.active = true;
  timer.stateAfter = !stateNow;
  timer.deadlineMs = nowMs + durationMs;
  return true;
}

void OutputController::allOff() {
  setAll(false);
  flush();
}

void OutputController::flush() {
  bank_.flush();
}

void OutputController::service(uint32_t nowMs) {
  bool changed = false;
  const uint16_t count = channelCount();
  for (uint16_t channel = 0; channel < count; ++channel) {
    PulseTimer &timer = timers_[channel];
    if (!timer.active) {
      continue;
    }
    // Signed difference keeps this correct across millis() wrap-around.
    if (static_cast<int32_t>(nowMs - timer.deadlineMs) < 0) {
      continue;
    }
    timer.active = false;
    bank_.set(channel, timer.stateAfter);
    changed = true;
  }
  if (changed) {
    bank_.flush();
  }
}

bool OutputController::readBits(uint16_t start, uint16_t count, uint8_t *out) const {
  if (count == 0 || static_cast<uint32_t>(start) + count > channelCount()) {
    return false;
  }
  const size_t byteCount = (count + 7u) / 8u;
  for (size_t i = 0; i < byteCount; ++i) {
    out[i] = 0;
  }
  for (uint16_t i = 0; i < count; ++i) {
    if (bank_.get(start + i)) {
      out[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    }
  }
  return true;
}

bool OutputController::writeBits(uint16_t start, uint16_t count, const uint8_t *bits) {
  if (count == 0 || static_cast<uint32_t>(start) + count > channelCount()) {
    return false;
  }
  for (uint16_t i = 0; i < count; ++i) {
    const bool on = (bits[i / 8] >> (i % 8)) & 0x01;
    set(start + i, on);
  }
  return true;
}

}  // namespace hexaflame
