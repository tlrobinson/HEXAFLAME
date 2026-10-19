#pragma once

#include <stddef.h>
#include <stdint.h>

#include "output_bank.h"

// HEXAFLAME host link - device logic shared by every protocol.
//
// OutputController wraps an OutputBank with the operations a host protocol
// wants to express: set/toggle one channel, set/toggle everything, timed
// pulses, emergency all-off and bit-packed bulk reads/writes. Protocols
// talk to this class, never to the bank directly, so a new protocol gets
// pulse timers and bulk helpers for free and the bank stays a thin
// hardware shim.
//
// No Arduino or RTOS dependencies: time is passed in as milliseconds.

namespace hexaflame {

class OutputController {
public:
  static const uint16_t kMaxChannels = 64;

  explicit OutputController(OutputBank &bank);

  uint16_t channelCount() const;
  bool valid(uint16_t channel) const { return channel < channelCount(); }

  bool get(uint16_t channel) const;
  bool anyOn() const;

  // Each of these stages state only; call flush() (or let the protocol do
  // it) to reach the hardware. They return false for out-of-range channels.
  // Any explicit write cancels a pending pulse on that channel.
  bool set(uint16_t channel, bool on);
  bool toggle(uint16_t channel);
  void setAll(bool on);
  void toggleAll();

  // Drive `channel` to `stateNow` immediately and back to the opposite state
  // after `durationMs` (WaveShare "flash on" / "flash off"). A duration of 0
  // is just a plain write.
  bool pulse(uint16_t channel, bool stateNow, uint32_t durationMs, uint32_t nowMs);

  // Everything off, every pulse cancelled, flushed immediately.
  void allOff();

  void flush();

  // Expire pulse timers. Call from the main loop.
  void service(uint32_t nowMs);

  // Bit-packed helpers in Modbus coil order (LSB of byte 0 = `start`).
  // `out` must hold (count + 7) / 8 bytes. Both return false if the range
  // does not fit the bank.
  bool readBits(uint16_t start, uint16_t count, uint8_t *out) const;
  bool writeBits(uint16_t start, uint16_t count, const uint8_t *bits);

private:
  struct PulseTimer {
    bool active = false;
    bool stateAfter = false;
    uint32_t deadlineMs = 0;
  };

  OutputBank &bank_;
  PulseTimer timers_[kMaxChannels];
};

}  // namespace hexaflame
