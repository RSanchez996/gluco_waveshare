#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
struct WiFiStub {
    int status() { return WL_CONNECTED; }
    int RSSI() { return -51; }
    String SSID() { return "Wi-Fi"; }
};
inline WiFiStub WiFi;
