#pragma once
#include <Arduino.h>

// Minimal half-duplex UART wrapper for the TMC2209, ported from the
// MicroPython implementation. Supports only the registers needed by
// the sensorless-homing demo.
class TMC_UART {
public:
    TMC_UART(HardwareSerial& serial, uint32_t baudrate, uint8_t rx_pin, uint8_t tx_pin, uint8_t mtr_id);

    void setMotorId(uint8_t mtr_id) { _mtr_id = mtr_id; }

    // Returns the 32-bit register value, or 0 on failure (check `ok`).
    int32_t readInt(uint8_t reg, bool* ok = nullptr);

    // Write a 32-bit value. Returns true if the TMC acknowledged the write
    // by incrementing its interface counter.
    bool writeRegCheck(uint8_t reg, uint32_t val);

    // Simple "is the driver responding" probe (reads the IOIN register).
    bool test();

private:
    // Reads up to 4 data bytes from the register (positive response length).
    // Returns the number of payload bytes copied into `out` (0 or 4).
    int readReg(uint8_t reg, uint8_t out[4]);

    bool writeReg(uint8_t reg, uint32_t val);

    static uint8_t crc8(const uint8_t* data, size_t len);

    HardwareSerial& _ser;
    uint8_t _mtr_id;
    uint32_t _communication_pause_us;

    static constexpr uint8_t IFCNT = 0x02;
};
