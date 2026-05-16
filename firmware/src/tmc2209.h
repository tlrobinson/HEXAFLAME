#pragma once

#include <Arduino.h>

#include "tmc_uart.h"

// TMC2209 register-level driver. Datagram framing, CRC and the single-wire
// echo-skip all live here; the actual bytes move through an ITmcUart so the
// same code serves the RP2040 hardware UART and the Octopus SoftwareSerial.
class Tmc2209 {
public:
  explicit Tmc2209(ITmcUart &uart);

  bool begin();
  void setAddress(uint8_t address) { address_ = address; }

  // Round-trip probe: read a register and confirm a well-formed reply.
  bool test();

  int32_t getStallguardResult();

  bool readRegister(uint8_t reg, uint32_t &value);
  int32_t readRegister(uint8_t reg);
  bool writeRegister(uint8_t reg, uint32_t value, bool verify = true);

  bool setStallguardThreshold(uint8_t threshold);
  bool setCoolStepThreshold(uint32_t threshold = 1600);
  bool setIdlePowerDownDelay(uint8_t delay);

  // Set PDN_DISABLE (and, when microstepping is controlled over UART rather
  // than the MS1/MS2 straps, MSTEP_REG_SELECT) in GCONF.
  bool enableUartMode(bool microstepRegisterSelect);

  // Program the CHOPCONF MRES field so the driver microsteps at the given
  // resolution. `mres` is the raw CHOPCONF code (5 == 1/8 microstep).
  bool setMicrostepResolution(uint8_t mres);

  // Raw datagram passthrough for the tmc.raw diagnostic command.
  size_t transfer(const uint8_t *txData, size_t txLength, uint8_t *rxData, size_t rxMaxLength,
                  uint32_t timeoutMs);

private:
  uint8_t computeCrc8Atm(const uint8_t *datagram, size_t length) const;
  bool readReg(uint8_t reg, uint8_t *outData, size_t outLength);
  bool writeReg(uint8_t reg, uint32_t value);
  bool writeRegCheck(uint8_t reg, uint32_t value);

  ITmcUart &uart_;
  uint8_t address_ = 0;

  static constexpr uint8_t kGconfReg = 0x00;
  static constexpr uint8_t kIfcntReg = 0x02;
  static constexpr uint8_t kTpowerdownReg = 0x11;
  static constexpr uint8_t kTcoolthrsReg = 0x14;
  static constexpr uint8_t kChopconfReg = 0x6C;
  static constexpr uint8_t kSgthrsReg = 0x40;
  static constexpr uint8_t kSgResultReg = 0x41;
  static constexpr uint8_t kPdnDisableBit = 6;
  static constexpr uint8_t kMstepRegSelectBit = 7;
  static constexpr uint32_t kChopconfReset = 0x10000053UL;
  static constexpr uint32_t kReadTimeoutMs = 25;
  static constexpr uint32_t kCommunicationPauseUs = 2200;
};
