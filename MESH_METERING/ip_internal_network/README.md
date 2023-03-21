| Supported Targets | ESP32 | ESP32-C3 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- |

# Mesh IP Metering

This project is for limit testing and real life scenario testing. Nodes create mesh network and connect to a stated AP, then the routes can be test with L3 or higher network tests. 

## Functionality

Only for testing...

### Hardware Required

This example can be executed on any platform board, the only required interface is WiFi and connection to internet.

### Configure the project

Open the project configuration menu (`idf.py menuconfig`) to configure the mesh network channel, router SSID, router password and mesh softAP settings.

### Build and Flash

Build the project and flash it to multiple boards forming a mesh network, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

## Test results
Test results will be shown here..
