#include "tcp_client_network.h"

// -------------------------
//   WAIT FOR SERIAL INPUT
// -------------------------
String waitForLine() {
    String line = "";
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\r' || c == '\n') {
                if (line.length() > 0) return line;
            } else {
                line += c;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/*
// ------------------------------
//   WIFI + CLIENT CONFIG               <--- deprecated thorugh json
// ------------------------------
void setClientHandshakePreferences() {
    Serial.println("\n=== Configure Handshake ===");

    Serial.print("Enter client name: ");
    String nm = waitForLine();

    Preferences prefs;
    prefs.begin("client", false);
    prefs.putString("client_name", nm);
    prefs.end();
}  

*/

void setWifiCredentials() {
    Serial.println("\n=== WiFi Setup Mode ===");

    Serial.println("Enter new SSID:");
    String newSSID = waitForLine();

    Serial.println("Enter new password:");
    String newPASS = waitForLine();

    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putString("ssid", newSSID);
    prefs.putString("pass", newPASS);
    prefs.end();

    //setClientHandshakePreferences();

    Serial.println("\nCredentials saved. Rebooting...");
    delay(1000);
    ESP.restart();
}

// ------------------------------
//   TCP PACKAGE I/O
// ------------------------------
bool readBytes(WiFiClient &client, uint8_t *buffer, size_t n, uint32_t timeoutMs) {
    uint32_t start = millis();
    size_t readSoFar = 0;

    while (readSoFar < n) {
        if (client.available()) {
            int c = client.read();
            if (c >= 0) buffer[readSoFar++] = (uint8_t)c;
        }
        if (millis() - start > timeoutMs) return false;
        vTaskDelay(1);
    }
    return true;
}








bool readTcpPackage(WiFiClient &client, TcpPackage &pkg) {
    uint8_t header[5];
    if (!readBytes(client, header, 5)) return false;

    pkg.package_type = header[0];
    pkg.package_size = ((uint32_t)header[1] << 24) |
                       ((uint32_t)header[2] << 16) |
                       ((uint32_t)header[3] << 8)  |
                       ((uint32_t)header[4]);

    if (pkg.package_size > 0) {
        pkg.package = (unsigned char*) malloc(pkg.package_size);
        if (!pkg.package) return false;

        if (!readBytes(client, pkg.package, pkg.package_size)) {
            free(pkg.package);
            pkg.package = nullptr;
            return false;
        }
    } else {
        pkg.package = nullptr;
    }
    return true;
}








void freeTcpPackage(TcpPackage &pkg) {
    if (pkg.package) free(pkg.package);
    pkg.package = nullptr;
}





bool getNextTcpPackage(WiFiClient &client, TcpPackage &pkg) {
    return readTcpPackage(client, pkg);
}





// ------------------------------
//   HANDSHAKE + TIMESTAMP
// ------------------------------
void sendHandshakePackage(WiFiClient &client, const char* json_config) {
    // JSON lives in flash, but is memory-mapped on ESP32
    const char* payload = json_config;
    //Serial.print(payload);
    // Payload size = JSON length WITHOUT the null terminator
    uint32_t payload_size = strlen(payload);

    uint8_t header[5] = {
        HANDSHAKE_PACKAGE,
        (uint8_t)(payload_size >> 24),
        (uint8_t)(payload_size >> 16),
        (uint8_t)(payload_size >> 8),
        (uint8_t)(payload_size >> 0)
    };

    client.write(header, sizeof(header));
    client.write((const uint8_t*)payload, payload_size);
    client.flush();
}


void respondWithTimestampPackage(WiFiClient &client, TcpPackage &recvPkg) {
    uint32_t oldSize = recvPkg.package_size;
    uint32_t newSize = oldSize + 4;

    uint8_t* frame = (uint8_t*) malloc(5 + newSize);
    if (!frame) return;

    frame[0] = TIMESTAMP_PACKAGE;
    frame[1] = (newSize >> 24) & 0xFF;
    frame[2] = (newSize >> 16) & 0xFF;
    frame[3] = (newSize >> 8)  & 0xFF;
    frame[4] = (newSize >> 0)  & 0xFF;

    memcpy(frame + 5, recvPkg.package, oldSize);

    union { float f; uint8_t b[4]; } u;
    u.f = (float) esp_timer_get_time();
    memcpy(frame + 5 + oldSize, u.b, 4);

    client.write(frame, 5 + newSize);
    free(frame);
}


bool wifiConnectAndAllowOverride(String &clientNameOut) {
    Preferences prefs;

    // ---- Load WiFi ----
    prefs.begin("wifi", false);
    String ssid  = prefs.getString("ssid", "MultiFerroSpot");
    String pass  = prefs.getString("pass", "12345qwe");
    prefs.end();

    // ---- Load Client Name ----
    prefs.begin("client", false);
    clientNameOut = prefs.getString("client_name", "ESP32");
    uint8_t float_count = prefs.getUInt("float_count", 255);
    prefs.end();

    Serial.printf("\nPress ENTER to change WiFi credentials before connection.\n");
    Serial.printf("Connecting to SSID \"%s\" with password \"%s\" as client...\n",
        ssid.c_str(), pass.c_str()
    );

    WiFi.begin(ssid.c_str(), pass.c_str());

    // ---- Wait for WiFi connection with override option ----
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(300);
        Serial.print(".");

        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                setWifiCredentials();   // user reconfigures WiFi settings
            }
        }
    }

    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}






IPAddress discoverHostUDP() {
    WiFiUDP udp;
    udp.begin(5000);

    Serial.println("Waiting for host broadcast…");

    char buf[64];
    IPAddress host;

    while (true) {
        int len = udp.parsePacket();
        if (len > 0) {
            int n = udp.read(buf, sizeof(buf) - 1);
            buf[n] = '\0';

            if (strcmp(buf, "SERVER_ALIVE") == 0) {
                host = udp.remoteIP();
                Serial.printf("Discovered host: %s\n",
                               host.toString().c_str());
                return host;
            }
        }
        vTaskDelay(100);
    }
}




WiFiClient connectToHost(IPAddress host, int port) {
    WiFiClient client;

    Serial.printf("Connecting to host %s:%d…\n",
                  host.toString().c_str(), port);

    while (!client.connect(host, port)) {
        Serial.println("Connection failed, retrying...");
        vTaskDelay(1000);
    }

    Serial.println("Connected to host!");
    return client; // returns a connected client
}
