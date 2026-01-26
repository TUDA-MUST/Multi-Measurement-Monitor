#include <FastLED.h>
#include <SPI.h>
#include <pgmspace.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <ResenseHEX.h>
#include "../../lib/tcp_client_network.cpp"

Preferences prefs;

// ──────────────────────────────────────────────────────────────
// CONFIG
// ──────────────────────────────────────────────────────────────
#define NUM_LEDS 1
#define DATA_PIN 40

#define BUFFERSIZE 30
#define SAMPLE_SIZE_BYTES 36      // size of Sample struct
#define DATAQUEUE_SIZE 1024

#ifndef TX_PIN
#define TX_PIN 43
#endif
#ifndef RX_PIN
#define RX_PIN 44
#endif


const char jsonConfig[] PROGMEM =
#include "def_settings_config.json"
;

CRGB leds[NUM_LEDS];

HardwareSerial HexSerial(1); 
ResenseHEX hex(HexSerial); 
HexFrame frame; 


// ──────────────────────────────────────────────────────────────
// STRUCTS
// ──────────────────────────────────────────────────────────────
struct Sample {
    float forces[3];
    float torques[3];
    float temperature;
    float hex_timestamp;
    float esp_timestamp;
};

TaskHandle_t aquireTask;
TaskHandle_t sendTask;
QueueHandle_t datatransferQueue;





//handle the incoming settings

uint64_t waitAndApplySettings_simple(WiFiClient &client)
{
    TcpPackage settingsPkg;
    settingsPkg.package_type = PING_PACKAGE;

    // --------------------------------------------------
    // Wait for SETTINGS package
    // --------------------------------------------------
    while (settingsPkg.package_type != SETTINGS_PACKAGE) {
        getNextTcpPackage(client, settingsPkg);
        Serial.print(".");
    }
    Serial.println("\nSettings received!");

    // --------------------------------------------------
    // Parse SETTINGS JSON
    // --------------------------------------------------
    size_t estimatedSize = settingsPkg.package_size * 1.2;
    DynamicJsonDocument doc(estimatedSize);

    DeserializationError err = deserializeJson(doc, settingsPkg.package, settingsPkg.package_size);

    if (err) {
        Serial.print("JSON parse failed: ");
        Serial.println(err.c_str());
        return 0;
    }

    // --------------------------------------------------
    // Extract settings (with defaults)
    // --------------------------------------------------
    uint32_t duration_seconds = doc.containsKey("duration_seconds") ? doc["duration_seconds"]: 1;

    uint32_t sample_rate_hz = doc.containsKey("sample_rate_hz") ? doc["sample_rate_hz"]: 1000;

    Serial.printf("Settings: %lu Hz, %lu s\n", sample_rate_hz, duration_seconds);

    return (uint64_t)sample_rate_hz * duration_seconds;
}





//----------------------------------------
//   ISRs
//----------------------------------------




// ──────────────────────────────────────────────────────────────
// ACQUISITION TASK
// continuously capture analog value + timestamp
// ──────────────────────────────────────────────────────────────
void aquireTaskCode(void* params) {
    
    while (1) {
        if (hex.triggerAndRead(frame)) { 
            if (hex.validateLimits(frame)) { 
                Sample* s = (Sample*) malloc(sizeof(Sample));
                if (s) {
                    s->forces[0] = frame.fx;
                    s->forces[1] = frame.fy;
                    s->forces[2] = frame.fz;
                    s->torques[0] = frame.mx;
                    s->torques[1] = frame.my;
                    s->torques[2] = frame.mz;
                    s->temperature = frame.temperature;
                    s->hex_timestamp = (float)frame.timestamp;
                    s->esp_timestamp = (float)esp_timer_get_time();  // Get timestamp in microseconds
                    xQueueSend(datatransferQueue, &s, 0);
                }
            }
        }
        vTaskDelay(1); 
    }
}


