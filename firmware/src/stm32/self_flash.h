#pragma once

#include <stddef.h>
#include <stdint.h>

// In-place firmware self-update. Given a CRC-validated image buffer,
// erase the application flash region, program the new image, and reset
// so the BTT bootloader hands off to the fresh app on the next boot.
//
// Ported from the firmware-2 BTT Octopus build. The flash routine is
// implemented for the STM32F446 (the F446 Octopus variant) only;
// kSupported is false on other targets.
namespace SelfFlash {

#if defined(STM32F446xx)
constexpr bool kSupported = true;
#else
constexpr bool kSupported = false;
#endif

// Largest image accepted: application flash sectors 2+3+4 add up to
// 16+16+64 = 96 KiB.
constexpr uint32_t MAX_IMAGE_BYTES = 96u * 1024u;

// Erase the application region, program `size` bytes from `image`, and
// reset. The buffer is consumed in 32-bit words, so it must be 4-byte
// aligned and padded to a word boundary. Never returns.
[[noreturn]] void writeAndReset(const uint8_t *image, size_t size);

}  // namespace SelfFlash
