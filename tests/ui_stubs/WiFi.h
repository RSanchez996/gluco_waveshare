#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
struct IPAddressStub {
    String toString() { return "192.168.8.249"; }
};
struct WiFiStub {
    int status() { return WL_CONNECTED; }
    int RSSI() { return -51; }
    String SSID() { return "WIFI-RSC-CASA"; }
    IPAddressStub localIP() { return IPAddressStub{}; }
};
inline WiFiStub WiFi;
