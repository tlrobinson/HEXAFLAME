#include "stepper.h"
#include "stepper_pio.h"
#include "rgb_led.h"
#include <hardware/clocks.h>
#include <hardware/irq.h>

// Map from ms index -> (label, reduction, ms1, ms2, SG_adjustment, serialport node address).
struct MicrostepEntry {
    const char* label;
    float reduction;
    uint8_t ms1;
    uint8_t ms2;
    uint8_t sg_adj;
    uint8_t sp_na;
};

static const MicrostepEntry MICROSTEP_MAP[] = {
    { "1/8",  0.125f,    0, 0, 1, 0 },
    { "1/16", 0.0625f,   1, 1, 2, 3 },
    { "1/32", 0.03125f,  1, 0, 4, 1 },
    { "1/64", 0.015625f, 0, 1, 8, 2 },
};

Stepper* Stepper::_instance = nullptr;

Stepper::Stepper(uint32_t max_frequency, uint32_t frequency, bool debug)
    : _tmc(Serial1, UART_RX, UART_TX, /*mtr_id placeholder*/ 0, 230400),
      _debug(debug),
      _max_frequency(max_frequency),
      _frequency(frequency),
      // Adafruit_NeoPixel grabs a state machine on pio0; park the stepper on
      // pio1 so there is no contention for programs or SMs.
      _pio(pio1),
      _sm0(0), _sm1(1), _sm2(2),
      _offset0(0), _offset1(0), _offset2(0),
      _full_rev(1600),
      _SG_adj(1),
      _sp_na(0),
      _max_homing_revs(5),
      _max_steps(8000),
      _stepper_spinning(false),
      _stallguarded(true)
{
    Serial.println("\nUploading stepper_controller ...");
    _instance = this;

    // Output pins.
    pinMode(DIR_PIN, OUTPUT);
    pinMode(MS1_PIN, OUTPUT);
    pinMode(MS2_PIN, OUTPUT);

    // Load the three PIO programs.
    _offset0 = pio_add_program(_pio, &steps_mot_program);
    _offset1 = pio_add_program(_pio, &stop_stepper_program);
    _offset2 = pio_add_program(_pio, &steps_counter_program);

    // Configure the step pin for PIO output.
    pio_gpio_init(_pio, STEP_PIN);
    pio_sm_set_consecutive_pindirs(_pio, _sm0, STEP_PIN, 1, true);

    // SM0: frequency generator driving STEP_PIN.
    {
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_wrap(&c, _offset0, _offset0 + steps_mot_program.length - 1);
        sm_config_set_set_pins(&c, STEP_PIN, 1);
        float div = (float)clock_get_hz(clk_sys) / (float)_frequency;
        sm_config_set_clkdiv(&c, div);
        pio_sm_init(_pio, _sm0, _offset0, &c);
        // Prime the FIFO with the slowest-speed delay. The PIO's `pull noblock`
        // picks this up on the first iteration after the SM is enabled.
        pio_sm_put_blocking(_pio, _sm0, 65535);
        pio_sm_set_enabled(_pio, _sm0, false);
    }

    // SM1: stop-on-count watcher. Raises PIO IRQ 1 when X underflows.
    {
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_wrap(&c, _offset1, _offset1 + stop_stepper_program.length - 1);
        sm_config_set_in_pins(&c, STEP_PIN);
        float div = (float)clock_get_hz(clk_sys) / (float)_max_frequency;
        sm_config_set_clkdiv(&c, div);
        pio_sm_init(_pio, _sm1, _offset1, &c);
        pio_sm_set_enabled(_pio, _sm1, false);
    }

    // Route PIO IRQ 1 (raised by SM1's `irq rel 0`) to NVIC PIO1_IRQ_0.
    pio_set_irq0_source_enabled(_pio, pis_interrupt1, true);
    irq_set_exclusive_handler(PIO1_IRQ_0, Stepper::onPioIrq);
    irq_set_enabled(PIO1_IRQ_0, true);

    // SM2: step counter.
    {
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_wrap(&c, _offset2, _offset2 + steps_counter_program.length - 1);
        sm_config_set_in_pins(&c, STEP_PIN);
        float div = (float)clock_get_hz(clk_sys) / (float)_max_frequency;
        sm_config_set_clkdiv(&c, div);
        pio_sm_init(_pio, _sm2, _offset2, &c);
        // Enable before setPlsCounter(): exec_wait_blocking only completes on
        // an enabled SM (the forced instruction pre-empts any stalled wait,
        // but the SM must be clocked to actually execute it).
        pio_sm_set_enabled(_pio, _sm2, true);
        setPlsCounter(0);
    }

    // Pick the microstep setting first: it selects the TMC UART node address.
    if (!microStep(0)) {
        Serial.println("ERROR: invalid microstep selection");
        return;
    }

    // The UART was opened with mtr_id=0 at construction; now that the
    // microstep selection told us the real node address, point the driver at it.
    _tmc.setMotorId(_sp_na);

    if (_tmc.test()) {
        setStallguard(0);
    }

    _max_steps = _max_homing_revs * _full_rev;
}

