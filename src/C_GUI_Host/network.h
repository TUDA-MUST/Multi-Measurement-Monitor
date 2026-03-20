#ifndef NETWORK_H
#define NETWORK_H

#include <gio/gio.h>
#include <stdint.h>
#include "client.h"

// --------------------
// TCP Package types:
// PING PACKAGE:
//  A package sent by the Client monitor to Clients to see if the 
//  TCP connection is still alive. Can be discarded by Client. Data
//  carried by the PING Package should be zero-bytes.
//  -> sent by ClientMonitor
// STOP PACKAGE:
//  Will only be sent after a SETTINGS PACKAGE. Client Monitor stops 
//  measurement thread and expects client to stop measuring and reset 
//  the connection. Package size is zero.
//  -> sent by ClientMonitor
// SETTINGS PACKAGE:
//  Will send the settings as a json as requested by the client during
//  the handshake. Look at an example for further details. Sending of 
//  the settings will initiate a measurement.
//  -> sent by ClientMonitor
// DATA PACKAGE:
//  Are expected to contain a data stream on n floats per sample (specified in
//  handshake). Will only be handled after SETTINGS PACKAGE was sent and before 
//  a STOP PACKAGE was sent
//  -> sent by Client
// TIMESTAMP PACKAGE:
//  Will be sent by the ClientMonitor every few seconds during measurement to
//  synchronize timestamps over multiple clients. The Client expects the Client
//  to echo the received timestamp data, but append its local time in µs to the
//  data as a float
//  -> sent by ClientMonitor, echoed by Client
// HANDSHAKE PACKAGE:
//  Sent by the Client directly after TCP connection was established. Should contain
//  a json, that defines the properties of the client and its requested GUI.
//  -> sent by Client, answered by a single PING_PACKAGE in TCP stream by ClientMonitor
//     (PING_PACKAGE neccesary for Windows to reliably handle Clients)
// --------------------
typedef enum {
    PING_PACKAGE      = 0,
    STOP_PACKAGE      = 1,
    SETTINGS_PACKAGE  = 2,
    DATA_PACKAGE      = 3,
    TIMESTAMP_PACKAGE = 4,
    HANDSHAKE_PACKAGE = 5
} PackageType_e;

typedef uint8_t PackageType8_t; // storage type

// --------------------
// TCP Package struct
// --------------------
typedef struct {
    PackageType8_t package_type; // 1 byte storage
    uint32_t package_size;
    unsigned char *package;
} TcpPackage;




// --------------------
// TCP Helper functions
// --------------------
gboolean read_tcp_package(GSocket *sock, TcpPackage *pkg, guint32 timeout_ms);
void free_tcp_package(TcpPackage *pkg);
gboolean get_next_tcp_package(GSocket *sock, TcpPackage *pkg, guint32 timeout_ms);
static gboolean read_bytes(GSocket *sock, guint8 *buffer, gsize n, guint32 timeout_ms);
void send_settings_to_client(ClientInfo *info);
void disconnect_client(ClientInfo *info);
gboolean monitor_client_connection(gpointer user_data);
void update_client_visual_state(ClientInfo *info);
#endif // NETWORK_H
