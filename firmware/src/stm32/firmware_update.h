#pragma once

#include <stdint.h>

// Serial firmware-upload receiver, triggered by the JSON-RPC
// "firmware.upload" method. Once the RPC result has been sent the
// firmware blocks reading the raw image body from USB serial, verifies
// its CRC32, and self-flashes.
//
// Progress is reported as JSON-RPC "event" notifications (event name
// "firmware-upload", with a "state" field) so the host transport stays
// pure newline-delimited JSON.
namespace FirmwareUpdate {

// Receive `size` bytes of firmware.bin from Serial and, on a CRC match,
// self-flash and reset. Blocks until the image arrives, a 10 s idle
// timeout fires, or an error occurs. Never returns on success.
void receive(uint32_t size, uint32_t expectedCrc32);

}  // namespace FirmwareUpdate
