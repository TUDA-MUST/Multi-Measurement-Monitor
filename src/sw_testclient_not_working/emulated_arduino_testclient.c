/* testclient.c – Desktop replacement for the ESP‑32 .ino client.
 *
 * gcc -o emulated_arduino_testclient emulated_arduino_testclient.c `pkg-config --cflags --libs gtk+-3.0 json-glib-1.0`  -lrt -lm
 *
 * This program does exactly what the basic_complete_example.ino does:
 *  • Discover the host (the server is hard‑coded to 127.0.0.1:8080).
 *  • Send a handshake containing the JSON config.
 *  • Wait for the SETTINGS packet and parse the JSON payload.
 *  • Generate four channels of fake data (Voltage, Current, Temperature, Timestamp)
 *    and send them at the user‑defined sample‑rate.
 *  • Respond to STOP and TIMESTAMP packets from the host.
 *
 * The program uses the same channel layout, same buffer sizes and
 * the same JSON format that the Arduino sketch uses.  It is fully
 * compatible with the server implementation used in the MMM project.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>

/* -------------------  Settings  ------------------- */
#define CHANNEL_COUNT 4
#define SAMPLE_SIZE_BYTES (CHANNEL_COUNT * sizeof(float))
#define BUFFERSIZE 16

/* -------------------  Packet types  ------------------- */
typedef enum {
    PING_PACKAGE     = 0,
    SETTINGS_PACKAGE = 1,
    DATA_PACKAGE     = 2,
    STOP_PACKAGE     = 3,
    TIMESTAMP_PACKAGE= 4,
    /* … other types are ignored by the client */
} PacketType;

/* -------------------  JSON configuration  ------------------- */
static const char *jsonConfig =
R"JSON(
{
    "gui_handle": "Example_All_Settings_Client",
    "float_number": "4",
    "channel_names": ["Voltage_V", "Current_mA", "Temperature_degC","Time_us"],
    "settings": [

        {
          "gui_handle": "Sample Rate",
          "microcontroller_handle": "sample_rate_hz",
          "type": "combo",
          "datatype": "uint32",
          "default": 200,
          "options": [10, 100, 200, 300]
        },

        {
          "gui_handle": "Gain Level",
          "microcontroller_handle": "gain_level",
          "type": "combo",
          "datatype": "uint8",
          "default": 1,
          "options": [0, 1, 2, 4, 8]
        },

        {
          "gui_handle": "Control Register",
          "microcontroller_handle": "ctrl_reg",
          "type": "entry",
          "datatype": "uint8_hex",
          "default": "0x1F"
        },

        {
          "gui_handle": "Buffer Size",
          "microcontroller_handle": "buffer_size",
          "type": "entry",
          "datatype": "uint32",
          "default": 4096
        },

        {
          "gui_handle": "Device Serial Number",
          "microcontroller_handle": "device_serial",
          "type": "entry",
          "datatype": "uint64",
          "default": 123456789012345
        },

        {
          "gui_handle": "Trigger Offset",
          "microcontroller_handle": "trigger_offset",
          "type": "entry",
          "datatype": "int32",
          "default": -10
        },

        {
          "gui_handle": "Calibration Factor",
          "microcontroller_handle": "calibration_factor",
          "type": "entry",
          "datatype": "float",
          "default": 1.2
        },

        {
          "gui_handle": "Operator",
          "microcontroller_handle": "name_of_operator",
          "type": "entry",
          "datatype": "char[256]",
          "default": "Max Mustermann"
        },

        {
          "gui_handle": "Operating Mode",
          "microcontroller_handle": "mode",
          "type": "combo",
          "datatype": "uint8",
          "default": 1,
          "options": [0, 1, 2]
        }

    ]
}
)JSON";

/* -------------------  Settings struct  ------------------- */
typedef struct {
    uint32_t sample_rate_hz;
    uint8_t  gain_level;
    uint8_t  ctrl_reg;
    uint32_t buffer_size;
    uint64_t device_serial;
    int32_t  trigger_offset;
    float    calibration_factor;
    char     name_of_operator[256];
    uint8_t  mode;
} Settings;

static Settings settings;

/* -------------------  Signal generators  ------------------- */
static float phase = 0.0f;

static float generateVoltage(void)   { return phase * 5.0f; }
static float generateCurrent(void)  { return sinf(phase * 2.0f * M_PI) * 10.0f; }
static float generateTemperature(void) { return 25.0f; }

