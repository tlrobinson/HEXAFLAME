#include "settings.h"

#include <Preferences.h>

namespace {
const char *kNamespace = "hexalink";
const char *kKeyAddress = "addr";
const char *kKeyParity = "parity";
const char *kKeyBaud = "baud";
}  // namespace

LinkSettings SettingsStore::load() {
  LinkSettings settings;
  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    settings.address = prefs.getUChar(kKeyAddress, settings.address);
    settings.parity = prefs.getUChar(kKeyParity, settings.parity);
    settings.baudCode = prefs.getUChar(kKeyBaud, settings.baudCode);
    prefs.end();
  }
  if (settings.address == 0) {
    settings.address = 1;
  }
  return settings;
}

void SettingsStore::save(const LinkSettings &settings) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }
  prefs.putUChar(kKeyAddress, settings.address);
  prefs.putUChar(kKeyParity, settings.parity);
  prefs.putUChar(kKeyBaud, settings.baudCode);
  prefs.end();
}
