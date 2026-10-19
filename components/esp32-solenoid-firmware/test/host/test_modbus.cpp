// Host-side checks for lib/hexaflame_link. Builds with any C++11 compiler;
// no Arduino. Request/response vectors (including CRCs) are the ones
// printed in the WaveShare "Modbus RTU Relay 32CH" wiki, so passing here
// means a host that drives the WaveShare module drives this firmware too.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "modbus_crc.h"
#include "modbus_rtu_slave.h"
#include "output_controller.h"

using namespace hexaflame;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_failures;                                                              \
    }                                                                            \
  } while (0)

class FakeBank : public OutputBank {
public:
  explicit FakeBank(uint16_t count) : count_(count) {}
  uint16_t channelCount() const override { return count_; }
  bool get(uint16_t channel) const override { return channel < count_ && staged_[channel]; }
  void set(uint16_t channel, bool on) override {
    if (channel < count_) staged_[channel] = on;
  }
  void flush() override {
    std::memcpy(hardware_, staged_, sizeof(hardware_));
    ++flushes;
  }
  bool hw(uint16_t channel) const { return hardware_[channel]; }
  int flushes = 0;

private:
  uint16_t count_;
  bool staged_[64] = {};
  bool hardware_[64] = {};
};

class CaptureSink : public ByteSink {
public:
  void write(const uint8_t *data, size_t length) override { bytes.insert(bytes.end(), data, data + length); }
  std::vector<uint8_t> take() {
    std::vector<uint8_t> out = bytes;
    bytes.clear();
    return out;
  }
  std::vector<uint8_t> bytes;
};

class TestListener : public ModbusRtuSlave::Listener {
public:
  void onAddressChanged(uint8_t address) override { lastAddress = address; }
  void onUartParamsChanged(uint8_t parity, uint8_t baudCode) override {
    lastParity = parity;
    lastBaud = baudCode;
  }
  bool readDiagnosticRegister(uint16_t offset, uint16_t &value) override {
    if (offset == 0) {
      value = 0xBEEF;
      return true;
    }
    return false;
  }
  int lastAddress = -1;
  int lastParity = -1;
  int lastBaud = -1;
};

std::vector<uint8_t> hex(const char *text) {
  std::vector<uint8_t> out;
  unsigned value = 0;
  int digits = 0;
  for (const char *p = text;; ++p) {
    const char c = *p;
    int nibble = -1;
    if (c >= '0' && c <= '9') nibble = c - '0';
    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
    if (nibble >= 0) {
      value = (value << 4) | static_cast<unsigned>(nibble);
      if (++digits == 2) {
        out.push_back(static_cast<uint8_t>(value));
        value = 0;
        digits = 0;
      }
    }
    if (c == '\0') break;
  }
  return out;
}

std::string dump(const std::vector<uint8_t> &bytes) {
  std::string out;
  char buf[4];
  for (uint8_t b : bytes) {
    std::snprintf(buf, sizeof(buf), "%02X ", b);
    out += buf;
  }
  return out;
}

std::vector<uint8_t> withCrc(const char *text) {
  std::vector<uint8_t> frame = hex(text);
  const uint16_t crc = modbusCrc16(frame.data(), frame.size());
  frame.push_back(static_cast<uint8_t>(crc & 0xFF));
  frame.push_back(static_cast<uint8_t>(crc >> 8));
  return frame;
}

void expectReply(const char *label, const std::vector<uint8_t> &got, const char *wantHex) {
  const std::vector<uint8_t> want = hex(wantHex);
  if (got != want) {
    std::printf("FAIL %s\n  want: %s\n  got:  %s\n", label, dump(want).c_str(), dump(got).c_str());
    ++g_failures;
  }
}

// Reply expected = payload + computed CRC (for frames the wiki does not list).
void expectReplyCrc(const char *label, const std::vector<uint8_t> &got, const char *payloadHex) {
  const std::vector<uint8_t> want = withCrc(payloadHex);
  if (got != want) {
    std::printf("FAIL %s\n  want: %s\n  got:  %s\n", label, dump(want).c_str(), dump(got).c_str());
    ++g_failures;
  }
}

