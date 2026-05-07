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

bool Tmc2209::setStallguardThreshold(uint8_t threshold) {
  return uart_.writeRegCheck(kSgthrsReg, threshold);
}

bool Tmc2209::setCoolStepThreshold(uint32_t threshold) {
  return uart_.writeRegCheck(kTcoolthrsReg, threshold);
}

bool Tmc2209::writeRegister(uint8_t reg, uint32_t value) {
  return uart_.writeRegCheck(reg, value);
}
