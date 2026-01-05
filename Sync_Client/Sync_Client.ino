#include <FastLED.h>
#include <SPI.h>
#include <pgmspace.h>
#include <ArduinoJson.h>
#include "../lib/tcp_client_network.cpp"

Preferences prefs;

// ──────────────────────────────────────────────────────────────
// CONFIG
// ──────────────────────────────────────────────────────────────
#define NUM_LEDS 2
#define DATA_PIN 21

#define ADC_PIN 1        // <<<<<< acquire from this analog pin

#define BUFFERSIZE 16
#define SAMPLE_SIZE_BYTES 8      // float value + float timestamp
#define DATAQUEUE_SIZE 1024

const char jsonConfig[] PROGMEM =
#include "def_settings_config.json"
;

CRGB leds[NUM_LEDS];



// ──────────────────────────────────────────────────────────────
// STRUCTS
// ──────────────────────────────────────────────────────────────
struct Sample {
    float value;
    float timestamp;
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

void IRAM_ATTR risingWrite_Isr() {
    // Create and initialize the sample for LOW value (before rising edge)
    Sample* lowSample = (Sample*) malloc(sizeof(Sample));
    if (lowSample) {
        lowSample->value = 0.0f; // Set value to LOW
        lowSample->timestamp = (float)esp_timer_get_time(); // Get timestamp
        xQueueSendFromISR(datatransferQueue, &lowSample, NULL);
    }
    
    // Create and initialize the sample for HIGH value (after rising edge)
    Sample* highSample = (Sample*) malloc(sizeof(Sample));
    if (highSample) {
        highSample->value = 1.0f; // Set value to HIGH
        highSample->timestamp = (float)esp_timer_get_time(); // Get timestamp
        xQueueSendFromISR(datatransferQueue, &highSample, NULL);
    }
}






void IRAM_ATTR fallingWrite_Isr() {
    // Create and initialize the sample for HIGH value (before falling edge)
    Sample* highSample = (Sample*) malloc(sizeof(Sample));
    if (highSample) {
        highSample->value = 1.0f; // Set value to HIGH
        highSample->timestamp = (float)esp_timer_get_time(); // Get timestamp
        xQueueSendFromISR(datatransferQueue, &highSample, NULL);
    }
    
    // Create and initialize the sample for LOW value (after falling edge)
    Sample* lowSample = (Sample*) malloc(sizeof(Sample));
    if (lowSample) {
        lowSample->value = 0.0f; // Set value to LOW
        lowSample->timestamp = (float)esp_timer_get_time(); // Get timestamp
        xQueueSendFromISR(datatransferQueue, &lowSample, NULL);
    }
}





// ──────────────────────────────────────────────────────────────
// ACQUISITION TASK
// continuously capture analog value + timestamp
// ──────────────────────────────────────────────────────────────
void aquireTaskCode(void* params) {
    attachInterrupt(digitalPinToInterrupt(ADC_PIN), risingWrite_Isr, RISING);
    attachInterrupt(digitalPinToInterrupt(ADC_PIN), fallingWrite_Isr, FALLING);
    while (1) {
        Sample* s = (Sample*) malloc(sizeof(Sample));
        if (s) {
            //int raw = digitalRead(ADC_PIN);  // Digital read (HIGH or LOW)
            s->value = (float) digitalRead(ADC_PIN); // 1.0 (HIGH) or 0.0 (LOW)
            s->timestamp = (float)esp_timer_get_time();  // Get timestamp in microseconds
            xQueueSend(datatransferQueue, &s, 0);
        }
        vTaskDelay(2);   // adjust sampling speed
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
    xTaskCreatePinnedToCore(aquireTaskCode, "acq", 4096, NULL, 1, &aquireTask, 1);

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

            memcpy(buffer + j*SAMPLE_SIZE_BYTES, &(s->value), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 4, &(s->timestamp), 4);

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

    pinMode(ADC_PIN, INPUT);  // Set the pin as an input for digital read

    // queue for samples
    datatransferQueue = xQueueCreate(DATAQUEUE_SIZE, sizeof(Sample*));

    xTaskCreatePinnedToCore(sendTaskCode, "send", 8192, NULL, 1, &sendTask, 0);
}


// ──────────────────────────────────────────────────────────────
void loop() {
    vTaskDelay(portMAX_DELAY);
}