struct Fixture {
  FakeBank bank;
  OutputController outputs;
  CaptureSink sink;
  ModbusRtuSlave slave;
  TestListener listener;
  uint32_t now = 1000;

  explicit Fixture(uint16_t channels = 64) : bank(channels), outputs(bank), slave(outputs, sink) {
    slave.setListener(&listener);
  }

  // Feed a complete frame (CRC included, as printed in the wiki).
  std::vector<uint8_t> send(const char *frameHex) {
    const std::vector<uint8_t> frame = hex(frameHex);
    slave.feed(frame.data(), frame.size(), now);
    slave.service(now);
    return sink.take();
  }

  // Feed a payload and let the test compute its CRC.
  std::vector<uint8_t> sendCrc(const char *payloadHex) {
    const std::vector<uint8_t> frame = withCrc(payloadHex);
    slave.feed(frame.data(), frame.size(), now);
    slave.service(now);
    return sink.take();
  }

  // Let the inter-frame silence timeout elapse.
  std::vector<uint8_t> quiet() {
    now += 60;
    slave.service(now);
    return sink.take();
  }
};

void testCrc() {
  const std::vector<uint8_t> frame = hex("01 05 00 00 FF 00");
  CHECK(modbusCrc16(frame.data(), frame.size()) == 0x3A8C);  // wire order 8C 3A
}

void testWaveshareVectors() {
  Fixture f;

  // Single relay on / off / toggle echo the request.
  expectReply("relay0 on", f.send("01 05 00 00 FF 00 8C 3A"), "01 05 00 00 FF 00 8C 3A");
  CHECK(f.bank.hw(0));
  expectReply("relay1 on", f.send("01 05 00 01 FF 00 DD FA"), "01 05 00 01 FF 00 DD FA");
  expectReply("relay0 off", f.send("01 05 00 00 00 00 CD CA"), "01 05 00 00 00 00 CD CA");
  CHECK(!f.bank.hw(0));
  expectReply("relay2 toggle", f.send("01 05 00 02 55 00 53 5A"), "01 05 00 02 55 00 53 5A");
  CHECK(f.bank.hw(2));
  expectReply("relay3 on", f.send("01 05 00 03 FF 00 7C 3A"), "01 05 00 03 FF 00 7C 3A");

  // Read relay status: relays 1..3 (0-based 1,2,3) all on -> 0x07; wiki
  // example shows 0x05 for "1 and 3 on", so turn relay 2 off first.
  f.send("01 05 00 02 00 00 6C 0A");
  expectReply("read 1..3", f.send("01 01 00 01 00 03 2D CB"), "01 01 01 05 91 8B");

  // All off, then read all 32.
  expectReply("all off", f.send("01 05 00 FF 00 00 FD FA"), "01 05 00 FF 00 00 FD FA");
  expectReply("read 32", f.send("01 01 00 00 00 20 3D D2"), "01 01 04 00 00 00 00 FB D1");

  // All on, all toggle.
  expectReply("all on", f.send("01 05 00 FF FF 00 BC 0A"), "01 05 00 FF FF 00 BC 0A");
  CHECK(f.bank.hw(31) && f.bank.hw(63));
  expectReply("all toggle", f.send("01 05 00 FF 55 00 C2 AA"), "01 05 00 FF 55 00 C2 AA");
  CHECK(!f.outputs.anyOn());

  // Write multiple coils (relays 1-2 on, 3-32 off) and the wiki's reply.
  expectReply("write multi", f.send("01 0F 00 00 00 20 04 03 00 00 00 C4 CC"), "01 0F 00 00 00 20 54 13");
  CHECK(f.bank.hw(0) && f.bank.hw(1) && !f.bank.hw(2));
  expectReply("write multi all on", f.send("01 0F 00 00 00 20 04 FF FF FF FF C5 1C"), "01 0F 00 00 00 20 54 13");
  CHECK(f.bank.hw(31) && !f.bank.hw(32));
  expectReplyCrc("read 4..15", f.send("01 01 00 04 00 0C 7D CE"), "01 01 02 FF 0F");
  expectReply("write multi all off", f.send("01 0F 00 00 00 20 04 00 00 00 00 C4 88"), "01 0F 00 00 00 20 54 13");
  CHECK(!f.outputs.anyOn());

  // Software version: 0x012C -> V3.00.
  f.slave.setSoftwareVersion(300);
  expectReply("version", f.send("01 03 80 00 00 01 AD CA"), "01 03 02 01 2C B8 09");

  // Set address by broadcast, then discover it by broadcast read.
  expectReply("set addr 2", f.send("00 06 40 00 00 02 1C 1A"), "00 06 40 00 00 02 1C 1A");
  CHECK(f.listener.lastAddress == 2);
  CHECK(f.slave.address() == 2);
  expectReply("read addr", f.send("00 03 40 00 00 01 90 1B"), "00 03 02 00 02 04 45");
  // Old address is now ignored, new one answers.
  CHECK(f.send("01 05 00 00 FF 00 8C 3A").empty());
  expectReplyCrc("relay0 on @2", f.sendCrc("02 05 00 00 FF 00"), "02 05 00 00 FF 00");
  f.sendCrc("02 06 40 00 00 01");  // back to 1 (addressed)
  CHECK(f.slave.address() == 1);

  // UART params: 115200 none.
  expectReply("baud 115200", f.send("01 06 20 00 00 05 42 09"), "01 06 20 00 00 05 42 09");
  CHECK(f.listener.lastBaud == 5 && f.listener.lastParity == 0);
  expectReplyCrc("read uart", f.sendCrc("01 03 20 00 00 01"), "01 03 02 00 05");

  // Exception format from the wiki: 01 85 03 02 91 (illegal data value).
  expectReply("bad coil value", f.sendCrc("01 05 00 00 12 34"), "01 85 03 02 91");
  // Illegal data address (coil 0x0500 is outside every map range).
  expectReplyCrc("bad coil addr", f.sendCrc("01 05 05 00 FF 00"), "01 85 02");
}

