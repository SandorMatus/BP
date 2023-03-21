# BP
the answer is not hut in the woods
| Supported Targets | ESP32 | ESP32-C3 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- |

# HOW TO RUN

Put any project folder into your IDF repo and launch in VS code or whatever. Don't forget to add components to your idf !! If you are not using OCPP don't add mongoose and AO because they require special header file in the project folder.

## Functionality

Every project works as stated in README, but the protocol projects are WIP and need testing (stack tracing, memory testing).

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
