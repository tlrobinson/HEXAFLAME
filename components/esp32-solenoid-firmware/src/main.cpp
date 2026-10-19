#include <Arduino.h>
#include <Wire.h>

#include "board_config.h"
#include "mcp23017_bank.h"
#include "modbus_rtu_slave.h"
#include "output_controller.h"
#include "settings.h"
#include "status_led.h"

#if ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE && !HEXAFLAME_LINK_UART
#include "USB.h"
#define HEXAFLAME_USB_CDC_LINK 1
#else
#define HEXAFLAME_USB_CDC_LINK 0
#endif

// HEXAFLAME solenoid firmware - Adafruit ESP32-S3 Feather.
//
// Layers, top to bottom:
//
//   transport      USB CDC `Serial` (default) or hardware `Serial1`
//   LinkProtocol   ModbusRtuSlave (WaveShare 32CH register map)
//   OutputController   channel logic shared by every protocol
//   OutputBank     Mcp23017SolenoidBank (the Adafruit solenoid boards)
//
// Only this file knows about all four. To replace Modbus with a purpose-
// built protocol, implement hexaflame::LinkProtocol and construct it here
// in place of g_modbus; the bank, controller and transport are untouched.

namespace {

// --------------------------------------------------------------------------
// Transport
// --------------------------------------------------------------------------

#if HEXAFLAME_LINK_UART
HardwareSerial &g_link = Serial1;
#else
decltype(Serial) &g_link = Serial;
#endif

class StreamSink : public hexaflame::ByteSink {
public:
  explicit StreamSink(Stream &stream) : stream_(stream) {}
  void write(const uint8_t *data, size_t length) override {
    stream_.write(data, length);
#if !HEXAFLAME_USB_CDC_LINK
    stream_.flush();
#endif
  }

private:
  Stream &stream_;
};

// --------------------------------------------------------------------------
// Debug log (off unless HEXAFLAME_DEBUG_LOG=1; never on the Modbus link)
// --------------------------------------------------------------------------

#if HEXAFLAME_DEBUG_LOG && !HEXAFLAME_LINK_UART
#define LOGF(...) Serial1.printf(__VA_ARGS__)
void beginDebugLog() {
  Serial1.begin(board::kDebugBaud, SERIAL_8N1, board::kUartRxPin, board::kUartTxPin);
}
#else
#define LOGF(...) \
  do {            \
  } while (0)
void beginDebugLog() {}
#endif

// --------------------------------------------------------------------------
// Objects
// --------------------------------------------------------------------------

Mcp23017SolenoidBank g_bank(Wire, board::kSolenoidBaseAddress, board::kSolenoidMaxBoards);
hexaflame::OutputController g_outputs(g_bank);
StreamSink g_sink(g_link);
hexaflame::ModbusRtuSlave g_modbus(g_outputs, g_sink);
hexaflame::LinkProtocol &g_protocol = g_modbus;
StatusLed g_led(board::kNeoPixelPin, board::kNeoPixelPowerPin);
SettingsStore g_settingsStore;
LinkSettings g_settings;

volatile bool g_hostDtr = false;
bool g_hostConnected = false;
bool g_watchdogTripped = false;
uint32_t g_lastHandled = 0;
uint32_t g_lastExceptions = 0;

// Persists link settings and serves the vendor diagnostics registers.
class AppListener : public hexaflame::ModbusRtuSlave::Listener {
public:
  void onAddressChanged(uint8_t address) override {
    g_settings.address = address;
    g_settingsStore.save(g_settings);
    LOGF("address -> %u\n", address);
  }

  void onUartParamsChanged(uint8_t parity, uint8_t baudCode) override {
    g_settings.parity = parity;
    g_settings.baudCode = baudCode;
    g_settingsStore.save(g_settings);
    LOGF("uart -> parity %u baud code %u (applies after reset)\n", parity, baudCode);
  }

