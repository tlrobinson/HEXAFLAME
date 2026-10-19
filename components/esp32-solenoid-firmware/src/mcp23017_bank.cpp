#include "mcp23017_bank.h"

namespace {

// MCP23017 register map with IOCON.BANK = 0 (the power-on default).
const uint8_t kRegIodirA = 0x00;
const uint8_t kRegIpolB = 0x05;   // doubles as IOCON when BANK = 1
const uint8_t kRegIocon = 0x0A;
const uint8_t kRegOlatA = 0x14;

}  // namespace

Mcp23017SolenoidBank::Mcp23017SolenoidBank(TwoWire &wire, uint8_t baseAddress, uint8_t boardCount)
    : wire_(wire), base_(baseAddress), boardCount_(boardCount < kMaxBoards ? boardCount : kMaxBoards) {}

void Mcp23017SolenoidBank::begin(uint32_t nowMs) {
  for (uint8_t i = 0; i < boardCount_; ++i) {
    slots_[i].nextProbeMs = nowMs;
  }
  service(nowMs);
}

void Mcp23017SolenoidBank::service(uint32_t nowMs) {
  for (uint8_t i = 0; i < boardCount_; ++i) {
    Slot &slot = slots_[i];
    if (slot.state == SlotState::Online) {
      continue;
    }
    if (static_cast<int32_t>(nowMs - slot.nextProbeMs) < 0) {
      continue;
    }
    if (initialiseSlot(i)) {
      slot.state = SlotState::Online;
      slot.dirty = false;
    } else {
      slot.nextProbeMs = nowMs + (slot.state == SlotState::Offline ? kOfflineProbeIntervalMs : kAbsentProbeIntervalMs);
    }
  }
}

bool Mcp23017SolenoidBank::get(uint16_t channel) const {
  if (channel >= channelCount()) {
    return false;
  }
  return (slots_[channel / kChannelsPerBoard].staged >> (channel % kChannelsPerBoard)) & 0x01;
}

void Mcp23017SolenoidBank::set(uint16_t channel, bool on) {
  if (channel >= channelCount()) {
    return;
  }
  Slot &slot = slots_[channel / kChannelsPerBoard];
  const uint8_t bit = static_cast<uint8_t>(1u << (channel % kChannelsPerBoard));
  const uint8_t next = on ? static_cast<uint8_t>(slot.staged | bit) : static_cast<uint8_t>(slot.staged & ~bit);
  if (next != slot.staged) {
    slot.staged = next;
    slot.dirty = true;
  }
}

void Mcp23017SolenoidBank::flush() {
  const uint32_t nowMs = millis();
  for (uint8_t i = 0; i < boardCount_; ++i) {
    Slot &slot = slots_[i];
    if (slot.state != SlotState::Online || !slot.dirty) {
      continue;
    }
    if (writeRegister(addressOf(i), kRegOlatA, slot.staged)) {
      slot.dirty = false;
    } else {
      markOffline(i, nowMs);  // stays dirty: re-applied on recovery
    }
  }
}

uint8_t Mcp23017SolenoidBank::onlineMask() const {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < boardCount_; ++i) {
    if (slots_[i].state == SlotState::Online) {
      mask |= static_cast<uint8_t>(1u << i);
    }
  }
  return mask;
}

uint8_t Mcp23017SolenoidBank::offlineMask() const {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < boardCount_; ++i) {
    if (slots_[i].state == SlotState::Offline) {
      mask |= static_cast<uint8_t>(1u << i);
    }
  }
  return mask;
}

bool Mcp23017SolenoidBank::readOutputLatch(uint8_t index, uint8_t &value) {
  if (index >= boardCount_ || slots_[index].state != SlotState::Online) {
    return false;
  }
  if (!readRegister(addressOf(index), kRegOlatA, value)) {
    markOffline(index, millis());
    return false;
  }
  return true;
}

bool Mcp23017SolenoidBank::writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  wire_.beginTransmission(address);
  wire_.write(reg);
  wire_.write(value);
  return wire_.endTransmission() == 0;
}

bool Mcp23017SolenoidBank::readRegister(uint8_t address, uint8_t reg, uint8_t &value) {
  wire_.beginTransmission(address);
  wire_.write(reg);
  if (wire_.endTransmission(false) != 0) {
    return false;
  }
  if (wire_.requestFrom(address, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  const int byte = wire_.read();
  if (byte < 0) {
    return false;
  }
  value = static_cast<uint8_t>(byte);
  return true;
}

bool Mcp23017SolenoidBank::initialiseSlot(uint8_t index) {
  const uint8_t address = addressOf(index);
  const uint8_t staged = slots_[index].staged;

  // Force BANK = 0 addressing whatever state the chip is in. In BANK = 1
  // mode register 0x05 is IOCON, so writing 0 there flips back to BANK = 0;
  // in BANK = 0 mode it is IPOLB, where 0 is the default. Then clear IOCON
  // proper (sequential addressing, active-low INT, no mirroring).
  if (!writeRegister(address, kRegIpolB, 0x00) || !writeRegister(address, kRegIocon, 0x00)) {
    return false;
  }
  // Latch the commanded state before enabling the outputs so a recovering
  // board never glitches the solenoids through the power-on input state.
  if (!writeRegister(address, kRegOlatA, staged) || !writeRegister(address, kRegIodirA, 0x00)) {
    return false;
  }
  // Read back: proves a real MCP23017 answered rather than a stray ACK.
  uint8_t iodir = 0xFF;
  uint8_t olat = 0xFF;
  if (!readRegister(address, kRegIodirA, iodir) || !readRegister(address, kRegOlatA, olat)) {
    return false;
  }
  return iodir == 0x00 && olat == staged;
}

void Mcp23017SolenoidBank::markOffline(uint8_t index, uint32_t nowMs) {
  Slot &slot = slots_[index];
  errors_++;
  slot.state = SlotState::Offline;
  slot.nextProbeMs = nowMs + kOfflineProbeIntervalMs;
}
