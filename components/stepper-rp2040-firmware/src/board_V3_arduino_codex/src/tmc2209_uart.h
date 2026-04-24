#pragma once

#include <Arduino.h>
#include <SerialUART.h>

class Tmc2209Uart {
public:
  Tmc2209Uart(SerialUART &serial, uint8_t rxPin, uint8_t txPin, uint8_t mtrId, uint32_t baudrate);

  bool begin();
  bool test();
  int32_t readInt(uint8_t reg);
  bool writeRegCheck(uint8_t reg, uint32_t value);
  void setMotorId(uint8_t mtrId) { mtrId_ = mtrId; }

private:
  uint8_t computeCrc8Atm(const uint8_t *datagram, size_t length) const;
  bool readReg(uint8_t reg, uint8_t *outData, size_t outLength);
  bool writeReg(uint8_t reg, uint32_t value);

  SerialUART &serial_;
  uint8_t rxPin_;
  uint8_t txPin_;
  uint8_t mtrId_;
  uint32_t baudrate_;
  uint32_t communicationPauseUs_;
  static constexpr uint8_t kIfcntReg = 0x02;
};
