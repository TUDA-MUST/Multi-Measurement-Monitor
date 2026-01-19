#include "ADS122C04.h"

// Commands
#define CMD_RESET     0x06
#define CMD_START     0x08
#define CMD_POWERDOWN 0x02
#define CMD_RDATA     0x10
#define CMD_RREG      0x20
#define CMD_WREG      0x40

ADS122C04::ADS122C04(uint8_t addr) : _addr(addr) {
    _reg[0] = 0x00;
    _reg[1] = 0x00;
    _reg[2] = 0x00;
    _reg[3] = 0x00;
}

bool ADS122C04::begin(TwoWire &wire) {
    _wire = &wire;
    if (!isConnected()) return false;
    reset();
    return true;
}

bool ADS122C04::isConnected() {
    _wire->beginTransmission(_addr);
    return (_wire->endTransmission() == 0);
}

void ADS122C04::reset() {
    sendCmd(CMD_RESET);
    delay(1);
    _reg[0] = _reg[1] = _reg[2] = _reg[3] = 0x00;
}

void ADS122C04::powerDown() {
    sendCmd(CMD_POWERDOWN);
}

void ADS122C04::start() {
    sendCmd(CMD_START);
}

bool ADS122C04::dataReady() {
    return (readReg(2) & 0x80) != 0;
}

int32_t ADS122C04::readData() {
    _wire->beginTransmission(_addr);
    _wire->write(CMD_RDATA);
    _wire->endTransmission(false);
    _wire->requestFrom(_addr, (uint8_t)3);
    
    int32_t raw = ((uint32_t)_wire->read() << 16) |
                  ((uint32_t)_wire->read() << 8)  |
                   (uint32_t)_wire->read();
    
    if (raw & 0x800000) raw |= 0xFF000000;  // Sign extend
    return raw;
}

int32_t ADS122C04::read() {
    start();
    while (!dataReady());
    return readData();
}

int32_t ADS122C04::readChannel(uint8_t ch) {
    setMux((Mux)(MUX_AIN0_AVSS + (ch & 0x03)));
    return read();
}

void ADS122C04::readAllChannels(int32_t *values) {
    for (uint8_t ch = 0; ch < 4; ch++) {
        values[ch] = readChannel(ch);
    }
}

// Configuration setters
void ADS122C04::setMux(Mux mux) {
    updateReg(0, 0x0F, mux, 4);
}

void ADS122C04::setGain(Gain gain) {
    updateReg(0, 0xF1, gain, 1);
}

void ADS122C04::setPGABypass(bool bypass) {
    updateReg(0, 0xFE, bypass ? 1 : 0, 0);
}

void ADS122C04::setDataRate(DataRate rate) {
    updateReg(1, 0x1F, rate, 5);
}

void ADS122C04::setTurboMode(bool turbo) {
    updateReg(1, 0xEF, turbo ? 1 : 0, 4);
}

void ADS122C04::setContinuousMode(bool continuous) {
    updateReg(1, 0xF7, continuous ? 1 : 0, 3);
}

void ADS122C04::setVRef(VRef ref) {
    updateReg(1, 0xF9, ref, 1);
}

void ADS122C04::setTempSensor(bool enable) {
    updateReg(1, 0xFE, enable ? 1 : 0, 0);
}

void ADS122C04::setDataCounter(bool enable) {
    updateReg(2, 0xBF, enable ? 1 : 0, 6);
}

void ADS122C04::setCRC(CRC mode) {
    updateReg(2, 0xCF, mode, 4);
}

void ADS122C04::setBurnoutCurrent(bool enable) {
    updateReg(2, 0xF7, enable ? 1 : 0, 3);
}

void ADS122C04::setIDAC(IDAC current) {
    updateReg(2, 0xF8, current, 0);
}

void ADS122C04::setIDAC1Route(IDACRoute route) {
    updateReg(3, 0x1F, route, 5);
}

void ADS122C04::setIDAC2Route(IDACRoute route) {
    updateReg(3, 0xE3, route, 2);
}

// Configuration getters
uint8_t ADS122C04::getMux()      { return (_reg[0] >> 4) & 0x0F; }
uint8_t ADS122C04::getGain()     { return (_reg[0] >> 1) & 0x07; }
uint8_t ADS122C04::getDataRate() { return (_reg[1] >> 5) & 0x07; }
uint8_t ADS122C04::getVRef()     { return (_reg[1] >> 1) & 0x03; }

float ADS122C04::readInternalTemp() {
    uint8_t savedReg0 = _reg[0];
    uint8_t savedReg1 = _reg[1];
    
    setMux(MUX_SHORTED);
    setTempSensor(true);
    setDataRate(SPS_20);
    
    int32_t raw = read();
    
    // Restore settings
    writeReg(0, savedReg0);
    writeReg(1, savedReg1);
    
    // Temperature is 14-bit, shift right and scale
    return (raw >> 10) * 0.03125f;
}

// Private methods
void ADS122C04::writeReg(uint8_t reg, uint8_t value) {
    _wire->beginTransmission(_addr);
    _wire->write(CMD_WREG | (reg << 2));
    _wire->write(value);
    _wire->endTransmission();
    _reg[reg] = value;
}

uint8_t ADS122C04::readReg(uint8_t reg) {
    _wire->beginTransmission(_addr);
    _wire->write(CMD_RREG | (reg << 2));
    _wire->endTransmission(false);
    _wire->requestFrom(_addr, (uint8_t)1);
    return _wire->read();
}

void ADS122C04::sendCmd(uint8_t cmd) {
    _wire->beginTransmission(_addr);
    _wire->write(cmd);
    _wire->endTransmission();
}

void ADS122C04::updateReg(uint8_t reg, uint8_t mask, uint8_t value, uint8_t shift) {
    _reg[reg] = (_reg[reg] & mask) | (value << shift);
    writeReg(reg, _reg[reg]);
}
