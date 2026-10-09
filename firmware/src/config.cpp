#include "app.hpp"
#include "net.hpp"
#include "providers.hpp"
#include "runtime.hpp"
#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <cmath>
#include <esp_wifi.h>
#include <new>
#include <nvs.h>
using net::Doc;
std::atomic<uint32_t> configRevision{0};
std::atomic<bool> configUiActive{false};
namespace {
Config settings;
SemaphoreHandle_t settingsMutex = nullptr, resultsMutex = nullptr;
QueueHandle_t jobs = nullptr;
std::atomic<bool> busy{false};
std::atomic<const char *> storageState{"missing"};
enum class Kind {
    Scan,
    Login,
    Geocode,
    Wifi,
    RemoveWifi,
    Location,
    Patient,
    Full,
    Networks,
    Reset
};
struct Request {
    Kind kind = Kind::Scan;
    String text, password, region;
    bool connect = false;
    LocationChoice location{};
    ConnectionChoice patient{};
    Config full;
};
struct Results {
    SetupJob scan = SetupJob::Idle, login = SetupJob::Idle, geocode = SetupJob::Idle,
             mutation = SetupJob::Idle;
    char scanError[160]{}, loginError[220]{}, geocodeError[160]{}, mutationError[160]{};
    WifiScanEntry wifi[16]{};
    size_t wifiCount = 0;
    ConnectionChoice people[MAX_CONNECTIONS]{};
    size_t peopleCount = 0;
    LocationChoice locations[8]{};
    size_t locationCount = 0;
    char region[8]{};
};
Results results;
LibreCredentials staged;
class ResultLock {
  public:
    ResultLock() { xSemaphoreTake(resultsMutex, portMAX_DELAY); }
    ~ResultLock() { xSemaphoreGive(resultsMutex); }
};
void toJson(const Config &c, Doc &d) {
    d["ssid"] = c.ssid;
    d["wifi_password"] = c.wifiPass;
    auto networks = d.createNestedArray("wifi_networks");
    for (size_t i = 0; i < c.extraWifiCount; ++i) {
        auto n = networks.createNestedObject();
        n["ssid"] = c.extraWifi[i].ssid;
        n["password"] = c.extraWifi[i].password;
    }
    d["libre_user"] = c.libreUser;
    d["libre_password"] = c.librePass;
    d["libre_region"] = c.libreRegion;
    d["libre_version"] = c.libreVersion;
    d["patient_id"] = c.patientId;
    d["patient_name"] = c.patientName;
    d["city"] = c.city;
    d["timezone"] = c.timezone;
    d["latitude"] = c.latitude;
    d["longitude"] = c.longitude;
    d["location_set"] = c.locationSet;
}
void fromJson(JsonVariantConst d, Config &c) {
    c.ssid = d["ssid"] | "";
    c.wifiPass = d["wifi_password"] | "";
    c.libreUser = d["libre_user"] | "";
    c.librePass = d["libre_password"] | "";
    c.libreRegion = d["libre_region"] | "eu";
    c.libreVersion = d["libre_version"] | "5.1.1";
    c.patientId = d["patient_id"] | "";
    c.patientName = d["patient_name"] | "";
    c.city = d["city"] | "";
    c.timezone = d["timezone"] | "Europe/Madrid";
    c.latitude = d["latitude"] | 0.0f;
    c.longitude = d["longitude"] | 0.0f;
    c.locationSet = d["location_set"] | false;
    c.extraWifiCount = 0;
    for (JsonObjectConst n : d["wifi_networks"].as<JsonArrayConst>()) {
        if (c.extraWifiCount == MAX_EXTRA_WIFI)
            break;
        c.extraWifi[c.extraWifiCount++] = {String(n["ssid"] | ""), String(n["password"] | "")};
    }
}
bool validWifi(const String &ssid, const String &password, String &error) {
    if (ssid.isEmpty() || ssid.length() > 32) {
        error = "SSID no válido";
        return false;
    }
    if (!password.isEmpty() && (password.length() < 8 || password.length() > 63)) {
        error = "La clave Wi-Fi debe tener 8–63 caracteres";
        return false;
    }
    return true;
}
bool validRegion(const String &r) {
    for (const char *s : {"eu", "eu2", "us", "de", "fr", "ae", "ap", "au", "ca", "jp", "la", "ru"})
        if (r == s)
            return true;
    return false;
}
bool validLocation(const Config &c) {
    return (c.timezone == "Europe/Madrid" || c.timezone == "Atlantic/Canary" ||
            c.timezone == "UTC") &&
           (!c.locationSet ||
            (!c.city.isEmpty() && c.city.length() <= 139 && std::isfinite(c.latitude) &&
             std::isfinite(c.longitude) && c.latitude >= -90 && c.latitude <= 90 &&
             c.longitude >= -180 && c.longitude <= 180));
}
bool validFull(const Config &c, String &error) {
    if (!validWifi(c.ssid, c.wifiPass, error))
        return false;
    if (c.extraWifiCount > MAX_EXTRA_WIFI)
        return false;
    for (size_t i = 0; i < c.extraWifiCount; ++i) {
        if (!validWifi(c.extraWifi[i].ssid, c.extraWifi[i].password, error))
            return false;
        if (c.extraWifi[i].ssid == c.ssid) {
            error = "Red principal repetida";
            return false;
        }
        for (size_t j = 0; j < i; ++j)
            if (c.extraWifi[j].ssid == c.extraWifi[i].ssid) {
                error = "Redes repetidas";
                return false;
            }
    }
    if (c.libreUser.isEmpty() || c.libreUser.length() > 160 || c.librePass.isEmpty() ||
        c.librePass.length() > 256 || !validRegion(c.libreRegion) || c.libreVersion.isEmpty() ||
        c.libreVersion.length() > 20 || c.patientId.isEmpty() || c.patientId.length() > 99 ||
        c.patientName.length() > 99 || !validLocation(c)) {
        error = "Revisa cuenta, usuario y ubicación";
        return false;
    }
    return true;
}
bool persist(const Config &next, String &error) {
    if (strcmp(storageState.load(), "invalid") == 0 ||
        strcmp(storageState.load(), "unavailable") == 0) {
        error = "NVS dañada o inaccesible; respalda antes de guardar";
        return false;
    }
    Doc d(10000);
    toJson(next, d);
    if (d.overflowed()) {
        error = "Ajustes demasiado grandes";
        return false;
    }
    String json;
    serializeJson(d, json);
    xSemaphoreTake(storageMutex, portMAX_DELAY);
    Preferences p;
    bool ok = p.begin("glucowave", false);
    if (ok) {
        ok = p.putString("settings", json) == json.length();
        p.end();
    }
    xSemaphoreGive(storageMutex);
    if (!ok) {
        error = "No se pudo guardar en NVS";
        return false;
    }
    configPublish(next);
    storageState = "ok";
    return true;
}
SetupJob &stateFor(Kind k) {
    if (k == Kind::Scan)
        return results.scan;
    if (k == Kind::Login)
        return results.login;
    if (k == Kind::Geocode)
        return results.geocode;
    return results.mutation;
}
char *errorFor(Kind k) {
    if (k == Kind::Scan)
        return results.scanError;
    if (k == Kind::Login)
        return results.loginError;
    if (k == Kind::Geocode)
        return results.geocodeError;
    return results.mutationError;
}
bool submit(Request *r, String &error) {
    if (!r) {
        error = "Memoria insuficiente";
        return false;
    }
    bool expected = false;
    if (!busy.compare_exchange_strong(expected, true)) {
        delete r;
        error = "Espera a que termine la operación anterior";
        return false;
    }
    {
        ResultLock lock;
        stateFor(r->kind) = SetupJob::Running;
        errorFor(r->kind)[0] = 0;
    }
    if (xQueueSend(jobs, &r, 0) != pdPASS) {
        {
            ResultLock lock;
            stateFor(r->kind) = SetupJob::Failed;
            strlcpy(errorFor(r->kind), "Cola de ajustes ocupada", 160);
        }
        delete r;
        busy.store(false);
        error = "Cola de ajustes ocupada";
        return false;
    }
    return true;
}
void applyTimezone(const Config &c) {
    const char *tz = c.timezone == "Atlantic/Canary" ? "WET0WEST,M3.5.0/1,M10.5.0"
                     : c.timezone == "UTC"           ? "UTC0"
                                                     : "CET-1CEST,M3.5.0,M10.5.0/3";
    setenv("TZ", tz, 1);
    tzset();
    if (time(nullptr) < 1700000000) {
        configTzTime(tz, "pool.ntp.org", "time.cloudflare.com");
    }
}
bool scan(String &error) {
    WiFi.scanDelete();
    int found = WiFi.scanNetworks(true, false, false, 120);
    const uint32_t deadline = millis() + 8000;
    while (found == WIFI_SCAN_RUNNING && !runtime::due(millis(), deadline)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        found = WiFi.scanComplete();
    }
    if (found < 0) {
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        error = "Búsqueda Wi-Fi agotada; puedes escribir el SSID";
        return false;
    }
    WifiScanEntry items[16]{};
    size_t count = 0;
    for (int i = 0; i < found; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty())
            continue;
        size_t j = 0;
        while (j < count && ssid != items[j].ssid)
            ++j;
        if (j == count) {
            if (count == 16)
                continue;
            ++count;
        } else if (items[j].rssi >= WiFi.RSSI(i))
            continue;
        strlcpy(items[j].ssid, ssid.c_str(), sizeof(items[j].ssid));
        items[j].rssi = WiFi.RSSI(i);
        items[j].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    std::sort(items, items + count,
              [](const WifiScanEntry &a, const WifiScanEntry &b) { return a.rssi > b.rssi; });
    WiFi.scanDelete();
    {
        ResultLock lock;
        memcpy(results.wifi, items, sizeof(items));
        results.wifiCount = count;
    }
    return true;
}
bool geocode(const String &query, String &error) {
    String q = query;
    q.trim();
    if (q.length() < 2) {
        error = "Escribe una localidad";
        return false;
    }
    Doc filter(1024);
    filter["message"] = true;
    auto p = filter.createNestedArray("results").createNestedObject();
    for (const char *k : {"name", "admin1", "country", "latitude", "longitude", "timezone"})
        p[k] = true;
    Doc d(12 * 1024);
    const String url =
        "http://geocoding-api.open-meteo.com/v1/search?count=8&language=es&format=json&name=" +
        net::encode(q);
    Serial.printf("[GEOCODE] Buscando localidad: %s\n", q.c_str());
    auto r = net::request(url, d, "GET", "", {}, 24 * 1024, filter.as<JsonVariantConst>());
    if (r.status != 200) {
        Serial.printf("[GEOCODE] Error HTTP %d: %s\n", r.status, r.error.c_str());
        error = r.error;
        return false;
    }
    LocationChoice items[8]{};
    size_t count = 0;
    for (JsonObjectConst item : d["results"].as<JsonArrayConst>()) {
        if (count == 8)
            break;
        if (!item["latitude"].is<double>() || !item["longitude"].is<double>())
            continue;
        String name = item["name"] | "";
        for (const char *k : {"admin1", "country"})
            if (*(item[k] | ""))
                name += ", " + String(item[k] | "");
        auto &out = items[count++];
        strlcpy(out.name, name.c_str(), sizeof(out.name));
        out.latitude = item["latitude"];
        out.longitude = item["longitude"];
        const String zone = item["timezone"] | "UTC";
        strlcpy(out.timezone,
                (zone == "Europe/Madrid" || zone == "Atlantic/Canary") ? zone.c_str() : "UTC",
                sizeof(out.timezone));
    }
    Serial.printf("[GEOCODE] Resultados obtenidos: %u\n", unsigned(count));
    {
        ResultLock lock;
        memcpy(results.locations, items, sizeof(items));
        results.locationCount = count;
    }
    return true;
}
} // namespace
void configRuntimeInit() {
    settingsMutex = xSemaphoreCreateMutex();
    resultsMutex = xSemaphoreCreateMutex();
    jobs = xQueueCreate(1, sizeof(Request *));
    if (!settingsMutex || !resultsMutex || !jobs)
        abort();
}
Config configSnapshot() {
    xSemaphoreTake(settingsMutex, portMAX_DELAY);
    Config copy = settings;
    xSemaphoreGive(settingsMutex);
    return copy;
}
void configPublish(const Config &next) {
    xSemaphoreTake(settingsMutex, portMAX_DELAY);
    settings = next;
    configRevision.fetch_add(1, std::memory_order_release);
    xSemaphoreGive(settingsMutex);
}
void configLoad() {
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open("glucowave", NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND)
        return;
    if (opened != ESP_OK) {
        storageState = "unavailable";
        return;
    }
    nvs_close(handle);
    Preferences p;
    String raw;
    if (!p.begin("glucowave", true)) {
        storageState = "unavailable";
        return;
    }
    raw = p.getString("settings", "");
    p.end();
    if (raw.isEmpty())
        return;
    Doc d(10000);
    if (deserializeJson(d, raw) || !d.is<JsonObject>()) {
        storageState = "invalid";
        return;
    }
    Config next;
    fromJson(d.as<JsonVariantConst>(), next);
    String error;
    bool valid = next.ssid.length() <= 32 && next.wifiPass.length() <= 63 &&
                 next.libreUser.length() <= 160 && next.librePass.length() <= 256 &&
                 validRegion(next.libreRegion) && next.libreVersion.length() <= 20 &&
                 next.patientId.length() <= 99 && next.patientName.length() <= 99 &&
                 next.city.length() <= 139 && validLocation(next);
    if (!next.ssid.isEmpty())
        valid = valid && validWifi(next.ssid, next.wifiPass, error);
    for (size_t i = 0; valid && i < next.extraWifiCount; ++i)
        valid = validWifi(next.extraWifi[i].ssid, next.extraWifi[i].password, error);
    if (!valid) {
        storageState = "invalid";
        return;
    }
    configPublish(next);
    storageState = "ok";
}
bool configNeedsSetup() {
    const Config c = configSnapshot();
    return c.ssid.isEmpty() || c.libreUser.isEmpty() || c.patientId.isEmpty();
}
const char *configStorageState() { return storageState.load(); }
bool configSelectPatient(const char *id, const char *name) {
    if (!id || !*id || strlen(id) > 99 || !name || strlen(name) > 99)
        return false;
    Config next = configSnapshot();
    if (next.patientId == id && next.patientName == name)
        return true;
    next.patientId = id;
    next.patientName = name;
    String error;
    return persist(next, error);
}
bool requestPatientSelection(const char *id, const char *name) {
    if (!id || !*id || strlen(id) > 99 || !name || strlen(name) > 99)
        return false;
    PatientSelection p{};
    strlcpy(p.id, id, sizeof(p.id));
    strlcpy(p.name, name, sizeof(p.name));
    return xQueueOverwrite(patientQueue, &p) == pdPASS;
}
bool deviceWifiScan(String &error) { return submit(new (std::nothrow) Request{}, error); }
SetupJob deviceWifiResults(WifiScanEntry *out, size_t cap, size_t &count, String &error) {
    ResultLock lock;
    count = 0;
    if (results.scan == SetupJob::Failed)
        error = results.scanError;
    if (results.scan == SetupJob::Ready)
        for (size_t i = 0; i < results.wifiCount && count < cap; ++i)
            out[count++] = results.wifi[i];
    return results.scan;
}
bool deviceGeocode(const String &query, String &error) {
    String q = query;
    q.trim();
    if (q.length() < 2 || q.length() > 80) {
        error = "Escribe una localidad";
        return false;
    }
    auto *r = new (std::nothrow) Request{};
    if (r) {
        r->kind = Kind::Geocode;
        r->text = q;
    }
    return submit(r, error);
}
SetupJob deviceGeocodeResults(LocationChoice *out, size_t cap, size_t &count, String &error) {
    ResultLock lock;
    count = 0;
    if (results.geocode == SetupJob::Failed)
        error = results.geocodeError;
    if (results.geocode == SetupJob::Ready)
        for (size_t i = 0; i < results.locationCount && count < cap; ++i)
            out[count++] = results.locations[i];
    return results.geocode;
}
bool deviceLibreLogin(const String &user, const String &password, const String &region,
                      String &error) {
    auto *r = new (std::nothrow) Request{};
    if (r) {
        r->kind = Kind::Login;
        r->text = user;
        r->password = password;
        r->region = region;
    }
    return submit(r, error);
}
SetupJob deviceLibreResults(ConnectionChoice *out, size_t cap, size_t &count, String &error) {
    ResultLock lock;
    count = 0;
    if (results.login == SetupJob::Failed)
        error = results.loginError;
    if (results.login == SetupJob::Ready)
        for (size_t i = 0; i < results.peopleCount && count < cap; ++i)
            out[count++] = results.people[i];
    return results.login;
}
String deviceLibreRegion() {
    ResultLock lock;
    return results.region;
}
bool deviceQueueWifi(const String &ssid, const String &pass, bool connect, String &error) {
    auto *r = new (std::nothrow) Request{};
    if (r) {
        r->kind = Kind::Wifi;
        r->text = ssid;
        r->password = pass;
        r->connect = connect;
    }
    return submit(r, error);
}
bool deviceQueueRemoveWifi(const String &ssid, String &error) {
    auto *r = new (std::nothrow) Request{};
    if (r) {
        r->kind = Kind::RemoveWifi;
        r->text = ssid;
    }
    return submit(r, error);
}
bool deviceQueueLocation(const LocationChoice &p, String &error) {
    auto *r = new (std::nothrow) Request{};
    if (r) {
        r->kind = Kind::Location;
        r->location = p;
    }
    return submit(r, error);
}
bool deviceQueueLibreUser(const ConnectionChoice &p, String &error) {
    auto *r = new (std::nothrow) Request{};
    if (r) {
        r->kind = Kind::Patient;
        r->patient = p;
    }
    return submit(r, error);
}
SetupJob deviceMutationResult(String &error) {
    ResultLock lock;
    if (results.mutation == SetupJob::Failed)
        error = results.mutationError;
    return results.mutation;
}
// Synchronous implementations are called only by configService on the data core.
bool deviceSaveWifi(const String &name, const String &password, bool connect, String &error) {
    Config next = configSnapshot();
    String ssid = name;
    ssid.trim();
    String pass = password;
    if (pass.isEmpty()) {
        if (ssid == next.ssid)
            pass = next.wifiPass;
        for (size_t i = 0; i < next.extraWifiCount; ++i)
            if (ssid == next.extraWifi[i].ssid)
                pass = next.extraWifi[i].password;
    }
    if (!validWifi(ssid, pass, error))
        return false;
    if (connect || next.ssid.isEmpty()) {
        for (size_t i = 0; i < next.extraWifiCount;) {
            if (next.extraWifi[i].ssid == ssid) {
                for (size_t j = i + 1; j < next.extraWifiCount; ++j)
                    next.extraWifi[j - 1] = next.extraWifi[j];
                --next.extraWifiCount;
            } else
                ++i;
        }
        if (next.ssid != ssid && !next.ssid.isEmpty()) {
            if (next.extraWifiCount == MAX_EXTRA_WIFI) {
                error = "Libera una red guardada";
                return false;
            }
            next.extraWifi[next.extraWifiCount++] = {next.ssid, next.wifiPass};
        }
        next.ssid = ssid;
        next.wifiPass = pass;
    } else if (next.ssid == ssid)
        next.wifiPass = pass;
    else {
        size_t i = 0;
        while (i < next.extraWifiCount && next.extraWifi[i].ssid != ssid)
            ++i;
        if (i == MAX_EXTRA_WIFI) {
            error = "Solo caben cinco redes";
            return false;
        }
        if (i == next.extraWifiCount)
            ++next.extraWifiCount;
        next.extraWifi[i] = {ssid, pass};
    }
    if (!persist(next, error))
        return false;
    // Portal radio transitions run later in the same data worker.
    if (!portalActive() && connect && !(WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid))
        WiFi.begin(ssid.c_str(), pass.c_str());
    return true;
}
bool deviceRemoveWifi(const String &ssid, String &error) {
    Config next = configSnapshot();
    bool reconnect = false;
    if (next.ssid == ssid) {
        if (!next.extraWifiCount) {
            error = "Añade otra red antes de eliminar la principal";
            return false;
        }
        next.ssid = next.extraWifi[0].ssid;
        next.wifiPass = next.extraWifi[0].password;
        reconnect = true;
        for (size_t i = 1; i < next.extraWifiCount; ++i)
            next.extraWifi[i - 1] = next.extraWifi[i];
        --next.extraWifiCount;
    } else {
        size_t i = 0;
        while (i < next.extraWifiCount && next.extraWifi[i].ssid != ssid)
            ++i;
        if (i == next.extraWifiCount) {
            error = "Red no guardada";
            return false;
        }
        for (size_t j = i + 1; j < next.extraWifiCount; ++j)
            next.extraWifi[j - 1] = next.extraWifi[j];
        --next.extraWifiCount;
    }
    if (!persist(next, error))
        return false;
    if (reconnect && !portalActive())
        WiFi.begin(next.ssid.c_str(), next.wifiPass.c_str());
    return true;
}
bool deviceSaveLocation(const LocationChoice &p, String &error) {
    Config next = configSnapshot();
    next.city = p.name;
    next.latitude = p.latitude;
    next.longitude = p.longitude;
    next.timezone = p.timezone;
    next.locationSet = true;
    if (!validLocation(next)) {
        error = "Ubicación no válida";
        return false;
    }
    if (!persist(next, error))
        return false;
    applyTimezone(next);
    return true;
}
bool deviceSaveLibreUser(const ConnectionChoice &p, String &error) {
    bool valid = false;
    {
        ResultLock lock;
        if (results.login == SetupJob::Ready)
            for (size_t i = 0; i < results.peopleCount; ++i)
                if (strcmp(results.people[i].id, p.id) == 0)
                    valid = true;
    }
    if (!valid || staged.user.isEmpty()) {
        error = "Inicia sesión antes de seleccionar usuario";
        return false;
    }
    Config next = configSnapshot();
    next.libreUser = staged.user;
    next.librePass = staged.password;
    next.libreRegion = staged.region;
    next.libreVersion = staged.version;
    next.patientId = p.id;
    next.patientName = p.name;
    return persist(next, error);
}
bool configQueueFull(const Config &next, String &error, bool networksOnly) {
    auto *r = new (std::nothrow) Request{};
    if (r) {
        r->kind = networksOnly ? Kind::Networks : Kind::Full;
        r->full = next;
    }
    return submit(r, error);
}
bool configQueueReset(String &error) {
    auto *r = new (std::nothrow) Request{};
    if (r)
        r->kind = Kind::Reset;
    return submit(r, error);
}
bool configBusy() { return busy.load(); }
bool configService() {
    Request *r = nullptr;
    if (xQueueReceive(jobs, &r, 0) != pdPASS)
        return false;
    String error;
    bool ok = false;
    appDiagnosticStage("Ajustes: operación en CPU0");
    switch (r->kind) {
    case Kind::Scan:
        ok = scan(error);
        break;
    case Kind::Geocode:
        ok = geocode(r->text, error);
        break;
    case Kind::Login: {
        Config c = configSnapshot();
        LibreCredentials credentials{r->text, r->password, r->region, c.libreVersion};
        credentials.user.trim();
        if (credentials.password.isEmpty() && credentials.user == c.libreUser &&
            credentials.region == c.libreRegion)
            credentials.password = c.librePass;
        if (credentials.user.isEmpty() || credentials.user.length() > 160 ||
            credentials.password.isEmpty() || credentials.password.length() > 256 ||
            !validRegion(credentials.region)) {
            error = "Revisa correo, contraseña y región";
            break;
        }
        ConnectionChoice people[MAX_CONNECTIONS]{};
        size_t count = 0;
        String region;
        ok = libreListConnections(credentials, people, MAX_CONNECTIONS, count, region, error);
        if (ok) {
            credentials.region = region;
            staged = credentials;
            ResultLock lock;
            memcpy(results.people, people, sizeof(people));
            results.peopleCount = count;
            strlcpy(results.region, region.c_str(), sizeof(results.region));
        }
        break;
    }
    case Kind::Wifi:
        ok = deviceSaveWifi(r->text, r->password, r->connect, error);
        break;
    case Kind::RemoveWifi:
        ok = deviceRemoveWifi(r->text, error);
        break;
    case Kind::Location:
        ok = deviceSaveLocation(r->location, error);
        break;
    case Kind::Patient:
        ok = deviceSaveLibreUser(r->patient, error);
        break;
    case Kind::Networks: {
        if (r->full.extraWifiCount > MAX_EXTRA_WIFI) {
            error = "Demasiadas redes";
            break;
        }
        Config next = configSnapshot();
        next.extraWifiCount = r->full.extraWifiCount;
        for (size_t i = 0; i < next.extraWifiCount; ++i)
            next.extraWifi[i] = r->full.extraWifi[i];
        // Validate networks even before LibreLinkUp has been configured.
        ok = next.extraWifiCount <= MAX_EXTRA_WIFI;
        for (size_t i = 0; ok && i < next.extraWifiCount; ++i) {
            ok = validWifi(next.extraWifi[i].ssid, next.extraWifi[i].password, error) &&
                 next.extraWifi[i].ssid != next.ssid;
            for (size_t j = 0; ok && j < i; ++j)
                ok = next.extraWifi[i].ssid != next.extraWifi[j].ssid;
        }
        if (ok)
            ok = persist(next, error);
        else if (error.isEmpty())
            error = "Redes repetidas o no válidas";
        break;
    }
    case Kind::Full:
        ok = validFull(r->full, error) && persist(r->full, error);
        if (ok)
            applyTimezone(r->full);
        break;
    case Kind::Reset: {
        xSemaphoreTake(storageMutex, portMAX_DELAY);
        Preferences p;
        ok = true;
        for (const char *ns : {"glucowave", "glucousers", "glucohist", "glucodiag"})
            if (p.begin(ns, false)) {
                ok = p.clear() && ok;
                p.end();
            } else
                ok = false;
        xSemaphoreGive(storageMutex);
        if (ok) {
            configPublish(Config{});
            markPlannedRestart();
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        } else
            error = "No se pudo limpiar NVS";
        break;
    }
    }
    {
        ResultLock lock;
        stateFor(r->kind) = ok ? SetupJob::Ready : SetupJob::Failed;
        strlcpy(errorFor(r->kind), error.c_str(), 160);
    }
    delete r;
    busy.store(false);
    appDiagnosticStage("Ajustes: inactivo");
    return true;
}