// ──────────────────────────────────────────────────────────────
// SEND TASK
// sends blocks of [value, timestamp] over TCP
// ──────────────────────────────────────────────────────────────
void sendTaskCode(void* params) {

    // LED: WiFi connecting
    leds[0] = CRGB::Yellow; FastLED.show();

    // connect to network
    String clientName;
    wifiConnectAndAllowOverride(clientName);

    IPAddress hostIP = discoverHostUDP();
    WiFiClient client = connectToHost(hostIP, 8080);

    sendHandshakePackage(client, jsonConfig);
    client.flush();


    leds[0] = CRGB::Magenta; FastLED.show();
    Serial.println("TCP connected — waiting for SETTINGS package.");

    uint64_t total_samples = waitAndApplySettings_simple(client);

    
    Serial.printf("Expecting to send %lld samples\n", total_samples);

    // LED: ready
    leds[0] = CRGB::Green; FastLED.show();

    // start acquiring
    xTaskCreatePinnedToCore(aquireTaskCode, "acq", 8192, NULL, 1, &aquireTask, 1);

    // allocate output buffer
    const int dataSize = SAMPLE_SIZE_BYTES * BUFFERSIZE;
    uint8_t* buffer = (uint8_t*) malloc(dataSize);
    uint8_t header[5];

    header[0] = DATA_PACKAGE;
    header[1] = (dataSize >> 24) & 0xFF;
    header[2] = (dataSize >> 16) & 0xFF;
    header[3] = (dataSize >> 8)  & 0xFF;
    header[4] = (dataSize >> 0)  & 0xFF;

    uint32_t totalOut = 5 + dataSize;
    uint8_t* out = (uint8_t*) malloc(totalOut);

    int64_t sent = 0;
    TcpPackage dataPkg;

    while (sent < total_samples) {

        // handle incoming commands (STOP, TIMESTAMP, etc.)
        while (client.available()) {
            getNextTcpPackage(client, dataPkg);

            switch(dataPkg.package_type) {
                case STOP_PACKAGE:
                    Serial.println("STOP requested.");
                    vTaskDelete(aquireTask);
                    client.stop();
                    WiFi.disconnect();
                    free(out);
                    ESP.restart();
                    break;

                case TIMESTAMP_PACKAGE:
                    respondWithTimestampPackage(client, dataPkg);
                    break;

                default:
                    break;
            }
        }

        // if queue contains at least BUFFERSIZE samples
        if (uxQueueMessagesWaiting(datatransferQueue) < BUFFERSIZE) {
            vTaskDelay(1);
            continue;
        }

        // fill the buffer
        for (int j = 0; j < BUFFERSIZE; j++) {
            Sample* s;
            xQueueReceive(datatransferQueue, &s, portMAX_DELAY);

            memcpy(buffer + j*SAMPLE_SIZE_BYTES, &(s->forces[0]), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 4, &(s->forces[1]), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 8, &(s->forces[2]), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 12, &(s->torques[0]), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 16, &(s->torques[1]), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 20, &(s->torques[2]), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 24, &(s->temperature), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 28, &(s->hex_timestamp), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 32, &(s->esp_timestamp), 4);

            free(s);
        }

        // build packet
        memcpy(out, header, 5);
        memcpy(out + 5, buffer, dataSize);

        client.write(out, totalOut);
        sent += BUFFERSIZE;
    }

    // cleanup
    free(out);
    free(buffer);

    vTaskDelete(aquireTask);
    client.stop();
    WiFi.disconnect();
    delay(200);
    ESP.restart();
}


// ──────────────────────────────────────────────────────────────
// SETUP
// ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(921600);
    
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
    FastLED.setBrightness(5);

    leds[0] = CRGB::Orange; FastLED.show();
    vTaskDelay(900);

    // HEX Serial Interface
    HexSerial.begin(ResenseHEX::DEFAULT_BAUD, ResenseHEX::DEFAULT_CONFIG, RX_PIN, TX_PIN); 
  
    while (!Serial && !HexSerial) vTaskDelay(10); 
    Serial.println("Starting Tara!");
    // block until taring is completed (may fail outside Software-Trigger-Mode)
    if(hex.tareBlocking()) Serial.println("Taring successful."); 
    else {
        Serial.println("Taring failed, restarting client!");
        delay(200);
        ESP.restart();
    }

    // queue for samples
    datatransferQueue = xQueueCreate(DATAQUEUE_SIZE, sizeof(Sample*));

    xTaskCreatePinnedToCore(sendTaskCode, "send", 8192, NULL, 1, &sendTask, 0);
}


// ──────────────────────────────────────────────────────────────
void loop() {
    vTaskDelay(portMAX_DELAY);
}
