#include "tmc_uart_hw.h"

HardwareTmcUart::HardwareTmcUart(SerialUART &serial, uint8_t rxPin, uint8_t txPin, uint32_t baudrate)
    : serial_(serial),
      rxPin_(rxPin),
      txPin_(txPin),
      baudrate_(baudrate),
      communicationPauseUs_(max<uint32_t>(500000000UL / max<uint32_t>(1, baudrate), 500UL)) {}

bool HardwareTmcUart::begin() {
  serial_.setRX(rxPin_);
  serial_.setTX(txPin_);
  serial_.begin(baudrate_);
  delay(10);
  while (serial_.available() > 0) {
    serial_.read();
  }
  return true;
}

size_t HardwareTmcUart::transfer(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxMax,
                                 uint32_t timeoutMs) {
  while (serial_.available() > 0) {
    serial_.read();
  }

  if (tx != nullptr && txLen > 0) {
    serial_.write(tx, txLen);
    serial_.flush();
    delayMicroseconds(communicationPauseUs_);
  }

  // A read datagram is eight bytes; collect until the bus has been idle for a
  // short gap once a plausible reply has arrived, or until the timeout.
  const uint32_t startMs = millis();
  uint32_t lastByteMs = startMs;
  size_t count = 0;
  while (count < rxMax) {
    if (serial_.available() > 0) {
      rx[count++] = static_cast<uint8_t>(serial_.read());
      lastByteMs = millis();
      continue;
    }
    const uint32_t now = millis();
    if ((now - startMs) >= timeoutMs) {
      break;
    }
    if (count >= 8 && (now - lastByteMs) >= 2) {
      break;
    }
  }
  return count;
}
