#include "tmc2209.h"

Tmc2209::Tmc2209(ITmcUart &uart) : uart_(uart) {}

bool Tmc2209::begin() {
  return uart_.begin();
}

uint8_t Tmc2209::computeCrc8Atm(const uint8_t *datagram, size_t length) const {
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

bool Tmc2209::readReg(uint8_t reg, uint8_t *outData, size_t outLength) {
  uint8_t frame[4] = {0x55, address_, reg, 0x00};
  frame[3] = computeCrc8Atm(frame, 3);

  // The reply is an 8-byte read datagram. On the shared single-wire bus the
  // four request bytes are echoed back first, so the buffer may hold up to
  // twelve bytes; the four payload bytes always sit five back from the end.
  uint8_t buffer[16] = {};
  const size_t count = uart_.transfer(frame, sizeof(frame), buffer, sizeof(buffer), kReadTimeoutMs);

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

int32_t Tmc2209::readRegister(uint8_t reg) {
  uint32_t value = 0;
  if (readRegister(reg, value)) {
    return static_cast<int32_t>(value);
  }
  return 0;
}

bool Tmc2209::readRegister(uint8_t reg, uint32_t &value) {
  uint8_t bytes[4] = {};
  for (int tries = 0; tries < 10; ++tries) {
    if (readReg(reg, bytes, sizeof(bytes))) {
      value = (static_cast<uint32_t>(bytes[0]) << 24U) |
              (static_cast<uint32_t>(bytes[1]) << 16U) |
              (static_cast<uint32_t>(bytes[2]) << 8U) |
              static_cast<uint32_t>(bytes[3]);
      return true;
    }
  }
  return false;
}

bool Tmc2209::writeReg(uint8_t reg, uint32_t value) {
  uint8_t frame[8] = {
      0x55,
      address_,
      static_cast<uint8_t>(reg | 0x80U),
      static_cast<uint8_t>((value >> 24U) & 0xFFU),
      static_cast<uint8_t>((value >> 16U) & 0xFFU),
      static_cast<uint8_t>((value >> 8U) & 0xFFU),
      static_cast<uint8_t>(value & 0xFFU),
      0x00,
  };
  frame[7] = computeCrc8Atm(frame, 7);

  uint8_t scratch[16] = {};
  uart_.transfer(frame, sizeof(frame), scratch, sizeof(scratch), 5);
  return true;
}

bool Tmc2209::writeRegCheck(uint8_t reg, uint32_t value) {
  // IFCNT is an 8-bit counter the driver bumps on every accepted write.
  // A write issued back-to-back with another datagram is occasionally
  // dropped silently, so retry up to three times until the counter
  // actually moves. (Robustness pattern from the firmware-2 BTT build.)
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    const int32_t ifcntBefore = readRegister(kIfcntReg);
    if (!writeReg(reg, value)) {
      continue;
    }
    const int32_t ifcntAfter = readRegister(kIfcntReg);
    if (ifcntAfter != ifcntBefore) {
      return true;
    }
  }
  return false;
}

bool Tmc2209::writeRegister(uint8_t reg, uint32_t value, bool verify) {
  return verify ? writeRegCheck(reg, value) : writeReg(reg, value);
}

bool Tmc2209::test() {
  uint8_t bytes[4] = {};
  return readReg(0x06, bytes, sizeof(bytes));
}

int32_t Tmc2209::getStallguardResult() {
  return readRegister(kSgResultReg);
}

bool Tmc2209::setStallguardThreshold(uint8_t threshold) {
  return writeRegCheck(kSgthrsReg, threshold);
}

bool Tmc2209::setCoolStepThreshold(uint32_t threshold) {
  return writeRegCheck(kTcoolthrsReg, threshold);
}

bool Tmc2209::setIdlePowerDownDelay(uint8_t delay) {
  return writeRegCheck(kTpowerdownReg, delay);
}

bool Tmc2209::enableUartMode(bool microstepRegisterSelect) {
  uint32_t gconf = 0;
  if (!readRegister(kGconfReg, gconf)) {
    gconf = 0;
  }
  gconf |= (1UL << kPdnDisableBit);
  if (microstepRegisterSelect) {
    gconf |= (1UL << kMstepRegSelectBit);
  }
  return writeRegCheck(kGconfReg, gconf);
}

bool Tmc2209::setMicrostepResolution(uint8_t mres) {
  uint32_t chopconf = 0;
  if (!readRegister(kChopconfReg, chopconf)) {
    chopconf = kChopconfReset;
  }
  chopconf &= ~(0xFUL << 24U);
  chopconf |= (static_cast<uint32_t>(mres & 0x0F) << 24U);
  return writeRegCheck(kChopconfReg, chopconf);
}

size_t Tmc2209::transfer(const uint8_t *txData, size_t txLength, uint8_t *rxData, size_t rxMaxLength,
                         uint32_t timeoutMs) {
  return uart_.transfer(txData, txLength, rxData, rxMaxLength, timeoutMs);
}