  // 0x9000 + offset, read-only.
  //   0  boards online mask          6  host connected
  //   1  boards offline mask         7  frames handled
  //   2  I2C error count             8  frames rejected
  //   3  uptime (s)                  9  exceptions sent
  //   4  firmware version           10  I2C base address
  //   5  channel count           16+n  live output latch of board n
  bool readDiagnosticRegister(uint16_t offset, uint16_t &value) override {
    if (offset >= 16 && offset < 16 + board::kSolenoidMaxBoards) {
      uint8_t latch = 0;
      if (!g_bank.readOutputLatch(static_cast<uint8_t>(offset - 16), latch)) {
        return false;
      }
      value = latch;
      return true;
    }
    switch (offset) {
      case 0: value = g_bank.onlineMask(); return true;
      case 1: value = g_bank.offlineMask(); return true;
      case 2: value = static_cast<uint16_t>(g_bank.errorCount()); return true;
      case 3: value = static_cast<uint16_t>(millis() / 1000); return true;
      case 4: value = board::kFirmwareVersion; return true;
      case 5: value = g_outputs.channelCount(); return true;
      case 6: value = g_hostConnected ? 1 : 0; return true;
      case 7: value = static_cast<uint16_t>(g_protocol.stats().framesHandled); return true;
      case 8: value = static_cast<uint16_t>(g_protocol.stats().framesRejected); return true;
      case 9: value = static_cast<uint16_t>(g_protocol.stats().exceptionsSent); return true;
      case 10: value = g_bank.baseAddress(); return true;
      default: return false;
    }
  }
};

AppListener g_listener;

// --------------------------------------------------------------------------
// USB CDC line state -> host connected / disconnected
// --------------------------------------------------------------------------

#if HEXAFLAME_USB_CDC_LINK
void onUsbCdcEvent(void *, esp_event_base_t, int32_t eventId, void *eventData) {
  if (eventId == ARDUINO_USB_CDC_LINE_STATE_EVENT) {
    auto *data = static_cast<arduino_usb_cdc_event_data_t *>(eventData);
    g_hostDtr = data->line_state.dtr;
  } else if (eventId == ARDUINO_USB_CDC_DISCONNECTED_EVENT) {
    g_hostDtr = false;
  }
}
#endif

void beginLink() {
#if HEXAFLAME_LINK_UART
  uint32_t config = SERIAL_8N1;
  if (g_settings.parity == 1) {
    config = SERIAL_8E1;
  } else if (g_settings.parity == 2) {
    config = SERIAL_8O1;
  }
  Serial1.begin(hexaflame::modbus::baudFromCode(g_settings.baudCode), config, board::kUartRxPin, board::kUartTxPin);
  g_hostDtr = true;  // a UART has no notion of an open port
#else
  // Serial (USB CDC) was started by the core before setup(). Do not block
  // on a host that is not reading, and watch DTR for the fail-safe.
  Serial.setTxTimeoutMs(5);
#if HEXAFLAME_USB_CDC_LINK
  Serial.onEvent(onUsbCdcEvent);
#endif
#endif
}

void pumpLink(uint32_t nowMs) {
  uint8_t buffer[64];
  size_t count = 0;
  while (g_link.available() > 0 && count < sizeof(buffer)) {
    const int byte = g_link.read();
    if (byte < 0) {
      break;
    }
    buffer[count++] = static_cast<uint8_t>(byte);
  }
  if (count > 0) {
    g_protocol.feed(buffer, count, nowMs);
  }
}

void updateStatusLed(uint32_t nowMs) {
  const hexaflame::LinkStats &stats = g_protocol.stats();
  if (stats.exceptionsSent != g_lastExceptions) {
    g_lastExceptions = stats.exceptionsSent;
    g_led.flash(60, 0, 0, nowMs);
  } else if (stats.framesHandled != g_lastHandled) {
    g_lastHandled = stats.framesHandled;
    g_led.flash(40, 40, 40, nowMs);
  }

  if (g_bank.offlineMask() != 0) {
    g_led.setMode(StatusLed::Mode::Fault);
  } else if (g_bank.onlineMask() == 0) {
    g_led.setMode(StatusLed::Mode::NoBoards);
  } else if (!g_hostConnected) {
    g_led.setMode(StatusLed::Mode::HostAway);
  } else {
    g_led.setMode(StatusLed::Mode::Ready);
  }
  g_led.service(nowMs);
}

}  // namespace

void setup() {
  g_led.begin();
  beginDebugLog();
  LOGF("\nHEXAFLAME solenoid firmware v%u.%02u\n", board::kFirmwareVersion / 100, board::kFirmwareVersion % 100);

  // STEMMA QT rail on (the variant already does this; belt and braces).
  pinMode(PIN_I2C_POWER, OUTPUT);
  digitalWrite(PIN_I2C_POWER, HIGH);

  Wire.begin(board::kI2cSda, board::kI2cScl, board::kI2cClockHz);
  Wire.setTimeOut(board::kI2cTimeoutMs);
  g_bank.begin(millis());
  LOGF("solenoid boards online: 0x%02X\n", g_bank.onlineMask());

  g_settings = g_settingsStore.load();
  g_modbus.setAddress(g_settings.address);
  g_modbus.setUartParams(g_settings.parity, g_settings.baudCode);
  g_modbus.setSoftwareVersion(board::kFirmwareVersion);
  g_modbus.setListener(&g_listener);
  LOGF("modbus address %u, %s\n", g_modbus.address(), g_protocol.name());

  beginLink();
}

void loop() {
  const uint32_t nowMs = millis();

  pumpLink(nowMs);
  g_protocol.service(nowMs);
  g_outputs.service(nowMs);
  g_bank.service(nowMs);

  // Host disconnect fail-safe: when the port that was driving us closes,
  // every output goes off. A flame effect must not stay lit because the
  // controlling app crashed.
  const bool connected = g_hostDtr;
  if (connected != g_hostConnected) {
    g_hostConnected = connected;
    LOGF("host %s\n", connected ? "connected" : "disconnected");
    if (!connected && board::kAllOffOnHostDisconnect) {
      g_outputs.allOff();
    }
  }

  // Optional silence watchdog for hosts that poll continuously.
  if (board::kLinkWatchdogMs > 0) {
    const uint32_t lastFrameMs = g_protocol.stats().lastFrameMs;
    const bool silent = g_protocol.stats().framesHandled > 0 && nowMs - lastFrameMs > board::kLinkWatchdogMs;
    if (silent && !g_watchdogTripped && g_outputs.anyOn()) {
      g_watchdogTripped = true;
      LOGF("link watchdog: all off\n");
      g_outputs.allOff();
    } else if (!silent) {
      g_watchdogTripped = false;
    }
  }

  updateStatusLed(nowMs);
}