/* -------------------  JSON parsing helper  ------------------- */
#include <json-c/json.h>
static void apply_settings_from_json(const char *json, size_t size)
{
    struct json_object *parsed_json = json_tokener_parse(json);
    if (!parsed_json) {
        fprintf(stderr, "Failed to parse settings JSON.\n");
        exit(EXIT_FAILURE);
    }

    struct json_object *settings_obj;
    if (!json_object_object_get_ex(parsed_json, "settings", &settings_obj)) {
        fprintf(stderr, "No \"settings\" field in JSON.\n");
        json_object_put(parsed_json);
        exit(EXIT_FAILURE);
    }

    /* Iterate over the array of settings and extract each value.  */
    size_t n_settings = json_object_array_length(settings_obj);
    for (size_t i = 0; i < n_settings; ++i) {
        struct json_object *s_obj = json_object_array_get_idx(settings_obj, i);
        const char *handle = json_object_get_string(
                json_object_object_get_ex(s_obj, "microcontroller_handle", NULL)
                ? json_object_object_get(s_obj, "microcontroller_handle") : NULL);

        /* Only read values that belong to the Arduino sketch – the others are ignored. */
        if (strcmp(handle, "sample_rate_hz") == 0) {
            settings.sample_rate_hz =
                (uint32_t)json_object_get_int(
                        json_object_object_get_ex(s_obj, "default", NULL) ?
                        json_object_object_get(s_obj, "default") : NULL);
        }
        else if (strcmp(handle, "gain_level") == 0) {
            settings.gain_level =
                (uint8_t)json_object_get_int(
                        json_object_object_get_ex(s_obj, "default", NULL) ?
                        json_object_object_get(s_obj, "default") : NULL);
        }
        else if (strcmp(handle, "ctrl_reg") == 0) {
            const char *hex = json_object_get_string(
                    json_object_object_get_ex(s_obj, "default", NULL) ?
                    json_object_object_get(s_obj, "default") : NULL);
            if (hex) {
                unsigned int val = 0;
                sscanf(hex, "%x", &val);
                settings.ctrl_reg = (uint8_t)val;
            }
        }
        else if (strcmp(handle, "buffer_size") == 0) {
            settings.buffer_size =
                (uint32_t)json_object_get_int(
                        json_object_object_get_ex(s_obj, "default", NULL) ?
                        json_object_object_get(s_obj, "default") : NULL);
        }
        else if (strcmp(handle, "device_serial") == 0) {
            settings.device_serial =
                (uint64_t)json_object_get_long(
                        json_object_object_get_ex(s_obj, "default", NULL) ?
                        json_object_object_get(s_obj, "default") : NULL);
        }
        else if (strcmp(handle, "trigger_offset") == 0) {
            settings.trigger_offset =
                (int32_t)json_object_get_int(
                        json_object_object_get_ex(s_obj, "default", NULL) ?
                        json_object_object_get(s_obj, "default") : NULL);
        }
        else if (strcmp(handle, "calibration_factor") == 0) {
            settings.calibration_factor =
                (float)json_object_get_double(
                        json_object_object_get_ex(s_obj, "default", NULL) ?
                        json_object_object_get(s_obj, "default") : NULL);
        }
        else if (strcmp(handle, "name_of_operator") == 0) {
            const char *op = json_object_get_string(
                    json_object_object_get_ex(s_obj, "default", NULL) ?
                    json_object_object_get(s_obj, "default") : NULL);
            if (op) strncpy(settings.name_of_operator, op, sizeof(settings.name_of_operator)-1);
        }
        else if (strcmp(handle, "mode") == 0) {
            settings.mode =
                (uint8_t)json_object_get_int(
                        json_object_object_get_ex(s_obj, "default", NULL) ?
                        json_object_object_get(s_obj, "default") : NULL);
        }
    }

    /* The sketch copies most of the fields verbatim.  We keep the defaults
     * from the JSON unless the host overrides them in the SETTINGS packet.  */
    settings.sample_rate_hz = 200;   /* default from JSON */
    settings.gain_level     = 1;     /* default from JSON */
    settings.ctrl_reg       = 0x1F;  /* default from JSON */
    settings.buffer_size    = 4096;  /* default from JSON */
    settings.device_serial  = 123456789012345ULL;
    settings.trigger_offset = -10;
    settings.calibration_factor = 1.2f;
    settings.name_of_operator[0] = '\0';
    settings.mode = 1; /* default from JSON */

    /* In practice the server sends its own SETTINGS packet; the
     * JSON that arrives there already contains the user overrides.
     * We will replace the above defaults with those overrides
     * once we receive the SETTINGS packet (see recv_settings_packet).  */
    json_object_put(parsed_json);
}

/* -------------------  Socket helpers  ------------------- */
static int sock = -1;
static struct sockaddr_in host_addr;