void Stepper::onPioIrq() {
    if (!_instance) return;
    if (pio1->irq & (1u << 1)) {
        pio1->irq = (1u << 1);  // clear
        pio_sm_set_enabled(pio1, _instance->_sm0, false);
        _instance->_stepper_spinning = false;
    }
}

void Stepper::onStallguard() {
    if (!_instance) return;
    pio_sm_set_enabled(pio1, _instance->_sm0, false);
    _instance->_stallguarded = true;
    _instance->_stepper_spinning = false;
}

bool Stepper::tmcTest() {
    return _tmc.test();
}

void Stepper::stopStepper() {
    pio_sm_set_enabled(_pio, _sm0, false);
    _stepper_spinning = false;
}

void Stepper::startStepper() {
    _stepper_spinning = true;
    pio_sm_set_enabled(_pio, _sm0, true);
}

void Stepper::deactivatePio() {
    pio_sm_set_enabled(_pio, _sm0, false);
    pio_sm_set_enabled(_pio, _sm1, false);
    pio_sm_set_enabled(_pio, _sm2, false);
    pio_remove_program(_pio, &steps_mot_program, _offset0);
    pio_remove_program(_pio, &stop_stepper_program, _offset1);
    pio_remove_program(_pio, &steps_counter_program, _offset2);
    Serial.println("State Machines deactivated");
}

// Back-to-back pio_sm_exec writes stomp each other: the second SMn_INSTR write
// can arrive before the SM has latched the first. MicroPython gets away with
// consecutive sm.exec() calls only because the interpreter inserts a few µs
// between them. A 1 µs gap here is ~125 PIO cycles at 125 MHz, plenty.
static inline void exec_pair(PIO pio, uint sm, uint instr_a, uint instr_b) {
    pio_sm_exec(pio, sm, instr_a);
    delayMicroseconds(1);
    pio_sm_exec(pio, sm, instr_b);
    delayMicroseconds(1);
}

void Stepper::setPlsToDo(uint32_t val) {
    pio_sm_set_enabled(_pio, _sm1, true);
    pio_sm_put_blocking(_pio, _sm1, val);
    exec_pair(_pio, _sm1,
              pio_encode_pull(false, false),
              pio_encode_mov(pio_x, pio_osr));
}

void Stepper::setPlsCounter(uint32_t val) {
    pio_sm_put_blocking(_pio, _sm2, val);
    exec_pair(_pio, _sm2,
              pio_encode_pull(false, false),
              pio_encode_mov(pio_x, pio_osr));
}

int32_t Stepper::getPlsCount() {
    exec_pair(_pio, _sm2,
              pio_encode_mov(pio_isr, pio_x),
              pio_encode_push(false, false));
    if (!pio_sm_is_rx_fifo_empty(_pio, _sm2)) {
        uint32_t v = pio_sm_get(_pio, _sm2);
        return (int32_t)(-(int32_t)v);
    }
    return -1;
}

