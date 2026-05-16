#include "tmc_uart_sw.h"

SoftwareTmcUart::SoftwareTmcUart(uint32_t pin, uint32_t baudrate)
    : pin_(pin), baudrate_(baudrate), serial_(pin, pin) {}

bool SoftwareTmcUart::begin() {
  serial_.begin(baudrate_);
  // Register as the active half-duplex listener now so the SoftwareSerial ISR
  // flips the line back to RX after every transmit burst.
  serial_.listen();
  return true;
}

size_t SoftwareTmcUart::transfer(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxMax,
                                 uint32_t timeoutMs) {
  while (serial_.available() > 0) {
    serial_.read();
  }

  serial_.listen();
  if (tx != nullptr && txLen > 0) {
    for (size_t i = 0; i < txLen; ++i) {
      serial_.write(tx[i]);
    }
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
    if (count >= 8 && (now - lastByteMs) >= 3) {
      break;
    }
  }
  return count;
}
