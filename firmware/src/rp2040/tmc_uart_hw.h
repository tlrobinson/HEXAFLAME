#pragma once

#include <Arduino.h>
#include <SerialUART.h>

#include "../tmc_uart.h"

// RP2040 TMC2209 transport over a hardware UART peripheral. The board ties the
// UART's TX and RX pins together onto the driver's PDN_UART line, so the
// transfer path tolerates the request bytes being echoed back ahead of the
// reply (Tmc2209 strips the echo).
class HardwareTmcUart : public ITmcUart {
public:
  HardwareTmcUart(SerialUART &serial, uint8_t rxPin, uint8_t txPin, uint32_t baudrate);

  bool begin() override;
  size_t transfer(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxMax,
                  uint32_t timeoutMs) override;

private:
  SerialUART &serial_;
  uint8_t rxPin_;
  uint8_t txPin_;
  uint32_t baudrate_;
  uint32_t communicationPauseUs_;
};
