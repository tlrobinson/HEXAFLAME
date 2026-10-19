#include "modbus_rtu_slave.h"

#include <string.h>

#include "modbus_crc.h"

namespace hexaflame {

namespace modbus {

uint32_t baudFromCode(uint8_t code) {
  static const uint32_t kRates[] = {4800, 9600, 19200, 38400, 57600, 115200, 128000, 256000};
  if (code > kBaudCodeMax) {
    return kRates[kBaudCode115200];
  }
  return kRates[code];
}

}  // namespace modbus

namespace {

uint16_t readU16(const uint8_t *p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

void writeU16(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value >> 8);
  p[1] = static_cast<uint8_t>(value & 0xFF);
}

}  // namespace

ModbusRtuSlave::ModbusRtuSlave(OutputController &outputs, ByteSink &sink)
    : outputs_(outputs), sink_(sink) {}

void ModbusRtuSlave::setAddress(uint8_t address) {
  if (address == modbus::kBroadcastAddress) {
    return;
  }
  address_ = address;
}

void ModbusRtuSlave::setUartParams(uint8_t parity, uint8_t baudCode) {
  parity_ = parity <= modbus::kParityMax ? parity : modbus::kParityNone;
  baudCode_ = baudCode <= modbus::kBaudCodeMax ? baudCode : modbus::kBaudCode115200;
}

// --------------------------------------------------------------------------
// Framing
// --------------------------------------------------------------------------

void ModbusRtuSlave::feed(const uint8_t *data, size_t length, uint32_t nowMs) {
  for (size_t i = 0; i < length; ++i) {
    if (rxLength_ == kRxBufferSize) {
      // Nothing parsable in a full buffer: slide the window.
      consume(1);
      stats_.framesRejected++;
    }
    rx_[rxLength_++] = data[i];
  }
  if (length > 0) {
    lastRxMs_ = nowMs;
    processBuffer(nowMs);
  }
}

void ModbusRtuSlave::service(uint32_t nowMs) {
  if (rxLength_ == 0 || static_cast<int32_t>(nowMs - lastRxMs_) < static_cast<int32_t>(kFrameTimeoutMs)) {
    return;
  }

  // The line has gone quiet with bytes still buffered: either a request
  // with a function code we cannot size, or a truncated/garbled frame.
  // Treat the whole buffer as one frame if its CRC checks out, otherwise
  // hunt for a valid frame inside it one byte at a time.
  while (rxLength_ > 0) {
    if (rxLength_ >= 4 && crcValid(rx_, rxLength_)) {
      const size_t length = rxLength_;
      handleFrame(rx_, length, nowMs);
      consume(length);
      break;
    }
    consume(1);
    stats_.framesRejected++;
    processBuffer(nowMs);
  }
}

bool ModbusRtuSlave::knownFunction(uint8_t function) {
  switch (function) {
    case modbus::kFnReadCoils:
    case modbus::kFnReadDiscreteInputs:
    case modbus::kFnReadHoldingRegisters:
    case modbus::kFnReadInputRegisters:
    case modbus::kFnWriteSingleCoil:
    case modbus::kFnWriteSingleRegister:
    case modbus::kFnWriteMultipleCoils:
    case modbus::kFnWriteMultipleRegisters:
      return true;
    default:
      return false;
  }
}

size_t ModbusRtuSlave::frameLengthAt(size_t offset) const {
  if (rxLength_ < offset + 2) {
    return 0;
  }
  switch (rx_[offset + 1]) {
    case modbus::kFnReadCoils:
    case modbus::kFnReadDiscreteInputs:
    case modbus::kFnReadHoldingRegisters:
    case modbus::kFnReadInputRegisters:
    case modbus::kFnWriteSingleCoil:
    case modbus::kFnWriteSingleRegister:
      return 8;
    case modbus::kFnWriteMultipleCoils:
    case modbus::kFnWriteMultipleRegisters:
      // addr fn start(2) qty(2) byteCount data... crc(2)
      return rxLength_ < offset + 7 ? 0 : 7u + rx_[offset + 6] + 2u;
    default:
      return 0;  // unknown: resolved by the silence timeout in service()
  }
}

size_t ModbusRtuSlave::findValidFrame(size_t from) const {
  for (size_t offset = from; offset + 4 <= rxLength_; ++offset) {
    const size_t length = frameLengthAt(offset);
    if (length != 0 && offset + length <= rxLength_ && crcValid(rx_ + offset, length)) {
      return offset;
    }
  }
  return 0;
}

void ModbusRtuSlave::processBuffer(uint32_t nowMs) {
  while (rxLength_ > 0) {
    if (rxLength_ >= 2 && !knownFunction(rx_[1])) {
      // Either a request we cannot size (answered after the silence
      // timeout) or noise ahead of a real frame. Prefer the real frame.
      const size_t offset = findValidFrame(1);
      if (offset == 0) {
        return;
      }
      stats_.framesRejected += offset;
      consume(offset);
      continue;
    }
    const size_t expected = frameLengthAt(0);
    if (expected == 0 || rxLength_ < expected) {
      return;
    }
    if (crcValid(rx_, expected)) {
      handleFrame(rx_, expected, nowMs);
      consume(expected);
    } else {
      consume(1);
      stats_.framesRejected++;
    }
  }
}

void ModbusRtuSlave::consume(size_t count) {
  if (count >= rxLength_) {
    rxLength_ = 0;
    return;
  }
  memmove(rx_, rx_ + count, rxLength_ - count);
  rxLength_ -= count;
}

bool ModbusRtuSlave::crcValid(const uint8_t *frame, size_t length) {
  if (length < 4) {
    return false;
  }
  const uint16_t expected = modbusCrc16(frame, length - 2);
  const uint16_t received = static_cast<uint16_t>(frame[length - 2] | (frame[length - 1] << 8));
  return expected == received;
}

// --------------------------------------------------------------------------
// Dispatch
// --------------------------------------------------------------------------

void ModbusRtuSlave::handleFrame(const uint8_t *frame, size_t length, uint32_t nowMs) {
  const uint8_t address = frame[0];
  const uint8_t function = frame[1];
  const bool broadcast = address == modbus::kBroadcastAddress;

  if (!broadcast && address != address_) {
    stats_.framesIgnored++;
    return;
  }
  stats_.framesHandled++;
  stats_.lastFrameMs = nowMs;

  switch (function) {
    case modbus::kFnReadCoils:
      handleReadCoils(frame, length, broadcast);
      break;
    case modbus::kFnReadHoldingRegisters:
      handleReadHoldingRegisters(frame, length, broadcast);
      break;
    case modbus::kFnWriteSingleCoil:
      handleWriteSingleCoil(frame, length, broadcast, nowMs);
      break;
    case modbus::kFnWriteSingleRegister:
      handleWriteSingleRegister(frame, length, broadcast);
      break;
    case modbus::kFnWriteMultipleCoils:
      handleWriteMultipleCoils(frame, length, broadcast);
      break;
    default:
      if (!broadcast) {
        sendException(address, function, modbus::kExceptionIllegalFunction);
      }
      break;
  }
}

void ModbusRtuSlave::handleReadCoils(const uint8_t *frame, size_t length, bool broadcast) {
  if (broadcast) {
    return;  // a read has no meaningful broadcast reply
  }
  if (length != 8) {
    sendException(frame[0], frame[1], modbus::kExceptionIllegalDataValue);
    return;
  }
  const uint16_t start = readU16(frame + 2);
  const uint16_t quantity = readU16(frame + 4);
  if (quantity < 1 || quantity > 2000) {
    sendException(frame[0], frame[1], modbus::kExceptionIllegalDataValue);
    return;
  }
  if (static_cast<uint32_t>(start) + quantity > outputs_.channelCount()) {
    sendException(frame[0], frame[1], modbus::kExceptionIllegalDataAddress);
    return;
  }

  const uint8_t byteCount = static_cast<uint8_t>((quantity + 7) / 8);
  tx_[0] = frame[0];
  tx_[1] = frame[1];
  tx_[2] = byteCount;
  outputs_.readBits(start, quantity, tx_ + 3);
  sendFrame(3u + byteCount);
}

void ModbusRtuSlave::handleReadHoldingRegisters(const uint8_t *frame, size_t length, bool broadcast) {
  if (length != 8) {
    if (!broadcast) {
      sendException(frame[0], frame[1], modbus::kExceptionIllegalDataValue);
    }
    return;
  }
  const uint16_t start = readU16(frame + 2);
  const uint16_t quantity = readU16(frame + 4);

  // Broadcast reads are ignored, except the WaveShare address-discovery
  // quirk: "00 03 40 00 00 01" is answered by whoever hears it.
  if (broadcast && !(start == modbus::kRegAddress && quantity == 1)) {
    return;
  }
  if (quantity < 1 || quantity > 125) {
    sendException(frame[0], frame[1], modbus::kExceptionIllegalDataValue);
    return;
  }

  tx_[0] = frame[0];
  tx_[1] = frame[1];
  tx_[2] = static_cast<uint8_t>(quantity * 2);
  for (uint16_t i = 0; i < quantity; ++i) {
    uint16_t value = 0;
    if (!readHoldingRegister(static_cast<uint16_t>(start + i), value)) {
      sendException(frame[0], frame[1], modbus::kExceptionIllegalDataAddress);
      return;
    }
    writeU16(tx_ + 3 + i * 2, value);
  }
  sendFrame(3u + quantity * 2u);
}

bool ModbusRtuSlave::readHoldingRegister(uint16_t reg, uint16_t &value) {
  if (reg == modbus::kRegUartParams) {
    value = static_cast<uint16_t>((parity_ << 8) | baudCode_);
    return true;
  }
  if (reg == modbus::kRegAddress) {
    value = address_;
    return true;
  }
  if (reg == modbus::kRegSoftwareVersion) {
    value = softwareVersion_;
    return true;
  }
  if (reg >= modbus::kRegDiagnosticsBase && reg < modbus::kRegDiagnosticsBase + modbus::kRegDiagnosticsCount) {
    return listener_ != nullptr && listener_->readDiagnosticRegister(static_cast<uint16_t>(reg - modbus::kRegDiagnosticsBase), value);
  }
  return false;
}

void ModbusRtuSlave::handleWriteSingleCoil(const uint8_t *frame, size_t length, bool broadcast, uint32_t nowMs) {
  if (length != 8) {
    if (!broadcast) {
      sendException(frame[0], frame[1], modbus::kExceptionIllegalDataValue);
    }
    return;
  }
  const uint16_t reg = readU16(frame + 2);
  const uint16_t value = readU16(frame + 4);
  uint8_t exception = 0;

  if (reg == modbus::kCoilAll) {
    if (value == modbus::kValueOn) {
      outputs_.setAll(true);
    } else if (value == modbus::kValueOff) {
      outputs_.setAll(false);
    } else if (value == modbus::kValueToggle) {
      outputs_.toggleAll();
    } else {
      exception = modbus::kExceptionIllegalDataValue;
    }
  } else if (reg < modbus::kCoilToggleBase) {
    if (!outputs_.valid(reg)) {
      exception = modbus::kExceptionIllegalDataAddress;
    } else if (value == modbus::kValueOn) {
      outputs_.set(reg, true);
    } else if (value == modbus::kValueOff) {
      outputs_.set(reg, false);
    } else if (value == modbus::kValueToggle) {
      outputs_.toggle(reg);
    } else {
      exception = modbus::kExceptionIllegalDataValue;
    }
  } else if (reg == modbus::kCoilToggleAll) {
    if (value == modbus::kValueOn) {
      outputs_.toggleAll();
    } else if (value != modbus::kValueOff) {
      exception = modbus::kExceptionIllegalDataValue;
    }
  } else if (reg < modbus::kCoilToggleBase + modbus::kCoilRangeSize) {
    const uint16_t channel = static_cast<uint16_t>(reg - modbus::kCoilToggleBase);
    if (!outputs_.valid(channel)) {
      exception = modbus::kExceptionIllegalDataAddress;
    } else if (value == modbus::kValueOn) {
      outputs_.toggle(channel);
    } else if (value != modbus::kValueOff) {
      exception = modbus::kExceptionIllegalDataValue;
    }
  } else if (reg >= modbus::kCoilFlashOnBase && reg < modbus::kCoilFlashOnBase + modbus::kCoilRangeSize) {
    const uint16_t channel = static_cast<uint16_t>(reg - modbus::kCoilFlashOnBase);
    if (!outputs_.valid(channel)) {
      exception = modbus::kExceptionIllegalDataAddress;
    } else if (value > modbus::kFlashMaxUnits) {
      exception = modbus::kExceptionIllegalDataValue;
    } else {
      outputs_.pulse(channel, true, static_cast<uint32_t>(value) * modbus::kFlashUnitMs, nowMs);
    }
  } else if (reg >= modbus::kCoilFlashOffBase && reg < modbus::kCoilFlashOffBase + modbus::kCoilRangeSize) {
    const uint16_t channel = static_cast<uint16_t>(reg - modbus::kCoilFlashOffBase);
    if (!outputs_.valid(channel)) {
      exception = modbus::kExceptionIllegalDataAddress;
    } else if (value > modbus::kFlashMaxUnits) {
      exception = modbus::kExceptionIllegalDataValue;
    } else {
      outputs_.pulse(channel, false, static_cast<uint32_t>(value) * modbus::kFlashUnitMs, nowMs);
    }
  } else {
    exception = modbus::kExceptionIllegalDataAddress;
  }

  if (exception != 0) {
    if (!broadcast) {
      sendException(frame[0], frame[1], exception);
    }
    return;
  }
  outputs_.flush();
  if (!broadcast) {
    sendEcho(frame, length);
  }
}

void ModbusRtuSlave::handleWriteSingleRegister(const uint8_t *frame, size_t length, bool broadcast) {
  if (length != 8) {
    if (!broadcast) {
      sendException(frame[0], frame[1], modbus::kExceptionIllegalDataValue);
    }
    return;
  }
  const uint16_t reg = readU16(frame + 2);
  const uint16_t value = readU16(frame + 4);
  uint8_t exception = 0;
  bool replyToBroadcast = false;

  if (reg == modbus::kRegUartParams) {
    const uint8_t parity = static_cast<uint8_t>(value >> 8);
    const uint8_t baudCode = static_cast<uint8_t>(value & 0xFF);
    if (parity > modbus::kParityMax || baudCode > modbus::kBaudCodeMax) {
      exception = modbus::kExceptionIllegalDataValue;
    } else {
      setUartParams(parity, baudCode);
      if (listener_ != nullptr) {
        listener_->onUartParamsChanged(parity, baudCode);
      }
    }
  } else if (reg == modbus::kRegAddress) {
    if (value < 1 || value > 0xFF) {
      exception = modbus::kExceptionIllegalDataValue;
    } else {
      setAddress(static_cast<uint8_t>(value));
      if (listener_ != nullptr) {
        listener_->onAddressChanged(address_);
      }
      replyToBroadcast = true;  // WaveShare quirk: "00 06 40 00 00 NN" is acknowledged
    }
  } else {
    exception = modbus::kExceptionIllegalDataAddress;
  }

  if (exception != 0) {
    if (!broadcast) {
      sendException(frame[0], frame[1], exception);
    }
    return;
  }
  if (!broadcast || replyToBroadcast) {
    sendEcho(frame, length);
  }
}

void ModbusRtuSlave::handleWriteMultipleCoils(const uint8_t *frame, size_t length, bool broadcast) {
  // addr fn start(2) qty(2) byteCount data[byteCount] crc(2)
  if (length < 9) {
    if (!broadcast) {
      sendException(frame[0], frame[1], modbus::kExceptionIllegalDataValue);
    }
    return;
  }
  const uint16_t start = readU16(frame + 2);
  const uint16_t quantity = readU16(frame + 4);
  const uint8_t byteCount = frame[6];
  const uint8_t expectedBytes = static_cast<uint8_t>((quantity + 7) / 8);

  if (quantity < 1 || quantity > 1968 || byteCount != expectedBytes || length != 9u + byteCount) {
    if (!broadcast) {
      sendException(frame[0], frame[1], modbus::kExceptionIllegalDataValue);
    }
    return;
  }
  if (static_cast<uint32_t>(start) + quantity > outputs_.channelCount()) {
    if (!broadcast) {
      sendException(frame[0], frame[1], modbus::kExceptionIllegalDataAddress);
    }
    return;
  }

  outputs_.writeBits(start, quantity, frame + 7);
  outputs_.flush();

  if (!broadcast) {
    memcpy(tx_, frame, 6);
    sendFrame(6);
  }
}

// --------------------------------------------------------------------------
// Replies
// --------------------------------------------------------------------------

void ModbusRtuSlave::sendEcho(const uint8_t *frame, size_t length) {
  if (length < 2 || length - 2 > kTxBufferSize - 2) {
    return;
  }
  memcpy(tx_, frame, length - 2);
  sendFrame(length - 2);
}

void ModbusRtuSlave::sendFrame(size_t payloadLength) {
  if (payloadLength + 2 > kTxBufferSize) {
    return;
  }
  const uint16_t crc = modbusCrc16(tx_, payloadLength);
  tx_[payloadLength] = static_cast<uint8_t>(crc & 0xFF);
  tx_[payloadLength + 1] = static_cast<uint8_t>(crc >> 8);
  sink_.write(tx_, payloadLength + 2);
}

void ModbusRtuSlave::sendException(uint8_t address, uint8_t function, uint8_t code) {
  stats_.exceptionsSent++;
  tx_[0] = address;
  tx_[1] = static_cast<uint8_t>(function | 0x80);
  tx_[2] = code;
  sendFrame(3);
}

}  // namespace hexaflame
