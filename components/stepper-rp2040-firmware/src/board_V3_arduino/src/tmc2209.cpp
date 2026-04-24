#include "tmc2209.h"

TMC2209::TMC2209(HardwareSerial& serial, uint8_t rx_pin, uint8_t tx_pin, uint8_t mtr_id, uint32_t baudrate)
    : _uart(serial, baudrate, rx_pin, tx_pin, mtr_id) {}

int32_t TMC2209::getStallguardResult() {
    return _uart.readInt(REG_SG_RESULT);
}

bool TMC2209::setStallguardThreshold(uint8_t threshold) {
    return _uart.writeRegCheck(REG_SGTHRS, threshold);
}

bool TMC2209::setCoolstepThreshold(uint32_t threshold) {
    return _uart.writeRegCheck(REG_TCOOLTHRS, threshold);
}

void TMC2209::setStallguardCallback(uint8_t threshold, void (*handler)()) {
    setStallguardThreshold(threshold);
    setCoolstepThreshold();
    pinMode(DIAG_PIN, INPUT_PULLDOWN);
    if (handler) {
        attachInterrupt(digitalPinToInterrupt(DIAG_PIN), handler, RISING);
    } else {
        detachInterrupt(digitalPinToInterrupt(DIAG_PIN));
    }
}

bool TMC2209::test() {
    return _uart.test();
}
