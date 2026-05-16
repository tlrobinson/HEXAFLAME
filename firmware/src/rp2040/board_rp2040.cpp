#include <Arduino.h>
#include <SerialUART.h>

#include "../board_config.h"
#include "step_engine_pio.h"
#include "tmc_uart_hw.h"

// RP2040 board definition - the original single-channel HEXAFLAME valve
// controller (Lewis' V3 board). Pin assignments match the original firmware.
namespace {
constexpr uint8_t kStepPin = 5;
constexpr uint8_t kDirPin = 6;
constexpr uint8_t kMs1Pin = 3;
constexpr uint8_t kMs2Pin = 4;
constexpr uint8_t kDiagPin = 11;
constexpr uint8_t kUartTxPin = 12;
constexpr uint8_t kUartRxPin = 13;
constexpr uint32_t kTmcBaud = 230400;

PioStepEngine g_engine(kStepPin, kDirPin);
HardwareTmcUart g_uart(Serial1, kUartRxPin, kUartTxPin, kTmcBaud);
Tmc2209 g_tmc(g_uart);

ChannelConfig g_channel = {
    "valve",        // name
    kDirPin,        // dirPin
    kMs1Pin,        // ms1Pin
    kMs2Pin,        // ms2Pin
    -1,             // enablePin (hard-wired on the V3 board)
    kDiagPin,       // diagPin
    0,              // microstepMode -> 1/8
    false,          // microstepViaUart (MS1/MS2 straps)
    0,              // tmcAddress
    &g_engine,
    &g_tmc,
};

bool g_begun = false;
}  // namespace

namespace Board {

const char *name() {
  return "rp2040";
}

void begin() {
  g_begun = true;
}

size_t channelCount() {
  return 1;
}

ChannelConfig &channelConfig(size_t index) {
  (void)index;
  return g_channel;
}

}  // namespace Board
