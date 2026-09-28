#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <atomic>
#include "core.hpp"

constexpr const char *APP_VERSION = "0.8.3";
constexpr size_t MAX_CONNECTIONS = 12;
enum class PortalMode : uint8_t { Off, WifiAccessPoint, WaitingForWifi, LocalNetwork };
struct Config {
    String ssid, wifiPass;
    String libreUser, librePass, libreRegion = "eu", libreVersion = "5.1.1", patientId;
    String patientName, city, timezone = "Europe/Madrid";
    float latitude = 0, longitude = 0;
    bool locationSet = false;
};
struct ConnectionChoice { char id[100]{}; char name[100]{}; };
struct PatientSelection { char id[100]{}; char name[100]{}; };
struct WeatherHour { int64_t epoch = 0; float temperature = 0; int rain = 0; int code = 0; bool isDay = true; };
struct WeatherDay { int64_t epoch = 0; float high = 0, low = 0; int rain = 0; int code = 0; };
struct AppState {
    gluco::Point points[gluco::kMaxPoints]{};
    size_t pointCount = 0;
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
extern Config config;
extern std::atomic<uint32_t> configRevision;
extern AppState *sharedState;
extern SemaphoreHandle_t stateMutex;
extern SemaphoreHandle_t storageMutex;
extern SemaphoreHandle_t httpMutex;
extern QueueHandle_t patientQueue;
void configLoad();
bool configSelectPatient(const char *id, const char *name);
bool requestPatientSelection(const char *id, const char *name);
bool configNeedsSetup();
void portalStart();
void portalLoop();
void portalStop();
bool portalActive();
PortalMode portalMode();
String portalSsid();
String portalPassword();
String portalUrl();
void displayInit();
void displayWake();
void displaySleep();
bool displayIsSleeping();
void uiInit();
void uiTick();
void uiShowSetup();
void uiShowHome();
void networkStart();
const char *weatherText(int code);
void markPlannedRestart();
const char *appResetReason();
uint32_t appBootCount();
int appWorkerCore();
void appDiagnosticStage(const char *stage);
const char *appPreviousFaultStage(unsigned index);
