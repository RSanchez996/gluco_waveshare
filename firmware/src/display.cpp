#include "app.hpp"
#include "fonts.hpp"
#include "runtime.hpp"
#include <esp_attr.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
using namespace esp_panel::board;
using namespace esp_panel::drivers;
namespace {
Board *board = nullptr;
Touch *touchDevice = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
uint16_t *frames[2]{};
unsigned back = 1;
SemaphoreHandle_t frameDone = nullptr;
uint16_t left[runtime::kHeight], right[runtime::kHeight];
std::atomic<uint32_t> completed{0};
std::atomic<bool> resyncRequested{false}, wakeRequested{false};
bool sleeping = false, wakeGuard = false;
uint32_t showAt = 0;
void fatal(const char *message) {
    Serial.printf("[FATAL DISPLAY] %s\n", message);
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
bool IRAM_ATTR frameComplete(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *,
                             void *) {
    completed.fetch_add(1, std::memory_order_relaxed);
    BaseType_t awake = pdFALSE;
    xSemaphoreGiveFromISR(frameDone, &awake);
    return awake == pdTRUE;
}
void clearDamage() {
    for (int y = 0; y < runtime::kHeight; ++y) {
        left[y] = runtime::kWidth;
        right[y] = 0;
    }
}
void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
    const int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        memcpy(frames[back] + y * runtime::kWidth + area->x1, pixels + (y - area->y1) * width,
               width * 2);
        left[y] = std::min<uint16_t>(left[y], area->x1);
        right[y] = std::max<uint16_t>(right[y], area->x2 + 1);
    }
    if (lv_disp_flush_is_last(drv)) {
        // Driver recognizes its own FB pointer: request a swap, not a full copy.
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, runtime::kWidth, runtime::kHeight,
                                                  frames[back]));
        // Read AFTER the swap request. The next complete callback means the
        // old frame is no longer read into either bounce buffer (IDF 5.5.5).
        const uint32_t before = completed.load(std::memory_order_relaxed);
        const uint32_t timeout = millis() + 250;
        while (completed.load(std::memory_order_relaxed) == before) {
            xSemaphoreTake(frameDone, pdMS_TO_TICKS(10));
            if (runtime::due(millis(), timeout))
                fatal("RGB no confirma el cambio de framebuffer");
        }
        const unsigned old = back ^ 1;
        // Keep both frames coherent; copy only damaged row spans. No 768 KB
        // full-screen memcpy and no writes to the frame being scanned out.
        unsigned rows = 0;
        for (int y = 0; y < runtime::kHeight; ++y) {
            if (right[y] <= left[y])
                continue;
            memcpy(frames[old] + y * runtime::kWidth + left[y],
                   frames[back] + y * runtime::kWidth + left[y], (right[y] - left[y]) * 2);
            if (++rows % 16 == 0)
                vTaskDelay(1);
        }
        back = old;
        clearDamage();
    }
    lv_disp_flush_ready(drv);
}
void touch(lv_indev_drv_t *, lv_indev_data_t *data) {
    TouchPoint point;
    const bool pressed = touchDevice && touchDevice->readPoints(&point, 1, 0) > 0;
    if (sleeping && pressed) {
        displayWake();
        wakeGuard = true;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (wakeGuard) {
        if (!pressed)
            wakeGuard = false;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (pressed) {
        data->point.x = point.x;
        data->point.y = point.y;
    }
}
} // namespace
void displayInit() {
    if (xPortGetCoreID() != runtime::kGuiCore)
        fatal("La inicialización RGB debe ejecutarse en CPU1");
    board = new Board();
    if (!board || !board->init())
        fatal("Perfil Waveshare 4.3B no inicializado");
    auto *lcd = board->getLCD();
    if (!lcd || !lcd->configFrameBufferNumber(2))
        fatal("No se pueden configurar dos framebuffers");
    auto *rgb = static_cast<BusRGB *>(lcd->getBus());
    if (!rgb->configRGB_FreqHz(runtime::kPixelClock) ||
        !rgb->configRGB_BounceBufferSize(runtime::kWidth * runtime::kBounceLines))
        fatal("Configuración RGB fallida");
    if (!board->begin())
        fatal("LCD/táctil/CH422G no disponibles");
    panel = lcd->getRefreshPanelHandle();
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(
        panel, 2, reinterpret_cast<void **>(&frames[0]), reinterpret_cast<void **>(&frames[1])));
    frameDone = xSemaphoreCreateBinary();
    if (!frameDone)
        fatal("Semáforo RGB no disponible");
    esp_lcd_rgb_panel_event_callbacks_t cb{};
    cb.on_frame_buf_complete = frameComplete;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &cb, nullptr));
    memset(frames[1], 0, runtime::kWidth * runtime::kHeight * 2);
    clearDamage();
    touchDevice = board->getTouch();
    if (!touchDevice)
        fatal("GT911 no disponible");
    lv_init();
    fonts::init();
    constexpr size_t count = runtime::kWidth * runtime::kDrawLines;
    auto *draw = static_cast<lv_color_t *>(
        heap_caps_malloc(count * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!draw)
        fatal("Sin SRAM interna para el buffer de dibujo");
    static lv_disp_draw_buf_t buf;
    lv_disp_draw_buf_init(&buf, draw, nullptr, count);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = runtime::kWidth;
    driver.ver_res = runtime::kHeight;
    driver.flush_cb = flush;
    driver.draw_buf = &buf;
    lv_disp_drv_register(&driver);
    static lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = touch;
    lv_indev_drv_register(&input);
    if (board->getBacklight())
        board->getBacklight()->on();
    Serial.println(
        "[DISPLAY] CPU1 800x480 ST7262/GT911/CH422G, 12MHz, 20 líneas, FB doble, dibujo SRAM");
}
void displayWake() {
    if (!sleeping)
        return;
    sleeping = false;
    showAt = millis() + 120;
}
void displayRequestWake() { wakeRequested.store(true); }
void displaySleep() {
    uiClearGraphSelection();
    sleeping = true;
    showAt = 0;
    if (board && board->getBacklight())
        board->getBacklight()->off();
}
bool displayIsSleeping() { return sleeping; }
void displayResync() {
    if (panel)
        ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(panel));
}
void displayRequestResync() { resyncRequested.store(true); }
void displayServiceResync() {
    if (wakeRequested.exchange(false))
        displayWake();
    // Recovery is explicit. No periodic restart after every successful HTTPS.
    if (resyncRequested.exchange(false))
        displayResync();
    if (showAt && runtime::due(millis(), showAt)) {
        if (board && board->getBacklight())
            board->getBacklight()->on();
        showAt = 0;
    }
}