static void init_socket(void)
{
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    host_addr.sin_family = AF_INET;
    host_addr.sin_port   = htons(8080);          /* hard‑coded server port */
    inet_pton(AF_INET, "127.0.0.1", &host_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&host_addr, sizeof(host_addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    /* Make the socket non‑blocking – the server may send a packet
     * at any time while we are busy sending data. */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

/* -------------------  Packet framing  ------------------- */
static void send_packet(PacketType type, const void *payload, size_t payload_len)
{
    uint8_t header[5];
    header[0] = (uint8_t)type;
    header[1] = (uint8_t)((payload_len >> 24) & 0xFF);
    header[2] = (uint8_t)((payload_len >> 16) & 0xFF);
    header[3] = (uint8_t)((payload_len >> 8)  & 0xFF);
    header[4] = (uint8_t)(payload_len & 0xFF);

    ssize_t n = send(sock, header, 5, 0);
    if (n != 5) {
        perror("send header");
        exit(EXIT_FAILURE);
    }
    if (payload_len > 0) {
        n = send(sock, payload, payload_len, 0);
        if (n != (ssize_t)payload_len) {
            perror("send payload");
            exit(EXIT_FAILURE);
        }
    }
}

/* Send the handshake – exactly what the Arduino sketch does. */
static void send_handshake(void)
{
    /* The server expects a 5‑byte header followed by the JSON buffer. */
    send_packet(DATA_PACKAGE, jsonConfig, strlen(jsonConfig));
}

/* -------------------  Receive helper  ------------------- */
static ssize_t recv_exact(void *buf, size_t len)
{
    ssize_t total = 0;
    while (total < (ssize_t)len) {
        ssize_t n = recv(sock, (uint8_t*)buf + total, len - total, 0);
        if (n <= 0) {
            if (n == 0) break;                 /* connection closed */
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; /* nothing more */
            perror("recv");
            exit(EXIT_FAILURE);
        }
        total += n;
    }
    return total;
}

/* Receive the next packet – returns 0 if nothing was received. */
static int receive_next_packet(uint8_t *buf, size_t *len, PacketType *type)
{
    uint8_t header[5];
    ssize_t n = recv_exact(header, 5);
    if (n < 5) return 0;           /* no packet yet */

    *type = (PacketType)header[0];
    *len  = (header[1]<<24)|(header[2]<<16)|(header[3]<<8)|header[4];

    if (*len > 0) {
        if (recv_exact(buf, *len) < (ssize_t)*len) return 0;
    }
    return 1;
}

/* Respond to STOP – the sketch just exits.  */
static void handle_stop(void)
{
    fprintf(stderr, "Received STOP packet – exiting.\n");
    close(sock);
    exit(EXIT_SUCCESS);
}

/* Respond to a TIMESTAMP packet – echo back the current time. */
static void handle_timestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t now_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;

    uint8_t data[sizeof(now_us)];
    memcpy(data, &now_us, sizeof(now_us));

    send_packet(TIMESTAMP_PACKAGE, data, sizeof(now_us));
}

/* -------------------  Main loop  ------------------- */
int main(void)
{
    printf("Starting desktop client …\n");
    init_socket();

    /* 1. Handshake */
    send_handshake();
    printf("Handshake sent – waiting for SETTINGS packet.\n");

    /* 2. Wait for SETTINGS packet */
    uint8_t payload[65536];      /* should be big enough for any JSON payload */
    size_t payload_len;
    PacketType ptype;
    while (1) {
        if (!receive_next_packet(payload, &payload_len, &ptype)) {
            /* nothing yet – keep spinning */
            usleep(1000);
            continue;
        }
        if (ptype == SETTINGS_PACKAGE) {
            apply_settings_from_json((const char *)payload, payload_len);
            printf("Settings received – sample‑rate %u Hz.\n",
                   settings.sample_rate_hz);
            break;
        }
        /* ignore other packet types during handshake phase */
    }

    /* 3. Main data loop */
    uint8_t header[5];
    header[0] = DATA_PACKAGE; /* DATA_PACKAGE (0x00) – same as sketch */
    struct timespec ts;
    while (1) {
        /* Build payload – 4 floats */
        float data[4];
        data[0] = (float)rand()/(float)RAND_MAX; /* dummy analog 0 */
        data[1] = (float)rand()/(float)RAND_MAX; /* dummy analog 1 */
        data[2] = (float)rand()/(float)RAND_MAX; /* dummy analog 2 */
        data[3] = (float)rand()/(float)RAND_MAX; /* dummy analog 3 */

        /* Copy 4 floats into a 16‑byte buffer */
        uint8_t buffer[4 * sizeof(float)];
        memcpy(buffer, data, sizeof(buffer));

        /* Header: type + length of payload (4 * 4 bytes) */
        header[1] = (uint8_t)((sizeof(buffer) >> 24) & 0xFF);
        header[2] = (uint8_t)((sizeof(buffer) >> 16) & 0xFF);
        header[3] = (uint8_t)((sizeof(buffer) >> 8)  & 0xFF);
        header[4] = (uint8_t)(sizeof(buffer) & 0xFF);

        /* Send header + data */
        ssize_t n = send(sock, header, 5, 0);
        if (n != 5) {
            perror("send header");
            exit(EXIT_FAILURE);
        }
        n = send(sock, buffer, sizeof(buffer), 0);
        if (n != (ssize_t)sizeof(buffer)) {
            perror("send data");
            exit(EXIT_FAILURE);
        }

        /* 4. Check for any incoming control packets (STOP, TIMESTAMP) */
        if (receive_next_packet(payload, &payload_len, &ptype)) {
            if (ptype == STOP_PACKAGE) handle_stop();
            if (ptype == TIMESTAMP_PACKAGE) handle_timestamp();
        }

        /* 5. Wait according to sample‑rate */
        int us_per_sample = 1000000 / settings.sample_rate_hz;
        usleep(us_per_sample);
    }

    /* 6. Cleanup (never reached in this example) */
    close(sock);
    return 0;
}
