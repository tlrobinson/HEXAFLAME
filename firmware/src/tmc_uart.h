#pragma once

#include <Arduino.h>

// Hardware-independent byte transport for the TMC2209's single-wire UART.
//
// On the RP2040 the driver is reached over a hardware UART peripheral; on the
// Octopus each driver has its own bit-banged SoftwareSerial line. Both share
// the same electrical quirk: TX and RX are joined, so every byte written is
// echoed back before the driver's reply arrives. Datagram framing, CRC and
// echo-skipping all live in Tmc2209 - this interface only moves raw bytes.
class ITmcUart {
public:
  virtual ~ITmcUart() {}

  // One-time setup of the underlying serial peripheral.
  virtual bool begin() = 0;

  // Write `txLen` bytes, then collect reply bytes (including any echoed TX
  // bytes) into `rx` until `timeoutMs` elapses or `rxMax` is reached.
  // Returns the number of bytes placed in `rx`.
  virtual size_t transfer(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxMax,
                          uint32_t timeoutMs) = 0;
};
