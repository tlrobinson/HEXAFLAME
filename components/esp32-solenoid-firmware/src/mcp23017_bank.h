#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "output_bank.h"

// OutputBank over one or more Adafruit I2C-to-8-Channel Solenoid Driver
// boards (MCP23017 port A -> AO3406 low-side MOSFETs, active high).
//
// Boards are discovered by address: slot n is the board jumpered to
// base + n and owns channels 8n..8n+7. Slots are probed at boot and
// re-probed while the firmware runs, so a board that is powered up late,
// unplugged, or browns out is picked up (or dropped and recovered) without
// a reset. Channels of an absent board still hold state so a host writing
// a fixed-width coil mask never gets an error; when the board shows up it
// is initialised straight into the commanded state.
//
// The MCP23017 is driven directly over Wire (five registers) rather than
// through a library so the firmware has no external dependencies.
class Mcp23017SolenoidBank : public hexaflame::OutputBank {
public:
  static const uint8_t kMaxBoards = 8;
  static const uint8_t kChannelsPerBoard = 8;

  Mcp23017SolenoidBank(TwoWire &wire, uint8_t baseAddress, uint8_t boardCount);

  // Probe every slot once. Call after Wire.begin().
  void begin(uint32_t nowMs);

  // Re-probe absent/offline slots on their schedule. Call from loop().
  void service(uint32_t nowMs);

  // hexaflame::OutputBank
  uint16_t channelCount() const override { return static_cast<uint16_t>(boardCount_ * kChannelsPerBoard); }
  bool get(uint16_t channel) const override;
  void set(uint16_t channel, bool on) override;
  void flush() override;
  bool healthy() const override { return offlineMask() == 0; }

  // Diagnostics. Bit n of each mask refers to slot n (address base + n).
  uint8_t onlineMask() const;   // responding right now
  uint8_t offlineMask() const;  // responded before, failing now
  uint32_t errorCount() const { return errors_; }
  uint8_t baseAddress() const { return base_; }

  // Live read of a board's output latch (what the MOSFET gates actually
  // see). False if the slot is not online or the read fails.
  bool readOutputLatch(uint8_t index, uint8_t &value);

private:
  enum class SlotState : uint8_t { Absent, Online, Offline };

  struct Slot {
    SlotState state = SlotState::Absent;
    uint8_t staged = 0;   // commanded port A bits
    bool dirty = true;    // staged differs from what the chip holds
    uint32_t nextProbeMs = 0;
  };

  static const uint32_t kAbsentProbeIntervalMs = 2000;
  static const uint32_t kOfflineProbeIntervalMs = 500;

  bool writeRegister(uint8_t address, uint8_t reg, uint8_t value);
  bool readRegister(uint8_t address, uint8_t reg, uint8_t &value);
  bool initialiseSlot(uint8_t index);
  void markOffline(uint8_t index, uint32_t nowMs);
  uint8_t addressOf(uint8_t index) const { return static_cast<uint8_t>(base_ + index); }

  TwoWire &wire_;
  uint8_t base_;
  uint8_t boardCount_;
  Slot slots_[kMaxBoards];
  uint32_t errors_ = 0;
};
