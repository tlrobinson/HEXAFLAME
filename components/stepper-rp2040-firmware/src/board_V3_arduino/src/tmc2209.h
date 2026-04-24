#pragma once
#include "tmc2209_uart.h"

class TMC2209 {
public:
    TMC2209(HardwareSerial& serial, uint8_t rx_pin, uint8_t tx_pin, uint8_t mtr_id, uint32_t baudrate);

    void setMotorId(uint8_t mtr_id) { _uart.setMotorId(mtr_id); }

    int32_t getStallguardResult();
    bool setStallguardThreshold(uint8_t threshold);
    bool setCoolstepThreshold(uint32_t threshold = 1600);

    // Wires up the DIAG pin (GP11) interrupt with the provided handler.
    void setStallguardCallback(uint8_t threshold, void (*handler)());

    bool test();

private:
    TMC_UART _uart;

    static constexpr uint8_t REG_TCOOLTHRS = 0x14;
    static constexpr uint8_t REG_SGTHRS    = 0x40;
    static constexpr uint8_t REG_SG_RESULT = 0x41;

    static constexpr uint8_t DIAG_PIN = 11;
};
