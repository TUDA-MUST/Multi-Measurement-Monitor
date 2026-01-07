# Multi-Measurement-Monitor

[![Badge](https://img.shields.io/badge/Built%20w%2F-GTK3.0+-blue)](https://www.gtk.org/)


The Multi-Measurement-Monitor (MMM) provides a simple to use program for various clients to setup and record a measurement. Highlights are

- systemagnostic TCP interface (microcontrollers or PC) for Clients
- client can dynamically request GUI interface to configure settings 
- synchronisation of client times up to xxx
- live monitoring of channels

## First steps

To use the MMM download the working binaries or compile the project towards your target. There are two tested binaries, a version tested for Ubuntu 24.04 under /C_GUI_Host/MultiMeasurementMonitor and a version tested for Windows10 under/C_GUI_Host/WinApp/bin//MultiMeasurementMonitor.exe which requires the dependencies from the WinApp diretory.

Before starting the application create a WiFi hotspot to which the clients can connect. The MMM will broadcast a UDP message reading "SERVER_ALIVE" every few seconds in what it assumes to be the hotspot to be detectable by clients.

After starting the application on Windows a terminal will open in addtion to the main window. There warnings errors and other system messages will be printed. On linux those will only be available if the application is started through a terminal. There you will e.g. see if the application found the IP adress of your device in your hotspot.

The main tab provides the options to start/ stop all Clients currently connected to the monitor. It furthers enables the user to set the GlobalDuration of Measurements, as the application preallocates all samples for that time window (max. recorded #samples = GlobalDuration * sample rate of client).

Upon connecting a client will be assigned a new tab with the settings configured to its specifications. The name of the tab is colour coded to:
- GREEN: client connected and ready
- BLUE: measurement ongoing
- RED: client disconnected, no data collected
- ORANGE: client disconnected, measurement finished data available

---

## Perorming a mesurement



---

## Setting up Clients

Much of the work of interfacing with the MMM is handled by the provided Arduino library (/lib/tcp_client_network.h). Nonetheless there are guidelines a client must follow for a working interaction. 

---

## Interfaces and Configurations

---
