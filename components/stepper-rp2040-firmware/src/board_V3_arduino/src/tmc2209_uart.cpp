#include "tmc2209_uart.h"

TMC_UART::TMC_UART(HardwareSerial& serial, uint32_t baudrate, uint8_t rx_pin, uint8_t tx_pin, uint8_t mtr_id)
    : _ser(serial), _mtr_id(mtr_id) {
    (void)rx_pin; (void)tx_pin;
    // The caller is responsible for configuring pins and calling begin() before
    // the first exchange. Same pacing used in the Python port: gives the
    // half-duplex line time to turn around between write and read.
    _communication_pause_us = (uint32_t)(500.0f * 1e6f / baudrate);
}

uint8_t TMC_UART::crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int j = 0; j < 8; j++) {
            if (((crc >> 7) ^ (byte & 0x01)) & 1) {
                crc = ((crc << 1) ^ 0x07) & 0xFF;
            } else {
                crc = (crc << 1) & 0xFF;
            }
            byte >>= 1;
        }
    }
    return crc;
}

int TMC_UART::readReg(uint8_t reg, uint8_t out[4]) {
    uint8_t frame[4] = { 0x55, _mtr_id, reg, 0 };
    frame[3] = crc8(frame, 3);

    // Flush any stale bytes.
    while (_ser.available()) _ser.read();

    if (_ser.write(frame, 4) != 4) return 0;
    _ser.flush();
    delayMicroseconds(_communication_pause_us);

    // Expect 12 bytes: 4-byte echo of our request + 8-byte reply.
    // Give up to ~10 ms for the driver to reply.
    uint8_t buf[16];
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < 12 && (millis() - t0) < 10) {
        if (_ser.available()) {
            int b = _ser.read();
            if (b < 0) continue;
            buf[got++] = (uint8_t)b;
        }
    }
    delayMicroseconds(_communication_pause_us);

    if (got < 12) return 0;

    // Reply payload is at bytes [7..10] (same slice as the Python port).
    out[0] = buf[7];
    out[1] = buf[8];
    out[2] = buf[9];
    out[3] = buf[10];
    return 4;
}

int32_t TMC_UART::readInt(uint8_t reg, bool* ok) {
    uint8_t data[4];
    for (int tries = 0; tries < 10; tries++) {
        if (readReg(reg, data) == 4) {
            int32_t val = ((int32_t)data[0] << 24) |
                          ((int32_t)data[1] << 16) |
                          ((int32_t)data[2] << 8)  |
                          ((int32_t)data[3]);
            if (ok) *ok = true;
            return val;
        }
    }
    Serial.println("TMC2209: after 10 tries no valid answer. Is the driver powered?");
    if (ok) *ok = false;
    return 0;
}

bool TMC_UART::writeReg(uint8_t reg, uint32_t val) {
    uint8_t frame[8] = {
        0x55, _mtr_id, (uint8_t)(reg | 0x80),
        (uint8_t)((val >> 24) & 0xFF),
        (uint8_t)((val >> 16) & 0xFF),
        (uint8_t)((val >> 8) & 0xFF),
        (uint8_t)(val & 0xFF),
        0,
    };
    frame[7] = crc8(frame, 7);

    if (_ser.write(frame, 8) != 8) return false;
    _ser.flush();
    delayMicroseconds(_communication_pause_us);
    return true;
}

bool TMC_UART::writeRegCheck(uint8_t reg, uint32_t val) {
    int32_t ifcnt1 = readInt(IFCNT);
    writeReg(reg, val);
    // Read twice: the driver's IFCNT bump is not always visible on the very
    // first follow-up read in the half-duplex timing.
    readInt(IFCNT);
    int32_t ifcnt2 = readInt(IFCNT);

    if (ifcnt1 >= ifcnt2) {
        Serial.print("TMC2209: writing not successful! reg=0x");
        Serial.print(reg, HEX);
        Serial.print(" IFCNT ");
        Serial.print(ifcnt1);
        Serial.print(" -> ");
        Serial.println(ifcnt2);
        return false;
    }
    return true;
}

bool TMC_UART::test() {
    uint8_t data[4];
    return readReg(0x06, data) == 4;  // IOIN register
}
