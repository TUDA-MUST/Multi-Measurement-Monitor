/*
 * Minimal C version of ESP32 MMM client
 * Compile: gcc testclient.c -o testclient  `pkg-config --cflags --libs gtk+-3.0 json-glib-1.0` -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <math.h>

// -----------------------------
// CONFIG
// -----------------------------

#define CHANNEL_COUNT 4
#define SAMPLE_SIZE_BYTES (CHANNEL_COUNT * 4)
#define BUFFERSIZE 16

#define PORT 8080
char discovered_host[INET_ADDRSTRLEN] = {0};

// -----------------------------
// PACKAGE TYPES
// -----------------------------

typedef enum {
    PING_PACKAGE      = 0,
    STOP_PACKAGE      = 1,
    SETTINGS_PACKAGE  = 2,
    DATA_PACKAGE      = 3,
    TIMESTAMP_PACKAGE = 4,
    HANDSHAKE_PACKAGE = 5
} PackageType;

// -----------------------------
// SETTINGS STRUCT
// -----------------------------

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t gain_level;
    uint8_t ctrl_reg;
    uint32_t buffer_size;
    uint64_t device_serial;
    int32_t trigger_offset;
    float calibration_factor;
    char name_of_operator[256];
    uint8_t mode;
} Settings;

Settings settings;

// -----------------------------
// TIME
// -----------------------------

uint64_t get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

// -----------------------------
// NETWORK HELPERS
// -----------------------------


void discover_server(char *out_ip) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5000);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("UDP bind failed");
        exit(1);
    }

    printf("Listening for SERVER_ALIVE broadcast on UDP 5000...\n");

    while (1) {
        char buffer[256];
        struct sockaddr_in sender;
        socklen_t sender_len = sizeof(sender);

        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                           (struct sockaddr*)&sender, &sender_len);

        if (len <= 0) continue;

        buffer[len] = '\0';

        if (strcmp(buffer, "SERVER_ALIVE") == 0) {
            inet_ntop(AF_INET, &sender.sin_addr, out_ip, INET_ADDRSTRLEN);

            printf("Discovered server at %s\n", out_ip);
            close(sock);
            return;
        }
    }
}




int connect_to_host(const char *host) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, host, &server.sin_addr);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("connect failed");
        exit(1);
    }

    printf("Connected to server at %s\n", host);
    return sock;
}






void send_handshake(int sock) {
    const char *json =
     "{\n"
"\"gui_handle\": \"Example_All_Settings_Client\",\n"
"\"float_number\": \"4\",\n"
"\"channel_names\": [\"Voltage_V\", \"Current_mA\", \"Temperature_degC\",\"Time_us\"],\n"
"\"settings\": [\n"

"{\"gui_handle\":\"Sample Rate\",\"microcontroller_handle\":\"sample_rate_hz\",\"type\":\"combo\",\"datatype\":\"uint32\",\"default\":200,\"options\":[10,100,200,300]},\n"
"{\"gui_handle\":\"Gain Level\",\"microcontroller_handle\":\"gain_level\",\"type\":\"combo\",\"datatype\":\"uint8\",\"default\":1,\"options\":[0,1,2,4,8]},\n"
"{\"gui_handle\":\"Control Register\",\"microcontroller_handle\":\"ctrl_reg\",\"type\":\"entry\",\"datatype\":\"uint8_hex\",\"default\":\"0x1F\"},\n"
"{\"gui_handle\":\"Buffer Size\",\"microcontroller_handle\":\"buffer_size\",\"type\":\"entry\",\"datatype\":\"uint32\",\"default\":4096},\n"
"{\"gui_handle\":\"Device Serial Number\",\"microcontroller_handle\":\"device_serial\",\"type\":\"entry\",\"datatype\":\"uint64\",\"default\":123456789012345},\n"
"{\"gui_handle\":\"Trigger Offset\",\"microcontroller_handle\":\"trigger_offset\",\"type\":\"entry\",\"datatype\":\"int32\",\"default\":-10},\n"
"{\"gui_handle\":\"Calibration Factor\",\"microcontroller_handle\":\"calibration_factor\",\"type\":\"entry\",\"datatype\":\"float\",\"default\":1.2},\n"
"{\"gui_handle\":\"Operator\",\"microcontroller_handle\":\"name_of_operator\",\"type\":\"entry\",\"datatype\":\"char[256]\",\"default\":\"Max Mustermann\"},\n"
"{\"gui_handle\":\"Operating Mode\",\"microcontroller_handle\":\"mode\",\"type\":\"combo\",\"datatype\":\"uint8\",\"default\":1,\"options\":[0,1,2]}\n"

"]\n"
"}";

    uint32_t len = strlen(json);

    uint32_t total = 5 + len;
    uint8_t *packet = malloc(total);

    if (!packet) {
        perror("malloc failed");
        exit(1);
    }

    // header
    packet[0] = HANDSHAKE_PACKAGE;
    packet[1] = (len >> 24) & 0xFF;
    packet[2] = (len >> 16) & 0xFF;
    packet[3] = (len >> 8) & 0xFF;
    packet[4] = len & 0xFF;

    // payload
    memcpy(packet + 5, json, len);

    // SINGLE send
    size_t sent = 0;
    while (sent < total) {
        ssize_t s = send(sock, packet + sent, total - sent, 0);
        if (s <= 0) {
            perror("send failed");
            free(packet);
            exit(1);
        }
        sent += s;
    }

    free(packet);
}






// -----------------------------
// READ PACKAGE
// -----------------------------

int read_exact(int sock, uint8_t *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        int r = recv(sock, buf + total, len - total, 0);
        if (r <= 0) return 0;
        total += r;
    }
    return 1;
}

int read_package(int sock, uint8_t *type, uint32_t *size, uint8_t **data) {
    uint8_t header[5];

    if (!read_exact(sock, header, 5)) return 0;

    *type = header[0];
    *size = (header[1]<<24) | (header[2]<<16) | (header[3]<<8) | header[4];

    *data = malloc(*size);
    if (!read_exact(sock, *data, *size)) {
        free(*data);
        return 0;
    }

    return 1;
}

// -----------------------------
// SETTINGS (VERY SIMPLE PARSE)
// -----------------------------

void apply_settings(char *json) {
    // Minimal parsing (replace with cJSON if needed)
    settings.sample_rate_hz = 20;
    settings.gain_level = 1;
    settings.buffer_size = 4096;
    settings.calibration_factor = 1.0;

    printf("\n--- SETTINGS RECEIVED ---\n");
    printf("sample_rate_hz: %u\n", settings.sample_rate_hz);
    printf("-------------------------\n");
}

// -----------------------------
// SIGNAL GENERATION
// -----------------------------

float phase = 0;

float generateVoltage() {
    return phase * 5.0f;
}

float generateCurrent() {
    return sinf(phase * 2 * M_PI) * 10.0f;
}

float generateTemperature() {
    return 25.0f;
}

// -----------------------------
// MAIN
// -----------------------------

int main() {

    discover_server(discovered_host);
    int sock = connect_to_host(discovered_host);

    send_handshake(sock);

    // Wait for settings
    while (1) {
        uint8_t type;
        uint32_t size;
        uint8_t *data;

        if (!read_package(sock, &type, &size, &data)) break;

        if (type == SETTINGS_PACKAGE) {
            apply_settings((char*)data);
            free(data);
            break;
        }

        free(data);
    }

    // MAIN LOOP
    while (1) {

        uint8_t header[5];
        uint8_t buffer[SAMPLE_SIZE_BYTES * BUFFERSIZE];

        int dataSize = sizeof(buffer);

        header[0] = DATA_PACKAGE;
        header[1] = (dataSize >> 24) & 0xFF;
        header[2] = (dataSize >> 16) & 0xFF;
        header[3] = (dataSize >> 8) & 0xFF;
        header[4] = dataSize & 0xFF;

        // Fill buffer
        for (int i = 0; i < BUFFERSIZE; i++) {

            float sample[CHANNEL_COUNT];

            uint64_t t = get_time_us();

            sample[0] = generateVoltage();
            sample[1] = generateCurrent();
            sample[2] = generateTemperature();
            sample[3] = (float)t;

            memcpy(buffer + i * SAMPLE_SIZE_BYTES, sample, SAMPLE_SIZE_BYTES);

            phase += 1.0 / (12 * settings.sample_rate_hz);
            if (phase > 1) phase -= 1;
        }

        send(sock, header, 5, 0);
        send(sock, buffer, dataSize, 0);

        usleep(1000000 / settings.sample_rate_hz);
    }

    close(sock);
    return 0;
}
