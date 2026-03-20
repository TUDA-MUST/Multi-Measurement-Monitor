#include <FastLED.h>
#include <SPI.h>
#include <pgmspace.h>
#include <ArduinoJson.h>
#include <MCP4725.h>
#include "./lib/ADS122C04.cpp"
#include "../../lib/tcp_client_network.cpp"

Preferences prefs;

// ──────────────────────────────────────────────────────────────
// CONFIG
// ──────────────────────────────────────────────────────────────
#define NUM_LEDS 1
#define DATA_PIN 40

#define ADC0_I2C_ADDRESS 0x41
#define ADC1_I2C_ADDRESS 0x42
#define NEOPIXEL_I2C_POWER 2


#define ADC_PIN 17        // <<<<<< acquire sync flank from this analog pin (button prress)

#define BUFFERSIZE 30
#define SAMPLE_SIZE_BYTES 24      // sensor_nr + float value[4] + float timestamp
#define DATAQUEUE_SIZE 1024



//  DAC vars
byte DAC_bias_Adress = 0x60;
MCP4725 DAC_bias(DAC_bias_Adress);
float vref = 0.3;
double ADC_vals[4] = {0};

// SFE_ADS122C04 adc;

bool serial_output_active = true;
uint32_t adc_sample_rate = 20;
ADS122C04 adc(0x41);
int32_t values[4];


const char jsonConfig[] PROGMEM =
#include "def_settings_config.json"
;

CRGB leds[NUM_LEDS];



// ──────────────────────────────────────────────────────────────
// STRUCTS
// ──────────────────────────────────────────────────────────────
struct Sample {
    float button_press;              // button pressed or not
    float value [4];                 // ADC values
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


    //----------------------------
    // configuration of ADC and DAC
    //-----------------------------

    // -------- Gain --------
    if (doc.containsKey("gain")) {
        const char* gainStr = doc["gain"];
        if      (!strcmp(gainStr, "GAIN_1"))   adc.setGain(ADS122C04::GAIN_1);
        else if (!strcmp(gainStr, "GAIN_2"))   adc.setGain(ADS122C04::GAIN_2);
        else if (!strcmp(gainStr, "GAIN_4"))   adc.setGain(ADS122C04::GAIN_4);
        else if (!strcmp(gainStr, "GAIN_8"))   adc.setGain(ADS122C04::GAIN_8);
        else if (!strcmp(gainStr, "GAIN_16"))  adc.setGain(ADS122C04::GAIN_16);
        else if (!strcmp(gainStr, "GAIN_32"))  adc.setGain(ADS122C04::GAIN_32);
        else if (!strcmp(gainStr, "GAIN_64"))  adc.setGain(ADS122C04::GAIN_64);
        else if (!strcmp(gainStr, "GAIN_128")) adc.setGain(ADS122C04::GAIN_128);
    }

    // -------- Data rate --------
    switch (sample_rate_hz) {
        case 20:   adc.setDataRate(ADS122C04::SPS_20);   break;
        case 45:   adc.setDataRate(ADS122C04::SPS_45);   break;
        case 90:   adc.setDataRate(ADS122C04::SPS_90);   break;
        case 175:  adc.setDataRate(ADS122C04::SPS_175);  break;
        case 330:  adc.setDataRate(ADS122C04::SPS_330);  break;
        case 600:  adc.setDataRate(ADS122C04::SPS_600);  break;
        case 1000:
        default:   adc.setDataRate(ADS122C04::SPS_1000); break;
    }
    adc_sample_rate = sample_rate_hz;

