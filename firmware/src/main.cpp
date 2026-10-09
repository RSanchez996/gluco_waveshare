#include "app.hpp"
#include "runtime.hpp"
#include "sdk_guard.hpp"
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <nvs_flash.h>

AppState *sharedState = nullptr;
SemaphoreHandle_t stateMutex = nullptr, storageMutex = nullptr, httpMutex = nullptr;
QueueHandle_t patientQueue = nullptr;
namespace {
std::atomic<TaskHandle_t> guiHandle{nullptr}, dataHandle{nullptr};
SemaphoreHandle_t displayReady = nullptr;
esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;
struct Retained {
    uint32_t magic, checksum, boots;
    char stage[64];
};
RTC_NOINIT_ATTR Retained retained;
char previousStage[64]{};
constexpr uint32_t magic = 0x47573230;
uint32_t hash(const Retained &r) {
    uint32_t n = 2166136261u;
    for (char c : r.stage) {
        n ^= uint8_t(c);
        n *= 16777619u;
    }
    return n ^ r.boots;
}
portMUX_TYPE rtcLock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> homeRequested{false};
uint32_t boots = 1;
void halt(const char *message) {
    Serial.printf("[FATAL] %s\n", message);
    // Yield: no busy loop, no watchdog disabled or fed to hide an error.
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
void gui(void *) {
    guiHandle = xTaskGetCurrentTaskHandle();
    displayInit();
    xSemaphoreGive(displayReady);
    uiInit();
    if (configNeedsSetup())
        uiShowSetup();
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    uint32_t uiAt = 0, statsAt = 0;
    for (;;) {
        displayServiceResync();
        if (homeRequested.exchange(false))
            uiShowHome();
        if (runtime::due(millis(), uiAt)) {
            uiTick();
            uiAt = millis() + 100;
        }
        lv_timer_handler();
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        if (runtime::due(millis(), statsAt)) {
            statsAt = millis() + 60000;
            Serial.printf(
                "[HEALTH] uptime=%lu s internal=%u largest=%u psram=%u stack(gui/data)=%u/%u\n",
                static_cast<unsigned long>(millis() / 1000),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                unsigned(ESP.getFreePsram()), unsigned(uxTaskGetStackHighWaterMark(nullptr)),
                dataHandle ? unsigned(uxTaskGetStackHighWaterMark(dataHandle.load())) : 0);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
} // namespace
int appWorkerCore() { return runtime::kDataCore; }
void appRecordDataTask(TaskHandle_t data) { dataHandle.store(data); }
void uiRequestHome() { homeRequested.store(true); }
uint32_t appBootCount() { return boots; }
const char *appPreviousFaultStage(unsigned i) { return i == 0 ? previousStage : ""; }
void appDiagnosticStage(const char *stage) {
    portENTER_CRITICAL(&rtcLock);
    retained.magic = 0;
    strlcpy(retained.stage, stage ? stage : "", sizeof(retained.stage));
    retained.boots = boots;
    retained.checksum = hash(retained);
    retained.magic = magic;
    portEXIT_CRITICAL(&rtcLock);
}
const char *appResetReason() {
    switch (resetReason) {
    case ESP_RST_POWERON:
        return "Encendido";
    case ESP_RST_SW:
        return "Software";
    case ESP_RST_PANIC:
        return "Excepción";
    case ESP_RST_INT_WDT:
        return "WDT interrupción";
    case ESP_RST_TASK_WDT:
        return "WDT tarea";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_BROWNOUT:
        return "Tensión baja";
    default:
        return "Otro";
    }
}
void markPlannedRestart() { appDiagnosticStage("Reinicio solicitado por el usuario"); }
void appDiagnosticsJson(Print &out) {
    out.printf(
        "{\"version\":\"%s\",\"uptime_s\":%lu,\"reset\":\"%s\",\"internal_free\":%u,\"internal_"
        "largest\":%u,\"psram_free\":%u,\"gui_stack_free\":%u,\"data_stack_free\":%u}",
        APP_VERSION, static_cast<unsigned long>(millis() / 1000), appResetReason(),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(ESP.getFreePsram()),
        guiHandle ? unsigned(uxTaskGetStackHighWaterMark(guiHandle.load())) : 0,
        dataHandle ? unsigned(uxTaskGetStackHighWaterMark(dataHandle.load())) : 0);
}
extern "C" void app_main() {
    const esp_err_t initialNvs = nvs_flash_init();
    if (initialNvs != ESP_OK) {
        ESP_LOGE("gluco",
                 "NVS no accesible (%s); configuración conservada. Respalda antes de borrar.",
                 esp_err_to_name(initialNvs));
        for (;;)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }
    initArduino();
    Serial.begin(115200);
    delay(400);
    resetReason = esp_reset_reason();
    if (retained.magic == magic && memchr(retained.stage, 0, sizeof(retained.stage)) &&
        retained.checksum == hash(retained)) {
        boots = retained.boots + 1;
        strlcpy(previousStage, retained.stage, sizeof(previousStage));
    }
    Serial.printf("\n[Gluco %s] ESP-IDF %s reset=%s previous=%s\n", APP_VERSION,
                  esp_get_idf_version(), appResetReason(), previousStage);
    const size_t psramChip = esp_psram_get_size();
    const size_t psramHeap = ESP.getPsramSize();
    Serial.printf("[PSRAM] chip=%u (%u MB) heap=%u (%u MB) found=%d\n",
                  unsigned(psramChip), unsigned(psramChip / (1024 * 1024)),
                  unsigned(psramHeap), unsigned(psramHeap / (1024 * 1024)),
                  int(psramFound()));
    if (!psramFound() || (psramChip > 0 ? psramChip < 7 * 1024 * 1024 : psramHeap < 4 * 1024 * 1024))
        halt("Se requieren 8 MB de PSRAM OPI");
    // Never silently erase NVS on an error: existing credentials stay recoverable.
    const esp_err_t nvs = nvs_flash_init();
    if (nvs != ESP_OK)
        halt("NVS no accesible: respalda antes de borrar");
    stateMutex = xSemaphoreCreateMutex();
    storageMutex = xSemaphoreCreateMutex();
    httpMutex = xSemaphoreCreateMutex();
    patientQueue = xQueueCreate(1, sizeof(PatientSelection));
    sharedState = new AppState{};
    if (!stateMutex || !storageMutex || !httpMutex || !patientQueue || !sharedState)
        halt("Memoria de arranque insuficiente");
    configRuntimeInit();
    configLoad();
    // Initialize/calibrate radio before RGB DMA starts; no automatic Wi-Fi NVS writes.
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_STA);
    Config settings = configSnapshot();
    configTzTime(settings.timezone == "Atlantic/Canary" ? "WET0WEST,M3.5.0/1,M10.5.0"
                 : settings.timezone == "UTC"           ? "UTC0"
                                                        : "CET-1CEST,M3.5.0,M10.5.0/3",
                 "pool.ntp.org", "time.cloudflare.com");
    if (!settings.ssid.isEmpty())
        WiFi.begin(settings.ssid.c_str(), settings.wifiPass.c_str());
    // IDF keeps both idle watchdogs enabled; GUI is additionally monitored.
    const esp_task_wdt_config_t wdt{8000, (1u << 0) | (1u << 1), true};
    if (esp_task_wdt_status(nullptr) == ESP_ERR_INVALID_STATE)
        ESP_ERROR_CHECK(esp_task_wdt_init(&wdt));
    else
        ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt));
    portalRuntimeStart();
    displayReady = xSemaphoreCreateBinary();
    if (!displayReady)
        halt("Sin semáforo de arranque");
    if (xTaskCreatePinnedToCore(gui, "gui", runtime::kGuiStack, nullptr, runtime::kGuiPriority,
                                nullptr, runtime::kGuiCore) != pdPASS)
        halt("No se pudo crear GUI");
    if (xSemaphoreTake(displayReady, pdMS_TO_TICKS(15000)) != pdTRUE)
        halt("LCD no confirmó arranque");
    networkStart();
}
