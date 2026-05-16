#pragma once

// Software entry into the STM32 system (DFU) bootloader, plus a plain
// system reset. Ported from the firmware-2 BTT Octopus build.
//
// The DFU jump is implemented for the STM32F4 family only (the F446
// Octopus variant). On other STM32 targets dfuSupported() is false and
// enterDfu() degrades to a plain reset.
namespace Bootloader {

// True when enterDfu() can actually reach the ROM DFU bootloader.
bool dfuSupported();

// Reset the MCU. Never returns.
[[noreturn]] void softReset();

// De-init every peripheral and jump into the ROM DFU bootloader so the
// device can be re-flashed over USB without a BOOT0 jumper. Never
// returns. Falls back to softReset() where dfuSupported() is false.
[[noreturn]] void enterDfu();

}  // namespace Bootloader
