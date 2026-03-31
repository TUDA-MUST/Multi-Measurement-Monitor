/*
This is a basic, but complete example of a client for the MMM, showing all settings configurations, 
as well as the data and program flow. 
The data the programm processes is nonsensical though...
*/

#include <WiFi.h>
#include <ArduinoJson.h>
#include "../src/lib/tcp_client_network.cpp"



#define CHANNEL_COUNT 4
#define SAMPLE_SIZE_BYTES (CHANNEL_COUNT * 4)
#define BUFFERSIZE 16

WiFiClient client;

// --------------------------------------------------
// CONFIG JSON
// --------------------------------------------------

const char jsonConfig[] PROGMEM = 
#include "basic_complete_example.json"
;


// --------------------------------------------------
// SETTINGS STRUCT
// --------------------------------------------------

struct Settings {

    uint32_t sample_rate_hz;
    uint8_t gain_level;
    uint8_t ctrl_reg;
    uint32_t buffer_size;
    uint64_t device_serial;
    int32_t trigger_offset;
    float calibration_factor;
    char name_of_operator[256];
    uint8_t mode;

};

Settings settings;


// --------------------------------------------------
// WAIT + PARSE SETTINGS
// --------------------------------------------------

void waitAndApplySettings(WiFiClient &client){
    TcpPackage pkg;
    pkg.package_type = PING_PACKAGE;

    while (pkg.package_type != SETTINGS_PACKAGE)
        getNextTcpPackage(client, pkg);

    DynamicJsonDocument doc(pkg.package_size * 2);

    deserializeJson(doc, pkg.package, pkg.package_size);

    settings.sample_rate_hz = doc["sample_rate_hz"] | 200;
    settings.gain_level = doc["gain_level"] | 1;
    settings.ctrl_reg = strtol(doc["ctrl_reg"] | "0x1F", NULL, 16);
    settings.buffer_size = doc["buffer_size"] | 4096;
    settings.device_serial = doc["device_serial"] | 0;
    settings.trigger_offset = doc["trigger_offset"] | 0;
    settings.calibration_factor = doc["calibration_factor"] | 1.0;

    strlcpy(settings.name_of_operator,
            doc["name_of_operator"] | "unknown",
            sizeof(settings.name_of_operator));

    settings.mode = doc["mode"] | 0;

    Serial.println("\n--- SETTINGS RECEIVED ---");

    Serial.printf("sample_rate_hz: %u\n", settings.sample_rate_hz);
    Serial.printf("gain_level: %u\n", settings.gain_level);
    Serial.printf("ctrl_reg: 0x%X\n", settings.ctrl_reg);
    Serial.printf("buffer_size: %u\n", settings.buffer_size);
    Serial.printf("device_serial: %llu\n", settings.device_serial);
    Serial.printf("trigger_offset: %ld\n", settings.trigger_offset);
    Serial.printf("calibration_factor: %f\n", settings.calibration_factor);
    Serial.printf("operator: %s\n", settings.name_of_operator);
    Serial.printf("mode: %u\n", settings.mode);

    Serial.println("-------------------------\n");
}


// --------------------------------------------------
// SIGNAL GENERATORS
// --------------------------------------------------

float phase = 0;

float generateVoltage(){
    return phase * 5.0;
}

float generateCurrent(){
    return sinf(phase * 2 * PI) * 10.0;
}

float generateTemperature(){
    return 25.0;
}


// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup(){

    Serial.begin(921600);


    String clientName;
    wifiConnectAndAllowOverride(clientName);

    IPAddress hostIP = discoverHostUDP();
    client = connectToHost(hostIP, 8080);

    sendHandshakePackage(client, jsonConfig);

    waitAndApplySettings(client);

}


// --------------------------------------------------
// LOOP
// --------------------------------------------------

void loop(){

    static uint8_t header[5];
    static uint8_t buffer[SAMPLE_SIZE_BYTES * BUFFERSIZE];

    int dataSize = SAMPLE_SIZE_BYTES * BUFFERSIZE;

    header[0] = DATA_PACKAGE;
    header[1] = (dataSize >> 24) & 0xFF;
    header[2] = (dataSize >> 16) & 0xFF;
    header[3] = (dataSize >> 8) & 0xFF;
    header[4] = dataSize & 0xFF;


    TcpPackage incoming;

    // --------------------------------------------------
    // HANDLE INCOMING PACKETS
    // --------------------------------------------------

    while (client.available()) {

        getNextTcpPackage(client, incoming);

        switch (incoming.package_type) {

            case STOP_PACKAGE:

                Serial.println("STOP received");

                client.stop();
                WiFi.disconnect();
                delay(200);

                ESP.restart();
                break;

            case TIMESTAMP_PACKAGE:

                respondWithTimestampPackage(client, incoming);
                break;

            default:
                break;
        }
    }


    // --------------------------------------------------
    // GENERATE BUFFER
    // --------------------------------------------------

    for (int i = 0; i < BUFFERSIZE; i++) {

        float sample[CHANNEL_COUNT];

        uint64_t t = esp_timer_get_time();

        sample[0] = generateVoltage();
        sample[1] = generateCurrent();
        sample[2] = generateTemperature();
        sample[3] = (float)t;

        memcpy(buffer + i * SAMPLE_SIZE_BYTES, sample, SAMPLE_SIZE_BYTES);

        phase += 1.0 / (12*settings.sample_rate_hz);
        if (phase > 1) phase -= 1;
    }


    // --------------------------------------------------
    // SEND DATA PACKAGE
    // --------------------------------------------------

    client.write(header, 5);                    //TODO: Rewrite into single client.write()
    client.write(buffer, dataSize);


    delayMicroseconds(1000000 / settings.sample_rate_hz);
}