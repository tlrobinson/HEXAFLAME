#include <Arduino.h>

#include "../board_config.h"
#include "step_engine_timer.h"
#include "tmc_uart_sw.h"

// BIGTREETECH Octopus Pro V1.1 board definition - eight independent stepper
// channels (MOTOR0..MOTOR7).
//
// Pin assignments are cross-referenced with the Klipper and Marlin board
// definitions for the Octopus / Octopus Pro V1.1 (the two are pin-compatible
// for the stepper sockets). Each channel gets:
//   - a STEP and DIR pin driven by a dedicated hardware timer,
//   - an active-LOW ENABLE pin,
//   - a single-wire TMC2209 UART pad,
//   - a StallGuard DIAG input.
//
// Microstepping is programmed over UART (the MS1/MS2 straps are not exposed),
// and every driver answers at UART node address 0.
namespace {

constexpr uint32_t kTmcBaud = 19200;

struct RawChannel {
  const char *name;
  uint32_t step;
  uint32_t dir;
  uint32_t enable;
  uint32_t uart;
  uint32_t diag;
};

const RawChannel kRawChannels[8] = {
    {"M0", PF13, PF12, PF14, PC4, PG6},
    {"M1", PG0, PG1, PF15, PD11, PG9},
    {"M2", PF11, PG3, PG5, PC6, PG10},
    {"M3", PG4, PC1, PA0, PC7, PG11},
    {"M4", PF9, PF10, PG2, PF2, PG12},
    {"M5", PC13, PF0, PF1, PE4, PG13},
    {"M6", PE2, PE3, PD4, PE1, PG14},
    {"M7", PE6, PA14, PE0, PD3, PG15},
};

// One hardware timer per channel for STEP-pulse generation. These eight
// timers exist on every supported variant (STM32F446 / F429 / H723) and avoid
// TIM6/TIM7, which the Arduino core reserves for tone()/Servo.
TIM_TypeDef *const kStepTimers[8] = {TIM1, TIM2, TIM3, TIM4, TIM5, TIM8, TIM12, TIM13};

TimerStepEngine g_engine0(kRawChannels[0].step, kRawChannels[0].dir, kStepTimers[0]);
TimerStepEngine g_engine1(kRawChannels[1].step, kRawChannels[1].dir, kStepTimers[1]);
TimerStepEngine g_engine2(kRawChannels[2].step, kRawChannels[2].dir, kStepTimers[2]);
TimerStepEngine g_engine3(kRawChannels[3].step, kRawChannels[3].dir, kStepTimers[3]);
TimerStepEngine g_engine4(kRawChannels[4].step, kRawChannels[4].dir, kStepTimers[4]);
TimerStepEngine g_engine5(kRawChannels[5].step, kRawChannels[5].dir, kStepTimers[5]);
TimerStepEngine g_engine6(kRawChannels[6].step, kRawChannels[6].dir, kStepTimers[6]);
TimerStepEngine g_engine7(kRawChannels[7].step, kRawChannels[7].dir, kStepTimers[7]);

StepEngine *const kEngines[8] = {&g_engine0, &g_engine1, &g_engine2, &g_engine3,
                                 &g_engine4, &g_engine5, &g_engine6, &g_engine7};

SoftwareTmcUart g_uart0(kRawChannels[0].uart, kTmcBaud);
SoftwareTmcUart g_uart1(kRawChannels[1].uart, kTmcBaud);
SoftwareTmcUart g_uart2(kRawChannels[2].uart, kTmcBaud);
SoftwareTmcUart g_uart3(kRawChannels[3].uart, kTmcBaud);
SoftwareTmcUart g_uart4(kRawChannels[4].uart, kTmcBaud);
SoftwareTmcUart g_uart5(kRawChannels[5].uart, kTmcBaud);
SoftwareTmcUart g_uart6(kRawChannels[6].uart, kTmcBaud);
SoftwareTmcUart g_uart7(kRawChannels[7].uart, kTmcBaud);

Tmc2209 g_tmc0(g_uart0);
Tmc2209 g_tmc1(g_uart1);
Tmc2209 g_tmc2(g_uart2);
Tmc2209 g_tmc3(g_uart3);
Tmc2209 g_tmc4(g_uart4);
Tmc2209 g_tmc5(g_uart5);
Tmc2209 g_tmc6(g_uart6);
Tmc2209 g_tmc7(g_uart7);

Tmc2209 *const kTmcs[8] = {&g_tmc0, &g_tmc1, &g_tmc2, &g_tmc3,
                           &g_tmc4, &g_tmc5, &g_tmc6, &g_tmc7};

ChannelConfig g_channels[8];
bool g_begun = false;

}  // namespace

namespace Board {

const char *name() {
#if defined(BOARD_OCTOPUS_F446)
  return "octopus-pro-v1.1-f446";
#else
  return "octopus-pro-v1.1-h723";
#endif
}

void begin() {
  if (g_begun) {
    return;
  }
  for (size_t i = 0; i < 8; ++i) {
    g_channels[i].name = kRawChannels[i].name;
    g_channels[i].dirPin = static_cast<int>(kRawChannels[i].dir);
    g_channels[i].ms1Pin = -1;
    g_channels[i].ms2Pin = -1;
    g_channels[i].enablePin = static_cast<int>(kRawChannels[i].enable);
    g_channels[i].diagPin = static_cast<int>(kRawChannels[i].diag);
    g_channels[i].microstepMode = 0;       // 1/8 microstep
    g_channels[i].microstepViaUart = true;
    g_channels[i].tmcAddress = 0;
    g_channels[i].engine = kEngines[i];
    g_channels[i].tmc = kTmcs[i];
  }
  g_begun = true;
}

size_t channelCount() {
  return 8;
}

ChannelConfig &channelConfig(size_t index) {
  return g_channels[index < 8 ? index : 0];
}

}  // namespace Board
