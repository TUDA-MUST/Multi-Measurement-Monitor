# Multi-Measurement-Monitor (MMM)

[![Built with GTK](https://img.shields.io/badge/Built%20w%2F-GTK3.0+-blue)](https://www.gtk.org/)

The **Multi-Measurement-Monitor (MMM)** is a simple-to-use monitoring and synchronization application that allows various clients (microcontrollers or PCs) to configure, perform, and record measurements through a unified interface.

---

## ✨ Key Features

- System-agnostic **TCP interface** for microcontrollers and PCs  
- Clients can dynamically request **GUI configuration interfaces**  
- **Timestamp synchronization** across all clients  
- **Live monitoring** of measurement channels  

---

## 📊 Time Synchronization Example

![Time Synchronization](/src/sync.png)

Drift of raw local times of ESP32-S3 sync clients recording a 50 Hz digital signal after two hours  
(left) vs. synchronized global time through MMM (right).

---

## 🚀 First Steps

### Installation

To use MMM, download the working binaries or compile the project for your target platform.

**Tested binaries:**
- **Ubuntu 24.04:**  
  
- **Windows 10:**   
  > Requires the dependencies located in the `WinApp` directory

---

### Network Setup

Before starting the application:

1. Create or connect to a **Wi-Fi hotspot** that clients can connect to.
2. MMM will periodically broadcast a UDP message containing  
   `"SERVER_ALIVE"` to make itself detectable by clients.
3. Through the broadcast clients can obtain the host IP and connect to the MMM under TCP Port 8080 via a Handshake package.

---

### Running the Application

- **Windows:**  
  A terminal opens alongside the main window, displaying warnings, errors, and system messages.
- **Linux:**  
  Messages are only visible when starting MMM from a terminal.

This output also confirms whether MMM detected the correct IP address within the hotspot network for broadcasting.

---

## 🖥️ Main Interface Overview

The **Main Tab** allows you to:

- Start / stop all connected clients
- Set the `GlobalDuration` of measurements  
  > MMM preallocates all samples for this time window  
  > `Max samples = GlobalDuration × client sample rate`

![Main Tab](/src/Main_tab.png)

---

### Client Tabs & Status Colors

Each connected client is assigned its own tab. The tab color indicates its status:

- 🟢 **GREEN**: client connected and ready
- 🔵 **BLUE**: measurement ongoing
- 🔴 **RED**: client disconnected, no data collected
- 🟠 **ORANGE**: client disconnected, measurement finished, data available



---

## 🧪 Performing a Measurement

Once connected, each client tab contains:

- **Left panel:** Client-specific settings  
- **Center:** Live monitor  
- **Right panel:** Standard actions  

Available actions:
- Individual **Start / Stop**
- **Load JSON Settings**
- Use default or modified settings
- **Export measurement data** as `.csv`
- **Select plotted Channels** in live monitor

![Client Tab](/src/sync_client.png)


**Data expectations:**
- Measurement values: `float`
- Timestamps: microseconds (`float`)


---

## 🔧 Setting Up Clients

Client-side interaction is largely handled by the provided Arduino library:

[`/lib/tcp_client_network.h`](https://github.com/TUDA-MUST/Multi-Measurement-Monitor/tree/main/lib)


### Required Client Workflow

1. Call `discoverHostUDP()` to find the MMM host via UDP broadcast  
2. Call `connectToHost()` to connect on **TCP port 8080**  
3. Call `sendHandshakePackage()` with a settings-config JSON  
4. Wait for settings, apply them, then start measuring   
5. Send data and handle incoming packages:
   - Discard `PING` packages
   - Echo `TIMESTAMP` packages with local time in µs (`float`) appended
   - Stop measuring on `STOP` packages
6. Reset the connection after measurement completion

Read through the `SyncClient` example for further explanation.

The lib, as well as the SyncClient example are both written in the Arduino IDE for an ESP32-S3 using 2.4GHz WiFi to connect to a Hotspot running the MMM.

---

## 🔌 Interfaces and Configurations

### 📄 JSON Files

Two types of JSON files are used:

#### 1. Handshake Configuration (Client → MMM)

Sent as payload of the `HANDSHAKE` package. Contains:

- `gui_handle` – Client name in GUI and CSV exports  
- `float_number` – Number of floats per sample  
  > Includes timestamp (e.g. `4 = 3 channels + 1 timestamp`)  
- `settings` – Array describing GUI settings

#### 2. Settings JSON (MMM → Client)

Used when:
- Clicking **Load JSON Settings…**
- Sending a `SETTINGS` package from MMM to client to start a measurement

Contains:
- `microcontroller_handle : value` pairs

📁 Examples: `/json_examples/`

---

### ⚙️ Settings Configuration

Each setting is one of two types:

- **combo** – Dropdown with selectable options  
- **entry** – Free-form input field  

#### Required Fields

- `gui_handle` – Label shown in MMM GUI  
- `microcontroller_handle` – Internal identifier  
- `type` – `combo` or `entry`  
- `datatype` – Data type of the setting  
- `default` – Default value  
- `options` – Required only for `combo`, array of options for dropdown menu  

#### Supported Datatypes

- `uint8`
- `uint8_hex` (displayed as hex, e.g. registers)
- `uint32`
- `uint64`
- `int32`
- `char[256]`
- `float`

⚠️ **Mandatory setting:**  
A numerical setting with the microcontroller handle  
`sample_rate_hz` is required to allocate buffers and start measuring.

---

## 📤 CSV Export Format

Exported `.csv` files follow a fixed structure:

1. Client name and timestamp of export 
2. Timestamp synchronization-echo log  
3. Header row:
   - `CHAN1 … CHANX`
   - `LOCAL_TIME`
   - `GLOBAL_TIME`
4. Measurement data until EOF  

### Example
```
ESP_black - 2025-12-10 17:10:34 UTC

Sent Timesync request at 2025-12-10 16:50:42.046 UTC, received echo at 2025-12-10 16:50:42.071 UTC, local time 21855958,0

Sent Timesync request at 2025-12-10 16:50:47.042 UTC, received echo at 2025-12-10 16:50:47.066 UTC, local time 26850960,0

CHAN1;LOCAL_TIME;GLOBAL_TIME

150,695969;17264514,000000;2025-12-10 16:50:37.468385 UTC

153,113556;17265606,000000;2025-12-10 16:50:37.469477 UTC
```


---

## 📡 TCP Packages


Communication between the MMM host and its clients is performed using custom TCP packages.
Each package follows a fixed binary layout.

---

### General Package Layout

Every TCP package is sent in the following order:

1. **package_type** (`uint8`)  
   Identifies the type of the package.

2. **package_size** (`uint32`)  
   Size of the payload in bytes (payload only, header excluded).

3. **package payload** (`unsigned char *`)  
   Raw payload data whose structure depends on the package type.


### Package Types


PING_PACKAGE (0)
- Direction: MMM -> Client
- Purpose: Keep-alive probing
- Payload: None (16-bytes zeroed)
- Client behavior:
  Discard the package.

---

STOP_PACKAGE (1)
- Direction: MMM -> Client
- Purpose: Stop an active measurement
- Payload: None
- Client behavior:
  - Stop sampling immediately
  - Finalize measurement
  - Prepare for connection reset

---

SETTINGS_PACKAGE (2)
- Direction: MMM -> Client
- Purpose: Transfer measurement settings and start a measurement
- Payload type: JSON encoded settings values
- Payload content:
  Plain settings JSON containing key–value pairs of the form:

  microcontroller_handle : value

- Client behavior:
  - Parse the JSON
  - Apply all received settings
  - Start measuring

---

DATA_PACKAGE (3)
- Direction: Client -> MMM
- Purpose: Transfer measurement samples
- Payload type: single or multiple binary float arrays
- Payload layout:

  value_1, value_2, ..., value_N

  value_1, value_2, ..., value_N 

  ...

  where:
  - All values are of type float
  - The last value is always the local timestamp in microseconds
  - N must match the float_number specified in the handshake

- MMM behavior:
  Samples are written into a preallocated measurement buffer.

---

TIMESTAMP_PACKAGE (4)
- Direction: MMM -> Client -> MMM (echo)
- Purpose: Time synchronization
- Payload (MMM -> Client): binary timestamp of MMM
- Payload (Client -> MMM):
  Single float representing the client’s local time in microseconds appended to binary timestamp of MMM
- Client behavior:
  - Append local timestamp
  - Echo the package back to the MMM

- MMM usage:
  Timestamp echos are used to compute a global time model and compensate clock drift.

---

HANDSHAKE_PACKAGE (5)
- Direction: Client -> MMM
- Purpose: Initial client registration
- Payload type: JSON encoded settings config
- Payload content:
  - GUI handle and metadata
  - Number of floats per sample
  - Settings configuration description

- MMM behavior:
  - Creates a new client tab
  - Builds the settings UI


---

<!--

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


-->