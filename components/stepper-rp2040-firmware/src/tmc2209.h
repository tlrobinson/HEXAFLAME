#pragma once

#include <Arduino.h>
#include <SerialUART.h>

#include "tmc2209_uart.h"

class Tmc2209 {
public:
  Tmc2209(SerialUART &serial, uint8_t rxPin, uint8_t txPin, uint8_t mtrId, uint32_t baudrate);

  bool begin();
  bool test();
  int32_t getStallguardResult();
  bool readRegister(uint8_t reg, uint32_t &value);
  int32_t readRegister(uint8_t reg);
  bool setStallguardThreshold(uint8_t threshold);
  bool setCoolStepThreshold(uint32_t threshold = 1600);
  bool enableUartMode();
  bool setIdlePowerDownDelay(uint8_t delay);
  bool writeRegister(uint8_t reg, uint32_t value, bool verify = true);
  size_t transfer(const uint8_t *txData, size_t txLength, uint8_t *rxData, size_t rxMaxLength, uint32_t timeoutMs);
  void setMotorId(uint8_t mtrId) { uart_.setMotorId(mtrId); }

private:
  Tmc2209Uart uart_;
  static constexpr uint8_t kGconfReg = 0x00;
  static constexpr uint8_t kPdnDisableBit = 6;
  static constexpr uint8_t kTpowerdownReg = 0x11;
  static constexpr uint8_t kTcoolthrsReg = 0x14;
  static constexpr uint8_t kSgthrsReg = 0x40;
  static constexpr uint8_t kSgResultReg = 0x41;
};
