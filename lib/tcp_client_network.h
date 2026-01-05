#ifndef TCP_CLIENT_NETWORK_H
#define TCP_CLIENT_NETWORK_H

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Arduino.h>

// ---- Package Types ----

typedef enum {
    PING_PACKAGE      = 0,
    STOP_PACKAGE      = 1,
    SETTINGS_PACKAGE  = 2,
    DATA_PACKAGE      = 3,
    TIMESTAMP_PACKAGE = 4,
    HANDSHAKE_PACKAGE = 5
} PackageType_e;

typedef uint8_t PackageType8_t;

typedef struct {
    PackageType8_t package_type;
    uint32_t package_size;
    unsigned char *package;
} TcpPackage;

// ---- Networking API ----

String waitForLine();

void setWifiCredentials();
void setClientHandshakePreferences();

void sendHandshakePackage(WiFiClient &client, const char* json_config);
void respondWithTimestampPackage(WiFiClient &client, TcpPackage &recvPkg);

bool readBytes(WiFiClient &client, uint8_t *buffer, size_t n, uint32_t timeoutMs = 2000);
bool readTcpPackage(WiFiClient &client, TcpPackage &pkg);
void freeTcpPackage(TcpPackage &pkg);
bool getNextTcpPackage(WiFiClient &client, TcpPackage &pkg);

bool wifiConnectAndAllowOverride(String &clientNameOut);
IPAddress discoverHostUDP();
WiFiClient connectToHost(IPAddress host, int port);


#endif
