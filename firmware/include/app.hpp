#pragma once
#include "core.hpp"
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <lvgl.h>

constexpr const char *APP_VERSION = "2.0.0";
constexpr size_t MAX_CONNECTIONS = 12;
constexpr size_t MAX_EXTRA_WIFI = 4;
enum class PortalMode : uint8_t { Off, WifiAccessPoint, WaitingForWifi, LocalNetwork };
struct WifiEntry {
    String ssid, password;
};
struct WifiScanEntry {
    char ssid[33]{};
    int rssi = -100;
    bool secure = true;
};
struct LocationChoice {
    char name[140]{};
    float latitude = 0, longitude = 0;
    char timezone[32]{};
};
enum class SetupJob : uint8_t { Idle, Running, Ready, Failed };
struct Config {
    String ssid, wifiPass;
    WifiEntry extraWifi[MAX_EXTRA_WIFI];
    size_t extraWifiCount = 0;
    String libreUser, librePass, libreRegion = "eu", libreVersion = "5.1.1", patientId;
    String patientName, city, timezone = "Europe/Madrid";
    float latitude = 0, longitude = 0;
    bool locationSet = false;
};
struct ConnectionChoice {
    char id[100]{};
    char name[100]{};
};
struct PatientSelection {
    char id[100]{};
    char name[100]{};
};
struct WeatherHour {
    int64_t epoch = 0;
    float temperature = 0;
    int rain = 0;
    int code = 0;
    bool isDay = true;
};
struct WeatherDay {
    int64_t epoch = 0;
    float high = 0, low = 0;
    int rain = 0;
    int code = 0;
};
struct AppState {
    gluco::Point points[gluco::kMaxPoints]{};
    size_t pointCount = 0;
    gluco::Point current{};
    bool currentValid = false;
    char direction[24]{};
    char glucoseError[160]{};
    int64_t glucoseFetched = 0;
    uint32_t glucoseRequestStartedMs = 0;
    char activePatientId[100]{};
    ConnectionChoice connections[MAX_CONNECTIONS]{};
    size_t connectionCount = 0;
    int64_t connectionsFetched = 0;
    char connectionsError[160]{};
    bool weatherValid = false;
    float temperature = 0, apparent = 0, high = 0, low = 0, wind = 0;
    int weatherCode = 0;
    bool weatherIsDay = true;
    char weatherError[140]{};
    WeatherHour hours[6]{};
    size_t hourCount = 0;
    WeatherDay days[4]{};
    size_t dayCount = 0;
    int64_t weatherFetched = 0;
};
// No shared mutable Strings: readers own a copy, the data worker owns writes.
Config configSnapshot();
void configPublish(const Config &next);
void configRuntimeInit();
bool configService();
void portalRuntimeStart();
void uiRequestHome();
void displayRequestWake();
void appRecordDataTask(TaskHandle_t data);
void appDiagnosticsJson(class Print &out);
extern std::atomic<uint32_t> configRevision;
extern AppState *sharedState;
extern SemaphoreHandle_t stateMutex;
extern SemaphoreHandle_t storageMutex;
extern SemaphoreHandle_t httpMutex;
extern QueueHandle_t patientQueue;
extern std::atomic<bool> configUiActive;
void configLoad();
bool configBusy();
bool configQueueFull(const Config &next, String &error, bool networksOnly = false);
bool configQueueReset(String &error);
String deviceLibreRegion();
bool configSelectPatient(const char *id, const char *name);
bool requestPatientSelection(const char *id, const char *name);
bool configNeedsSetup();
const char *configStorageState();
void portalStart();
void portalLoop();
void portalStop();
bool portalActive();
PortalMode portalMode();
String portalSsid();
String portalPassword();
String portalUrl();
bool deviceWifiScan(String &error);
SetupJob deviceWifiResults(WifiScanEntry *out, size_t capacity, size_t &count, String &error);
bool deviceSaveWifi(const String &ssid, const String &password, bool connectNow, String &error);
bool deviceRemoveWifi(const String &ssid, String &error);
bool deviceGeocode(const String &query, String &error);
SetupJob deviceGeocodeResults(LocationChoice *out, size_t capacity, size_t &count, String &error);
bool deviceSaveLocation(const LocationChoice &choice, String &error);
bool deviceLibreLogin(const String &user, const String &password, const String &region,
                      String &error);
SetupJob deviceLibreResults(ConnectionChoice *out, size_t capacity, size_t &count, String &error);
bool deviceSaveLibreUser(const ConnectionChoice &choice, String &error);
bool deviceQueueWifi(const String &ssid, const String &password, bool connectNow, String &error);
bool deviceQueueRemoveWifi(const String &ssid, String &error);
bool deviceQueueLocation(const LocationChoice &choice, String &error);
bool deviceQueueLibreUser(const ConnectionChoice &choice, String &error);
SetupJob deviceMutationResult(String &error);
void displayInit();
void displayWake();
void displaySleep();
bool displayIsSleeping();
void displayResync();
void displayRequestResync();
void displayServiceResync();
void uiInit();
void uiTick();
void uiShowSetup();
void uiShowHome();
void uiClearGraphSelection();
void networkStart();
const char *weatherText(int code);
void markPlannedRestart();
const char *appResetReason();
uint32_t appBootCount();
int appWorkerCore();
void appDiagnosticStage(const char *stage);
const char *appPreviousFaultStage(unsigned index);
