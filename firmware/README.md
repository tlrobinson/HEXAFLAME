# firmware-4

Portable HEXAFLAME stepper firmware. A single source tree builds for two very
different controllers and keeps the same JSON-RPC control protocol on both:

| Build env             | Board                         | MCU            | Channels |
|-----------------------|-------------------------------|----------------|----------|
| `rp2040`              | HEXAFLAME valve controller V3 | RP2040         | 1        |
| `octopus_pro_h723`    | BIGTREETECH Octopus Pro V1.1  | STM32H723ZET6  | 8        |
| `octopus_pro_f446`    | Octopus Pro / Octopus V1.1    | STM32F446ZET6  | 8        |

## Provenance

`firmware-4` is a merge of two earlier prototypes, both left untouched:

- **`components/firmware-1`** supplies the architecture and all motion code:
  one genuinely portable `Stepper` (sensorless homing, calibrated moves,
  motion ramps, ADSR envelopes, speed sweeps, StallGuard characterization)
  shared verbatim between both boards, behind two tiny hardware interfaces.
  It was judged the stronger base.
- **`firmware-2`** contributed its BIGTREETECH Octopus integration extras,
  which are otherwise orthogonal to motion control: software entry into the
  ROM DFU bootloader, in-place firmware self-flash, a serial firmware-upload
  receiver, and a retry-on-`IFCNT` wrapper for flaky TMC2209 register writes.

firmware-2's CAN-bus transport was intentionally left out: it relies on a
fixed 8-byte binary frame, which cannot carry this firmware's variable-length
JSON-RPC protocol. The control transport here is JSON-RPC over USB serial only.

## Building

```sh
pio run -e rp2040            # RP2040, single channel
pio run -e octopus_pro_h723  # Octopus Pro V1.1 (default env), eight channels
pio run -e octopus_pro_f446  # Octopus Pro V1.1 with the STM32F446 variant
```

## Architecture

Hardware differences are isolated behind two small interfaces so the motion
logic, the TMC2209 register layer and the JSON-RPC protocol are 100% shared:

- `StepEngine` - step-pulse generation.
  - `PioStepEngine` (RP2040): three PIO state machines, exactly as the
    original firmware.
  - `TimerStepEngine` (STM32): one hardware timer per channel whose update
    ISR toggles the STEP pin. Eight timers means eight channels step
    simultaneously and independently of the CPU.
- `ITmcUart` - byte transport to the TMC2209.
  - `HardwareTmcUart` (RP2040): a hardware UART peripheral.
  - `SoftwareTmcUart` (STM32): a per-channel single-wire SoftwareSerial line.

`Board` (one of `src/rp2040/board_rp2040.cpp` or `src/stm32/board_octopus.cpp`,
selected by `build_src_filter`) publishes a `ChannelConfig` per channel, wiring
each `Stepper` to its engine, driver and pins. `Stepper` carries no static
state, so the Octopus instantiates eight fully independent copies.

```
src/
  board_config.h, step_engine.h, tmc_uart.h, compat.h   interfaces
  tmc2209.{h,cpp}        TMC2209 register/datagram layer (shared)
  stepper.{h,cpp}        per-channel motion state machine (shared)
  status_led.{h,cpp}     NeoPixel (RP2040) / status LED (STM32)
  main.cpp               JSON-RPC dispatch + per-channel service loop (shared)
  rp2040/                PIO step engine, hardware-UART transport, board map
  stm32/                 timer step engine, SoftwareSerial transport, board map,
                         DFU bootloader entry, flash self-update, upload receiver
```

## Control protocol

Newline-delimited JSON-RPC 2.0 over USB serial. Every request may carry a
`channel` field (0-based, defaults to 0) and every response, event and log
line reports its `channel`.

```jsonc
{"jsonrpc":"2.0","id":1,"method":"home","params":{"channel":3,"hz":1200}}
{"jsonrpc":"2.0","id":2,"method":"move-percent","params":{"channel":3,"percent":50}}
{"jsonrpc":"2.0","id":3,"method":"board"}        // -> board name + channel_count
```

Send `{"method":"help"}` for the full method list. Timed/absolute moves and
ADSR envelopes are non-blocking and are serviced for every channel each loop
iteration, so all eight channels can be in motion at once. Homing, jog,
speed-test and characterization run to completion on their channel before the
next request is processed.

## Firmware update (Octopus)

Two methods, ported from firmware-2, let an Octopus re-flash itself without a
BOOT0 jumper or an SD card:

- `{"method":"dfu"}` - de-inits the HAL and jumps into the MCU's ROM DFU
  bootloader so the host can re-flash over USB.
- `{"method":"firmware.upload","params":{"size":<bytes>,"crc":<crc32>}}` -
  the firmware acknowledges, halts all channels, then blocks reading `size`
  raw bytes of `firmware.bin` from the serial line, verifies the CRC-32, and
  programs the image into the application flash region before resetting.
  Progress arrives as `event` notifications with event name `firmware-upload`
  and a `state` of `ready` / `flashing` / `error`. The host should stream the
  image body immediately after seeing the `ready` event.

Both features are implemented for the **STM32F446** Octopus variant only. On
the H723 build `dfu` falls back to a plain reset and `firmware.upload` returns
an error; flash the H723 externally instead. Neither method exists on the
RP2040 build, which keeps its UF2 `bootsel` reboot.

## Notes / caveats

- The Octopus Pro V1.1 (H723) clocks HSE from a 25 MHz crystal; `HSE_VALUE` is
  set accordingly in `platformio.ini`. Adjust if your board differs.
- The Octopus status LED is wired to PA13, which doubles as SWDIO; using it as
  an output forfeits SWD debugging.
- TMC2209 microstepping is set over UART on the Octopus (the MS1/MS2 straps are
  not exposed) and via the MS1/MS2 pins on the RP2040; both default to 1/8.
- The self-flash routine erases application flash sectors 2-4 (96 KiB) on the
  F446; if the image ever grows past 96 KiB, bump `MAX_IMAGE_BYTES` in
  `src/stm32/self_flash.h` and erase another sector.
