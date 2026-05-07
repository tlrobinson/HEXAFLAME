#include "tmc2209_uart.h"

namespace {
constexpr uint32_t kReadTimeoutMs = 20;
}

Tmc2209Uart::Tmc2209Uart(SerialUART &serial, uint8_t rxPin, uint8_t txPin, uint8_t mtrId, uint32_t baudrate)
    : serial_(serial),
      rxPin_(rxPin),
      txPin_(txPin),
      mtrId_(mtrId),
      baudrate_(baudrate),
      communicationPauseUs_(max<uint32_t>(500000000UL / max<uint32_t>(1, baudrate), 500UL)) {}

bool Tmc2209Uart::begin() {
  serial_.setRX(rxPin_);
  serial_.setTX(txPin_);
  serial_.begin(baudrate_);
  delay(10);
  while (serial_.available() > 0) {
    serial_.read();
  }
  return true;
}

uint8_t Tmc2209Uart::computeCrc8Atm(const uint8_t *datagram, size_t length) const {
  uint8_t crc = 0;

  for (size_t i = 0; i < length; ++i) {
    uint8_t byte = datagram[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (((crc >> 7) ^ (byte & 0x01U)) != 0U) {
        crc = static_cast<uint8_t>(((crc << 1U) ^ 0x07U) & 0xFFU);
      } else {
        crc = static_cast<uint8_t>((crc << 1U) & 0xFFU);
      }
      byte >>= 1U;
    }
  }

  return crc;
}

bool Tmc2209Uart::readReg(uint8_t reg, uint8_t *outData, size_t outLength) {
  uint8_t frame[4] = {0x55, mtrId_, reg, 0x00};
  frame[3] = computeCrc8Atm(frame, 3);

  while (serial_.available() > 0) {
    serial_.read();
  }

  const size_t written = serial_.write(frame, sizeof(frame));
  serial_.flush();
  if (written != sizeof(frame)) {
    return false;
  }

  delayMicroseconds(communicationPauseUs_);

  const uint32_t startMs = millis();
  while (millis() - startMs < kReadTimeoutMs) {
    if (serial_.available() >= 8) {
      break;
    }
    delay(1);
  }

  delayMicroseconds(communicationPauseUs_);

  uint8_t buffer[16] = {};
  size_t count = 0;
  while (serial_.available() > 0 && count < sizeof(buffer)) {
    buffer[count++] = static_cast<uint8_t>(serial_.read());
    delayMicroseconds(50);
  }

  if (count < 8 || outLength < 4) {
    return false;
  }

  const size_t dataOffset = count - 5;
  if (dataOffset + 3 >= count) {
    return false;
  }

  outData[0] = buffer[dataOffset];
  outData[1] = buffer[dataOffset + 1];
  outData[2] = buffer[dataOffset + 2];
  outData[3] = buffer[dataOffset + 3];
  return true;
}

int32_t Tmc2209Uart::readInt(uint8_t reg) {
  uint8_t bytes[4] = {};

  for (int tries = 0; tries < 10; ++tries) {
    if (readReg(reg, bytes, sizeof(bytes))) {
      const uint32_t value = (static_cast<uint32_t>(bytes[0]) << 24U) |
                             (static_cast<uint32_t>(bytes[1]) << 16U) |
                             (static_cast<uint32_t>(bytes[2]) << 8U) |
                             static_cast<uint32_t>(bytes[3]);
      return static_cast<int32_t>(value);
    }

    if (tries == 0) {
      Serial.println("TMC2209: did not get the expected 4 data bytes.");
    }
  }

  Serial.println("TMC2209: after 10 tries not valid answer. Is stepper power on?");
  return 0;
}

bool Tmc2209Uart::writeReg(uint8_t reg, uint32_t value) {
  uint8_t frame[8] = {
      0x55,
      mtrId_,
      static_cast<uint8_t>(reg | 0x80U),
      static_cast<uint8_t>((value >> 24U) & 0xFFU),
      static_cast<uint8_t>((value >> 16U) & 0xFFU),
      static_cast<uint8_t>((value >> 8U) & 0xFFU),
      static_cast<uint8_t>(value & 0xFFU),
      0x00,
  };
  frame[7] = computeCrc8Atm(frame, 7);

  while (serial_.available() > 0) {
    serial_.read();
  }

  const size_t written = serial_.write(frame, sizeof(frame));
  serial_.flush();
  delayMicroseconds(communicationPauseUs_);
  return written == sizeof(frame);
}

bool Tmc2209Uart::writeRegCheck(uint8_t reg, uint32_t value) {
  const int32_t ifcnt1 = readInt(kIfcntReg);
  if (!writeReg(reg, value)) {
    return false;
  }
  const int32_t ifcnt2 = readInt(kIfcntReg);
  const int32_t ifcnt3 = readInt(kIfcntReg);
  return ifcnt2 > ifcnt1 || ifcnt3 > ifcnt1;
}

bool Tmc2209Uart::test() {
  uint8_t bytes[4] = {};
  return readReg(0x06, bytes, sizeof(bytes));
}
