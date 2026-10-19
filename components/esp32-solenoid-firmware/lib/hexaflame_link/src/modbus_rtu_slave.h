#pragma once

#include <stddef.h>
#include <stdint.h>

#include "link_protocol.h"
#include "output_controller.h"

// Modbus RTU slave, register-compatible with the WaveShare "Modbus RTU
// Relay 32CH" so HEXAFLAME's existing relay driver (and any Modbus tool)
// drives this device unchanged. Only the register map is WaveShare's; the
// channel count comes from the OutputController, so the same class serves
// an 8-, 32- or 64-output device.
//
// Function codes
//   0x01  read coils                 start 0x0000.., any quantity in range
//   0x03  read holding registers     0x2000 UART params, 0x4000 address,
//                                    0x8000 software version,
//                                    0x9000..0x90FF vendor diagnostics
//   0x05  write single coil          see the address map below
//   0x06  write single register      0x2000 UART params, 0x4000 address
//   0x0F  write multiple coils       start 0x0000.., any quantity in range
//
// Write-single-coil address map (value in the data field)
//   0x0000..  one channel            0xFF00 on, 0x0000 off, 0x5500 toggle
//   0x00FF    every channel          0xFF00 on, 0x0000 off, 0x5500 toggle
//   0x0100..  toggle one channel     0xFF00 toggle, 0x0000 no-op
//   0x01FF    toggle every channel   0xFF00 toggle, 0x0000 no-op
//   0x0200..  flash on               on now, off after value * 100 ms
//   0x0400..  flash off              off now, on after value * 100 ms
//
// Address 0x00 is broadcast: writes are executed without a reply, except
// that - as on the WaveShare module - 0x03/0x06 on register 0x4000 do reply
// so a host can discover and set the address of a lone device.
//
// Framing: RTU frames are normally delimited by 3.5 character times of
// silence, which a USB CDC link cannot guarantee. Frames are instead
// parsed by their known length (per function code) with CRC confirmation,
// re-synchronising one byte at a time after a bad CRC. When the head of
// the buffer is not a known function code the parser first looks for a
// complete valid frame further along (noise before a real request) and
// only otherwise waits for a short silence, after which the bytes are
// answered with an "illegal function" exception.

namespace hexaflame {

namespace modbus {

const uint8_t kBroadcastAddress = 0x00;

const uint8_t kFnReadCoils = 0x01;
const uint8_t kFnReadDiscreteInputs = 0x02;
const uint8_t kFnReadHoldingRegisters = 0x03;
const uint8_t kFnReadInputRegisters = 0x04;
const uint8_t kFnWriteSingleCoil = 0x05;
const uint8_t kFnWriteSingleRegister = 0x06;
const uint8_t kFnWriteMultipleCoils = 0x0F;
const uint8_t kFnWriteMultipleRegisters = 0x10;

const uint8_t kExceptionIllegalFunction = 0x01;
const uint8_t kExceptionIllegalDataAddress = 0x02;
const uint8_t kExceptionIllegalDataValue = 0x03;

const uint16_t kCoilAll = 0x00FF;
const uint16_t kCoilToggleBase = 0x0100;
const uint16_t kCoilToggleAll = 0x01FF;
const uint16_t kCoilFlashOnBase = 0x0200;
const uint16_t kCoilFlashOffBase = 0x0400;
const uint16_t kCoilRangeSize = 0x0100;
const uint16_t kFlashMaxUnits = 0x7FFF;
const uint32_t kFlashUnitMs = 100;

const uint16_t kValueOn = 0xFF00;
const uint16_t kValueOff = 0x0000;
const uint16_t kValueToggle = 0x5500;

const uint16_t kRegUartParams = 0x2000;
const uint16_t kRegAddress = 0x4000;
const uint16_t kRegSoftwareVersion = 0x8000;
const uint16_t kRegDiagnosticsBase = 0x9000;
const uint16_t kRegDiagnosticsCount = 0x0100;

const uint8_t kParityNone = 0;
const uint8_t kParityMax = 2;
const uint8_t kBaudCodeMax = 7;
const uint8_t kBaudCode115200 = 5;

// Baud-rate code (register 0x2000 low byte) to bits per second.
uint32_t baudFromCode(uint8_t code);

}  // namespace modbus

class ModbusRtuSlave : public LinkProtocol {
public:
  // Application hooks. Every method has a do-nothing default.
  class Listener {
  public:
    virtual ~Listener() = default;
    // Register 0x4000 written: persist the new address.
    virtual void onAddressChanged(uint8_t address) { (void)address; }
    // Register 0x2000 written: persist (and, on a UART link, apply) the
    // new line parameters.
    virtual void onUartParamsChanged(uint8_t parity, uint8_t baudCode) {
      (void)parity;
      (void)baudCode;
    }
    // Read-only vendor registers 0x9000 + offset. Return false when the
    // offset is not implemented so the slave answers with an exception.
    virtual bool readDiagnosticRegister(uint16_t offset, uint16_t &value) {
      (void)offset;
      (void)value;
      return false;
    }
  };

