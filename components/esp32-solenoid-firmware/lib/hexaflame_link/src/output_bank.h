#pragma once

#include <stddef.h>
#include <stdint.h>

// HEXAFLAME host link - hardware side.
//
// An OutputBank is a numbered set of boolean outputs (solenoid valves,
// relays, igniters, ...). The implementation owns the hardware; everything
// above it (OutputController, the protocols) only sees channel numbers.
//
// Writes are staged with set() and pushed to hardware by flush(). A protocol
// that updates many channels at once (Modbus 0x0F, for example) therefore
// costs one bus transaction per physical device instead of one per channel.

namespace hexaflame {

class OutputBank {
public:
  virtual ~OutputBank() = default;

  // Number of addressable channels. Channels that have no hardware behind
  // them (an unpopulated expander slot, say) still count: they keep state
  // so a host that writes a fixed-width mask never sees an error.
  virtual uint16_t channelCount() const = 0;

  // Commanded (staged) state of a channel. Out-of-range channels read false.
  virtual bool get(uint16_t channel) const = 0;

  // Stage a new state. Out-of-range channels are ignored.
  virtual void set(uint16_t channel, bool on) = 0;

  // Push staged state to the hardware.
  virtual void flush() = 0;

  // True when every physical output the bank has seen is still reachable.
  virtual bool healthy() const { return true; }
};

}  // namespace hexaflame