void testFlashTimers() {
  Fixture f;
  // Flash on relay 0 for 700 ms.
  expectReply("flash on", f.send("01 05 02 00 00 07 8D B0"), "01 05 02 00 00 07 8D B0");
  CHECK(f.bank.hw(0));
  f.now += 699;
  f.outputs.service(f.now);
  CHECK(f.bank.hw(0));
  f.now += 1;
  f.outputs.service(f.now);
  CHECK(!f.bank.hw(0));

  // Flash off relay 1 for 600 ms: off now, on later.
  f.send("01 05 00 01 FF 00 DD FA");
  CHECK(f.bank.hw(1));
  expectReply("flash off", f.send("01 05 04 01 00 06 1D 38"), "01 05 04 01 00 06 1D 38");
  CHECK(!f.bank.hw(1));
  f.now += 600;
  f.outputs.service(f.now);
  CHECK(f.bank.hw(1));

  // An explicit write cancels a pending pulse.
  f.send("01 05 02 00 00 07 8D B0");
  f.send("01 05 00 00 FF 00 8C 3A");
  f.now += 1000;
  f.outputs.service(f.now);
  CHECK(f.bank.hw(0));

  // Per-relay toggle register.
  f.sendCrc("01 05 01 00 FF 00");
  CHECK(!f.bank.hw(0));
  f.sendCrc("01 05 01 00 FF 00");
  CHECK(f.bank.hw(0));
  f.sendCrc("01 05 01 00 00 00");  // 0x0000 = no-op
  CHECK(f.bank.hw(0));
}

