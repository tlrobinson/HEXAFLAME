#include "tmc2209.h"

Tmc2209::Tmc2209(SerialUART &serial, uint8_t rxPin, uint8_t txPin, uint8_t mtrId, uint32_t baudrate)
    : uart_(serial, rxPin, txPin, mtrId, baudrate) {}

bool Tmc2209::begin() {
  return uart_.begin();
}

bool Tmc2209::test() {
  return uart_.test();
}

int32_t Tmc2209::getStallguardResult() {
  return uart_.readInt(kSgResultReg);
}

int32_t Tmc2209::readRegister(uint8_t reg) {
  return uart_.readInt(reg);
}

bool Tmc2209::readRegister(uint8_t reg, uint32_t &value) {
  return uart_.readRegister(reg, value);
}

bool Tmc2209::setStallguardThreshold(uint8_t threshold) {
  return uart_.writeRegCheck(kSgthrsReg, threshold);
}

bool Tmc2209::setCoolStepThreshold(uint32_t threshold) {
  return uart_.writeRegCheck(kTcoolthrsReg, threshold);
}

bool Tmc2209::enableUartMode() {
  uint32_t gconf = 0;
  if (!readRegister(kGconfReg, gconf)) {
    gconf = 0;
  }

  return uart_.writeRegCheck(kGconfReg, gconf | (1UL << kPdnDisableBit));
}

bool Tmc2209::setIdlePowerDownDelay(uint8_t delay) {
  return uart_.writeRegCheck(kTpowerdownReg, delay);
}

bool Tmc2209::writeRegister(uint8_t reg, uint32_t value, bool verify) {
  return uart_.writeRegister(reg, value, verify);
}

size_t Tmc2209::transfer(const uint8_t *txData, size_t txLength, uint8_t *rxData, size_t rxMaxLength,
                         uint32_t timeoutMs) {
  return uart_.transfer(txData, txLength, rxData, rxMaxLength, timeoutMs);
}
