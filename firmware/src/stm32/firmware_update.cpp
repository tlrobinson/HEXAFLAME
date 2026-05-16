#include "firmware_update.h"

#include <Arduino.h>

#include "self_flash.h"

namespace {

// Emit one firmware-upload progress event as a JSON-RPC notification.
// `extra` is a comma-separated list of pre-formatted JSON members (no
// leading comma) and may be empty.
void emitUploadEvent(const char *state, const String &extra) {
  String line =
      "{\"jsonrpc\":\"2.0\",\"method\":\"event\",\"params\":{\"event\":"
      "\"firmware-upload\",\"data\":{\"state\":\"";
  line += state;
  line += "\"";
  if (extra.length() > 0) {
    line += ",";
    line += extra;
  }
  line += "}}}";
  Serial.println(line);
  Serial.flush();
}

}  // namespace

namespace FirmwareUpdate {

#if defined(STM32F446xx)

// Staging buffer in BSS (kept out of flash). 4-byte aligned because
// SelfFlash programs the image one 32-bit word at a time.
alignas(4) static uint8_t g_uploadBuf[SelfFlash::MAX_IMAGE_BYTES];

// Standard CRC-32 (IEEE 802.3 / zlib), table-free to save code size.
static uint32_t crc32(const uint8_t *data, uint32_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
  }
  return ~crc;
}

void receive(uint32_t size, uint32_t expectedCrc32) {
  if (size == 0 || size > SelfFlash::MAX_IMAGE_BYTES) {
    emitUploadEvent("error", String("\"message\":\"image size out of range\""
                                    ",\"size\":") +
                                 size + ",\"max\":" + SelfFlash::MAX_IMAGE_BYTES);
    return;
  }

  // Tell the host to start streaming the body, then discard anything
  // left in the input buffer so the next byte we read is image data.
  emitUploadEvent("ready", String("\"size\":") + size);
  while (Serial.available() > 0) {
    Serial.read();
  }

  uint32_t received = 0;
  uint32_t lastRxMs = millis();
  while (received < size) {
    const int n = Serial.available();
    if (n > 0) {
      const uint32_t want = size - received;
      const uint32_t take =
          static_cast<uint32_t>(n) < want ? static_cast<uint32_t>(n) : want;
      const size_t got = Serial.readBytes(
          reinterpret_cast<char *>(g_uploadBuf + received), take);
      received += got;
      lastRxMs = millis();
    } else if ((millis() - lastRxMs) > 10000) {
      emitUploadEvent("error", String("\"message\":\"receive timeout\""
                                      ",\"received\":") +
                                   received);
      return;
    }
  }

  // Pad the trailing partial word with 0xFF (the post-erase value) so we
  // never program stale bytes.
  const uint32_t padded = (size + 3u) & ~3u;
  for (uint32_t i = size; i < padded; ++i) {
    g_uploadBuf[i] = 0xFF;
  }

  const uint32_t gotCrc = crc32(g_uploadBuf, size);
  if (gotCrc != expectedCrc32) {
    emitUploadEvent("error", String("\"message\":\"crc mismatch\",\"got\":") +
                                 gotCrc + ",\"expected\":" + expectedCrc32);
    return;
  }

  // Let the host read this line and brace for the USB disconnect that
  // comes with the post-flash reset.
  emitUploadEvent("flashing", String("\"size\":") + size);
  delay(50);

  SelfFlash::writeAndReset(g_uploadBuf, size);  // never returns
}

#else  // self-flash unsupported on this STM32 target

void receive(uint32_t size, uint32_t expectedCrc32) {
  (void)size;
  (void)expectedCrc32;
  emitUploadEvent("error",
                  "\"message\":\"firmware self-flash is not supported on this "
                  "board\"");
}

#endif

}  // namespace FirmwareUpdate