void testFramingRobustness() {
  Fixture f;
  // Frame split across arbitrary chunks.
  const std::vector<uint8_t> frame = withCrc("01 05 00 04 FF 00");
  f.slave.feed(frame.data(), 3, f.now);
  f.slave.feed(frame.data() + 3, 2, f.now + 1);
  f.slave.feed(frame.data() + 5, 3, f.now + 2);
  f.slave.service(f.now + 2);
  expectReplyCrc("split frame", f.sink.take(), "01 05 00 04 FF 00");
  CHECK(f.bank.hw(4));

  // Leading garbage that looks like an unknown function code: the parser
  // finds the complete frame behind it right away.
  std::vector<uint8_t> garbage = hex("FF 13 37");
  const std::vector<uint8_t> off4 = withCrc("01 05 00 04 00 00");
  garbage.insert(garbage.end(), off4.begin(), off4.end());
  f.slave.feed(garbage.data(), garbage.size(), f.now);
  f.slave.service(f.now);
  expectReplyCrc("garbage prefix", f.sink.take(), "01 05 00 04 00 00");
  CHECK(!f.bank.hw(4));
  CHECK(f.slave.stats().framesRejected == 3);

  // Unknown function code with nothing valid behind it stays buffered
  // until the silence timeout (checked below), so an unsized request
  // cannot be mistaken for noise.

  // Garbage with a known function code and a bad CRC resyncs immediately.
  std::vector<uint8_t> bad = hex("01 05 00 00");
  const std::vector<uint8_t> on4 = withCrc("01 05 00 04 FF 00");
  bad.insert(bad.end(), on4.begin(), on4.end());
  f.slave.feed(bad.data(), bad.size(), f.now);
  f.slave.service(f.now);
  expectReplyCrc("bad crc resync", f.sink.take(), "01 05 00 04 FF 00");
  CHECK(f.bank.hw(4));

  // Two frames in one chunk.
  std::vector<uint8_t> two = withCrc("01 05 00 00 FF 00");
  const std::vector<uint8_t> second = withCrc("01 05 00 01 FF 00");
  two.insert(two.end(), second.begin(), second.end());
  f.slave.feed(two.data(), two.size(), f.now);
  CHECK(f.sink.take() == two);

  // Frame for another slave is ignored silently.
  CHECK(f.sendCrc("05 05 00 00 FF 00").empty());
  CHECK(f.slave.stats().framesIgnored == 1);

  // Unknown function code: no length known, answered after the silence
  // timeout with "illegal function".
  const std::vector<uint8_t> unknown = withCrc("01 2B 0E 01 00");
  f.slave.feed(unknown.data(), unknown.size(), f.now);
  f.slave.service(f.now + 10);
  CHECK(f.sink.take().empty());
  expectReplyCrc("illegal function", f.quiet(), "01 AB 01");

  // Truncated frame is discarded after the timeout, next frame is fine.
  f.slave.feed(frame.data(), 5, f.now);
  CHECK(f.quiet().empty());
  f.now += 100;
  expectReplyCrc("after truncated", f.sendCrc("01 05 00 04 FF 00"), "01 05 00 04 FF 00");
}

void testRangeChecks() {
  Fixture f(8);  // single solenoid board: 8 channels
  // Coil 8 does not exist.
  expectReplyCrc("coil 8", f.sendCrc("01 05 00 08 FF 00"), "01 85 02");
  // Writing 32 coils to an 8-channel device is an address error...
  expectReplyCrc("32 coils on 8", f.send("01 0F 00 00 00 20 04 00 00 00 00 C4 88"), "01 8F 02");
  // ...which is why the ESP32 build exposes 64 logical channels.
  Fixture g(64);
  expectReply("32 coils on 64", g.send("01 0F 00 00 00 20 04 00 00 00 00 C4 88"), "01 0F 00 00 00 20 54 13");
  // Diagnostics register via the listener; unimplemented offset is an error.
  expectReplyCrc("diag 0", g.sendCrc("01 03 90 00 00 01"), "01 03 02 BE EF");
  expectReplyCrc("diag 1", g.sendCrc("01 03 90 01 00 01"), "01 83 02");
}

}  // namespace

int main() {
  testCrc();
  testWaveshareVectors();
  testFlashTimers();
  testFramingRobustness();
  testRangeChecks();
  if (g_failures == 0) {
    std::printf("hexaflame_link: all host tests passed\n");
    return 0;
  }
  std::printf("hexaflame_link: %d failure(s)\n", g_failures);
  return 1;
}
