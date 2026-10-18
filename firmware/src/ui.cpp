#include "app.hpp"
#include "diabetesm_links.hpp"
#include "fonts.hpp"
#include "history_store.hpp"
#include "ota.hpp"
#include "runtime.hpp"
#include "theme.hpp"
#include "weather_icons.hpp"
#include <WiFi.h>
#include <algorithm>
#include <cmath>
namespace {
Config config;
uint32_t uiConfigRevision = UINT32_MAX;
enum class Page {
    Home,
    Clock,
    Users,
    Settings,
    Wifi,
    WifiEdit,
    Location,
    Libre,
    Setup,
    DiabetesM,
    Ota
};
Page page = Page::Home;
AppState snapshot;
enum class PendingPage {
    None,
    Home,
    Clock,
    Users,
    Settings,
    Wifi,
    WifiEdit,
    Location,
    Libre,
    Setup,
    CloseSetup,
    DiabetesM,
    Ota
};
PendingPage pendingPage = PendingPage::None;
lv_obj_t *timeLabel = nullptr, *dateLabel = nullptr, *weatherLabel = nullptr,
         *weatherSummary = nullptr, *homeIcon = nullptr, *glucoseLabel = nullptr,
         *detailLabel = nullptr, *graph = nullptr, *arrow = nullptr, *tirHomeLabel = nullptr;
lv_obj_t *glucosePanel = nullptr, *glucoseAccent = nullptr;
lv_obj_t *clockBig = nullptr, *clockDate = nullptr, *clockWeather = nullptr,
         *clockSummary = nullptr, *clockIcon = nullptr, *hoursBox = nullptr, *hoursTab = nullptr,
         *daysTab = nullptr, *summaryTab = nullptr, *setupInfo = nullptr, *setupQr = nullptr;
lv_obj_t *keyboard = nullptr, *keyboardToggle = nullptr, *keyboardLastInput = nullptr,
         *wifiList = nullptr, *wifiSaved = nullptr, *wifiSsid = nullptr, *wifiPass = nullptr,
         *cityInput = nullptr, *locationList = nullptr, *loginUser = nullptr, *loginPass = nullptr,
         *loginRegion = nullptr, *loginList = nullptr, *diabetesmUser = nullptr,
         *diabetesmPass = nullptr, *diabetesmSwitch = nullptr, *diabetesmPatient = nullptr,
         *diabetesmMode = nullptr, *setupStatus = nullptr;
lv_obj_t *otaCurrentLabel = nullptr, *otaRemoteLabel = nullptr, *otaStatusLabel = nullptr,
         *otaChangesLabel = nullptr, *otaProgressBar = nullptr, *otaCheckBtn = nullptr,
         *otaInstallBtn = nullptr, *otaCancelBtn = nullptr, *settingsWifiLabel = nullptr;
WifiScanEntry scanEntries[16]{};
size_t scanCount = 0;
LocationChoice locationEntries[8]{};
size_t locationCount = 0;
ConnectionChoice loginEntries[MAX_CONNECTIONS]{};
size_t loginCount = 0;
String chosenSsid;
bool chosenSecure = true;
SetupJob lastWifiJob = SetupJob::Idle, lastLocationJob = SetupJob::Idle,
         lastLoginJob = SetupJob::Idle;
bool wifiRequested = false, locationRequested = false, loginRequested = false;
enum class SetupAction {
    None,
    SaveWifi,
    RemoveWifi,
    SearchLocation,
    SaveLocation,
    Login,
    SaveLogin,
    SaveDiabetesM
};
SetupAction setupAction = SetupAction::None;
SetupAction mutationAwaiting = SetupAction::None;
String actionSsid, actionPassword, actionUser, actionRegion, actionTargetPatientId;
bool actionConnect = false, actionSyncFiltered = false;
size_t actionIndex = 0;
int64_t selectedGraphEpoch = 0;
weathericons::Icon homeGlyph{}, clockGlyph{}, hourGlyphs[6]{}, dayGlyphs[4]{};
enum class ForecastView { Hours, Days, Summary };
ForecastView forecastView = ForecastView::Hours;
int64_t renderedWeather = -1, renderedGlucose = -1, renderedMinute = -1, renderedGraphBucket = -1,
        renderedLatestEpoch = -1;
int64_t renderedRequestStep = -1;
int renderedLatestValue = -1;
size_t renderedPoints = size_t(-1);
size_t renderedTotalReadings = size_t(-1);
int renderedTir = -1;
char renderedGlucoseError[160]{}, renderedWeatherError[140]{};
PortalMode renderedPortal = PortalMode::Off;
char pendingPatientId[100]{};
int64_t renderedConnections = -1;
size_t renderedConnectionCount = size_t(-1);
char renderedConnectionsError[160]{};
lv_color_t c(uint32_t value) { return lv_color_hex(value); }
uint32_t rangeColor(double value) {
    switch (gluco::range(value)) {
    case gluco::Range::LowRed:
        return theme::Red;
    case gluco::Range::Green:
        return theme::Green;
    case gluco::Range::HighYellow:
        return theme::Yellow;
    case gluco::Range::VeryHighOrange:
        return theme::Orange;
    default:
        return theme::Muted;
    }
}
const gluco::Point *latestReading() {
    if (snapshot.currentValid)
        return &snapshot.current;
    return snapshot.pointCount ? &snapshot.points[snapshot.pointCount - 1] : nullptr;
}
void resetRenderCache() {
    renderedWeather = renderedGlucose = renderedMinute = renderedGraphBucket = renderedLatestEpoch =
        renderedRequestStep = -1;
    renderedLatestValue = -1;
    renderedPoints = size_t(-1);
    renderedTotalReadings = size_t(-1);
    renderedTir = -1;
    renderedGlucoseError[0] = renderedWeatherError[0] = 0;
}

String displayCity() {
    String s = config.city;
    const int comma = s.indexOf(',');
    if (comma > 0)
        s.remove(comma);
    s.trim();
    return s;
}
const char *shortWeatherText(int code) {
    if (code == 0)
        return "Despejado";
    if (code <= 3)
        return "Nubes";
    if (code <= 48)
        return "Niebla";
    if (code <= 57)
        return "Llovizna";
    if (code <= 67)
        return "Lluvia";
    if (code <= 77)
        return "Nieve";
    if (code <= 82)
        return "Chubascos";
    if (code <= 86)
        return "Nieve";
    return "Tormenta";
}
void base(lv_obj_t *root) {
    lv_obj_set_style_bg_color(root, c(theme::Background), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
}
lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, const char *text,
                const lv_font_t *font = &fonts::montserrat16, uint32_t color = theme::Ink) {
    lv_obj_t *o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_label_set_text(o, text);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, c(color), 0);
    return o;
}
lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, const char *text, lv_event_cb_t cb,
                 void *userData = nullptr) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, 52);
    lv_obj_set_style_bg_color(b, c(theme::Button), 0);
    lv_obj_set_style_bg_color(b, c(0x265166), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, c(theme::Grid), 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, userData);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &fonts::montserrat20, 0);
    lv_obj_set_style_text_color(l, c(theme::Ink), 0);
    lv_obj_center(l);
    return b;
}
void drawLine(lv_draw_ctx_t *ctx, double x1, double y1, double x2, double y2, uint32_t color,
              int width = 2) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c(color);
    d.width = width;
    d.round_start = 1;
    d.round_end = 1;
    lv_point_t a{lv_coord_t(std::lround(x1)), lv_coord_t(std::lround(y1))},
        b{lv_coord_t(std::lround(x2)), lv_coord_t(std::lround(y2))};
    lv_draw_line(ctx, &d, &a, &b);
}
void drawDashes(lv_draw_ctx_t *ctx, int x, int y, int width, uint32_t color) {
    for (int offset = 0; offset < width; offset += 16)
        drawLine(ctx, x + offset, y, x + std::min(offset + 7, width), y, color, 1);
}
void drawDottedSegment(lv_draw_ctx_t *ctx, double x1, double y1, double x2, double y2,
                       uint32_t color) {
    const double dx = x2 - x1, dy = y2 - y1, length = std::hypot(dx, dy);
    if (length < 2)
        return;
    for (double at = 0; at < length; at += 9) {
        const double end = std::min(at + 4.0, length);
        drawLine(ctx, x1 + dx * at / length, y1 + dy * at / length, x1 + dx * end / length,
                 y1 + dy * end / length, color, 2);
    }
}
double slope(const gluco::Point &a, const gluco::Point &b) {
    return double(b.glucose - a.glucose) / double(b.epoch - a.epoch);
}
double smoothSlope(const gluco::Point &a, const gluco::Point &b, const gluco::Point &c) {
    const double d0 = slope(a, b), d1 = slope(b, c);
    if (d0 * d1 <= 0)
        return 0;
    const double h0 = double(b.epoch - a.epoch), h1 = double(c.epoch - b.epoch);
    const double w0 = 2 * h1 + h0, w1 = h1 + 2 * h0;
    return (w0 + w1) / (w0 / d0 + w1 / d1);
}
void drawSettingsIcon(lv_event_t *event) {
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    auto *ctx = lv_event_get_draw_ctx(event);
    const int x = (area.x1 + area.x2) / 2, y = (area.y1 + area.y2) / 2;
    for (int i = 0; i < 8; ++i) {
        const double angle = i * 3.14159265358979323846 / 4.0;
        drawLine(ctx, x + 7 * cos(angle), y + 7 * sin(angle), x + 12 * cos(angle),
                 y + 12 * sin(angle), theme::Ink, 4);
    }
    lv_draw_rect_dsc_t disk;
    lv_draw_rect_dsc_init(&disk);
    disk.bg_color = c(theme::Ink);
    disk.radius = LV_RADIUS_CIRCLE;
    lv_area_t rim{lv_coord_t(x - 9), lv_coord_t(y - 9), lv_coord_t(x + 9), lv_coord_t(y + 9)};
    lv_draw_rect(ctx, &disk, &rim);
    disk.bg_color = c(theme::Button);
    lv_area_t center{lv_coord_t(x - 3), lv_coord_t(y - 3), lv_coord_t(x + 3), lv_coord_t(y + 3)};
    lv_draw_rect(ctx, &disk, &center);
}
void drawText(lv_draw_ctx_t *ctx, int x, int y, int w, const char *text, uint32_t color,
              lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = c(color);
    d.font = &fonts::montserrat14;
    d.align = align;
    lv_area_t area{lv_coord_t(x), lv_coord_t(y), lv_coord_t(x + w), lv_coord_t(y + 20)};
    lv_draw_label(ctx, &d, &area, text, nullptr);
}
void drawGraph(lv_event_t *e) {
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target(e), &bounds);
    auto *ctx = lv_event_get_draw_ctx(e);
    const int x = bounds.x1 + 45, y = bounds.y1 + 30;
    const int w = lv_area_get_width(&bounds) - 62, h = lv_area_get_height(&bounds) - 67;
    const int64_t now = time(nullptr);
    int high = 300, low = 40;
    for (size_t i = 0; i < snapshot.pointCount; ++i) {
        high = std::max(high, (int(snapshot.points[i].glucose) / 50 + 1) * 50);
        if (snapshot.points[i].glucose < 40)
            low = 20;
    }
    if (snapshot.currentValid) {
        high = std::max(high, (int(snapshot.current.glucose) / 50 + 1) * 50);
        if (snapshot.current.glucose < 40)
            low = 20;
    }
    const auto py = [&](double g) { return y + h - (g - low) * h / (high - low); };
    char lastReadingTxt[64] = "ÚLTIMA LECTURA: --:--";
    const int64_t lastEpoch = snapshot.currentValid
                                  ? snapshot.current.epoch
                                  : (snapshot.pointCount ? snapshot.points[snapshot.pointCount - 1].epoch : 0);
    if (lastEpoch >= 1700000000) {
        const time_t stamp = lastEpoch;
        tm local{};
        localtime_r(&stamp, &local);
        snprintf(lastReadingTxt, sizeof(lastReadingTxt), "ÚLTIMA LECTURA: %02d:%02d", local.tm_hour,
                 local.tm_min);
    }
    drawText(ctx, x + 5, bounds.y1 + 7, 210, lastReadingTxt, theme::Blue);
    if (snapshot.glucoseError[0]) {
        const bool inProgress = strstr(snapshot.glucoseError, "...") != nullptr;
        drawText(ctx, x + 220, bounds.y1 + 7, bounds.x2 - (x + 230), snapshot.glucoseError,
                 inProgress ? theme::Muted : theme::Red);
    }
    lv_draw_rect_dsc_t band;
    lv_draw_rect_dsc_init(&band);
    band.bg_color = c(theme::Green);
    band.bg_opa = LV_OPA_10;
    lv_area_t safe{lv_coord_t(x), lv_coord_t(py(180)), lv_coord_t(x + w), lv_coord_t(py(70))};
    lv_draw_rect(ctx, &band, &safe);
    for (int value : {70, 180, 240}) {
        const uint32_t threshold =
            value == 70 ? theme::Red : (value == 180 ? theme::Yellow : theme::Orange);
        drawDashes(ctx, x, int(std::lround(py(value))), w, threshold);
        char txt[8];
        snprintf(txt, sizeof(txt), "%d", value);
        drawText(ctx, bounds.x1 + 4, py(value) - 8, 34, txt, threshold, LV_TEXT_ALIGN_RIGHT);
    }
    // Los ticks dependen de horas reales, no de fracciones del ancho. Las
    // etiquetas principales son 00:00, 03:00, 06:00... también al cambiar día.
    if (now >= 1700000000) {
        const int64_t start = now - gluco::kHistorySeconds;
        const int64_t first = (start / 3600 + 1) * 3600;
        for (int64_t epoch = first; epoch <= now; epoch += 3600) {
            const int px = x + int(std::lround(gluco::graphX(epoch, now, w)));
            const time_t stamp = epoch;
            tm local{};
            localtime_r(&stamp, &local);
            if (local.tm_hour % 3) {
                drawLine(ctx, px, y + h + 3, px, y + h + 6, theme::Muted, 1);
                continue;
            }
            drawLine(ctx, px, y, px, y + h, theme::Grid, 1);
            char tick[10];
            strftime(tick, sizeof(tick), "%H:00", &local);
            drawText(ctx, std::max(x, std::min(px - 25, x + w - 50)), y + h + 8, 50, tick,
                     theme::Muted);
        }
    }
    for (size_t i = 1; i < snapshot.pointCount; ++i) {
        const auto &a = snapshot.points[i - 1], &b = snapshot.points[i];
        if (a.epoch < now - gluco::kHistorySeconds || b.epoch > now || !gluco::graphConnect(a, b))
            continue;
        const double x0 = x + gluco::graphX(a.epoch, now, w),
                     x1 = x + gluco::graphX(b.epoch, now, w);
        const double sec = slope(a, b), dt = double(b.epoch - a.epoch);
        const double m0 = i >= 2 && gluco::graphConnect(snapshot.points[i - 2], a)
                              ? smoothSlope(snapshot.points[i - 2], a, b)
                              : sec;
        const double m1 =
            i + 1 < snapshot.pointCount && gluco::graphConnect(b, snapshot.points[i + 1])
                ? smoothSlope(a, b, snapshot.points[i + 1])
                : sec;
        const int steps = std::max(1, std::min(8, int(std::ceil((x1 - x0) / 5.0))));
        double lastX = x0, lastY = py(a.glucose);
        for (int step = 1; step <= steps; ++step) {
            const double t = double(step) / steps, t2 = t * t, t3 = t2 * t;
            double value = (2 * t3 - 3 * t2 + 1) * a.glucose + (t3 - 2 * t2 + t) * dt * m0 +
                           (-2 * t3 + 3 * t2) * b.glucose + (t3 - t2) * dt * m1;
            value = std::max<double>(std::min(a.glucose, b.glucose),
                                     std::min<double>(std::max(a.glucose, b.glucose), value));
            const double nextX = x0 + (x1 - x0) * t, nextY = py(value);
            drawLine(ctx, lastX, lastY, nextX, nextY, rangeColor(value), 3);
            lastX = nextX;
            lastY = nextY;
        }
    }
    if (snapshot.currentValid && snapshot.current.epoch >= now - gluco::kHistorySeconds &&
        snapshot.current.epoch <= now) {
        const auto &p = snapshot.current;
        const double px = x + gluco::graphX(p.epoch, now, w), yy = py(p.glucose);
        if (snapshot.pointCount) {
            const auto &last = snapshot.points[snapshot.pointCount - 1];
            if (last.epoch >= now - gluco::kHistorySeconds && gluco::graphConnect(last, p))
                drawDottedSegment(ctx, x + gluco::graphX(last.epoch, now, w), py(last.glucose), px,
                                  yy, rangeColor(p.glucose));
        }
        lv_draw_rect_dsc_t dot;
        lv_draw_rect_dsc_init(&dot);
        dot.bg_color = c(theme::Ink);
        dot.radius = LV_RADIUS_CIRCLE;
        lv_area_t ring{lv_coord_t(px - 6), lv_coord_t(yy - 6), lv_coord_t(px + 6),
                       lv_coord_t(yy + 6)};
        lv_draw_rect(ctx, &dot, &ring);
        dot.bg_color = c(rangeColor(p.glucose));
        lv_area_t center{lv_coord_t(px - 4), lv_coord_t(yy - 4), lv_coord_t(px + 4),
                         lv_coord_t(yy + 4)};
        lv_draw_rect(ctx, &dot, &center);
    }
    if (!selectedGraphEpoch)
        return;
    const gluco::Point *selected = nullptr;
    if (snapshot.currentValid && snapshot.current.epoch == selectedGraphEpoch)
        selected = &snapshot.current;
    for (size_t i = 0; !selected && i < snapshot.pointCount; ++i) {
        if (snapshot.points[i].epoch == selectedGraphEpoch) {
            selected = &snapshot.points[i];
            break;
        }
    }
    if (!selected || selected->epoch < now - gluco::kHistorySeconds || selected->epoch > now)
        return;
    const int sx = int(std::lround(x + gluco::graphX(selected->epoch, now, w)));
    const int sy = int(std::lround(py(selected->glucose)));
    drawLine(ctx, sx, y, sx, y + h, theme::Blue, 2);
    lv_draw_rect_dsc_t marker;
    lv_draw_rect_dsc_init(&marker);
    marker.bg_color = c(rangeColor(selected->glucose));
    marker.radius = LV_RADIUS_CIRCLE;
    lv_area_t pointArea{lv_coord_t(sx - 6), lv_coord_t(sy - 6), lv_coord_t(sx + 6),
                        lv_coord_t(sy + 6)};
    lv_draw_rect(ctx, &marker, &pointArea);
    time_t timestamp = selected->epoch;
    tm local{};
    localtime_r(&timestamp, &local);
    char stamp[20] = "--/-- --:--";
    strftime(stamp, sizeof(stamp), "%d/%m %H:%M", &local);
    char detail[48];
    snprintf(detail, sizeof(detail), "%s   %d mg/dL", stamp, int(selected->glucose));
    const int tooltipX = std::max(x + 190, std::min(sx - 112, bounds.x2 - 239));
    lv_draw_rect_dsc_t bubble;
    lv_draw_rect_dsc_init(&bubble);
    bubble.bg_color = c(theme::Button);
    bubble.bg_opa = LV_OPA_COVER;
    bubble.radius = 7;
    lv_area_t tooltip{lv_coord_t(tooltipX), lv_coord_t(bounds.y1 + 3), lv_coord_t(tooltipX + 234),
                      lv_coord_t(bounds.y1 + 27)};
    lv_draw_rect(ctx, &bubble, &tooltip);
    drawText(ctx, tooltipX + 8, bounds.y1 + 5, 218, detail, theme::Ink, LV_TEXT_ALIGN_CENTER);
}
void chooseGraphPoint(lv_event_t *event) {
    if (!snapshot.pointCount && !snapshot.currentValid)
        return;
    lv_point_t touch{};
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev)
        return;
    lv_indev_get_point(indev, &touch);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target(event), &bounds);
    const int x = bounds.x1 + 45, y = bounds.y1 + 30;
    const int w = lv_area_get_width(&bounds) - 62, h = lv_area_get_height(&bounds) - 67;
    if (touch.x < x || touch.x > x + w || touch.y < y || touch.y > y + h)
        return;
    const int64_t now = time(nullptr);
    int bestDistance = w + 1;
    int64_t closestEpoch = 0;
    for (size_t i = 0; i < snapshot.pointCount; ++i) {
        const auto &p = snapshot.points[i];
        if (p.epoch < now - gluco::kHistorySeconds || p.epoch > now)
            continue;
        const int px = x + int(std::lround(gluco::graphX(p.epoch, now, w)));
        const int distance = std::abs(px - touch.x);
        if (distance < bestDistance) {
            bestDistance = distance;
            closestEpoch = p.epoch;
        }
    }
    if (snapshot.currentValid && snapshot.current.epoch >= now - gluco::kHistorySeconds &&
        snapshot.current.epoch <= now) {
        const int px = x + int(std::lround(gluco::graphX(snapshot.current.epoch, now, w)));
        if (std::abs(px - touch.x) <= bestDistance)
            closestEpoch = snapshot.current.epoch;
    }
    if (closestEpoch && closestEpoch != selectedGraphEpoch) {
        selectedGraphEpoch = closestEpoch;
        lv_obj_invalidate(graph);
    }
}
void drawArrow(lv_event_t *e) {
    const gluco::Point *reading = latestReading();
    if (!reading)
        return;
    const char *d = snapshot.direction;
    double angle = 0;
    bool known = true, twice = false;
    if (!strcmp(d, "Flat"))
        angle = 0;
    else if (!strcmp(d, "FortyFiveUp"))
        angle = -.7854;
    else if (!strcmp(d, "FortyFiveDown"))
        angle = .7854;
    else if (!strcmp(d, "SingleUp") || !strcmp(d, "DoubleUp")) {
        angle = -1.5708;
        twice = !strcmp(d, "DoubleUp");
    } else if (!strcmp(d, "SingleDown") || !strcmp(d, "DoubleDown")) {
        angle = 1.5708;
        twice = !strcmp(d, "DoubleDown");
    } else
        known = false;
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(e), &area);
    auto *ctx = lv_event_get_draw_ctx(e);
    uint32_t color = rangeColor(reading->glucose);
    if (!known) {
        drawText(ctx, area.x1 + 20, area.y1 + 22, 55, "--", theme::Muted);
        return;
    }
    for (int i = 0; i < (twice ? 2 : 1); ++i) {
        double cx = area.x1 + (twice ? 25 + i * 27 : 40), cy = area.y1 + 35, dx = cos(angle),
               dy = sin(angle), tx = cx + 23 * dx, ty = cy + 23 * dy;
        drawLine(ctx, cx - 20 * dx, cy - 20 * dy, tx, ty, color, 5);
        drawLine(ctx, tx, ty, tx - 12 * dx - 9 * dy, ty - 12 * dy + 9 * dx, color, 5);
        drawLine(ctx, tx, ty, tx - 12 * dx + 9 * dy, ty - 12 * dy - 9 * dx, color, 5);
    }
}
// Un callback táctil no debe destruir el propio botón aún en procesamiento.
// uiTick aplica la transición desde el bucle principal, fuera del evento LVGL.
void settings(lv_event_t *) { pendingPage = PendingPage::Settings; }
void lockScreenAction(lv_event_t *) { displaySleep(); }
void sleepHomeBackground(lv_event_t *event) {
    // La zona superior apaga la retroiluminación; la gráfica queda reservada
    // para inspeccionar medidas y los botones mantienen sus acciones.
    if (page != Page::Home || lv_event_get_target(event) != lv_scr_act() || !graph)
        return;
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t touch{};
    if (indev) {
        lv_indev_get_point(indev, &touch);
        lv_area_t bounds;
        lv_obj_get_coords(graph, &bounds);
        if (touch.y < bounds.y1)
            displaySleep();
    }
}
void clockPage(lv_event_t *);
void homePage(lv_event_t *);
void usersPage(lv_event_t *);
void gotoSettings(lv_event_t *);
void updateForecast();
void requestHours(lv_event_t *) {
    forecastView = ForecastView::Hours;
    updateForecast();
}
void requestDays(lv_event_t *) {
    forecastView = ForecastView::Days;
    updateForecast();
}
void requestSummary(lv_event_t *) {
    forecastView = ForecastView::Summary;
    updateForecast();
}
void buildHome() {
    portalStop();
    configUiActive.store(false, std::memory_order_release);
    setupStatus = keyboard = nullptr;
    page = Page::Home;
    selectedGraphEpoch = 0;
    resetRenderCache();
    lv_obj_clean(lv_scr_act());
    base(lv_scr_act());
    lv_obj_add_flag(lv_scr_act(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *weatherCard = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(weatherCard, 18, 8);
    lv_obj_set_size(weatherCard, 372, 118);
    lv_obj_set_style_bg_color(weatherCard, c(theme::Card), 0);
    lv_obj_set_style_border_width(weatherCard, 1, 0);
    lv_obj_set_style_border_color(weatherCard, c(theme::Grid), 0);
    lv_obj_set_style_radius(weatherCard, 18, 0);
    lv_obj_clear_flag(weatherCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(weatherCard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(weatherCard, 0, 0);
    lv_obj_add_event_cb(weatherCard, clockPage, LV_EVENT_CLICKED, nullptr);

    timeLabel = label(weatherCard, 16, 8, 165, "--:--", &fonts::montserrat48);
    dateLabel = label(weatherCard, 18, 62, 165, "Esperando hora", &fonts::montserrat16, theme::Muted);
    lv_label_set_long_mode(dateLabel, LV_LABEL_LONG_WRAP);

    lv_obj_t *sep = lv_obj_create(weatherCard);
    lv_obj_set_pos(sep, 186, 16);
    lv_obj_set_size(sep, 1, 86);
    lv_obj_set_style_bg_color(sep, c(theme::Grid), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);

    homeGlyph.background = theme::Card;
    homeIcon = weathericons::create(weatherCard, 198, 14, 50, &homeGlyph);
    weatherLabel = label(weatherCard, 254, 14, 110, "-- °C", &fonts::montserrat32);
    weatherSummary =
        label(weatherCard, 198, 66, 165, "Clima pendiente", &fonts::montserrat14, theme::Muted);
    lv_label_set_long_mode(weatherSummary, LV_LABEL_LONG_WRAP);

    glucosePanel = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(glucosePanel, 410, 8);
    lv_obj_set_size(glucosePanel, 372, 118);
    lv_obj_set_style_bg_color(glucosePanel, c(theme::Card), 0);
    lv_obj_set_style_border_width(glucosePanel, 1, 0);
    lv_obj_set_style_border_color(glucosePanel, c(theme::Grid), 0);
    lv_obj_set_style_radius(glucosePanel, 18, 0);
    lv_obj_set_style_pad_all(glucosePanel, 0, 0);
    lv_obj_clear_flag(glucosePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(glucosePanel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(glucosePanel, lockScreenAction, LV_EVENT_CLICKED, nullptr);

    glucoseAccent = lv_obj_create(glucosePanel);
    lv_obj_set_pos(glucoseAccent, 14, 16);
    lv_obj_set_size(glucoseAccent, 4, 86);
    lv_obj_set_style_bg_color(glucoseAccent, c(theme::Muted), 0);
    lv_obj_set_style_border_width(glucoseAccent, 0, 0);
    lv_obj_set_style_radius(glucoseAccent, 3, 0);
    lv_obj_clear_flag(glucoseAccent, LV_OBJ_FLAG_CLICKABLE);

    label(glucosePanel, 28, 12, 200, "GLUCOSA ACTUAL", &fonts::montserrat14, theme::Muted);
    const gluco::Point *currentReading = latestReading();
    if (currentReading && currentReading->glucose > 0) {
        char val[12];
        snprintf(val, sizeof(val), "%d", currentReading->glucose);
        const uint32_t col = rangeColor(currentReading->glucose);
        glucoseLabel = label(glucosePanel, 28, 34, 125, val, &fonts::montserrat48, col);
        lv_obj_set_style_bg_color(glucoseAccent, c(col), 0);
    } else {
        glucoseLabel = label(glucosePanel, 28, 34, 125, "---", &fonts::montserrat48, theme::Muted);
    }
    label(glucosePanel, 155, 64, 60, "mg/dL", &fonts::montserrat14, theme::Muted);

    arrow = lv_obj_create(glucosePanel);
    lv_obj_set_pos(arrow, 235, 30);
    lv_obj_set_size(arrow, 70, 57);
    lv_obj_set_style_bg_opa(arrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arrow, 0, 0);
    lv_obj_clear_flag(arrow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(arrow, drawArrow, LV_EVENT_DRAW_MAIN, nullptr);

    detailLabel =
        label(glucosePanel, 28, 90, 180, "Sin lectura", &fonts::montserrat16, theme::Muted);

    tirHomeLabel =
        label(glucosePanel, 210, 90, 120, "TIR --%", &fonts::montserrat14, theme::Muted);
    lv_obj_set_style_text_align(tirHomeLabel, LV_TEXT_ALIGN_CENTER, 0);
    if (snapshot.dailySummary.valid && snapshot.dailySummary.totalReadings > 0) {
        char tirBuf[32];
        snprintf(tirBuf, sizeof(tirBuf), "TIR %.0f%%", snapshot.dailySummary.tirPercent);
        lv_label_set_text(tirHomeLabel, tirBuf);
        lv_obj_set_style_text_color(tirHomeLabel, c(tirColor(snapshot.dailySummary.tirPercent)), 0);
    }

    graph = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(graph, 18, runtime::kGraphY);
    lv_obj_set_size(graph, 764, runtime::kGraphHeight);
    lv_obj_set_style_bg_color(graph, c(theme::Card), 0);
    lv_obj_set_style_border_width(graph, 1, 0);
    lv_obj_set_style_border_color(graph, c(theme::Grid), 0);
    lv_obj_set_style_radius(graph, 18, 0);
    lv_obj_set_style_pad_all(graph, 0, 0);
    lv_obj_clear_flag(graph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(graph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(graph, drawGraph, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(graph, chooseGraphPoint, LV_EVENT_CLICKED, nullptr);
}
void buildClock() {
    configUiActive.store(false, std::memory_order_release);
    setupStatus = keyboard = nullptr;
    page = Page::Clock;
    resetRenderCache();
    lv_obj_clean(lv_scr_act());
    base(lv_scr_act());

    lv_obj_t *clockCard = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(clockCard, 18, 12);
    lv_obj_set_size(clockCard, 372, 128);
    lv_obj_set_style_bg_color(clockCard, c(theme::Card), 0);
    lv_obj_set_style_border_width(clockCard, 1, 0);
    lv_obj_set_style_border_color(clockCard, c(theme::Grid), 0);
    lv_obj_set_style_radius(clockCard, 18, 0);
    lv_obj_set_style_pad_all(clockCard, 0, 0);
    lv_obj_clear_flag(clockCard, LV_OBJ_FLAG_SCROLLABLE);

    clockBig = label(clockCard, 24, 14, 324, "--:--", &fonts::montserrat48);
    clockDate = label(clockCard, 26, 76, 320, "Esperando hora", &fonts::montserrat20, theme::Muted);
    lv_label_set_long_mode(clockDate, LV_LABEL_LONG_WRAP);

    lv_obj_t *weatherCard = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(weatherCard, 410, 12);
    lv_obj_set_size(weatherCard, 372, 128);
    lv_obj_set_style_bg_color(weatherCard, c(theme::Card), 0);
    lv_obj_set_style_border_width(weatherCard, 1, 0);
    lv_obj_set_style_border_color(weatherCard, c(theme::Grid), 0);
    lv_obj_set_style_radius(weatherCard, 18, 0);
    lv_obj_clear_flag(weatherCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(weatherCard, 0, 0);

    clockGlyph.background = theme::Card;
    clockIcon = weathericons::create(weatherCard, 16, 20, 88, &clockGlyph);
    clockWeather = label(weatherCard, 116, 12, 240, "-- °C", &fonts::montserrat48);
    clockSummary =
        label(weatherCard, 116, 68, 240, "Clima pendiente", &fonts::montserrat14, theme::Muted);
    lv_label_set_long_mode(clockSummary, LV_LABEL_LONG_WRAP);

    label(lv_scr_act(), 24, 148, 400, "PRONÓSTICO LOCAL", &fonts::montserrat16, theme::Blue);

    hoursBox = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(hoursBox, 18, 172);
    lv_obj_set_size(hoursBox, 764, 238);
    lv_obj_set_style_bg_color(hoursBox, c(theme::Card), 0);
    lv_obj_set_style_border_width(hoursBox, 1, 0);
    lv_obj_set_style_border_color(hoursBox, c(theme::Grid), 0);
    lv_obj_set_style_radius(hoursBox, 18, 0);
    lv_obj_set_style_pad_all(hoursBox, 0, 0);
    lv_obj_clear_flag(hoursBox, LV_OBJ_FLAG_SCROLLABLE);
    label(hoursBox, 35, 95, 690, "Esperando pronóstico...", &fonts::montserrat20, theme::Muted);

    button(lv_scr_act(), 18, 418, 145, "Glucosa", homePage);
    hoursTab = button(lv_scr_act(), 171, 418, 145, "Por horas", requestHours);
    daysTab = button(lv_scr_act(), 324, 418, 145, "4 días", requestDays);
    summaryTab = button(lv_scr_act(), 477, 418, 185, "Resumen día", requestSummary);
    lv_obj_t *clockGear = button(lv_scr_act(), 670, 418, 112, "", settings);
    lv_obj_add_event_cb(clockGear, drawSettingsIcon, LV_EVENT_DRAW_MAIN, nullptr);
}
void selectUser(lv_event_t *event) {
    const size_t index = size_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    if (index >= snapshot.connectionCount)
        return;
    if (requestPatientSelection(snapshot.connections[index].id, snapshot.connections[index].name)) {
        selectedGraphEpoch = 0;
        strlcpy(pendingPatientId, snapshot.connections[index].id, sizeof(pendingPatientId));
        pendingPage = PendingPage::Home;
    }
}
void buildUsers() {
    // No hay campos en edición: dejar que la tarea de red actualice la lista
    // cuando aún no esté en caché, igual que en la portada.
    configUiActive.store(false, std::memory_order_release);
    setupStatus = keyboard = nullptr;
    page = Page::Users;
    lv_obj_clean(lv_scr_act());
    base(lv_scr_act());
    label(lv_scr_act(), 28, 20, 600, "Elegir usuario de glucosa", &fonts::montserrat28);
    String current = "Usuario actual: " +
                     (config.patientName.isEmpty() ? String("ninguno") : config.patientName);
    if (!config.patientId.isEmpty()) {
        char activeDm[64]{};
        if (dmLinkHasAccount(config.patientId.c_str(), activeDm, sizeof(activeDm))) {
            current += "  (DM: " + String(activeDm) + ")";
        } else {
            current += "  (Sin cuenta Diabetes:M)";
        }
    }
    label(lv_scr_act(), 30, 58, 700, current.c_str(), &fonts::montserrat16, theme::Muted);
    if (!snapshot.connectionCount) {
        const char *message = snapshot.connectionsError[0] ? snapshot.connectionsError
                              : config.libreUser.isEmpty()
                                  ? "Inicia sesión en Cuenta LibreLinkUp para cargar usuarios."
                                  : "Actualizando usuarios de LibreLinkUp...";
        auto *info = label(lv_scr_act(), 55, 155, 690, message, &fonts::montserrat20, theme::Muted);
        lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    } else {
        lv_obj_t *list = lv_obj_create(lv_scr_act());
        lv_obj_set_pos(list, 28, 92);
        lv_obj_set_size(list, 744, 305);
        lv_obj_set_style_bg_color(list, c(theme::Card), 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_style_radius(list, 18, 0);
        lv_obj_set_style_pad_all(list, 10, 0);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        for (size_t i = 0; i < snapshot.connectionCount; ++i) {
            char dmU[64]{};
            const bool hasDm = dmLinkHasAccount(snapshot.connections[i].id, dmU, sizeof(dmU));
            String itemText = String(snapshot.connections[i].name) +
                              (hasDm ? "   [DM Conectado]" : "   [Sin DM]");
            auto *b = button(list, 12, 12 + int(i) * 62, 690, itemText.c_str(),
                             selectUser, reinterpret_cast<void *>(uintptr_t(i)));
            if (config.patientId == snapshot.connections[i].id)
                lv_obj_set_style_bg_color(b, c(theme::Green), 0);
        }
    }
    if (snapshot.connectionsError[0])
        label(lv_scr_act(), 30, 399, 560, snapshot.connectionsError, &fonts::montserrat14,
              theme::Yellow);
    button(lv_scr_act(), 614, 414, 168, "Volver", gotoSettings);
    renderedConnections = snapshot.connectionsFetched;
    renderedConnectionCount = snapshot.connectionCount;
    strlcpy(renderedConnectionsError, snapshot.connectionsError, sizeof(renderedConnectionsError));
}
void closeSetup(lv_event_t *) { pendingPage = PendingPage::CloseSetup; }
void buildSetup() {
    setupStatus = keyboard = nullptr;
    page = Page::Setup;
    renderedPortal = portalMode();
    setupQr = nullptr;
    setupInfo = nullptr;
    lv_obj_clean(lv_scr_act());
    base(lv_scr_act());
    if (renderedPortal == PortalMode::WifiAccessPoint) {
        label(lv_scr_act(), 28, 22, 650, "Paso 1 de 2  ·  Configurar Wi-Fi", &fonts::montserrat28);
        label(lv_scr_act(), 30, 65, 720,
              "Escanea el QR, conecta el móvil a la red temporal y guarda el Wi-Fi de casa.",
              &fonts::montserrat16, theme::Muted);
        setupQr = lv_qrcode_create(lv_scr_act(), 230, c(0x000000), c(0xFFFFFF));
        lv_obj_set_pos(setupQr, 36, 125);
        String qr = "WIFI:T:WPA;S:" + portalSsid() + ";P:" + portalPassword() + ";;";
        lv_qrcode_update(setupQr, qr.c_str(), qr.length());
        setupInfo = label(lv_scr_act(), 310, 140, 455, "", &fonts::montserrat20);
        lv_label_set_long_mode(setupInfo, LV_LABEL_LONG_WRAP);
    } else if (renderedPortal == PortalMode::WaitingForWifi) {
        label(lv_scr_act(), 28, 22, 700, "Conectando a la red de casa", &fonts::montserrat28);
        label(lv_scr_act(), 70, 155, 660,
              "Wi-Fi guardado en memoria.\n\nEspera mientras la pantalla se conecta y sincroniza "
              "la red.\n\nCuando esté preparada aparecerá el segundo QR.",
              &fonts::montserrat24, theme::Ink);
    } else {
        label(lv_scr_act(), 28, 22, 700, "Paso 2 de 2  ·  Servicios y ajustes",
              &fonts::montserrat28);
        label(lv_scr_act(), 30, 65, 730,
              "Conecta el móvil al mismo Wi-Fi de casa y escanea este segundo QR.",
              &fonts::montserrat16, theme::Muted);
        setupQr = lv_qrcode_create(lv_scr_act(), 230, c(0x000000), c(0xFFFFFF));
        lv_obj_set_pos(setupQr, 36, 125);
        String qr = portalUrl();
        lv_qrcode_update(setupQr, qr.c_str(), qr.length());
        String info = "Abre desde el móvil:\n\n" + portalUrl() +
                      "\n\nSeñal Wi-Fi: " + String(WiFi.RSSI()) +
                      " dBm\n\nConfigura LibreLinkUp, Diabetes:M, Wi-Fi y clima desde tu móvil."
                      "\n\nPuedes cerrar esta pantalla cuando quieras.";
        setupInfo = label(lv_scr_act(), 310, 130, 455, info.c_str(), &fonts::montserrat20);
        lv_label_set_long_mode(setupInfo, LV_LABEL_LONG_WRAP);
    }
    button(lv_scr_act(), 600, 405, 170, "Cerrar", closeSetup);
}
void gotoSettings(lv_event_t *) { pendingPage = PendingPage::Settings; }
void gotoWifi(lv_event_t *) { pendingPage = PendingPage::Wifi; }
void gotoLocation(lv_event_t *) { pendingPage = PendingPage::Location; }
void gotoLibre(lv_event_t *) { pendingPage = PendingPage::Libre; }
void gotoDiabetesM(lv_event_t *) { pendingPage = PendingPage::DiabetesM; }
void gotoQr(lv_event_t *) { pendingPage = PendingPage::Setup; }
void gotoOta(lv_event_t *) {
    if (WiFi.status() == WL_CONNECTED && !otaBusy()) {
        String err;
        otaRequestCheck(err);
    }
    pendingPage = PendingPage::Ota;
}
void setupMessage(const String &message) {
    if (setupStatus)
        lv_label_set_text(setupStatus, message.c_str());
}
void setupHeader(const char *title, bool root = false) {
    configUiActive.store(true, std::memory_order_release);
    keyboard = keyboardToggle = keyboardLastInput = wifiList = wifiSaved = wifiSsid = wifiPass =
        cityInput = locationList = loginUser = loginPass = loginRegion = loginList = diabetesmUser =
            diabetesmPass = diabetesmSwitch = diabetesmPatient = diabetesmMode = setupStatus =
                otaCurrentLabel = otaRemoteLabel = otaStatusLabel = otaChangesLabel =
                    otaProgressBar = otaCheckBtn = otaInstallBtn = otaCancelBtn =
                        settingsWifiLabel = nullptr;
    lv_obj_clean(lv_scr_act());
    base(lv_scr_act());
    label(lv_scr_act(), 26, 16, 680, title, &fonts::montserrat28);
    if (root)
        button(lv_scr_act(), 711, 13, 61, "X", homePage);
    else
        button(lv_scr_act(), 646, 13, 136, "Volver", gotoSettings);
    setupStatus = label(lv_scr_act(), 28, 431, 735, "", &fonts::montserrat14, theme::Blue);
    lv_label_set_long_mode(setupStatus, LV_LABEL_LONG_DOT);
}

// El mapa español y su control map son estáticos: LVGL conserva los punteros.
const char *spanishLower[] = {"1#",
                              "q",
                              "w",
                              "e",
                              "r",
                              "t",
                              "y",
                              "u",
                              "i",
                              "o",
                              "p",
                              LV_SYMBOL_BACKSPACE,
                              "\n",
                              "a",
                              "s",
                              "d",
                              "f",
                              "g",
                              "h",
                              "j",
                              "k",
                              "l",
                              "ñ",
                              "\n",
                              "ABC",
                              "z",
                              "x",
                              "c",
                              "v",
                              "b",
                              "n",
                              "m",
                              ",",
                              ".",
                              "\n",
                              LV_SYMBOL_LEFT,
                              "@",
                              " ",
                              ".",
                              "-",
                              LV_SYMBOL_RIGHT,
                              LV_SYMBOL_OK,
                              ""};
const char *spanishUpper[] = {"1#",
                              "Q",
                              "W",
                              "E",
                              "R",
                              "T",
                              "Y",
                              "U",
                              "I",
                              "O",
                              "P",
                              LV_SYMBOL_BACKSPACE,
                              "\n",
                              "A",
                              "S",
                              "D",
                              "F",
                              "G",
                              "H",
                              "J",
                              "K",
                              "L",
                              "Ñ",
                              "\n",
                              "abc",
                              "Z",
                              "X",
                              "C",
                              "V",
                              "B",
                              "N",
                              "M",
                              ",",
                              ".",
                              "\n",
                              LV_SYMBOL_LEFT,
                              "@",
                              " ",
                              ".",
                              "-",
                              LV_SYMBOL_RIGHT,
                              LV_SYMBOL_OK,
                              ""};
const lv_btnmatrix_ctrl_t spanishCtrl[] = {LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           LV_BTNMATRIX_CTRL_NO_REPEAT | 2,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           1,
                                           LV_BTNMATRIX_CTRL_NO_REPEAT | 2,
                                           1,
                                           7,
                                           1,
                                           1,
                                           LV_BTNMATRIX_CTRL_NO_REPEAT | 2,
                                           LV_KEYBOARD_CTRL_BTN_FLAGS | 2};
// Las tildes se encuentran en 1#, junto a los símbolos; las letras normales
// conservan las filas QWERTY / ASDF / ZXCV de un teclado español.
const char *spanishSpecial[] = {"1",
                                "2",
                                "3",
                                "4",
                                "5",
                                "6",
                                "7",
                                "8",
                                "9",
                                "0",
                                LV_SYMBOL_BACKSPACE,
                                "\n",
                                "abc",
                                "á",
                                "é",
                                "í",
                                "ó",
                                "ú",
                                "ü",
                                "ñ",
                                "¿",
                                "?",
                                "¡",
                                "!",
                                "\n",
                                "Á",
                                "É",
                                "Í",
                                "Ó",
                                "Ú",
                                "Ü",
                                "Ñ",
                                "@",
                                "_",
                                "-",
                                ".",
                                ",",
                                "\n",
                                "/",
                                ":",
                                ";",
                                "(",
                                ")",
                                "'",
                                "\"",
                                "#",
                                "$",
                                "%",
                                "&",
                                "=",
                                "\n",
                                "abc",
                                LV_SYMBOL_LEFT,
                                " ",
                                LV_SYMBOL_RIGHT,
                                "Más",
                                LV_SYMBOL_OK,
                                ""};
const lv_btnmatrix_ctrl_t spanishSpecialCtrl[] = {1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  LV_BTNMATRIX_CTRL_NO_REPEAT | 2,
                                                  LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  1,
                                                  LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
                                                  LV_BTNMATRIX_CTRL_NO_REPEAT | 2,
                                                  7,
                                                  LV_BTNMATRIX_CTRL_NO_REPEAT | 2,
                                                  LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
                                                  LV_KEYBOARD_CTRL_BTN_FLAGS | 2};
// La segunda página usa caracteres ASCII para claves y SSID poco habituales.
const char *spanishSpecial2[] = {"*",
                                 "+",
                                 "=",
                                 "\\",
                                 "|",
                                 "<",
                                 ">",
                                 "[",
                                 "]",
                                 "{",
                                 "}",
                                 LV_SYMBOL_BACKSPACE,
                                 "\n",
                                 "~",
                                 "`",
                                 "^",
                                 "%",
                                 "&",
                                 "$",
                                 "#",
                                 "@",
                                 "_",
                                 "-",
                                 "?",
                                 "!",
                                 "\n",
                                 "1",
                                 "2",
                                 "3",
                                 "4",
                                 "5",
                                 "6",
                                 "7",
                                 "8",
                                 "9",
                                 "0",
                                 "/",
                                 ":",
                                 "\n",
                                 "Volver",
                                 LV_SYMBOL_LEFT,
                                 " ",
                                 LV_SYMBOL_RIGHT,
                                 "abc",
                                 LV_SYMBOL_OK,
                                 ""};
const lv_btnmatrix_ctrl_t spanishSpecial2Ctrl[] = {1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   LV_BTNMATRIX_CTRL_NO_REPEAT | 2,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   1,
                                                   LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
                                                   LV_BTNMATRIX_CTRL_NO_REPEAT | 2,
                                                   7,
                                                   LV_BTNMATRIX_CTRL_NO_REPEAT | 2,
                                                   LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
                                                   LV_KEYBOARD_CTRL_BTN_FLAGS | 2};
void keyboardKey(lv_event_t *event) {
    lv_obj_t *kb = lv_event_get_target(event);
    uint16_t id = lv_btnmatrix_get_selected_btn(kb);
    if (id == LV_BTNMATRIX_BTN_NONE)
        return;
    const char *key = lv_btnmatrix_get_btn_text(kb, id);
    if (key && strcmp(key, "Más") == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
        return;
    }
    if (key && strcmp(key, "Volver") == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
        return;
    }
    lv_keyboard_def_event_cb(event);
}
void keyboardButtonText(const char *text) {
    if (!keyboardToggle)
        return;
    auto *caption = lv_obj_get_child(keyboardToggle, 0);
    if (caption) {
        lv_label_set_text(caption, text);
        lv_obj_center(caption);
    }
}
void hideKeyboard(lv_event_t *) {
    if (!keyboard)
        return;
    if (keyboardLastInput)
        lv_obj_clear_state(keyboardLastInput, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(keyboard, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    keyboardButtonText("Mostrar teclado");
}
void showKeyboard(lv_obj_t *field) {
    if (!keyboard || !field)
        return;
    if (keyboardLastInput && keyboardLastInput != field)
        lv_obj_clear_state(keyboardLastInput, LV_STATE_FOCUSED);
    keyboardLastInput = field;
    lv_keyboard_set_textarea(keyboard, field);
    lv_obj_add_state(field, LV_STATE_FOCUSED);
    lv_event_send(field, LV_EVENT_FOCUSED, nullptr);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard);
    keyboardButtonText("Ocultar teclado");
}
void toggleKeyboard(lv_event_t *) {
    if (!keyboard)
        return;
    if (!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN)) {
        hideKeyboard(nullptr);
        return;
    }
    lv_obj_t *field = keyboardLastInput;
    if (!field)
        field = page == Page::WifiEdit    ? wifiSsid
                : page == Page::Location  ? cityInput
                : page == Page::DiabetesM ? diabetesmUser
                                          : loginUser;
    showKeyboard(field);
}
void focusInput(lv_event_t *e) { showKeyboard(lv_event_get_target(e)); }
lv_obj_t *setupInput(int x, int y, int w, const char *value, int limit, bool secret = false) {
    constexpr lv_style_selector_t focusedCursor =
        lv_style_selector_t(LV_PART_CURSOR) | lv_style_selector_t(LV_STATE_FOCUSED);
    auto *field = lv_textarea_create(lv_scr_act());
    lv_obj_set_pos(field, x, y);
    lv_obj_set_size(field, w, 48);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_max_length(field, limit);
    if (secret)
        lv_textarea_set_password_mode(field, true);
    lv_textarea_set_text(field, value);
    lv_obj_set_style_bg_color(field, c(theme::Card), 0);
    lv_obj_set_style_text_color(field, c(theme::Ink), 0);
    lv_obj_set_style_text_font(field, &fonts::montserrat16, 0);
    lv_obj_set_style_border_color(field, c(theme::Grid), 0);
    lv_obj_set_style_border_color(field, c(theme::Blue), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(field, c(theme::Blue), focusedCursor);
    lv_obj_set_style_border_width(field, 2, focusedCursor);
    lv_obj_set_style_border_side(field, LV_BORDER_SIDE_LEFT, focusedCursor);
    lv_obj_set_style_bg_opa(field, LV_OPA_TRANSP, focusedCursor);
    lv_obj_set_style_anim_time(field, 0, LV_PART_CURSOR);
    lv_obj_add_event_cb(field, focusInput, LV_EVENT_CLICKED, nullptr);
    return field;
}
void makeKeyboard() {
    constexpr lv_style_selector_t pressedItems =
        lv_style_selector_t(LV_PART_ITEMS) | lv_style_selector_t(LV_STATE_PRESSED);
    keyboard = lv_keyboard_create(lv_scr_act());
    // El constructor de LVGL alinea el teclado al fondo. set_pos() conservaría
    // esa alineación y desplazaría casi todo el teclado fuera de la pantalla.
    lv_obj_set_size(keyboard, 800, 240);
    lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, 0, 240);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, spanishLower, spanishCtrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, spanishUpper, spanishCtrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, spanishSpecial, spanishSpecialCtrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, spanishSpecial2, spanishSpecial2Ctrl);
    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, keyboardKey, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_set_style_bg_color(keyboard, c(theme::Background), 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_radius(keyboard, 0, 0);
    lv_obj_set_style_pad_all(keyboard, 7, 0);
    lv_obj_set_style_pad_row(keyboard, 5, 0);
    lv_obj_set_style_pad_column(keyboard, 5, 0);
    lv_obj_set_style_outline_width(keyboard, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(keyboard, c(theme::Button), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, c(0x265166), pressedItems);
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard, c(theme::Grid), LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, 7, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, c(theme::Ink), LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard, &fonts::montserrat20, LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard, hideKeyboard, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(keyboard, hideKeyboard, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    keyboardToggle = button(lv_scr_act(), 446, 13, 178, "Mostrar teclado", toggleKeyboard);
}
lv_obj_t *setupList(int x, int y, int w, int h) {
    auto *box = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, c(theme::Card), 0);
    lv_obj_set_style_border_color(box, c(theme::Grid), 0);
    lv_obj_set_style_pad_all(box, 2, 0);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    return box;
}
void startOtaCheck(lv_event_t *) {
    String err;
    if (!otaRequestCheck(err))
        setupMessage(err);
}
void startOtaInstall(lv_event_t *) {
    String err;
    if (!otaRequestInstall(err))
        setupMessage(err);
}
void paintOta() {
    if (page != Page::Ota)
        return;
    const OtaStatus st = otaGetStatus();
    if (otaCurrentLabel) {
        char curTxt[180];
        if (st.remoteVersion[0]) {
            snprintf(curTxt, sizeof(curTxt), "Instalado: v%s [%s]   ·   En GitHub: v%s %s",
                     st.currentVersion, st.activePartition[0] ? st.activePartition : "app0",
                     st.remoteVersion, st.remoteBuild);
        } else {
            snprintf(curTxt, sizeof(curTxt), "Instalado: v%s [%s]  ·  %s", st.currentVersion,
                     st.activePartition[0] ? st.activePartition : "app0", st.currentBuild);
        }
        if (strcmp(lv_label_get_text(otaCurrentLabel), curTxt) != 0)
            lv_label_set_text(otaCurrentLabel, curTxt);
    }
    if (otaStatusLabel) {
        uint32_t col = theme::Ink;
        if (st.state == OtaState::UpdateAvailable || st.state == OtaState::Rebooting)
            col = theme::GreenBold;
        else if (st.state == OtaState::UpToDate)
            col = theme::Green;
        else if (st.state == OtaState::Failed)
            col = theme::Red;
        else if (st.state == OtaState::Downloading || st.state == OtaState::Installing ||
                 st.state == OtaState::Checking)
            col = theme::Blue;
        lv_obj_set_style_text_color(otaStatusLabel, c(col), 0);
        if (strcmp(lv_label_get_text(otaStatusLabel), st.message) != 0)
            lv_label_set_text(otaStatusLabel, st.message);
    }
    if (otaChangesLabel && st.changes[0]) {
        if (strcmp(lv_label_get_text(otaChangesLabel), st.changes) != 0)
            lv_label_set_text(otaChangesLabel, st.changes);
    }
    if (otaProgressBar) {
        lv_bar_set_value(otaProgressBar, st.progressPercent, LV_ANIM_OFF);
        if (st.state == OtaState::Downloading || st.state == OtaState::Installing ||
            st.state == OtaState::Rebooting) {
            lv_obj_clear_flag(otaProgressBar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(otaProgressBar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const bool busy = otaBusy();
    const bool installing = st.state == OtaState::Downloading || st.state == OtaState::Installing ||
                            st.state == OtaState::Rebooting;
    if (st.state == OtaState::UpdateAvailable && !busy) {
        if (otaInstallBtn) {
            lv_obj_clear_flag(otaInstallBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(otaInstallBtn, LV_STATE_DISABLED);
        }
        if (otaCheckBtn)
            lv_obj_add_flag(otaCheckBtn, LV_OBJ_FLAG_HIDDEN);
        if (otaCancelBtn) {
            lv_obj_clear_flag(otaCancelBtn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(otaCancelBtn, LV_STATE_DISABLED);
            if (auto *lbl = lv_obj_get_child(otaCancelBtn, 0))
                lv_label_set_text(lbl, "No, ahora no");
        }
    } else {
        if (otaInstallBtn)
            lv_obj_add_flag(otaInstallBtn, LV_OBJ_FLAG_HIDDEN);
        if (otaCheckBtn) {
            lv_obj_clear_flag(otaCheckBtn, LV_OBJ_FLAG_HIDDEN);
            if (busy || installing)
                lv_obj_add_state(otaCheckBtn, LV_STATE_DISABLED);
            else
                lv_obj_clear_state(otaCheckBtn, LV_STATE_DISABLED);
        }
        if (otaCancelBtn) {
            lv_obj_clear_flag(otaCancelBtn, LV_OBJ_FLAG_HIDDEN);
            if (installing)
                lv_obj_add_state(otaCancelBtn, LV_STATE_DISABLED);
            else
                lv_obj_clear_state(otaCancelBtn, LV_STATE_DISABLED);
            if (auto *lbl = lv_obj_get_child(otaCancelBtn, 0))
                lv_label_set_text(lbl, "Volver a Ajustes");
        }
    }
}
void buildOta() {
    page = Page::Ota;
    setupHeader("Comprobar actualizaciones");
    configUiActive.store(false, std::memory_order_release);

    auto *card = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(card, 28, 68);
    lv_obj_set_size(card, 744, 284);
    lv_obj_set_style_bg_color(card, c(theme::Card), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, c(theme::Grid), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    otaCurrentLabel =
        label(card, 18, 12, 708, "Instalado: ...", &fonts::montserrat14, theme::Muted);
    lv_label_set_long_mode(otaCurrentLabel, LV_LABEL_LONG_DOT);
    otaStatusLabel = label(card, 18, 36, 708, "Comprobando actualizaciones en GitHub...",
                           &fonts::montserrat16, theme::Blue);
    lv_label_set_long_mode(otaStatusLabel, LV_LABEL_LONG_WRAP);

    label(card, 18, 86, 708, "CAMBIOS Y NOVEDADES DE LA VERSIÓN:", &fonts::montserrat14,
          theme::Blue);

    auto *changesBox = lv_obj_create(card);
    lv_obj_set_pos(changesBox, 18, 108);
    lv_obj_set_size(changesBox, 708, 134);
    lv_obj_set_style_bg_color(changesBox, c(theme::Background), 0);
    lv_obj_set_style_border_width(changesBox, 1, 0);
    lv_obj_set_style_border_color(changesBox, c(theme::Grid), 0);
    lv_obj_set_style_radius(changesBox, 12, 0);
    lv_obj_set_style_pad_all(changesBox, 10, 0);
    lv_obj_set_scroll_dir(changesBox, LV_DIR_VER);

    otaChangesLabel = label(changesBox, 0, 0, 676, "Cargando listado de cambios...",
                            &fonts::montserrat14, theme::Ink);
    lv_label_set_long_mode(otaChangesLabel, LV_LABEL_LONG_WRAP);

    otaProgressBar = lv_bar_create(card);
    lv_obj_set_pos(otaProgressBar, 18, 252);
    lv_obj_set_size(otaProgressBar, 708, 20);
    lv_bar_set_range(otaProgressBar, 0, 100);
    lv_bar_set_value(otaProgressBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(otaProgressBar, c(theme::Background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(otaProgressBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(otaProgressBar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(otaProgressBar, c(theme::Grid), LV_PART_MAIN);
    lv_obj_set_style_radius(otaProgressBar, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(otaProgressBar, c(theme::Green), LV_PART_INDICATOR);
    lv_obj_set_style_radius(otaProgressBar, 10, LV_PART_INDICATOR);
    lv_obj_add_flag(otaProgressBar, LV_OBJ_FLAG_HIDDEN);

    otaCheckBtn = button(lv_scr_act(), 28, 364, 356, "Comprobar actualizaciones", startOtaCheck);
    otaInstallBtn = button(lv_scr_act(), 28, 364, 356, "Sí, instalar ahora", startOtaInstall);
    lv_obj_set_style_bg_color(otaInstallBtn, c(0x1C563F), 0);
    lv_obj_add_flag(otaInstallBtn, LV_OBJ_FLAG_HIDDEN);
    otaCancelBtn = button(lv_scr_act(), 408, 364, 364, "Volver a Ajustes", gotoSettings);

    if (WiFi.status() == WL_CONNECTED && !otaBusy() && otaGetStatus().state == OtaState::Idle) {
        String err;
        otaRequestCheck(err);
    }
    paintOta();
    setupMessage(WiFi.status() == WL_CONNECTED
                     ? "Se verifica el SHA-256 antes de instalar. Tus cuentas y Wi-Fi se conservan."
                     : "Conecta primero el Wi-Fi para buscar actualizaciones.");
}
String settingsWifiAndLocText() {
    String wifiTxt;
    if (WiFi.status() == WL_CONNECTED && !WiFi.SSID().isEmpty()) {
        wifiTxt = "Wi-Fi: " + WiFi.SSID();
    } else if (config.ssid.isEmpty() && config.extraWifiCount == 0) {
        wifiTxt = "Wi-Fi: pendiente";
    } else {
        wifiTxt = "Wi-Fi: desconectado";
    }
    const String shortCity = displayCity();
    const String locTxt =
        (config.locationSet && !shortCity.isEmpty()) ? ("Clima: " + shortCity) : "Clima: sin ubicación";
    return wifiTxt + "   ·   " + locTxt;
}
void buildSettings() {
    page = Page::Settings;
    setupHeader("Ajustes", true);
    configUiActive.store(false, std::memory_order_release);
    label(lv_scr_act(), 29, 64, 730, "Configura todo desde la pantalla. El QR es opcional.",
          &fonts::montserrat16, theme::Muted);
    button(lv_scr_act(), 28, 96, 356, "Redes Wi-Fi", gotoWifi);
    button(lv_scr_act(), 408, 96, 364, "Ubicación y clima", gotoLocation);
    button(lv_scr_act(), 28, 156, 356, "Cuenta LibreLinkUp", gotoLibre);
    button(lv_scr_act(), 408, 156, 364, "Gestionar usuarios", usersPage);
    button(lv_scr_act(), 28, 216, 355, "Configurar con QR", gotoQr);
    button(lv_scr_act(), 408, 216, 364, "Cuenta Diabetes:M", gotoDiabetesM);
    button(lv_scr_act(), 28, 276, 744, "Comprobar actualizaciones", gotoOta);

    const String wifiAndLoc = settingsWifiAndLocText();
    settingsWifiLabel = label(lv_scr_act(), 30, 342, 742, wifiAndLoc.c_str(), &fonts::montserrat16);
    lv_label_set_long_mode(settingsWifiLabel, LV_LABEL_LONG_DOT);

    auto *libreLbl = label(lv_scr_act(), 30, 368, 742,
                           config.libreUser.isEmpty() ? "LibreLinkUp: pendiente"
                                                      : ("LibreLinkUp: " + config.libreUser).c_str(),
                           &fonts::montserrat16);
    lv_label_set_long_mode(libreLbl, LV_LABEL_LONG_DOT);

    auto *dmLbl = label(lv_scr_act(), 30, 394, 742,
                        config.diabetesmEnabled
                            ? ("Diabetes:M: activo (" +
                               (config.diabetesmUser.isEmpty() ? "sin usuario" : config.diabetesmUser) + ")")
                                  .c_str()
                            : "Diabetes:M: desactivado",
                        &fonts::montserrat16);
    lv_label_set_long_mode(dmLbl, LV_LABEL_LONG_DOT);
    if (strcmp(configStorageState(), "invalid") == 0 ||
        strcmp(configStorageState(), "unavailable") == 0)
        setupMessage("NVS no legible: haz una copia antes de guardar ajustes.");
    else
        setupMessage(config.ssid.isEmpty() ? "Empieza por Wi-Fi de 2,4 GHz."
                                           : "Los cambios se conservan al apagar el equipo.");
}

void selectNearby(lv_event_t *e) {
    const size_t i = size_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (i >= scanCount)
        return;
    chosenSsid = scanEntries[i].ssid;
    chosenSecure = scanEntries[i].secure;
    pendingPage = PendingPage::WifiEdit;
}
void selectSaved(lv_event_t *e) {
    const size_t i = size_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (i > config.extraWifiCount)
        return;
    chosenSsid = i == 0 ? config.ssid : config.extraWifi[i - 1].ssid;
    chosenSecure = true;
    pendingPage = PendingPage::WifiEdit;
}
void removeSaved(lv_event_t *e) {
    const size_t i = size_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (i > config.extraWifiCount)
        return;
    actionSsid = i == 0 ? config.ssid : config.extraWifi[i - 1].ssid;
    setupAction = SetupAction::RemoveWifi;
}
void paintSavedWifi() {
    if (!wifiSaved)
        return;
    lv_obj_clean(wifiSaved);
    if (config.ssid.isEmpty() && !config.extraWifiCount)
        label(wifiSaved, 10, 14, 340, "Ninguna red guardada", &fonts::montserrat14, theme::Muted);
    for (size_t i = 0; i < 1 + config.extraWifiCount; ++i) {
        const String name = i == 0 ? config.ssid : config.extraWifi[i - 1].ssid;
        if (name.isEmpty())
            continue;
        String title =
            name + (WiFi.status() == WL_CONNECTED && WiFi.SSID() == name ? "  ·  Conectada"
                                                                         : "  ·  Guardada");
        auto *b = button(wifiSaved, 5, 5 + int(i) * 58, 258, title.c_str(), selectSaved,
                         reinterpret_cast<void *>(uintptr_t(i)));
        lv_obj_set_height(b, 50);
        auto *del = button(wifiSaved, 268, 5 + int(i) * 58, 66, "X", removeSaved,
                           reinterpret_cast<void *>(uintptr_t(i)));
        lv_obj_set_height(del, 50);
    }
}
void paintNearbyWifi() {
    if (!wifiList)
        return;
    lv_obj_clean(wifiList);
    if (!scanCount) {
        label(wifiList, 12, 12, 330, "Sin redes visibles", &fonts::montserrat14, theme::Muted);
        return;
    }
    for (size_t i = 0; i < scanCount; ++i) {
        String title = String(scanEntries[i].ssid) + "  " + String(scanEntries[i].rssi) + " dBm";
        bool saved = config.ssid == scanEntries[i].ssid;
        for (size_t j = 0; j < config.extraWifiCount; ++j)
            saved |= config.extraWifi[j].ssid == scanEntries[i].ssid;
        title += saved ? " · Guardada" : " · Nueva";
        auto *b = button(wifiList, 5, 5 + int(i) * 56, 337, title.c_str(), selectNearby,
                         reinterpret_cast<void *>(uintptr_t(i)));
        lv_obj_set_height(b, 49);
    }
}
void rescanWifi(lv_event_t *) {
    String error;
    scanCount = 0;
    paintNearbyWifi();
    lastWifiJob = SetupJob::Idle;
    if (deviceWifiScan(error)) {
        wifiRequested = true;
        setupMessage("Buscando redes de 2,4 GHz...");
    } else
        setupMessage(error);
}
void manualWifi(lv_event_t *) {
    chosenSsid = "";
    chosenSecure = true;
    pendingPage = PendingPage::WifiEdit;
}
void buildWifi() {
    page = Page::Wifi;
    setupHeader("Redes Wi-Fi");
    label(lv_scr_act(), 28, 72, 330, "GUARDADAS", &fonts::montserrat16, theme::Blue);
    label(lv_scr_act(), 408, 72, 356, "DISPONIBLES", &fonts::montserrat16, theme::Blue);
    wifiSaved = setupList(25, 105, 354, 251);
    wifiList = setupList(402, 105, 373, 251);
    paintSavedWifi();
    scanCount = 0;
    lastWifiJob = SetupJob::Idle;
    button(lv_scr_act(), 27, 369, 225, "Buscar", rescanWifi);
    button(lv_scr_act(), 264, 369, 347, "Escribir SSID", manualWifi);
    button(lv_scr_act(), 626, 369, 147, "Ajustes", gotoSettings);
    if (config.ssid.isEmpty() || WiFi.status() == WL_CONNECTED) {
        String error;
        if (deviceWifiScan(error)) {
            wifiRequested = true;
            setupMessage("Buscando redes cercanas...");
        } else
            setupMessage(error);
    } else
        setupMessage("Conectando Wi-Fi; busca redes cuando se estabilice.");
}
void queueWifiFromForm(bool connect) {
    actionSsid = lv_textarea_get_text(wifiSsid);
    actionPassword = lv_textarea_get_text(wifiPass);
    bool known = actionSsid == config.ssid;
    for (size_t i = 0; i < config.extraWifiCount; ++i)
        known |= actionSsid == config.extraWifi[i].ssid;
    if (chosenSecure && actionSsid == chosenSsid && !known && actionPassword.isEmpty()) {
        setupMessage("Esta red requiere contraseña.");
        return;
    }
    hideKeyboard(nullptr);
    actionConnect = connect;
    setupAction = SetupAction::SaveWifi;
}
void saveWifiConnect(lv_event_t *) { queueWifiFromForm(true); }
void saveWifiOnly(lv_event_t *) { queueWifiFromForm(false); }
void buildWifiEdit() {
    page = Page::WifiEdit;
    setupHeader("Añadir o editar Wi-Fi");
    label(lv_scr_act(), 30, 68, 680, "Nombre (SSID, también para una red oculta)",
          &fonts::montserrat16, theme::Muted);
    wifiSsid = setupInput(27, 95, 746, chosenSsid.c_str(), 32);
    label(lv_scr_act(), 30, 157, 680, "Contraseña (vacía si la red está abierta o ya guardada)",
          &fonts::montserrat16, theme::Muted);
    wifiPass = setupInput(27, 184, 746, "", 63, true);
    label(lv_scr_act(), 30, 243, 740,
          "Solo se admite Wi-Fi de 2,4 GHz. Las redes guardadas persisten.", &fonts::montserrat16,
          theme::Muted);
    button(lv_scr_act(), 28, 315, 348, "Guardar y conectar", saveWifiConnect);
    button(lv_scr_act(), 394, 315, 377, "Guardar para después", saveWifiOnly);
    button(lv_scr_act(), 597, 373, 175, "Redes", gotoWifi);
    makeKeyboard();
}
void searchLocation(lv_event_t *) {
    actionSsid = lv_textarea_get_text(cityInput);
    hideKeyboard(nullptr);
    setupAction = SetupAction::SearchLocation;
}
void chooseLocation(lv_event_t *e) {
    actionIndex = size_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    setupAction = SetupAction::SaveLocation;
}
void paintLocations() {
    if (!locationList)
        return;
    lv_obj_clean(locationList);
    for (size_t i = 0; i < locationCount; ++i) {
        auto *b = button(locationList, 6, 5 + int(i) * 57, 690, locationEntries[i].name,
                         chooseLocation, reinterpret_cast<void *>(uintptr_t(i)));
        lv_obj_set_height(b, 51);
    }
}
void buildLocation() {
    page = Page::Location;
    setupHeader("Ubicación para el clima");
    label(lv_scr_act(), 28, 72, 700, "Busca una localidad y elige el resultado correcto.",
          &fonts::montserrat16, theme::Muted);
    cityInput = setupInput(28, 104, 541, config.locationSet ? config.city.c_str() : "", 80);
    button(lv_scr_act(), 586, 103, 186, "Buscar", searchLocation);
    locationList = setupList(28, 174, 744, 246);
    locationCount = 0;
    locationRequested = false;
    lastLocationJob = SetupJob::Idle;
    setupMessage(config.locationSet ? "Guardada: " + config.city
                                    : "Conecta el Wi-Fi antes de buscar.");
    makeKeyboard();
}
void startLibreLogin(lv_event_t *) {
    actionUser = lv_textarea_get_text(loginUser);
    actionPassword = lv_textarea_get_text(loginPass);
    const char *regions[] = {"eu", "eu2", "us", "de", "fr", "ae",
                             "ap", "au",  "ca", "jp", "la", "ru"};
    const uint16_t selected = lv_dropdown_get_selected(loginRegion);
    actionRegion = regions[selected < 12 ? selected : 0];
    hideKeyboard(nullptr);
    setupAction = SetupAction::Login;
}
void chooseLibre(lv_event_t *e) {
    actionIndex = size_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    setupAction = SetupAction::SaveLogin;
}
void paintLibreChoices() {
    if (!loginList)
        return;
    lv_obj_clean(loginList);
    for (size_t i = 0; i < loginCount; ++i) {
        auto *b = button(loginList, 5, 5 + int(i) * 54, 690, loginEntries[i].name, chooseLibre,
                         reinterpret_cast<void *>(uintptr_t(i)));
        lv_obj_set_height(b, 48);
    }
}
void buildLibre() {
    page = Page::Libre;
    setupHeader("Cuenta LibreLinkUp");
    label(lv_scr_act(), 28, 65, 660, "Correo receptor de las lecturas compartidas",
          &fonts::montserrat14, theme::Muted);
    loginUser = setupInput(28, 87, 548, config.libreUser.c_str(), 160);
    label(lv_scr_act(), 28, 150, 500, "Contraseña", &fonts::montserrat14, theme::Muted);
    loginPass = setupInput(28, 170, 548, "", 256, true);
    label(lv_scr_act(), 590, 65, 190, "Región", &fonts::montserrat14, theme::Muted);
    loginRegion = lv_dropdown_create(lv_scr_act());
    lv_obj_set_pos(loginRegion, 587, 87);
    lv_obj_set_size(loginRegion, 184, 48);
    lv_dropdown_set_options(
        loginRegion,
        "Europa\nEuropa 2\nEE. "
        "UU.\nAlemania\nFrancia\nEAU\nAsia\nAustralia\nCanadá\nJapón\nLatinoamérica\nRusia");
    lv_obj_set_style_text_font(loginRegion, &fonts::montserrat16, 0);
    // El texto usa la fuente castellana; el indicador es un símbolo LVGL.
    lv_obj_set_style_text_font(loginRegion, &lv_font_montserrat_14, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(loginRegion, c(theme::Ink), LV_PART_INDICATOR);
    lv_obj_set_style_text_font(lv_dropdown_get_list(loginRegion), &fonts::montserrat16, 0);
    const char *regions[] = {"eu", "eu2", "us", "de", "fr", "ae",
                             "ap", "au",  "ca", "jp", "la", "ru"};
    for (uint16_t i = 0; i < 12; ++i)
        if (config.libreRegion == regions[i])
            lv_dropdown_set_selected(loginRegion, i);
    button(lv_scr_act(), 584, 167, 189, "Iniciar sesión", startLibreLogin);
    label(lv_scr_act(), 28, 235, 725,
          "Después selecciona la persona con quien se comparten los datos.", &fonts::montserrat14,
          theme::Muted);
    loginList = setupList(28, 266, 744, 155);
    loginRequested = false;
    lastLoginJob = SetupJob::Idle;
    loginCount = 0;
    setupMessage(config.patientName.isEmpty() ? "Inicia sesión para obtener los usuarios."
                                              : "Usuario guardado: " + config.patientName);
    makeKeyboard();
}
void saveDiabetesM(lv_event_t *) {
    actionUser = lv_textarea_get_text(diabetesmUser);
    actionPassword = lv_textarea_get_text(diabetesmPass);
    actionConnect = diabetesmSwitch && lv_obj_has_state(diabetesmSwitch, LV_STATE_CHECKED);
    actionSyncFiltered = diabetesmMode && lv_dropdown_get_selected(diabetesmMode) == 1;
    actionTargetPatientId = config.patientId;
    if (diabetesmPatient && snapshot.connectionCount > 0) {
        const uint16_t idx = lv_dropdown_get_selected(diabetesmPatient);
        if (idx < snapshot.connectionCount) {
            actionTargetPatientId = snapshot.connections[idx].id;
        }
    }
    hideKeyboard(nullptr);
    setupAction = SetupAction::SaveDiabetesM;
}
void buildDiabetesM() {
    page = Page::DiabetesM;
    setupHeader("Cuenta Diabetes:M");
    label(lv_scr_act(), 28, 70, 744, "Sube automáticamente las lecturas de glucosa a Diabetes:M",
          &fonts::montserrat14, theme::Muted);

    auto styleDropdown = [](lv_obj_t *d) {
        lv_obj_set_style_bg_color(d, c(theme::Card), 0);
        lv_obj_set_style_border_color(d, c(theme::Grid), 0);
        lv_obj_set_style_border_width(d, 1, 0);
        lv_obj_set_style_radius(d, 12, 0);
        lv_obj_set_style_text_font(d, &fonts::montserrat16, 0);
        lv_obj_set_style_text_color(d, c(theme::Ink), 0);
        lv_obj_set_style_text_font(d, &lv_font_montserrat_14, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(d, c(theme::Ink), LV_PART_INDICATOR);
        lv_obj_t *list = lv_dropdown_get_list(d);
        lv_obj_set_style_bg_color(list, c(theme::Card), 0);
        lv_obj_set_style_border_color(list, c(theme::Grid), 0);
        lv_obj_set_style_border_width(list, 1, 0);
        lv_obj_set_style_radius(list, 12, 0);
        lv_obj_set_style_text_font(list, &fonts::montserrat16, 0);
        lv_obj_set_style_text_color(list, c(theme::Ink), 0);
        constexpr lv_style_selector_t selChecked =
            lv_style_selector_t(LV_PART_SELECTED) | lv_style_selector_t(LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(list, c(theme::Button), selChecked);
        lv_obj_set_style_text_color(list, c(theme::Ink), selChecked);
    };

    label(lv_scr_act(), 28, 94, 360, "Vincular a usuario LibreLinkUp", &fonts::montserrat14, theme::Muted);
    diabetesmPatient = lv_dropdown_create(lv_scr_act());
    lv_obj_set_pos(diabetesmPatient, 28, 114);
    lv_obj_set_size(diabetesmPatient, 360, 44);
    String options;
    int selectedIndex = 0;
    if (snapshot.connectionCount > 0) {
        for (size_t i = 0; i < snapshot.connectionCount; ++i) {
            if (i > 0)
                options += "\n";
            char u[64]{};
            const bool linked = dmLinkHasAccount(snapshot.connections[i].id, u, sizeof(u));
            options += snapshot.connections[i].name;
            if (linked)
                options += " (Vinculado)";
            if (config.patientId == snapshot.connections[i].id)
                selectedIndex = i;
        }
    } else {
        options = config.patientName.isEmpty() ? "Paciente actual" : config.patientName.c_str();
    }
    lv_dropdown_set_options(diabetesmPatient, options.c_str());
    styleDropdown(diabetesmPatient);
    lv_dropdown_set_selected(diabetesmPatient, selectedIndex);

    label(lv_scr_act(), 412, 94, 360, "Frecuencia y procesado", &fonts::montserrat14, theme::Muted);
    diabetesmMode = lv_dropdown_create(lv_scr_act());
    lv_obj_set_pos(diabetesmMode, 412, 114);
    lv_obj_set_size(diabetesmMode, 360, 44);
    lv_dropdown_set_options(diabetesmMode,
                            "Cada 2 min: Suavizado (Bolo)\n"
                            "Cada 15 min: Filtrado LibreView");
    styleDropdown(diabetesmMode);
    lv_dropdown_set_selected(diabetesmMode, config.diabetesmSyncFiltered ? 1 : 0);

    label(lv_scr_act(), 28, 168, 360, "Usuario / Correo de Diabetes:M", &fonts::montserrat14, theme::Muted);
    diabetesmUser = setupInput(28, 188, 360, config.diabetesmUser.c_str(), 160);
    lv_obj_set_size(diabetesmUser, 360, 44);

    label(lv_scr_act(), 412, 168, 360, "Contraseña (vacía para conservar)", &fonts::montserrat14, theme::Muted);
    diabetesmPass = setupInput(412, 188, 360, "", 256, true);
    lv_obj_set_size(diabetesmPass, 360, 44);

    diabetesmSwitch = lv_switch_create(lv_scr_act());
    lv_obj_set_pos(diabetesmSwitch, 28, 248);
    lv_obj_set_size(diabetesmSwitch, 60, 30);
    lv_obj_set_style_bg_color(
        diabetesmSwitch, c(theme::Green),
        lv_style_selector_t(LV_PART_INDICATOR) | lv_style_selector_t(LV_STATE_CHECKED));
    if (config.diabetesmEnabled)
        lv_obj_add_state(diabetesmSwitch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(diabetesmSwitch, LV_STATE_CHECKED);

    label(lv_scr_act(), 100, 254, 650, "Activar sincronización continua con Diabetes:M", &fonts::montserrat16);

    button(lv_scr_act(), 28, 292, 360, "Guardar", saveDiabetesM);
    button(lv_scr_act(), 412, 292, 360, "Ajustes", gotoSettings);

    setupMessage(config.diabetesmEnabled ? "Sincronización activa con cada lectura de LibreLinkUp."
                                         : "Activa la casilla para sincronizar.");
    makeKeyboard();
}
void clockPage(lv_event_t *) { pendingPage = PendingPage::Clock; }
void homePage(lv_event_t *) { pendingPage = PendingPage::Home; }
void usersPage(lv_event_t *) { pendingPage = PendingPage::Users; }
void setClock(lv_obj_t *timeOut, lv_obj_t *dateOut) {
    if (!timeOut || !dateOut)
        return;
    const time_t now = time(nullptr);
    if (now < 1700000000) {
        lv_label_set_text(timeOut, "--:--");
        lv_label_set_text(dateOut, "Sincronizando hora");
        return;
    }
    tm local{};
    localtime_r(&now, &local);
    char timeText[12], dateText[64];
    strftime(timeText, sizeof(timeText), "%H:%M", &local);
    const char *days[]{"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};
    const char *months[]{"enero", "febrero", "marzo",      "abril",   "mayo",      "junio",
                         "julio", "agosto",  "septiembre", "octubre", "noviembre", "diciembre"};
    char dayCap[24];
    snprintf(dayCap, sizeof(dayCap), "%s", days[local.tm_wday]);
    dayCap[0] = toupper(dayCap[0]);
    if (timeOut == timeLabel) {
        snprintf(dateText, sizeof(dateText), "%s\n%d de %s", dayCap, local.tm_mday,
                 months[local.tm_mon]);
    } else {
        snprintf(dateText, sizeof(dateText), "%s, %d de %s", dayCap, local.tm_mday,
                 months[local.tm_mon]);
    }
    lv_label_set_text(timeOut, timeText);
    lv_label_set_text(dateOut, dateText);
}
void updateWeather(lv_obj_t *temp, lv_obj_t *summary, lv_obj_t *icon, weathericons::Icon &glyph) {
    if (!temp || !summary || !icon)
        return;
    if (!snapshot.weatherValid) {
        lv_label_set_text(temp, "-- °C");
        lv_label_set_text(summary,
                          snapshot.weatherError[0] ? snapshot.weatherError : "Esperando clima...");
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const String city = displayCity();
    char value[24], details[180];
    snprintf(value, sizeof(value), "%.0f °C", snapshot.temperature);
    if (temp == weatherLabel) {
        snprintf(details, sizeof(details), "%s\nMáx %.0f° · Mín %.0f°",
                 weatherText(snapshot.weatherCode), snapshot.high, snapshot.low);
    } else {
        snprintf(details, sizeof(details), "%s\n%s · Sens. %.0f°\nMáx %.0f° · Mín %.0f°", city.c_str(),
                 weatherText(snapshot.weatherCode), snapshot.apparent, snapshot.high, snapshot.low);
    }
    lv_label_set_text(temp, value);
    lv_label_set_text(summary, details);
    glyph.code = snapshot.weatherCode;
    glyph.day = snapshot.weatherIsDay;
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(icon);
}
void updateForecast() {
    if (!hoursBox)
        return;
    if (hoursTab && daysTab && summaryTab) {
        lv_obj_set_style_bg_color(
            hoursTab, c(forecastView == ForecastView::Hours ? 0x1C563F : theme::Button), 0);
        lv_obj_set_style_bg_color(
            daysTab, c(forecastView == ForecastView::Days ? 0x1C563F : theme::Button), 0);
        lv_obj_set_style_bg_color(
            summaryTab, c(forecastView == ForecastView::Summary ? 0x1C563F : theme::Button), 0);
    }
    lv_obj_clean(hoursBox);
    if (forecastView == ForecastView::Hours) {
        if (!snapshot.hourCount) {
            label(hoursBox, 26, 95, 700,
                  snapshot.weatherError[0] ? snapshot.weatherError
                                           : "Cargando previsión por horas...",
                  &fonts::montserrat20, theme::Muted);
            return;
        }
        for (size_t i = 0; i < snapshot.hourCount; ++i) {
            auto *card = lv_obj_create(hoursBox);
            lv_obj_set_pos(card, 8 + int(i) * 125, 9);
            lv_obj_set_size(card, 120, 220);
            lv_obj_set_style_bg_color(card, c(i == 0 ? theme::Button : theme::Background), 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_border_color(card, c(theme::Grid), 0);
            lv_obj_set_style_radius(card, 16, 0);
            lv_obj_set_style_pad_all(card, 0, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            const time_t t = snapshot.hours[i].epoch;
            tm local{};
            localtime_r(&t, &local);
            char hour[10], temp[20], rain[32];
            strftime(hour, sizeof(hour), "%H:%M", &local);
            snprintf(temp, sizeof(temp), "%.0f °C", snapshot.hours[i].temperature);
            snprintf(rain, sizeof(rain), "Lluvia %d%%", snapshot.hours[i].rain);
            auto *txt = label(card, 0, 10, 120, hour, &fonts::montserrat20,
                              i == 0 ? theme::Ink : theme::Muted);
            lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
            hourGlyphs[i].code = snapshot.hours[i].code;
            hourGlyphs[i].day = snapshot.hours[i].isDay;
            hourGlyphs[i].background = i == 0 ? theme::Button : theme::Background;
            weathericons::create(card, 25, 38, 70, &hourGlyphs[i]);
            txt = label(card, 0, 118, 120, temp, &fonts::montserrat28);
            lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
            txt = label(card, 0, 168, 120, rain, &fonts::montserrat16, theme::Blue);
            lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
        }
    } else if (forecastView == ForecastView::Days) {
        if (!snapshot.dayCount) {
            label(hoursBox, 26, 95, 700,
                  snapshot.weatherError[0] ? snapshot.weatherError
                                           : "Cargando previsión de cuatro días...",
                  &fonts::montserrat20, theme::Muted);
            return;
        }
        const char *weekday[]{"Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"};
        for (size_t i = 0; i < snapshot.dayCount; ++i) {
            auto *card = lv_obj_create(hoursBox);
            lv_obj_set_pos(card, 8 + int(i) * 189, 9);
            lv_obj_set_size(card, 182, 220);
            lv_obj_set_style_bg_color(card, c(i == 0 ? theme::Button : theme::Background), 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_border_color(card, c(theme::Grid), 0);
            lv_obj_set_style_radius(card, 16, 0);
            lv_obj_set_style_pad_all(card, 0, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            const time_t t = snapshot.days[i].epoch;
            tm local{};
            localtime_r(&t, &local);
            char day[22], temp[28], rain[32];
            if (i == 0)
                strlcpy(day, "Hoy", sizeof(day));
            else
                snprintf(day, sizeof(day), "%s %d", weekday[local.tm_wday], local.tm_mday);
            snprintf(temp, sizeof(temp), "%.0f° / %.0f°", snapshot.days[i].high,
                     snapshot.days[i].low);
            if (snapshot.days[i].rain >= 0)
                snprintf(rain, sizeof(rain), "Lluvia %d%%", snapshot.days[i].rain);
            else
                strlcpy(rain, "Lluvia --", sizeof(rain));
            auto *txt = label(card, 0, 10, 182, day, &fonts::montserrat20,
                              i == 0 ? theme::Ink : theme::Muted);
            lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
            dayGlyphs[i].code = snapshot.days[i].code;
            dayGlyphs[i].day = true;
            dayGlyphs[i].background = i == 0 ? theme::Button : theme::Background;
            weathericons::create(card, 53, 38, 76, &dayGlyphs[i]);
            txt = label(card, 0, 120, 182, temp, &fonts::montserrat28);
            lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
            txt = label(card, 4, 158, 174, shortWeatherText(snapshot.days[i].code),
                        &fonts::montserrat16, theme::Muted);
            lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
            txt = label(card, 0, 184, 182, rain, &fonts::montserrat16, theme::Blue);
            lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
        }
    } else if (forecastView == ForecastView::Summary) {
        // Card 1: Tiempo en Rango (24h)
        auto *tirCard = lv_obj_create(hoursBox);
        lv_obj_set_pos(tirCard, 8, 9);
        lv_obj_set_size(tirCard, 242, 220);
        lv_obj_set_style_bg_color(tirCard, c(theme::Background), 0);
        lv_obj_set_style_border_width(tirCard, 1, 0);
        lv_obj_set_style_border_color(tirCard, c(theme::Grid), 0);
        lv_obj_set_style_radius(tirCard, 16, 0);
        lv_obj_set_style_pad_all(tirCard, 0, 0);
        lv_obj_clear_flag(tirCard, LV_OBJ_FLAG_SCROLLABLE);

        label(tirCard, 14, 10, 214, "TIEMPO EN RANGO (24H)", &fonts::montserrat14, theme::Blue);
        if (snapshot.dailySummary.valid && snapshot.dailySummary.totalReadings > 0) {
            char tirVal[16];
            snprintf(tirVal, sizeof(tirVal), "%.0f%%", snapshot.dailySummary.tirPercent);
            uint32_t col = tirColor(snapshot.dailySummary.tirPercent);
            label(tirCard, 14, 34, 214, tirVal, &fonts::montserrat48, col);

            const char *status = snapshot.dailySummary.tirPercent >= 90.0f ? "Excelente (>90%)"
                                 : snapshot.dailySummary.tirPercent >= 70.0f ? "Objetivo cumplido"
                                                                             : "Bajo objetivo (<70%)";
            label(tirCard, 14, 94, 214, status, &fonts::montserrat16, col);

            char sub[80];
            snprintf(sub, sizeof(sub), "En rango: %.0f%%\nBajo: %.0f%%  ·  Alto: %.0f%%",
                     snapshot.dailySummary.tirPercent, snapshot.dailySummary.lowPercent,
                     snapshot.dailySummary.highPercent);
            auto *subTxt = label(tirCard, 14, 134, 214, sub, &fonts::montserrat14, theme::Muted);
            lv_label_set_long_mode(subTxt, LV_LABEL_LONG_WRAP);
        } else {
            label(tirCard, 14, 34, 214, "--%", &fonts::montserrat48, theme::Muted);
            label(tirCard, 14, 94, 214, "Sin lecturas 24h", &fonts::montserrat16, theme::Muted);
            label(tirCard, 14, 134, 214, "Sincronizando con Diabetes:M / LibreLinkUp...", &fonts::montserrat14, theme::Muted);
        }

        // Card 2: Estadísticas 24h (RAM + Diabetes:M)
        auto *statsCard = lv_obj_create(hoursBox);
        lv_obj_set_pos(statsCard, 258, 9);
        lv_obj_set_size(statsCard, 242, 220);
        lv_obj_set_style_bg_color(statsCard, c(theme::Background), 0);
        lv_obj_set_style_border_width(statsCard, 1, 0);
        lv_obj_set_style_border_color(statsCard, c(theme::Grid), 0);
        lv_obj_set_style_radius(statsCard, 16, 0);
        lv_obj_set_style_pad_all(statsCard, 0, 0);
        lv_obj_clear_flag(statsCard, LV_OBJ_FLAG_SCROLLABLE);

        label(statsCard, 14, 10, 214, "ESTADÍSTICAS 24H", &fonts::montserrat14, theme::Muted);
        if (snapshot.dailySummary.valid && snapshot.dailySummary.totalReadings > 0) {
            char avgVal[24];
            snprintf(avgVal, sizeof(avgVal), "%d mg/dL", snapshot.dailySummary.averageGlucose);
            uint32_t col = rangeColor(snapshot.dailySummary.averageGlucose);
            label(statsCard, 14, 38, 214, "Media 24h:", &fonts::montserrat14, theme::Muted);
            label(statsCard, 14, 60, 214, avgVal, &fonts::montserrat28, col);

            char minMax[48];
            snprintf(minMax, sizeof(minMax), "Mín: %d  ·  Máx: %d",
                     snapshot.dailySummary.minGlucose, snapshot.dailySummary.maxGlucose);
            label(statsCard, 14, 114, 214, minMax, &fonts::montserrat16);

            char countStr[48];
            snprintf(countStr, sizeof(countStr), "Muestras (5 min): %u",
                     unsigned(snapshot.dailySummary.totalReadings));
            label(statsCard, 14, 148, 214, countStr, &fonts::montserrat14, theme::Muted);
            label(statsCard, 14, 178, 214, "Diabetes:M · RAM (sin desgaste)", &fonts::montserrat14,
                  theme::Blue);
        } else {
            label(statsCard, 14, 50, 214, "---", &fonts::montserrat32, theme::Muted);
            label(statsCard, 14, 110, 214, "Historial en RAM vacío", &fonts::montserrat16, theme::Muted);
            label(statsCard, 14, 150, 214, "Se sincroniza cada 15 min desde Diabetes:M", &fonts::montserrat14, theme::Muted);
        }

        // Card 3: Paciente y Diabetes:M
        auto *syncCard = lv_obj_create(hoursBox);
        lv_obj_set_pos(syncCard, 508, 9);
        lv_obj_set_size(syncCard, 246, 220);
        lv_obj_set_style_bg_color(syncCard, c(theme::Background), 0);
        lv_obj_set_style_border_width(syncCard, 1, 0);
        lv_obj_set_style_border_color(syncCard, c(theme::Grid), 0);
        lv_obj_set_style_radius(syncCard, 16, 0);
        lv_obj_set_style_pad_all(syncCard, 0, 0);
        lv_obj_clear_flag(syncCard, LV_OBJ_FLAG_SCROLLABLE);

        label(syncCard, 14, 10, 218, "PACIENTE Y DIARIO", &fonts::montserrat14, theme::Muted);
        const char *pName = config.patientName.isEmpty() ? "Sin paciente" : config.patientName.c_str();
        auto *pTxt = label(syncCard, 14, 34, 218, pName, &fonts::montserrat20);
        lv_label_set_long_mode(pTxt, LV_LABEL_LONG_WRAP);

        char dmAcc[64]{};
        const bool linked = dmLinkHasAccount(config.patientId.c_str(), dmAcc, sizeof(dmAcc));
        if (linked) {
            label(syncCard, 14, 90, 218, "Diabetes:M vinculado", &fonts::montserrat16, theme::GreenBold);
            const char *syncMode = config.diabetesmSyncFiltered ? "Subida: 15m · TIR: cada 15m"
                                                                : "Subida: 2m · TIR: cada 15m";
            label(syncCard, 14, 122, 218, syncMode, &fonts::montserrat14, theme::Muted);
            if (dmAcc[0]) {
                auto *accTxt = label(syncCard, 14, 154, 218, dmAcc, &fonts::montserrat14, theme::Muted);
                lv_label_set_long_mode(accTxt, LV_LABEL_LONG_WRAP);
            }
        } else {
            label(syncCard, 14, 90, 218, "Sin cuenta Diabetes:M", &fonts::montserrat16, theme::Orange);
            label(syncCard, 14, 122, 218, "Vincula este usuario en Ajustes para subir lecturas.", &fonts::montserrat14, theme::Muted);
        }
    }
}

} // namespace
void uiInit() {
    config = configSnapshot();
    // lv_obj_clean() no borra los eventos del objeto pantalla: se registra una sola vez.
    lv_obj_add_event_cb(lv_scr_act(), sleepHomeBackground, LV_EVENT_CLICKED, nullptr);
    buildHome();
}
void uiShowSetup() { buildSettings(); }
void uiShowHome() {
    if (page == Page::Setup)
        buildHome();
}
void uiClearGraphSelection() {
    if (!selectedGraphEpoch)
        return;
    selectedGraphEpoch = 0;
    if (page == Page::Home && graph)
        lv_obj_invalidate(graph);
}
void uiTick() {
    const auto revision = configRevision.load(std::memory_order_acquire);
    if (revision != uiConfigRevision) {
        config = configSnapshot();
        uiConfigRevision = revision;
    }
    PendingPage next = pendingPage;
    pendingPage = PendingPage::None;
    if (mutationAwaiting != SetupAction::None && next != PendingPage::None) {
        next = PendingPage::None;
        setupMessage("Espera a que se guarden los ajustes...");
    }
    switch (next) {
    case PendingPage::Home:
        portalStop();
        buildHome();
        break;
    case PendingPage::Clock:
        portalStop();
        buildClock();
        break;
    case PendingPage::Users:
        portalStop();
        buildUsers();
        break;
    case PendingPage::Settings:
        portalStop();
        buildSettings();
        break;
    case PendingPage::Wifi:
        buildWifi();
        break;
    case PendingPage::WifiEdit:
        buildWifiEdit();
        break;
    case PendingPage::Location:
        buildLocation();
        break;
    case PendingPage::Libre:
        buildLibre();
        break;
    case PendingPage::Setup:
        portalStart();
        buildSetup();
        break;
    case PendingPage::CloseSetup:
        portalStop();
        buildSettings();
        break;
    case PendingPage::DiabetesM:
        buildDiabetesM();
        break;
    case PendingPage::Ota:
        buildOta();
        break;
    case PendingPage::None:
        break;
    }
    if (setupAction != SetupAction::None && mutationAwaiting != SetupAction::None) {
        setupAction = SetupAction::None;
        setupMessage("Espera a que se guarden los ajustes...");
    }
    if (setupAction != SetupAction::None) {
        const SetupAction action = setupAction;
        setupAction = SetupAction::None;
        String error;
        switch (action) {
        case SetupAction::SaveWifi:
            if (deviceQueueWifi(actionSsid, actionPassword, actionConnect, error)) {
                mutationAwaiting = action;
                setupMessage("Guardando Wi-Fi...");
            } else
                setupMessage(error);
            actionPassword = "";
            break;
        case SetupAction::RemoveWifi:
            if (deviceQueueRemoveWifi(actionSsid, error)) {
                mutationAwaiting = action;
                setupMessage("Eliminando red...");
            } else
                setupMessage(error);
            break;
        case SetupAction::SearchLocation:
            if (deviceGeocode(actionSsid, error)) {
                locationRequested = true;
                lastLocationJob = SetupJob::Idle;
                locationCount = 0;
                paintLocations();
                setupMessage("Buscando localidad...");
            } else
                setupMessage(error);
            break;
        case SetupAction::SaveLocation:
            if (actionIndex < locationCount &&
                deviceQueueLocation(locationEntries[actionIndex], error)) {
                mutationAwaiting = action;
                setupMessage("Guardando ubicación...");
            } else
                setupMessage(error.isEmpty() ? "Selecciona una ubicación válida" : error);
            break;
        case SetupAction::Login:
            if (deviceLibreLogin(actionUser, actionPassword, actionRegion, error)) {
                loginRequested = true;
                lastLoginJob = SetupJob::Idle;
                loginCount = 0;
                paintLibreChoices();
                setupMessage("Conectando con LibreLinkUp...");
            } else
                setupMessage(error);
            actionPassword = "";
            break;
        case SetupAction::SaveLogin:
            if (actionIndex < loginCount &&
                deviceQueueLibreUser(loginEntries[actionIndex], error)) {
                mutationAwaiting = action;
                setupMessage("Guardando cuenta y usuario...");
            } else
                setupMessage(error.isEmpty() ? "Selecciona un usuario" : error);
            break;
        case SetupAction::SaveDiabetesM:
            if (deviceQueueDiabetesM(actionUser, actionPassword, actionConnect, error,
                                     actionTargetPatientId, actionSyncFiltered)) {
                mutationAwaiting = action;
                setupMessage("Guardando Diabetes:M...");
            } else
                setupMessage(error);
            actionPassword = "";
            break;
        case SetupAction::None:
            break;
        }
    }
    if (mutationAwaiting != SetupAction::None) {
        String error;
        const auto state = deviceMutationResult(error);
        if (state == SetupJob::Ready) {
            const SetupAction saved = mutationAwaiting;
            mutationAwaiting = SetupAction::None;
            if (saved == SetupAction::RemoveWifi) {
                paintSavedWifi();
                setupMessage("Red eliminada.");
            } else if (saved == SetupAction::SaveWifi) {
                pendingPage = PendingPage::Settings;
                setupMessage("Wi-Fi guardado.");
            } else if (saved == SetupAction::SaveDiabetesM) {
                pendingPage = PendingPage::Settings;
                setupMessage("Diabetes:M guardado.");
            } else {
                pendingPage = PendingPage::Settings;
                setupMessage("Cambios guardados.");
            }
        } else if (state == SetupJob::Failed) {
            mutationAwaiting = SetupAction::None;
            setupMessage(error.isEmpty() ? "No se pudieron guardar los cambios" : error);
        }
    }
    if (page == Page::Wifi && wifiRequested) {
        size_t count = 0;
        String error;
        const auto state = deviceWifiResults(scanEntries, 16, count, error);
        if (state != lastWifiJob || (state == SetupJob::Ready && count != scanCount)) {
            lastWifiJob = state;
            if (state == SetupJob::Ready) {
                scanCount = count;
                paintNearbyWifi();
                setupMessage("Toca una red o escribe el SSID manualmente.");
            }
            if (state == SetupJob::Failed)
                setupMessage(error);
        }
    }
    if (page == Page::Location && locationRequested) {
        size_t count = 0;
        String error;
        const auto state = deviceGeocodeResults(locationEntries, 8, count, error);
        if (state != lastLocationJob) {
            lastLocationJob = state;
            if (state == SetupJob::Ready) {
                locationCount = count;
                paintLocations();
                setupMessage(count ? "Elige una localidad de la lista."
                                   : "No se encontraron localidades.");
            }
            if (state == SetupJob::Failed)
                setupMessage(error);
        }
    }
    if (page == Page::Libre && loginRequested) {
        size_t count = 0;
        String error;
        const auto state = deviceLibreResults(loginEntries, MAX_CONNECTIONS, count, error);
        if (state != lastLoginJob) {
            lastLoginJob = state;
            if (state == SetupJob::Ready) {
                loginCount = count;
                paintLibreChoices();
                setupMessage("Elige el usuario que quieres ver.");
            }
            if (state == SetupJob::Failed)
                setupMessage(error);
        }
    }
    if (page == Page::Ota) {
        paintOta();
        return;
    }
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    memcpy(&snapshot, sharedState, sizeof(snapshot));
    xSemaphoreGive(stateMutex);
    if (page == Page::Setup) {
        if (portalMode() != renderedPortal) {
            buildSetup();
            return;
        }
        if (renderedPortal == PortalMode::WifiAccessPoint && setupInfo) {
            String text = "Red temporal:  " + portalSsid() + "\n\nClave:  " + portalPassword() +
                          "\n\nPortal:  http://192.168.4.1\n\nTras guardar se cerrará esta red y "
                          "aparecerá un segundo QR.";
            if (strcmp(lv_label_get_text(setupInfo), text.c_str()) != 0)
                lv_label_set_text(setupInfo, text.c_str());
        }
        return;
    }
    if (page == Page::Settings || page == Page::Wifi || page == Page::WifiEdit ||
        page == Page::Location || page == Page::Libre || page == Page::DiabetesM) {
        if (page == Page::Settings) {
            if (settingsWifiLabel) {
                const String wifiAndLoc = settingsWifiAndLocText();
                if (strcmp(lv_label_get_text(settingsWifiLabel), wifiAndLoc.c_str()) != 0)
                    lv_label_set_text(settingsWifiLabel, wifiAndLoc.c_str());
            }
            if (setupStatus && strcmp(configStorageState(), "invalid") &&
                strcmp(configStorageState(), "unavailable")) {
                const char *wifiMessage = config.ssid.isEmpty() ? "Empieza por Wi-Fi de 2,4 GHz."
                                          : WiFi.status() == WL_CONNECTED
                                              ? "Wi-Fi conectado. Los cambios están guardados."
                                              : "Conectando Wi-Fi; espera antes de iniciar sesión.";
                if (strcmp(lv_label_get_text(setupStatus), wifiMessage))
                    lv_label_set_text(setupStatus, wifiMessage);
            }
        }
        return;
    }
    if (page == Page::Users) {
        if (renderedConnections != snapshot.connectionsFetched ||
            renderedConnectionCount != snapshot.connectionCount ||
            strcmp(renderedConnectionsError, snapshot.connectionsError))
            buildUsers();
        return;
    }
    if (page == Page::Home && pendingPatientId[0]) {
        if (strcmp(snapshot.activePatientId, pendingPatientId) == 0 ||
            strcmp(snapshot.glucoseError, "No se pudo guardar el usuario seleccionado") == 0) {
            pendingPatientId[0] = 0;
            resetRenderCache();
            lv_obj_clear_flag(graph, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(arrow, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (!lv_obj_has_flag(graph, LV_OBJ_FLAG_HIDDEN))
                lv_obj_add_flag(graph, LV_OBJ_FLAG_HIDDEN);
            if (!lv_obj_has_flag(arrow, LV_OBJ_FLAG_HIDDEN))
                lv_obj_add_flag(arrow, LV_OBJ_FLAG_HIDDEN);
            if (strcmp(lv_label_get_text(glucoseLabel), "---"))
                lv_label_set_text(glucoseLabel, "---");
            if (strcmp(lv_label_get_text(detailLabel), "Cambio pendiente"))
                lv_label_set_text(detailLabel, "Cambio pendiente");
            const int64_t waitingMinute = time(nullptr) / 60;
            if (waitingMinute != renderedMinute) {
                renderedMinute = waitingMinute;
                setClock(timeLabel, dateLabel);
            }
            return;
        }
    }
    const int64_t now = time(nullptr), minute = now / 60;
    const bool minuteChanged = minute != renderedMinute;
    if (minuteChanged)
        renderedMinute = minute;
    const gluco::Point *latest = latestReading();
    const int64_t latestEpoch = latest ? latest->epoch : 0;
    const int latestValue = latest ? latest->glucose : 0;
    const int currentTir =
        snapshot.dailySummary.valid ? int(std::lround(snapshot.dailySummary.tirPercent * 10.0f)) : -1;
    const bool summaryChanged = renderedTotalReadings != snapshot.dailySummary.totalReadings ||
                                renderedTir != currentTir;
    const bool glucoseChanged =
        renderedGlucose != snapshot.glucoseFetched || renderedPoints != snapshot.pointCount ||
        renderedLatestEpoch != latestEpoch || renderedLatestValue != latestValue ||
        summaryChanged || strcmp(renderedGlucoseError, snapshot.glucoseError);
    const int64_t requestStep = snapshot.glucoseRequestStartedMs ? 1 : 0;
    const bool chartChanged =
        renderedGlucose != snapshot.glucoseFetched || renderedPoints != snapshot.pointCount ||
        renderedLatestEpoch != latestEpoch || renderedLatestValue != latestValue ||
        strcmp(renderedGlucoseError, snapshot.glucoseError) != 0 ||
        requestStep != renderedRequestStep;
    if (selectedGraphEpoch && renderedGlucose >= 0 && snapshot.glucoseFetched > 0 &&
        snapshot.glucoseFetched != renderedGlucose)
        uiClearGraphSelection();
    if (chartChanged && selectedGraphEpoch) {
        bool found = false;
        for (size_t i = 0; i < snapshot.pointCount; ++i)
            if (snapshot.points[i].epoch == selectedGraphEpoch) {
                found = true;
                break;
            }
        if (snapshot.currentValid && snapshot.current.epoch == selectedGraphEpoch)
            found = true;
        if (!found)
            selectedGraphEpoch = 0;
    }
    const bool weatherChanged = renderedWeather != snapshot.weatherFetched ||
                                strcmp(renderedWeatherError, snapshot.weatherError);
    if (page == Page::Home) {
        if (minuteChanged)
            setClock(timeLabel, dateLabel);
        if (weatherChanged)
            updateWeather(weatherLabel, weatherSummary, homeIcon, homeGlyph);
        if (glucoseChanged || minuteChanged) {
            if (latest) {
                char value[12];
                snprintf(value, sizeof(value), "%d", latest->glucose);
                lv_label_set_text(glucoseLabel, value);
                const uint32_t color = rangeColor(latest->glucose);
                lv_obj_set_style_text_color(glucoseLabel, c(color), 0);
                lv_obj_set_style_bg_color(glucoseAccent, c(color), 0);
                int diff = 0, minutes = 0;
                char detail[80];
                if (snapshot.currentValid && snapshot.pointCount &&
                    snapshot.current.epoch - snapshot.points[snapshot.pointCount - 1].epoch >= 60 &&
                    gluco::graphConnect(snapshot.points[snapshot.pointCount - 1],
                                        snapshot.current)) {
                    const auto &previous = snapshot.points[snapshot.pointCount - 1];
                    diff = int(snapshot.current.glucose) - int(previous.glucose);
                    minutes = int((snapshot.current.epoch - previous.epoch + 30) / 60);
                    snprintf(detail, sizeof(detail), "%+d en %d min", diff, minutes);
                } else if (!snapshot.currentValid &&
                           gluco::delta(snapshot.points, snapshot.pointCount, diff, minutes))
                    snprintf(detail, sizeof(detail), "%+d en %d min", diff, minutes);
                else
                    strlcpy(detail, "Sin delta", sizeof(detail));
                lv_label_set_text(detailLabel, detail);
            } else {
                lv_label_set_text(glucoseLabel, "---");
                lv_obj_set_style_text_color(glucoseLabel, c(theme::Muted), 0);
                lv_label_set_text(detailLabel, "Sin lectura");
                lv_obj_set_style_bg_color(glucoseAccent, c(theme::Muted), 0);
            }
            lv_obj_invalidate(arrow);
            if (tirHomeLabel) {
                if (snapshot.dailySummary.valid && snapshot.dailySummary.totalReadings > 0) {
                    char tirBuf[32];
                    snprintf(tirBuf, sizeof(tirBuf), "TIR %.0f%%", snapshot.dailySummary.tirPercent);
                    lv_label_set_text(tirHomeLabel, tirBuf);
                    lv_obj_set_style_text_color(tirHomeLabel, c(tirColor(snapshot.dailySummary.tirPercent)), 0);
                } else {
                    lv_label_set_text(tirHomeLabel, "TIR --%");
                    lv_obj_set_style_text_color(tirHomeLabel, c(theme::Muted), 0);
                }
            }
        }
        renderedRequestStep = requestStep;
        // Reposicionar la gráfica cada diez minutos sin redibujarla cada minuto.
        const int64_t graphBucket = now / (10 * 60);
        if (chartChanged ||
            ((snapshot.pointCount || snapshot.currentValid) && graphBucket != renderedGraphBucket))
            lv_obj_invalidate(graph);
        renderedGraphBucket = graphBucket;
    } else if (page == Page::Clock) {
        if (minuteChanged)
            setClock(clockBig, clockDate);
        if (weatherChanged)
            updateWeather(clockWeather, clockSummary, clockIcon, clockGlyph);
        if (weatherChanged || ((glucoseChanged || summaryChanged) && forecastView == ForecastView::Summary))
            updateForecast();
    }
    renderedGlucose = snapshot.glucoseFetched;
    renderedPoints = snapshot.pointCount;
    renderedLatestEpoch = latestEpoch;
    renderedLatestValue = latestValue;
    renderedTotalReadings = snapshot.dailySummary.totalReadings;
    renderedTir = currentTir;
    strlcpy(renderedGlucoseError, snapshot.glucoseError, sizeof(renderedGlucoseError));
    renderedWeather = snapshot.weatherFetched;
    strlcpy(renderedWeatherError, snapshot.weatherError, sizeof(renderedWeatherError));
}

