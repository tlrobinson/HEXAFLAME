#include "bootloader.h"

#include <Arduino.h>

#if defined(STM32F446xx)
#include <stm32f4xx_hal.h>
#endif

namespace Bootloader {

bool dfuSupported() {
#if defined(STM32F446xx)
  return true;
#else
  return false;
#endif
}

[[noreturn]] void softReset() {
  NVIC_SystemReset();
  for (;;) {
  }
}

#if defined(STM32F446xx)

// STM32F446 built-in system bootloader entry (RM0390). The first word is
// the initial main-stack pointer, the second the reset vector.
static constexpr uint32_t kSystemBootloaderBase = 0x1FFF0000U;

[[noreturn]] void enterDfu() {
  // Give the host a moment to drain the pending serial reply.
  HAL_Delay(50);

  // Hand the ROM bootloader a blank-slate HAL: a peripheral left running
  // can hang its own HAL_Init().
  HAL_RCC_DeInit();
  HAL_DeInit();

  // HAL_DeInit() stops the HAL tick on modern cores but older ones leave
  // the SysTick counter running; kill it explicitly.
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  // Clear any pending/enabled NVIC interrupts so the bootloader starts
  // clean.
  for (uint32_t i = 0; i < 8; ++i) {
    NVIC->ICER[i] = 0xFFFFFFFFU;
    NVIC->ICPR[i] = 0xFFFFFFFFU;
  }

  __disable_irq();

  // Remap system memory to address 0 so the bootloader's vector table is
  // seen from the Cortex-M vector base.
  __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();

  const uint32_t bootSp = *reinterpret_cast<uint32_t *>(kSystemBootloaderBase);
  const uint32_t bootEntry = *reinterpret_cast<uint32_t *>(kSystemBootloaderBase + 4);

  // Set MSP before the call so the bootloader's prologue runs on its own
  // stack rather than clobbering ours.
  __set_MSP(bootSp);
  reinterpret_cast<void (*)()>(bootEntry)();

  for (;;) {
  }
}

#else  // ROM DFU jump not implemented for this STM32 family.

[[noreturn]] void enterDfu() {
  softReset();
}

#endif

}  // namespace Bootloader
