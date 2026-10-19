# esp32-solenoid-firmware

Firmware for an **Adafruit ESP32-S3 Feather** driving one or more
**Adafruit I2C to 8 Channel Solenoid Driver** boards
([product 6318](https://www.adafruit.com/product/6318)) from HEXAFLAME over
USB serial.

The device speaks **Modbus RTU with the WaveShare "Modbus RTU Relay 32CH"
register map**, so HEXAFLAME's existing relay driver
(`components/web-control/src/devices/relay.ts`) and `relay_test.py` work
against it unchanged. The protocol is isolated behind two small interfaces
so it can be lifted into other device firmware, or swapped for a
purpose-built protocol shared across devices, without touching the
hardware code.

## Hardware

| Part | Notes |
|------|-------|
| Adafruit ESP32-S3 Feather (any flash/PSRAM variant) | `board = adafruit_feather_esp32s3` |
| Adafruit I2C to 8 Channel Solenoid Driver | MCP23017 at `0x20` + A0/A1/A2 jumpers; port A → eight AO3406 low-side MOSFETs |
| STEMMA QT cable | Feather `SDA`=GPIO3, `SCL`=GPIO4, rail switched by `PIN_I2C_POWER`=GPIO7 |
| Solenoid supply | 3–24 V DC into the driver board's `V+` terminal; logic `Vcc` from the Feather's 3V |

Up to **eight driver boards** can share the bus. Board *n* (jumpered to
`0x20 + n`) owns logical channels `8n … 8n+7`, so a board's channel
numbers depend only on its jumpers. Boards are discovered at boot and
re-probed while running: a board that is powered up late is picked up, a
board that drops off the bus is flagged and re-initialised straight into
the commanded state when it returns. The device always exposes **64
coils**; coils with no board behind them hold state but drive nothing, so
HEXAFLAME's fixed 32-coil writes never error.

### Status NeoPixel

| Colour | Meaning |
|--------|---------|
| blue | booting |
| magenta | no solenoid boards found on I2C |
| red, blinking | a board that was working stopped answering |
| amber | boards fine, host has not opened the serial port |
| green | boards fine, host connected |
| white flash | request handled |
| red flash | request answered with an exception |

## Building and flashing

```sh
pio run                      # build
pio run -t upload            # flash (auto-detects the port)
./test/host/run.sh           # protocol tests on the host, no hardware needed
./scripts/solenoid_test.py --port /dev/ttyACM0 status     # talk to the board
./scripts/solenoid_test.py --port /dev/ttyACM0 selftest   # scripted hardware check
```

or from `components/`: `pnpm nx run esp32-solenoid-firmware:build` /
`:upload` / `:test-host`.

The firmware uses the Feather's native USB (TinyUSB) and enumerates as
`239a:811b Adafruit Feather ESP32-S3`. Re-flashing works through the
normal 1200-baud touch. If the application is ever wedged, hold **BOOT**
while tapping **RESET**: the chip comes up as `303a:1001 Espressif USB
JTAG/serial debug unit` and `pio run -t upload` flashes it directly.
PlatformIO writes its own second-stage bootloader; the TinyUF2 bootloader
in the `uf2` partition is left in place.

> **Running in a VM?** USB passthrough is usually pinned to a VID:PID. The
> board's identity changes between download mode (`303a:1001`) and the
> running firmware (`239a:811b`), so after the first flash the new device
> may need to be attached to the VM by hand.

## Protocol

Modbus RTU, 115200 8N1 by default, device address 1. All of these are the
WaveShare module's frames; replies echo the request unless noted.

| Function | Address | Data | Effect |
|----------|---------|------|--------|
| `0x01` read coils | `0x0000`… | quantity | reply: coil bits, LSB first |
| `0x05` write coil | `0x0000`–`0x003F` | `FF00` / `0000` / `5500` | channel on / off / toggle |
| `0x05` | `0x00FF` | `FF00` / `0000` / `5500` | all on / off / toggle |
| `0x05` | `0x0100`–`0x013F` | `FF00` | toggle channel (`0000` no-op) |
| `0x05` | `0x01FF` | `FF00` | toggle all |
| `0x05` | `0x0200`–`0x023F` | `n` | flash on: on now, off after `n × 100 ms` |
| `0x05` | `0x0400`–`0x043F` | `n` | flash off: off now, on after `n × 100 ms` |
| `0x0F` write coils | `0x0000`… | quantity, bytes | bulk write (what HEXAFLAME sends) |
| `0x03` read register | `0x2000` | | UART params: parity `<< 8 \| baud code` |
| `0x03` | `0x4000` | | device address (answers broadcast `00` too) |
| `0x03` | `0x8000` | | software version × 100 |
| `0x03` | `0x9000`… | | diagnostics, see below |
| `0x06` write register | `0x2000` | parity, baud code | persisted; applied on the UART link after reset |
| `0x06` | `0x4000` | 1–255 | device address, persisted (accepts broadcast) |

Exceptions follow Modbus: `01` illegal function, `02` illegal address,
`03` illegal value. Broadcast (address `00`) writes execute silently,
except the two `0x4000` quirks above which the WaveShare module also
answers.

### Diagnostics registers (`0x03`, read-only)

| Register | Value |
|----------|-------|
| `0x9000` | boards online, bit *n* = board at `0x20 + n` |
| `0x9001` | boards offline (seen before, failing now) |
| `0x9002` | I2C error count |
| `0x9003` | uptime, seconds |
| `0x9004` | firmware version × 100 |
| `0x9005` | channel count (64) |
| `0x9006` | host connected (DTR) |
| `0x9007` / `0x9008` / `0x9009` | frames handled / bytes rejected / exceptions sent |
| `0x900A` | I2C base address |
| `0x9010 + n` | live output latch of board *n* (what the MOSFET gates see) |

### Fail-safes

- **Host disconnect → all off.** When the host closes the USB port (DTR
  drops: the HEXAFLAME tab closing, the app crashing, the cable pulled)
  every output switches off. A flame effect must not stay lit because the
  controller went away. Disable with `-D HEXAFLAME_ALL_OFF_ON_DISCONNECT=0`.
- **Silence watchdog (off by default).** `-D HEXAFLAME_LINK_WATCHDOG_MS=5000`
  switches everything off when no valid frame has arrived for 5 s. Left off
  because HEXAFLAME only sends state changes; turn it on for a host that
  polls.
- **Boot and recovery.** Outputs are latched low before the expander pins
  are made outputs, both at boot and when a board returns after a fault, so
  a solenoid never sees the chip's power-on state.

## Framing over USB

Modbus RTU normally delimits frames by 3.5 character times of silence,
which USB CDC cannot preserve. The slave instead sizes each frame from its
function code and confirms it with the CRC, re-synchronising a byte at a
time after a bad CRC and looking past noise for a complete valid frame.
Function codes it cannot size are collected until 50 ms of silence and
answered with "illegal function". Split, concatenated and garbage-prefixed
frames are all covered by `test/host/test_modbus.cpp`.

## Layout

```
platformio.ini
lib/hexaflame_link/src/          device-agnostic, no Arduino dependency
  output_bank.h                  OutputBank: N boolean outputs, staged + flush
  output_controller.{h,cpp}      set/toggle/all/pulse timers/bit-packed I/O
  link_protocol.h                LinkProtocol + ByteSink + LinkStats
  modbus_rtu_slave.{h,cpp}       ModbusRtuSlave (WaveShare 32CH map)
  modbus_crc.{h,cpp}
src/
  board_config.h                 pins, I2C addresses, build-flag defaults
  mcp23017_bank.{h,cpp}          OutputBank over the Adafruit solenoid boards
  status_led.{h,cpp}             NeoPixel
  settings.{h,cpp}               address / UART params in NVS
  main.cpp                       transport + wiring; the only file that knows all layers
scripts/solenoid_test.py         host tool: status, selftest, on/off/pulse/mask/walk/raw
test/host/                       protocol tests against the WaveShare wiki vectors
```

### Swapping the protocol

Implement `hexaflame::LinkProtocol` (three methods: `feed`, `service`,
`stats`) on top of `hexaflame::OutputController`, construct it in
`main.cpp` in place of `g_modbus`, and point `g_protocol` at it. The bank,
controller, transport, LED and fail-safes are unchanged. To reuse the same
protocol in another device's firmware, copy or `lib_extra_dirs` the
`lib/hexaflame_link` directory and give it an `OutputBank` for that
device's hardware.

Build flags: `HEXAFLAME_LINK_UART=1` moves the Modbus link to the Feather's
TX/RX header pins (Serial1, with the baud/parity from register `0x2000`);
`HEXAFLAME_DEBUG_LOG=1` prints a human-readable log on those pins instead
(USB link only).
