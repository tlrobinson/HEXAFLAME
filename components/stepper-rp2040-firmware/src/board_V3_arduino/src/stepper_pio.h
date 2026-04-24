#pragma once
#include <hardware/pio.h>

// ---------- steps_mot_program ----------
// Frequency generator driving the STEP pin.
// Pull (non-blocking) new delay from TX FIFO, keep it in OSR,
// then emit a pulse and idle for (delay) cycles.
//
//   main:
//       pull  noblock
//       mov   x, osr
//       mov   y, x
//       set   pins, 1    [15]
//       set   pins, 1    [15]
//   delay:
//       set   pins, 0
//       jmp   y--, delay
//       jmp   main
static const uint16_t steps_mot_program_instructions[] = {
    0x8080, //  0: pull   noblock
    0xa027, //  1: mov    x, osr
    0xa041, //  2: mov    y, x
    0xef01, //  3: set    pins, 1         [15]
    0xef01, //  4: set    pins, 1         [15]
    0xe000, //  5: set    pins, 0
    0x0085, //  6: jmp    y--, 5
    0x0000, //  7: jmp    0
};
static const struct pio_program steps_mot_program = {
    .instructions = steps_mot_program_instructions,
    .length = sizeof(steps_mot_program_instructions) / sizeof(uint16_t),
    .origin = -1,
};

// ---------- stop_stepper_program ----------
// Counts falling edges on the STEP pin down through X.
// When X underflows, raise a relative IRQ so the CPU can stop SM0.
//
//   wait_for_step:
//       wait  1 pin 0
//       wait  0 pin 0
//       jmp   x--, wait_for_step
//       irq   wait 0 rel
static const uint16_t stop_stepper_program_instructions[] = {
    0x20a0, //  0: wait   1 pin, 0
    0x2020, //  1: wait   0 pin, 0
    0x0040, //  2: jmp    x--, 0
    0xc030, //  3: irq    wait 0 rel
};
static const struct pio_program stop_stepper_program = {
    .instructions = stop_stepper_program_instructions,
    .length = sizeof(stop_stepper_program_instructions) / sizeof(uint16_t),
    .origin = -1,
};

// ---------- steps_counter_program ----------
// Decrements X on every rising edge of the STEP pin so the CPU
// can read back how many pulses have been generated.
//
//   loop:
//       wait  0 pin 0
//       wait  1 pin 0
//       jmp   x--, loop
static const uint16_t steps_counter_program_instructions[] = {
    0x2020, //  0: wait   0 pin, 0
    0x20a0, //  1: wait   1 pin, 0
    0x0040, //  2: jmp    x--, 0
};
static const struct pio_program steps_counter_program = {
    .instructions = steps_counter_program_instructions,
    .length = sizeof(steps_counter_program_instructions) / sizeof(uint16_t),
    .origin = -1,
};
