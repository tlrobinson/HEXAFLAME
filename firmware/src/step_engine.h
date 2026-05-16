#pragma once

#include <Arduino.h>

// Hardware-independent step-pulse generator.
//
// The original RP2040 firmware drove the STEP pin from the PIO, which let a
// tight StallGuard-polling loop run on the CPU without disturbing the step
// cadence. The Octopus has no PIO, so each channel instead owns a hardware
// timer whose ISR toggles the STEP pin. Both behave identically through this
// interface: emit a fixed number of pulses at a (possibly changing) rate,
// expose how many have been emitted, and report when the burst is finished.
//
// A StepEngine is single-channel; a board with N channels owns N engines.
class StepEngine {
public:
  virtual ~StepEngine() {}

  // One-time hardware setup (claim the PIO / timer, configure the STEP pin).
  virtual bool begin() = 0;

  // Set the rotation direction for the next/!current burst.
  virtual void setDirection(bool positive) = 0;

  // Arm a burst of `pulses` STEP edges, starting at `initialHz`. Resets the
  // completed-pulse counter. Does not start motion until start() is called.
  virtual void prepare(uint32_t pulses, uint32_t initialHz) = 0;

  // Begin emitting the prepared burst.
  virtual void start() = 0;

  // Change the pulse rate of an in-progress burst (used by motion ramps).
  virtual void updateSpeed(uint32_t hz) = 0;

  // Immediately halt pulse generation.
  virtual void stop() = 0;

  // True while a burst is actively emitting pulses.
  virtual bool isSpinning() = 0;

  // Number of STEP pulses emitted since the last prepare().
  virtual uint32_t completedPulses() = 0;
};
