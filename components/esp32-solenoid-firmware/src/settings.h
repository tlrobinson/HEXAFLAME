#pragma once

#include <Arduino.h>

// Link parameters a host can change over Modbus (registers 0x4000 and
// 0x2000), persisted in NVS so they survive a reboot like the WaveShare
// module's do.
struct LinkSettings {
  uint8_t address = 1;
  uint8_t parity = 0;    // 0 none, 1 even, 2 odd (WaveShare encoding)
  uint8_t baudCode = 5;  // 5 = 115200, see hexaflame::modbus::baudFromCode
};

class SettingsStore {
public:
  LinkSettings load();
  void save(const LinkSettings &settings);
};