void Stepper::setStallguard(int threshold) {
    if (threshold < 0) threshold = 0;
    if (threshold > 255) threshold = 255;
    _tmc.setStallguardCallback((uint8_t)threshold, Stepper::onStallguard);
    if (_debug) {
        if (threshold != 0) {
            Serial.print("Setting StallGuard (irq to GPIO 11) to value ");
            Serial.println(threshold);
        } else {
            Serial.println("Setting StallGuard (irq to GPIO 11) to 0, meaning max possible torque.");
        }
    }
}

uint32_t Stepper::getStepperValue(uint32_t stepper_freq) {
    if (stepper_freq == 0) return 0;
    int64_t v = ((int64_t)_frequency - (int64_t)stepper_freq * (int64_t)PIO_FIX) /
                ((int64_t)stepper_freq * (int64_t)PIO_VAR);
    if (v < 0) v = 0;
    if (v > 0xFFFFFFFFLL) v = 0xFFFFFFFFLL;
    return (uint32_t)v;
}

bool Stepper::microStep(int ms) {
    if (ms < 0 || ms >= (int)(sizeof(MICROSTEP_MAP) / sizeof(MICROSTEP_MAP[0]))) {
        Serial.println("Wrong parameter for micro_step");
        return false;
    }
    const MicrostepEntry& e = MICROSTEP_MAP[ms];
    digitalWrite(MS1_PIN, e.ms1);
    digitalWrite(MS2_PIN, e.ms2);
    Serial.print("Microstepping set to ");
    Serial.println(e.label);

    _full_rev = (uint32_t)(STEPPER_STEPS / e.reduction);
    _SG_adj = e.sg_adj;
    _sp_na = e.sp_na;
    Serial.print("Full revolution takes ");
    Serial.print(_full_rev);
    Serial.println(" steps");
    return true;
}

bool Stepper::homing(uint32_t h_speed, uint32_t stepper_freq, int startup_loops,
                     uint32_t retract_time_ms, int32_t retract_steps) {
    setStallguard(0);
    uint32_t max_homing_ms = retract_time_ms +
        (uint32_t)(_max_homing_revs * 1000ULL * _full_rev / stepper_freq);

    bool do_once = true;
    _stallguarded = false;
    stopStepper();
    setPlsCounter(0);
    setPlsToDo((uint32_t)retract_steps + _max_steps);
    pio_sm_put_blocking(_pio, _sm0, h_speed);
    startStepper();

    uint32_t min_sg_expected = (uint32_t)(0.15f * (float)stepper_freq / (float)_SG_adj);
    uint32_t sg_threshold = (uint32_t)(0.8f * (float)min_sg_expected);
    uint32_t sg_threshold_diag = (uint32_t)(0.45f * (float)min_sg_expected);

    if (_debug) {
        Serial.print("Homing with stepper speed of ");
        Serial.print(stepper_freq);
        Serial.print("Hz and UART StallGuard threshold of ");
        Serial.println(sg_threshold);
    }

    uint32_t t_ref = millis();
    uint32_t last_tick = t_ref;
    int32_t min_sg_seen = 1024;
    int i = 0;
    while (millis() - t_ref < max_homing_ms) {
        int32_t sg = _tmc.getStallguardResult();
        if (sg < min_sg_seen) min_sg_seen = sg;
        i++;
        // Diagnostic heartbeat once per second so a hang vs. a slow timeout
        // is distinguishable from the serial log alone.
        uint32_t now = millis();
        if (_debug && (now - last_tick) >= 1000) {
            last_tick = now;
            Serial.print("  .. t=");
            Serial.print(now - t_ref);
            Serial.print("ms i=");
            Serial.print(i);
            Serial.print(" sg=");
            Serial.print(sg);
            Serial.print(" min_sg=");
            Serial.print(min_sg_seen);
            Serial.print(" diag=");
            Serial.println(_stallguarded ? 1 : 0);
        }
        if (i > startup_loops) {
            if (do_once) {
                setStallguard((int)sg_threshold_diag);
                do_once = false;
            }
            if ((uint32_t)sg < sg_threshold || _stallguarded) {
                stopStepper();
                int32_t pls = getPlsCount();
                // Matches the Python check: a -1 "couldn't read" compares less
                // than 0.95 * max_steps and is treated as a valid homing.
                if (pls < (int32_t)(0.95f * (float)((uint32_t)retract_steps + _max_steps))) {
                    int times;
                    float time_s;
                    float bright;
                    if (_stallguarded) {
                        times = 1; time_s = 0.01f; bright = 0.8f;
                    } else {
                        times = 3; time_s = 0.05f; bright = 0.1f;
                    }
                    rgb_led.flashColor("red", bright, times, time_s);

                    if (_debug) {
                        if (_stallguarded) Serial.println("StallGuard detections via DIAG pin");
                        Serial.print("Homing reached in ");
                        Serial.print(i);
                        Serial.print(" iterations, ");
                        Serial.print(millis() - t_ref);
                        Serial.println(" ms");
                    }
                    return true;
                }
            }
        }
    }

    setStallguard(0);
    stopStepper();
    Serial.println("Failed homing");
    return false;
}

