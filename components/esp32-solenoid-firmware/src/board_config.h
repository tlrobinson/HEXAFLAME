#pragma once

#include <Arduino.h>

// Adafruit ESP32-S3 Feather + Adafruit I2C to 8 Channel Solenoid Driver.
//
// Pin names (SDA, SCL, PIN_NEOPIXEL, NEOPIXEL_POWER, PIN_I2C_POWER, TX, RX)
// come from the board's Arduino variant, so the same source builds for the
// other S3 Feathers by changing `board` in platformio.ini.

namespace board {

// I2C (STEMMA QT connector). The Feather's STEMMA QT rail is switched by
// PIN_I2C_POWER; the Arduino variant drives it high before setup() and we
// do so again in case a future variant stops.
const int kI2cSda = SDA;
const int kI2cScl = SCL;
const uint32_t kI2cClockHz = 400000;
const uint16_t kI2cTimeoutMs = 20;

// Solenoid driver boards. Each board is an MCP23017 whose port A drives the
// eight MOSFETs. The address jumpers A0..A2 select 0x20 + n; board n owns
// logical channels 8n .. 8n+7, so a board's channel numbers depend only on
// its jumpers, not on how many boards are present.
const uint8_t kSolenoidBaseAddress = 0x20;
const uint8_t kSolenoidMaxBoards = 8;

// Status NeoPixel.
const int kNeoPixelPin = PIN_NEOPIXEL;
const int kNeoPixelPowerPin = NEOPIXEL_POWER;

// Hardware UART used either as the Modbus link (HEXAFLAME_LINK_UART=1) or
// for the optional debug log.
const int kUartRxPin = RX;
const int kUartTxPin = TX;
const uint32_t kDebugBaud = 115200;

// Behaviour.
#ifndef HEXAFLAME_ALL_OFF_ON_DISCONNECT
#define HEXAFLAME_ALL_OFF_ON_DISCONNECT 1
#endif
#ifndef HEXAFLAME_LINK_WATCHDOG_MS
#define HEXAFLAME_LINK_WATCHDOG_MS 0
#endif
#ifndef HEXAFLAME_LINK_UART
#define HEXAFLAME_LINK_UART 0
#endif
#ifndef HEXAFLAME_DEBUG_LOG
#define HEXAFLAME_DEBUG_LOG 0
#endif
#ifndef HEXAFLAME_FIRMWARE_VERSION
#define HEXAFLAME_FIRMWARE_VERSION 100
#endif

const bool kAllOffOnHostDisconnect = HEXAFLAME_ALL_OFF_ON_DISCONNECT != 0;
const uint32_t kLinkWatchdogMs = HEXAFLAME_LINK_WATCHDOG_MS;
const uint16_t kFirmwareVersion = HEXAFLAME_FIRMWARE_VERSION;  // reported as value / 100

}  // namespace board
