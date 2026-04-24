#pragma once
#include <Arduino.h>
#include <hardware/pio.h>
#include "tmc2209.h"

class Stepper {
public:
    Stepper(uint32_t max_frequency = 125000000, uint32_t frequency = 5000000, bool debug = false);

    bool tmcTest();
    void stopStepper();
    void startStepper();
    void deactivatePio();

    // SENSORLESS homing, on both directions, stopping in the middle.
    // stepper_freq is clamped to the range supported by StallGuard.
    bool centering(uint32_t stepper_freq);

    void setStallguard(int threshold);

private:
    void setPlsToDo(uint32_t val);
    void setPlsCounter(uint32_t val);
    int32_t getPlsCount();

    uint32_t getStepperValue(uint32_t stepper_freq);

    // ms: 0=1/8, 1=1/16, 2=1/32, 3=1/64. Returns true if the value is valid.
    bool microStep(int ms);

    bool homing(uint32_t h_speed, uint32_t stepper_freq, int startup_loops,
                uint32_t retract_time_ms, int32_t retract_steps);
    void retract(uint32_t speed, int startup_loops, uint32_t* retract_time_ms, int32_t* retract_steps);

    // Static trampolines for the PIO and GPIO interrupts.
    static void onPioIrq();
    static void onStallguard();
    static Stepper* _instance;

    TMC2209 _tmc;
    bool _debug;

    uint32_t _max_frequency;
    uint32_t _frequency;

    // GPIO pins (RP2040 / RP2040-Zero)
    static constexpr uint8_t STEP_PIN = 5;
    static constexpr uint8_t DIR_PIN  = 6;
    static constexpr uint8_t MS1_PIN  = 3;
    static constexpr uint8_t MS2_PIN  = 4;
    static constexpr uint8_t UART_RX  = 13;
    static constexpr uint8_t UART_TX  = 12;

    static constexpr uint32_t STEPPER_STEPS = 200;

    // PIO cycle accounting for a single "period" of the frequency generator:
    // two fixed set-pins-high [15] instructions (32 cycles), plus five surrounding
    // ops, yields 37 fixed cycles. Each "delay" loop iteration adds 2 cycles.
    static constexpr uint32_t PIO_FIX = 37;
    static constexpr uint32_t PIO_VAR = 2;

    PIO _pio;
    uint _sm0;  // steps generator
    uint _sm1;  // stop-on-count
    uint _sm2;  // steps counter
    uint _offset0;
    uint _offset1;
    uint _offset2;

    uint32_t _full_rev;
    uint32_t _SG_adj;
    uint8_t  _sp_na;

    uint32_t _max_homing_revs;
    uint32_t _max_steps;

    volatile bool _stepper_spinning;
    volatile bool _stallguarded;
};
