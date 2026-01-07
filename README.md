# Multi-Measurement-Monitor

[![Badge](https://img.shields.io/badge/Built%20w%2F-GTK3.0+-blue)](https://www.gtk.org/)


The Multi-Measurement-Monitor (MMM) provides a simple to use program for various clients to setup and record a measurement. Highlights are

- systemagnostic TCP interface (microcontrollers or PC) for clients
- clients can dynamically request GUI interface to configure settings 
- synchronisation of client timestamps
- live monitoring of channels

![Chain](/src/sync.png)
Drift of of raw local times of ESP32-S3 sync clients recording a 50Hz digital singal after two hours (left) vs synchronized global time through MMM (right).

## First steps

To use the MMM download the working binaries or compile the project towards your target. There are two tested binaries, a version tested for Ubuntu 24.04 under /C_GUI_Host/MultiMeasurementMonitor and a version tested for Windows10 under/C_GUI_Host/WinApp/bin//MultiMeasurementMonitor.exe which requires the dependencies from the WinApp diretory.

Before starting the application create a WiFi hotspot to which the clients can connect. The MMM will broadcast a UDP message reading "SERVER_ALIVE" every few seconds in what it assumes to be the hotspot to be detectable by clients.

After starting the application on Windows a terminal will open in addtion to the main window. There warnings errors and other system messages will be printed. On linux those will only be available if the application is started through a terminal. There you will e.g. see if the application found the IP adress of your device in your hotspot.

The main tab provides the options to start/ stop all Clients currently connected to the monitor. It furthers enables the user to set the GlobalDuration of Measurements, as the application preallocates all samples for that time window (max. recorded #samples = GlobalDuration * sample rate of client).

![Chain](/src/Main_tab.png)

Upon connecting a client will be assigned a new tab with the settings configured to its specifications. The name of the tab is colour coded to:
- GREEN: client connected and ready
- BLUE: measurement ongoing
- RED: client disconnected, no data collected
- ORANGE: client disconnected, measurement finished data available

The MMM expects all measurement data it is send to be of datatype "float" and timestamps from clients to be in microsenconds of datatype "float".

---

## Perorming a mesurement

After connecting a client will have a tab set up with its settings on the left panel a live monitor in the middle and the standard actions on the right.

Those are individual start/ stop buttons as well as a load json button to load a settings file for the client. Other options are using the default settings values reported upon connection or altering settings in the client tab. The last button exports a recorded measurement into a .csv file for later use.

![Chain](/src/sync_client.png)

---

## Setting up Clients

Much of the work of interfacing with the MMM is handled by the provided Arduino library (/lib/tcp_client_network.h). Nonetheless there are guidelines a client must follow for a working interaction. 

1. Use dicoverHostUDP() to get the IP-Adress of the Host in the network through the UDP-broadcast
2. Use conntectToHost() to connect to to the MMM-Host on Port 8080.
3. Use sendHandshakePackage() to send the settings-config json required to the MMM in order to build the client tab
4. wait for settings, apply them and then start measuring (see SyncClient).
5. Send data and handle incoming packages (Discard PING Packages, echo TIMESTAMP Packages with appended local time in µs as float, stop measuring on STOP Package).
6. Reset connection after a measurement completes.


---

## Interfaces and Configurations



### JSON-Files

Two types of json files are involved in configuring clients. The first one is the config file the client sends to the MMM as payload in the HANDSHAKE Package. The config json contains three fields:

- gui_handle: name of the client in the GUI and in saved .csv files
- float_number: The number the of floats a client will send per sample (incl. timestamp, e.g. 4 = 3 channels + 1 timestamp)
- settings: an array of settings containting the settings configuration for the GUI.

The other type of json are plain settings. Those are used either upon pressing "Load JSON Settings..." or as payload in a SETTINGS packagae the MMM sends the client to start a measurement. Those files simply contain the microcontroller_handle of a setting as field name alongside a value for that field.

For examples look into /json_examples/.

### Settings Configuration

Every setting can be on of two types: combo or entry. A combo needs an array of choosable options and creates a dropdown to choose from, while an entry doesn't need options in the config file and is simple an input field.

Each setting needs the following fields

- gui_handle: String displayed in the GUI of the MMM
- microcontroller_handle: background handle for setting
- type: combo or entry
- datatype: datatype of setting
- default: default value for setting upon intialization
- options: field only required for for combos 

Settings can be of datatype:

- "uint8"
- "uint8_hex" 
   -> same as uint8, but displayed as hex, for configurations (e.g registers)
- "uint32"
- "uint64"
- "int32"
- "char[256]"
- "float"

The MMM always needs a numerical setting with the microcontroller handle "sample_rate_hz" in order to allocate a measurement buffer and start measuring.


### CSV-Export

Measurement data can be exported as a .csv file. Those files are built in a set pattern:

1. The first line contains the name of the client and the time the .csv file was written.
2. The following lines contain all timestamp echos for synchronisation.
3. Header line for CHAN1 to CHANX (depending on how many channels the client has), as well as the LOCAL_TIME (timestamps from client) and GLOBAL_TIME (fitted time based on syncing echos).
4. Actual measurement data until end of file.

E.g:

ESP_black - 2025-12-10 17:10:34 UTC
Sent Timesync request at 2025-12-10 16:50:42.046 UTC, received echo at 2025-12-10 16:50:42.071 UTC, local time 21855958,0
Sent Timesync request at 2025-12-10 16:50:47.042 UTC, received echo at 2025-12-10 16:50:47.066 UTC, local time 26850960,0
CHAN1;LOCAL_TIME;GLOBAL_TIME
150,695969;17264514,000000;2025-12-10 16:50:37.468385 UTC
153,113556;17265606,000000;2025-12-10 16:50:37.469477 UTC


### TCP-Packages

TCP Packages are of different types depending on the use case. Each package starts with one byte of package_type and for bytes of package_size (only size of payload *package) and the *package payload of varying size. 

typedef enum {
    PING_PACKAGE      = 0,
    STOP_PACKAGE      = 1,
    SETTINGS_PACKAGE  = 2,
    DATA_PACKAGE      = 3,
    TIMESTAMP_PACKAGE = 4,
    HANDSHAKE_PACKAGE = 5
} PackageType_e;

typedef struct {
    PackageType8_t package_type;
    uint32_t package_size;
    unsigned char *package;
} TcpPackage;


---
