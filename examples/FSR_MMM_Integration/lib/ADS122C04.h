#ifndef ADS122C04_H
#define ADS122C04_H

#include <Arduino.h>
#include <Wire.h>

class ADS122C04 {
public:
    // Input Multiplexer
    enum Mux : uint8_t {
        MUX_AIN0_AIN1   = 0x0,  // Differential
        MUX_AIN0_AIN2   = 0x1,
        MUX_AIN0_AIN3   = 0x2,
        MUX_AIN1_AIN0   = 0x3,
        MUX_AIN1_AIN2   = 0x4,
        MUX_AIN1_AIN3   = 0x5,
        MUX_AIN2_AIN3   = 0x6,
        MUX_AIN3_AIN2   = 0x7,
        MUX_AIN0_AVSS   = 0x8,  // Single-ended
        MUX_AIN1_AVSS   = 0x9,
        MUX_AIN2_AVSS   = 0xA,
        MUX_AIN3_AVSS   = 0xB,
        MUX_REF         = 0xC,  // (REFP - REFN) / 4
        MUX_AVDD        = 0xD,  // (AVDD - AVSS) / 4
        MUX_SHORTED     = 0xE   // Shorted to mid-supply
    };

    // Gain
    enum Gain : uint8_t {
        GAIN_1   = 0, GAIN_2   = 1, GAIN_4   = 2, GAIN_8   = 3,
        GAIN_16  = 4, GAIN_32  = 5, GAIN_64  = 6, GAIN_128 = 7
    };

    // Data Rate (normal mode SPS, turbo = 2x)
    enum DataRate : uint8_t {
        SPS_20  = 0, SPS_45  = 1, SPS_90  = 2, SPS_175 = 3,
        SPS_330 = 4, SPS_600 = 5, SPS_1000 = 6
    };

    // Voltage Reference
    enum VRef : uint8_t {
        VREF_INTERNAL = 0,  // 2.048V internal
        VREF_EXTERNAL = 1,  // External REFP/REFN
        VREF_AVDD     = 2   // Analog supply
    };

    // IDAC Current
    enum IDAC : uint8_t {
        IDAC_OFF     = 0,
        IDAC_10UA    = 1, IDAC_50UA   = 2, IDAC_100UA  = 3,
        IDAC_250UA   = 4, IDAC_500UA  = 5, IDAC_1000UA = 6, IDAC_1500UA = 7
    };

    // IDAC Routing
    enum IDACRoute : uint8_t {
        IDAC_DISABLED = 0,
        IDAC_AIN0 = 1, IDAC_AIN1 = 2, IDAC_AIN2 = 3, IDAC_AIN3 = 4,
        IDAC_REFP = 5, IDAC_REFN = 6
    };

    // CRC Mode
    enum CRC : uint8_t {
        CRC_OFF      = 0,
        CRC_INVERTED = 1,
        CRC_CRC16    = 2
    };

    ADS122C04(uint8_t addr = 0x45);
    
    bool begin(TwoWire &wire = Wire);
    bool isConnected();
    void reset();
    void powerDown();
    
    // Conversion
    void start();
    bool dataReady();
    int32_t readData();
    int32_t read();  // start + wait + read
    
    // Single-ended convenience
    int32_t readChannel(uint8_t ch);
    void readAllChannels(int32_t *values);
    
    // Configuration setters
    void setMux(Mux mux);
    void setGain(Gain gain);
    void setPGABypass(bool bypass);
    void setDataRate(DataRate rate);
    void setTurboMode(bool turbo);
    void setContinuousMode(bool continuous);
    void setVRef(VRef ref);
    void setTempSensor(bool enable);
    void setDataCounter(bool enable);
    void setCRC(CRC mode);
    void setBurnoutCurrent(bool enable);
    void setIDAC(IDAC current);
    void setIDAC1Route(IDACRoute route);
    void setIDAC2Route(IDACRoute route);
    
    // Configuration getters
    uint8_t getMux();
    uint8_t getGain();
    uint8_t getDataRate();
    uint8_t getVRef();
    
    // Internal temperature (°C)
    float readInternalTemp();

private:
    TwoWire *_wire;
    uint8_t _addr;
    uint8_t _reg[4];
    
    void writeReg(uint8_t reg, uint8_t value);
    uint8_t readReg(uint8_t reg);
    void sendCmd(uint8_t cmd);
    void updateReg(uint8_t reg, uint8_t mask, uint8_t value, uint8_t shift);
};

#endif
