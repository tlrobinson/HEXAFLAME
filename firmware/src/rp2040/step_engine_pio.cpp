#include "step_engine_pio.h"

#include <hardware/clocks.h>
#include <hardware/irq.h>

namespace {
const uint16_t kStepProgramInstructions[] = {
    static_cast<uint16_t>(pio_encode_pull(false, false)),
    static_cast<uint16_t>(pio_encode_mov(pio_x, pio_osr)),
    static_cast<uint16_t>(pio_encode_mov(pio_y, pio_x)),
    static_cast<uint16_t>(pio_encode_set(pio_pins, 1) | pio_encode_delay(15)),
    static_cast<uint16_t>(pio_encode_set(pio_pins, 1) | pio_encode_delay(15)),
    static_cast<uint16_t>(pio_encode_set(pio_pins, 0)),
    static_cast<uint16_t>(pio_encode_jmp_y_dec(5)),
    static_cast<uint16_t>(pio_encode_jmp(0)),
};

const pio_program kStepProgram = {
    .instructions = kStepProgramInstructions,
    .length = sizeof(kStepProgramInstructions) / sizeof(kStepProgramInstructions[0]),
    .origin = -1,
    .pio_version = 0,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x0,
#endif
};

const uint16_t kStopProgramInstructions[] = {
    static_cast<uint16_t>(pio_encode_wait_pin(true, 0)),
    static_cast<uint16_t>(pio_encode_wait_pin(false, 0)),
    static_cast<uint16_t>(pio_encode_jmp_x_dec(0)),
    static_cast<uint16_t>(pio_encode_irq_wait(true, 0)),
};

const pio_program kStopProgram = {
    .instructions = kStopProgramInstructions,
    .length = sizeof(kStopProgramInstructions) / sizeof(kStopProgramInstructions[0]),
    .origin = -1,
    .pio_version = 0,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x0,
#endif
};

const uint16_t kCountProgramInstructions[] = {
    static_cast<uint16_t>(pio_encode_wait_pin(false, 0)),
    static_cast<uint16_t>(pio_encode_wait_pin(true, 0)),
    static_cast<uint16_t>(pio_encode_jmp_x_dec(0)),
};

const pio_program kCountProgram = {
    .instructions = kCountProgramInstructions,
    .length = sizeof(kCountProgramInstructions) / sizeof(kCountProgramInstructions[0]),
    .origin = -1,
    .pio_version = 0,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x0,
#endif
};
}  // namespace

PioStepEngine *PioStepEngine::instance_ = nullptr;

PioStepEngine::PioStepEngine(uint8_t stepPin, uint8_t dirPin) : stepPin_(stepPin), dirPin_(dirPin) {}

bool PioStepEngine::begin() {
  instance_ = this;

  pinMode(stepPin_, OUTPUT);
  digitalWrite(stepPin_, LOW);
  pinMode(dirPin_, OUTPUT);

  offsetStep_ = pio_add_program(pio_, &kStepProgram);
  offsetStop_ = pio_add_program(pio_, &kStopProgram);
  offsetCount_ = pio_add_program(pio_, &kCountProgram);

  pio_gpio_init(pio_, stepPin_);
  pio_sm_set_consecutive_pindirs(pio_, smStep_, stepPin_, 1, true);

  {
    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offsetStep_, offsetStep_ + kStepProgram.length - 1);
    sm_config_set_set_pins(&config, stepPin_, 1);
    sm_config_set_clkdiv(&config,
                         static_cast<float>(clock_get_hz(clk_sys)) / static_cast<float>(pioClockHz_));
    pio_sm_init(pio_, smStep_, offsetStep_, &config);
    pio_sm_put_blocking(pio_, smStep_, 65535);
    pio_sm_set_enabled(pio_, smStep_, false);
  }

  {
    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offsetStop_, offsetStop_ + kStopProgram.length - 1);
    sm_config_set_in_pins(&config, stepPin_);
    sm_config_set_clkdiv(&config,
                         static_cast<float>(clock_get_hz(clk_sys)) / static_cast<float>(sampleClockHz_));
    pio_sm_init(pio_, smStop_, offsetStop_, &config);
    pio_sm_set_enabled(pio_, smStop_, false);
  }

  {
    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offsetCount_, offsetCount_ + kCountProgram.length - 1);
    sm_config_set_in_pins(&config, stepPin_);
    sm_config_set_clkdiv(&config,
                         static_cast<float>(clock_get_hz(clk_sys)) / static_cast<float>(sampleClockHz_));
    pio_sm_init(pio_, smCount_, offsetCount_, &config);
    pio_sm_set_enabled(pio_, smCount_, true);
    setPulseCounter(0);
  }

  pio_set_irq0_source_enabled(pio_, pis_interrupt1, true);
  irq_set_exclusive_handler(PIO1_IRQ_0, pioIrqHandler);
  irq_set_enabled(PIO1_IRQ_0, true);

  return true;
}