    // -------- Input multiplexer --------
    if (doc.containsKey("mux")) {
        const char* muxStr = doc["mux"];
        if      (!strcmp(muxStr, "MUX_AIN0_AIN1")) adc.setMux(ADS122C04::MUX_AIN0_AIN1);
        else if (!strcmp(muxStr, "MUX_AIN0_AIN2")) adc.setMux(ADS122C04::MUX_AIN0_AIN2);
        else if (!strcmp(muxStr, "MUX_AIN0_AIN3")) adc.setMux(ADS122C04::MUX_AIN0_AIN3);
        else if (!strcmp(muxStr, "MUX_AIN1_AIN0")) adc.setMux(ADS122C04::MUX_AIN1_AIN0);
        else if (!strcmp(muxStr, "MUX_AIN1_AIN2")) adc.setMux(ADS122C04::MUX_AIN1_AIN2);
        else if (!strcmp(muxStr, "MUX_AIN1_AIN3")) adc.setMux(ADS122C04::MUX_AIN1_AIN3);
        else if (!strcmp(muxStr, "MUX_AIN2_AIN3")) adc.setMux(ADS122C04::MUX_AIN2_AIN3);
        else if (!strcmp(muxStr, "MUX_AIN3_AIN2")) adc.setMux(ADS122C04::MUX_AIN3_AIN2);
        else if (!strcmp(muxStr, "MUX_AIN0_AVSS")) adc.setMux(ADS122C04::MUX_AIN0_AVSS);
        else if (!strcmp(muxStr, "MUX_AIN1_AVSS")) adc.setMux(ADS122C04::MUX_AIN1_AVSS);
        else if (!strcmp(muxStr, "MUX_AIN2_AVSS")) adc.setMux(ADS122C04::MUX_AIN2_AVSS);
        else if (!strcmp(muxStr, "MUX_AIN3_AVSS")) adc.setMux(ADS122C04::MUX_AIN3_AVSS);
        else if (!strcmp(muxStr, "MUX_REF"))       adc.setMux(ADS122C04::MUX_REF);
        else if (!strcmp(muxStr, "MUX_AVDD"))      adc.setMux(ADS122C04::MUX_AVDD);
        else if (!strcmp(muxStr, "MUX_SHORTED"))   adc.setMux(ADS122C04::MUX_SHORTED);
    }

    // -------- Voltage reference --------
    if (doc.containsKey("vref")) {
        const char* vrefStr = doc["vref"];
        if      (!strcmp(vrefStr, "VREF_INTERNAL")) adc.setVRef(ADS122C04::VREF_INTERNAL);
        else if (!strcmp(vrefStr, "VREF_EXTERNAL")) adc.setVRef(ADS122C04::VREF_EXTERNAL);
        else if (!strcmp(vrefStr, "VREF_AVDD"))     adc.setVRef(ADS122C04::VREF_AVDD);
    }

    // -------- IDAC current --------
    if (doc.containsKey("idac_current")) {
        const char* idacStr = doc["idac_current"];
        if      (!strcmp(idacStr, "IDAC_OFF"))     adc.setIDAC(ADS122C04::IDAC_OFF);
        else if (!strcmp(idacStr, "IDAC_10UA"))    adc.setIDAC(ADS122C04::IDAC_10UA);
        else if (!strcmp(idacStr, "IDAC_50UA"))    adc.setIDAC(ADS122C04::IDAC_50UA);
        else if (!strcmp(idacStr, "IDAC_100UA"))   adc.setIDAC(ADS122C04::IDAC_100UA);
        else if (!strcmp(idacStr, "IDAC_250UA"))   adc.setIDAC(ADS122C04::IDAC_250UA);
        else if (!strcmp(idacStr, "IDAC_500UA"))   adc.setIDAC(ADS122C04::IDAC_500UA);
        else if (!strcmp(idacStr, "IDAC_1000UA"))  adc.setIDAC(ADS122C04::IDAC_1000UA);
        else if (!strcmp(idacStr, "IDAC_1500UA"))  adc.setIDAC(ADS122C04::IDAC_1500UA);
    }

