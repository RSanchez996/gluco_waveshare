// Runs the production UI and LVGL on a host framebuffer; no hardware emulation.
#include "app.hpp"
#include "fonts.hpp"
#include <cmath>
#include <fstream>
#include <vector>
std::atomic<uint32_t> configRevision{0};
std::atomic<bool> configUiActive{false};
AppState data;
AppState *sharedState = &data;
SemaphoreHandle_t stateMutex = nullptr, storageMutex = nullptr, httpMutex = nullptr;
QueueHandle_t patientQueue = nullptr;
const char *weatherText(int) { return "Nubes y claros"; }
Config configSnapshot() {
    Config c;
    c.ssid = "Wi-Fi";
    c.libreUser = "usuario";
    c.patientId = "demo";
    c.patientName = "Usuario";
    c.city = "Madrid";
    c.locationSet = true;
    return c;
}
const char *configStorageState() { return "ready"; }
const char *appResetReason() { return "Encendido"; }
uint32_t appBootCount() { return 1; }
const char *appPreviousFaultStage(unsigned) { return ""; }
bool requestPatientSelection(const char *, const char *) { return true; }
void displayWake() {}
void displaySleep() {}
bool displayIsSleeping() { return false; }
void markPlannedRestart() {}
void portalStart() {}
void portalStop() {}
bool portalActive() { return false; }
PortalMode portalMode() { return PortalMode::Off; }
String portalSsid() { return "GlucoWave-DEMO"; }
String portalPassword() { return "demo"; }
String portalUrl() { return "http://192.168.4.1/"; }
bool deviceWifiScan(String &) { return false; }
bool deviceGeocode(const String &, String &) { return false; }
bool deviceLibreLogin(const String &, const String &, const String &, String &) { return false; }
bool deviceQueueWifi(const String &, const String &, bool, String &) { return false; }
bool deviceQueueRemoveWifi(const String &, String &) { return false; }
bool deviceQueueLocation(const LocationChoice &, String &) { return false; }
bool deviceQueueLibreUser(const ConnectionChoice &, String &) { return false; }
SetupJob deviceMutationResult(String &) { return SetupJob::Idle; }
SetupJob deviceWifiResults(WifiScanEntry *, size_t, size_t &, String &) { return SetupJob::Idle; }
SetupJob deviceGeocodeResults(LocationChoice *, size_t, size_t &, String &) {
    return SetupJob::Idle;
}
SetupJob deviceLibreResults(ConnectionChoice *, size_t, size_t &, String &) {
    return SetupJob::Idle;
}
static uint32_t tick = 0;
extern "C" uint32_t millis() { return tick; }
extern "C" void delay(uint32_t ms) { tick += ms; }
std::vector<lv_color_t> screen(800 * 480);
void flush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *p) {
    for (int y = a->y1; y <= a->y2; ++y)
        for (int x = a->x1; x <= a->x2; ++x)
            screen[y * 800 + x] = *p++;
    lv_disp_flush_ready(drv);
}
int main(int argc, char **argv) {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    auto now = time(nullptr);
    data.currentValid = true;
    data.current = {now, 126};
    data.glucoseFetched = now;
    data.weatherValid = true;
    data.temperature = 22;
    data.high = 25;
    data.low = 17;
    data.weatherCode = 2;
    data.weatherIsDay = true;
    data.pointCount = 144;
    for (size_t i = 0; i < data.pointCount; ++i)
        data.points[i] = {now - 12 * 3600 + int64_t(i) * 300,
                          int16_t(126 + 40 * sin(i / 13.0) + 10 * sin(i / 3.0))};
    lv_init();
    fonts::init();
    static lv_color_t pixels[800 * 12];
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, nullptr, 800 * 12);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = 800;
    drv.ver_res = 480;
    drv.flush_cb = flush;
    drv.draw_buf = &buffer;
    lv_disp_drv_register(&drv);
    uiInit();
    uiTick();
    lv_refr_now(nullptr);
    if (argc > 2) {
        uiShowSetup();
        uiTick();
        lv_refr_now(nullptr);
    }
    std::ofstream out(argc > 1 ? argv[1] : "ui.ppm", std::ios::binary);
    out << "P6\n800 480\n255\n";
    for (auto p : screen) {
        auto c = lv_color_to32(p);
        char rgb[3] = {char(c >> 16), char(c >> 8), char(c)};
        out.write(rgb, 3);
    }
    return out ? 0 : 1;
}
