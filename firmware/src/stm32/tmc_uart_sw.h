#pragma once

#include <Arduino.h>
#include <SoftwareSerial.h>

#include "../tmc_uart.h"

// STM32 TMC2209 transport over a per-channel single-wire SoftwareSerial line.
//
// Each Octopus driver's PDN_UART pad joins TX and RX through a series
// resistor, so SoftwareSerial is instantiated in half-duplex mode (same pin
// for RX and TX). Its pin-change-interrupt RX path buffers the driver's reply
// as it arrives, which is essential because the reply lands a few bit-times
// after the request burst finishes.
class SoftwareTmcUart : public ITmcUart {
public:
  SoftwareTmcUart(uint32_t pin, uint32_t baudrate);

  bool begin() override;
  size_t transfer(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxMax,
                  uint32_t timeoutMs) override;

private:
  uint32_t pin_;
  uint32_t baudrate_;
  SoftwareSerial serial_;
};