  ModbusRtuSlave(OutputController &outputs, ByteSink &sink);

  void setListener(Listener *listener) { listener_ = listener; }

  void setAddress(uint8_t address);
  uint8_t address() const { return address_; }

  void setUartParams(uint8_t parity, uint8_t baudCode);
  uint8_t parity() const { return parity_; }
  uint8_t baudCode() const { return baudCode_; }

  // Reported through register 0x8000 as value / 100 (300 = "V3.00").
  void setSoftwareVersion(uint16_t version) { softwareVersion_ = version; }

  // LinkProtocol
  const char *name() const override { return "modbus-rtu"; }
  void feed(const uint8_t *data, size_t length, uint32_t nowMs) override;
  void service(uint32_t nowMs) override;
  const LinkStats &stats() const override { return stats_; }

private:
  // Largest legal request is write-multiple-coils with 246 data bytes.
  static const size_t kRxBufferSize = 300;
  static const size_t kTxBufferSize = 300;
  // Partial frames older than this are abandoned (or, for unknown function
  // codes, answered). Generous because USB CDC delivers in 1 ms bursts.
  static const uint32_t kFrameTimeoutMs = 50;

  // Length of the frame starting at rx_[offset] as implied by its function
  // code, or 0 when the code is unknown or more bytes are needed to tell.
  size_t frameLengthAt(size_t offset) const;
  static bool knownFunction(uint8_t function);
  // Offset of the first complete, CRC-valid frame at or after `from`, or 0.
  size_t findValidFrame(size_t from) const;
  void processBuffer(uint32_t nowMs);
  void consume(size_t count);
  static bool crcValid(const uint8_t *frame, size_t length);

  void handleFrame(const uint8_t *frame, size_t length, uint32_t nowMs);
  void handleReadCoils(const uint8_t *frame, size_t length, bool broadcast);
  void handleReadHoldingRegisters(const uint8_t *frame, size_t length, bool broadcast);
  void handleWriteSingleCoil(const uint8_t *frame, size_t length, bool broadcast, uint32_t nowMs);
  void handleWriteSingleRegister(const uint8_t *frame, size_t length, bool broadcast);
  void handleWriteMultipleCoils(const uint8_t *frame, size_t length, bool broadcast);

  bool readHoldingRegister(uint16_t reg, uint16_t &value);

  void sendEcho(const uint8_t *frame, size_t length);
  void sendFrame(size_t payloadLength);  // tx_[0..payloadLength) + CRC
  void sendException(uint8_t address, uint8_t function, uint8_t code);

  OutputController &outputs_;
  ByteSink &sink_;
  Listener *listener_ = nullptr;

  uint8_t address_ = 1;
  uint8_t parity_ = modbus::kParityNone;
  uint8_t baudCode_ = modbus::kBaudCode115200;
  uint16_t softwareVersion_ = 100;

  uint8_t rx_[kRxBufferSize];
  size_t rxLength_ = 0;
  uint32_t lastRxMs_ = 0;
  uint8_t tx_[kTxBufferSize];

  LinkStats stats_;
};

}  // namespace hexaflame