uint32_t PioStepEngine::hzToPioValue(uint32_t hz) const {
  if (hz == 0) {
    return 0;
  }
  int64_t value = (static_cast<int64_t>(pioClockHz_) - static_cast<int64_t>(hz) * static_cast<int64_t>(kPioFix)) /
                  (static_cast<int64_t>(hz) * static_cast<int64_t>(kPioVar));
  if (value < 0) {
    value = 0;
  }
  return static_cast<uint32_t>(value);
}

void PioStepEngine::execInstructionPair(PIO pio, uint sm, uint instrA, uint instrB) {
  pio_sm_exec_wait_blocking(pio, sm, instrA);
  pio_sm_exec_wait_blocking(pio, sm, instrB);
}

void PioStepEngine::setPulseCounter(uint32_t pulses) {
  pio_sm_put_blocking(pio_, smCount_, pulses);
  execInstructionPair(pio_, smCount_, pio_encode_pull(false, false), pio_encode_mov(pio_x, pio_osr));
}

int32_t PioStepEngine::getPulseCount() {
  execInstructionPair(pio_, smCount_, pio_encode_mov(pio_isr, pio_x), pio_encode_push(false, false));
  if (pio_sm_is_rx_fifo_empty(pio_, smCount_)) {
    return -1;
  }
  return static_cast<int32_t>(-static_cast<int32_t>(pio_sm_get(pio_, smCount_)));
}

void PioStepEngine::setPulsesToDo(uint32_t pulses) {
  pio_sm_set_enabled(pio_, smStop_, true);
  pio_sm_put_blocking(pio_, smStop_, pulses);
  execInstructionPair(pio_, smStop_, pio_encode_pull(false, false), pio_encode_mov(pio_x, pio_osr));
}

void PioStepEngine::setDirection(bool positive) {
  digitalWrite(dirPin_, positive ? HIGH : LOW);
}

void PioStepEngine::prepare(uint32_t pulses, uint32_t initialHz) {
  pio_sm_set_enabled(pio_, smStep_, false);
  spinning_ = false;
  pio_sm_clear_fifos(pio_, smStep_);
  setPulseCounter(0);
  setPulsesToDo(pulses);
  pio_sm_put_blocking(pio_, smStep_, hzToPioValue(initialHz));
}

void PioStepEngine::start() {
  spinning_ = true;
  pio_sm_set_enabled(pio_, smStep_, true);
}

void PioStepEngine::updateSpeed(uint32_t hz) {
  if (!pio_sm_is_tx_fifo_full(pio_, smStep_)) {
    pio_sm_put(pio_, smStep_, hzToPioValue(hz));
  }
}

void PioStepEngine::stop() {
  pio_sm_set_enabled(pio_, smStep_, false);
  spinning_ = false;
}

bool PioStepEngine::isSpinning() {
  return spinning_;
}

uint32_t PioStepEngine::completedPulses() {
  const int32_t count = getPulseCount();
  return count < 0 ? 0 : static_cast<uint32_t>(count);
}

void PioStepEngine::pioIrqHandler() {
  if (instance_ == nullptr) {
    return;
  }
  if ((pio1->irq & (1u << 1)) == 0) {
    return;
  }
  pio1->irq = (1u << 1);
  pio_sm_set_enabled(pio1, instance_->smStep_, false);
  instance_->spinning_ = false;
}
