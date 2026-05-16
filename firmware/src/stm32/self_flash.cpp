#include "self_flash.h"

#if defined(STM32F446xx)

#include <stm32f4xx.h>

// STM32F446ZE flash sector layout (512 KiB part):
//
//   Sector 0 : 0x08000000  16 KiB  \  BTT DFU bootloader
//   Sector 1 : 0x08004000  16 KiB  /
//   Sector 2 : 0x08008000  16 KiB  \
//   Sector 3 : 0x0800C000  16 KiB   |  application region we replace
//   Sector 4 : 0x08010000  64 KiB  /
//   Sector 5 : 0x08020000 128 KiB     (unused)
//   Sector 6 : 0x08040000 128 KiB     (unused)
//   Sector 7 : 0x08060000 128 KiB     (unused)
//
// Erasing sectors 2 + 3 + 4 gives 96 KiB of room. If the firmware ever
// grows past 96 KiB, bump MAX_IMAGE_BYTES in self_flash.h and erase
// sector 5 as well.
static constexpr uint32_t APP_BASE_ADDR = 0x08008000u;
static constexpr uint8_t APP_SECTORS[] = {2, 3, 4};

// Flash unlock magic. Prefixed "SF_" so they do not collide with the
// HAL's FLASH_KEY1 / FLASH_KEY2 macros (same values).
static constexpr uint32_t SF_FLASH_KEY1 = 0x45670123u;
static constexpr uint32_t SF_FLASH_KEY2 = 0xCDEF89ABu;

// Status-register error bits cleared before every operation; a leftover
// would block the next program/erase. FLASH_SR_OPERR is absent from the
// F446 CMSIS header even though its bit (1) exists, so it is a literal.
static constexpr uint32_t SF_SR_SOP_BIT = (1u << 1);
static constexpr uint32_t FLASH_SR_ERR_FLAGS =
    FLASH_SR_EOP | SF_SR_SOP_BIT | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
    FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_RDERR;

__attribute__((always_inline)) static inline void wait_ready() {
  while (FLASH->SR & FLASH_SR_BSY) { /* spin */
  }
}

// The flash-write routine. Constraints:
//   1. Lives in RAM (.RamFunc) - erasing the sector it executes from is
//      an instant hang.
//   2. Calls nothing that lives in flash; everything is inline / at the
//      register level.
//   3. IRQs disabled - an interrupt vector fetch from freshly-erased
//      flash would crash the CPU.
__attribute__((noinline, section(".RamFunc"))) [[noreturn]] static void doFlash(
    const uint32_t *src, uint32_t words) {
  __disable_irq();

  FLASH->KEYR = SF_FLASH_KEY1;
  FLASH->KEYR = SF_FLASH_KEY2;

  // 32-bit parallelism (PSIZE=2): the max width at 3.3 V with no Vpp.
  const uint32_t psize32 = (2u << FLASH_CR_PSIZE_Pos);

  const uint8_t sectors[3] = {APP_SECTORS[0], APP_SECTORS[1], APP_SECTORS[2]};
  for (int i = 0; i < 3; ++i) {
    wait_ready();
    FLASH->SR = FLASH_SR_ERR_FLAGS;
    FLASH->CR = FLASH_CR_SER | psize32 |
                (static_cast<uint32_t>(sectors[i]) << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    wait_ready();
  }

  volatile uint32_t *dst = reinterpret_cast<volatile uint32_t *>(APP_BASE_ADDR);
  for (uint32_t i = 0; i < words; ++i) {
    wait_ready();
    FLASH->SR = FLASH_SR_ERR_FLAGS;
    FLASH->CR = FLASH_CR_PG | psize32;
    dst[i] = src[i];
    __DSB();
    wait_ready();
  }

  FLASH->CR = FLASH_CR_LOCK;

  // SYSRESETREQ; the VECTKEY (0x5FA) must be in the upper 16 bits.
  SCB->AIRCR = (0x5FAu << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
  __DSB();
  for (;;) { /* wait for reset */
  }
}

namespace SelfFlash {

[[noreturn]] void writeAndReset(const uint8_t *image, size_t size) {
  // Round up to whole 32-bit words. Bytes past `size` in the buffer must
  // already be padded (the caller pads with 0xFF).
  const uint32_t words = static_cast<uint32_t>((size + 3) / 4);
  const uint32_t *src = reinterpret_cast<const uint32_t *>(image);
  doFlash(src, words);
}

}  // namespace SelfFlash

#else  // self-flash unsupported on this STM32 target

namespace SelfFlash {

[[noreturn]] void writeAndReset(const uint8_t *, size_t) {
  // Unreachable: callers gate on SelfFlash::kSupported first.
  for (;;) {
  }
}

}  // namespace SelfFlash

#endif
