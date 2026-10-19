#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hexaflame {

// CRC-16/MODBUS (poly 0x8005 reflected, init 0xFFFF). Transmitted low byte
// first on the wire.
uint16_t modbusCrc16(const uint8_t *data, size_t length);

}  // namespace hexaflame