    // -------- IDAC routing --------
    if (doc.containsKey("idac_route")) {
        const char* routeStr = doc["idac_route"];
        if      (!strcmp(routeStr, "IDAC_DISABLED")) {
            adc.setIDAC1Route(ADS122C04::IDAC_DISABLED);
            adc.setIDAC2Route(ADS122C04::IDAC_DISABLED);
        }
        else if (!strcmp(routeStr, "IDAC_AIN0")) {
            adc.setIDAC1Route(ADS122C04::IDAC_AIN0);
            adc.setIDAC2Route(ADS122C04::IDAC_AIN0);
        }
        else if (!strcmp(routeStr, "IDAC_AIN1")) {
            adc.setIDAC1Route(ADS122C04::IDAC_AIN1);
            adc.setIDAC2Route(ADS122C04::IDAC_AIN1);
        }
        else if (!strcmp(routeStr, "IDAC_AIN2")) {
            adc.setIDAC1Route(ADS122C04::IDAC_AIN2);
            adc.setIDAC2Route(ADS122C04::IDAC_AIN2);
        }
        else if (!strcmp(routeStr, "IDAC_AIN3")) {
            adc.setIDAC1Route(ADS122C04::IDAC_AIN3);
            adc.setIDAC2Route(ADS122C04::IDAC_AIN3);
        }
        else if (!strcmp(routeStr, "IDAC_REFP")) {
            adc.setIDAC1Route(ADS122C04::IDAC_REFP);
            adc.setIDAC2Route(ADS122C04::IDAC_REFP);
        }
        else if (!strcmp(routeStr, "IDAC_REFN")) {
            adc.setIDAC1Route(ADS122C04::IDAC_REFN);
            adc.setIDAC2Route(ADS122C04::IDAC_REFN);
        }
    }

    // -------- CRC mode --------
    if (doc.containsKey("crc_mode")) {
        const char* crcStr = doc["crc_mode"];
        if      (!strcmp(crcStr, "CRC_OFF"))      adc.setCRC(ADS122C04::CRC_OFF);
        else if (!strcmp(crcStr, "CRC_INVERTED")) adc.setCRC(ADS122C04::CRC_INVERTED);
        else if (!strcmp(crcStr, "CRC_CRC16"))    adc.setCRC(ADS122C04::CRC_CRC16);
    }

    // -------- DAC --------
    DAC_bias.setMaxVoltage(3.3);
    vref = doc["vref_dac"].as<float>();
    DAC_bias.setVoltage(vref);

    return (uint64_t)sample_rate_hz * duration_seconds;
}





//----------------------------------------
//   ISRs
//----------------------------------------



















// ──────────────────────────────────────────────────────────────
// ACQUISITION TASK
// continuously capture analog value + timestamp
// ──────────────────────────────────────────────────────────────

void read_adc122c04(){
    // Create and initialize the sample for ADC values 
    Sample* adcSample = (Sample*)malloc(sizeof(Sample));
    if (adcSample) {
        adcSample->button_press = digitalRead(ADC_PIN);
        adc.readAllChannels(values);
        for (int i = 0; i < 4; i++) {
            adcSample->value[i] = (float)values[i];
        }
        adcSample->timestamp = (float)esp_timer_get_time(); // Get timestamp
        xQueueSend(datatransferQueue, &adcSample, 0);
    }
}






void aquireTaskCode(void* params) {
    
    //attachInterrupt(digitalPinToInterrupt(ADC_PIN), risingWrite_Isr, RISING);
    //attachInterrupt(digitalPinToInterrupt(ADC_PIN), fallingWrite_Isr, FALLING);
    
    uint64_t watchtime = esp_timer_get_time();
    uint64_t timeout = (adc_sample_rate > 0) ? (1000000ULL / adc_sample_rate) : 5000;
    while (1) {
        if(esp_timer_get_time() - watchtime > timeout){
            watchtime = esp_timer_get_time();
            read_adc122c04();
        }else vTaskDelay(1);   // adjust sampling speed
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

            memcpy(buffer + j*SAMPLE_SIZE_BYTES , &(s->button_press), 4);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 4, s->value, 16);
            memcpy(buffer + j*SAMPLE_SIZE_BYTES + 20, &(s->timestamp), 4);

            free(s);
        }

        // build packet
        memcpy(out, header, 5);
        memcpy(out + 5, buffer, dataSize);

        client.write(out, totalOut);
        //Serial.printf("write() = %d\n", written);
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


    pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_I2C_POWER, HIGH);

    Serial.println("Start FSR");

    Wire.begin();
    Wire.setClock(400000);

    DAC_bias.begin();
    

    // setupADS122C04();

    if (!adc.begin())
    {
        Serial.println("ADC not found! Stopping boot process.");
        while (1);
    }
    //adc.setTurboMode(true);
    

    
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
