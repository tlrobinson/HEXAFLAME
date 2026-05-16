#pragma once

#include <Arduino.h>

#include "step_engine.h"
#include "tmc2209.h"

// One microstepping option. The MS1/MS2 strap columns are used on the RP2040,
// where those pins also select the TMC2209's UART address; `mres` is the raw
// CHOPCONF code used on the Octopus, where microstepping is set over UART.
struct MicrostepSetting {
  const char *label;
  float reduction;       // step fraction, e.g. 1/8 microstep -> 0.125
  uint8_t ms1;           // MS1 strap level (RP2040)
  uint8_t ms2;           // MS2 strap level (RP2040)
  uint8_t sgAdjustment;  // StallGuard frequency scaling for this resolution
  uint8_t mres;          // CHOPCONF MRES field value (UART microstepping)
};

extern const MicrostepSetting kMicrostepMap[];
extern const size_t kMicrostepMapCount;

// Everything one channel needs to drive its motor and talk to its driver.
// A pin set to -1 simply isn't present on that board.
struct ChannelConfig {
  const char *name;
  int dirPin;
  int ms1Pin;
  int ms2Pin;
  int enablePin;          // active LOW; -1 if hard-wired
  int diagPin;            // StallGuard DIAG; -1 if not routed
  uint8_t microstepMode;  // index into kMicrostepMap
  bool microstepViaUart;  // true: program MRES over UART; false: use MS straps
  uint8_t tmcAddress;     // TMC2209 UART node address
  StepEngine *engine;
  Tmc2209 *tmc;
};

// Implemented by exactly one of src/rp2040/board_rp2040.cpp or
// src/stm32/board_octopus.cpp depending on the build target.
namespace Board {

// Human-readable board name, surfaced in the JSON-RPC "ready" event.
const char *name();

// Construct the per-channel engines, UARTs and drivers. Call once at startup
// before touching channelConfig().
void begin();

// Number of independent stepper channels on this board.
size_t channelCount();

// Hardware configuration for channel `index` (0-based).
ChannelConfig &channelConfig(size_t index);

}  // namespace Board