void Stepper::retract(uint32_t speed, int startup_loops,
                      uint32_t* retract_time_ms, int32_t* retract_steps) {
    setStallguard(0);
    stopStepper();
    setPlsCounter(0);
    setPlsToDo(_max_steps);
    pio_sm_put_blocking(_pio, _sm0, speed);
    startStepper();
    if (_debug) {
        Serial.println("Retract the stepper prior the first home search");
    }
    uint32_t t_ref = millis();
    for (int i = 0; i < startup_loops; i++) {
        (void)_tmc.getStallguardResult();
    }
    stopStepper();
    *retract_time_ms = millis() - t_ref;
    *retract_steps = getPlsCount();
    if (*retract_steps < 0) *retract_steps = 0;
}

bool Stepper::centering(uint32_t stepper_freq) {
    uint32_t min_freq = 400 * _SG_adj;
    uint32_t max_freq = 1200 * _SG_adj;
    stepper_freq = _SG_adj * stepper_freq;
    if (stepper_freq < min_freq) stepper_freq = min_freq;
    if (stepper_freq > max_freq) stepper_freq = max_freq;

    uint32_t stepper_val = getStepperValue(stepper_freq);
    int startup_loops = 10;

    digitalWrite(DIR_PIN, LOW);
    uint32_t retract_time_ms = 0;
    int32_t retract_steps = 0;
    retract(stepper_val, startup_loops, &retract_time_ms, &retract_steps);

    digitalWrite(DIR_PIN, HIGH);
    if (!homing(stepper_val, stepper_freq, startup_loops, retract_time_ms, retract_steps)) {
        stopStepper();
        rgb_led.flashColor("blue", 0.1f, 10, 0.05f);
        return false;
    }

    digitalWrite(DIR_PIN, LOW);
    if (!homing(stepper_val, stepper_freq, startup_loops, retract_time_ms, retract_steps)) {
        stopStepper();
        rgb_led.flashColor("blue", 0.1f, 10, 0.05f);
        return false;
    }

    int32_t steps_range = getPlsCount();
    if (steps_range < 0) steps_range = 0;
    int32_t half_range = steps_range / 2;
    digitalWrite(DIR_PIN, HIGH);
    setPlsToDo((uint32_t)half_range);
    pio_sm_put_blocking(_pio, _sm0, stepper_val);
    startStepper();
    uint32_t centering_time_ms = 100 + (uint32_t)((int64_t)half_range * 1000LL / (int64_t)stepper_freq);
    if (_debug) {
        Serial.print("Counted ");
        Serial.print(steps_range);
        Serial.print(" steps between the 2 homes; centering to ");
        Serial.println(half_range);
    }
    delay(centering_time_ms);
    if (!_stepper_spinning) {
        rgb_led.flashColor("green", 0.2f, 3, 0.05f);
    }
    return true;
}
