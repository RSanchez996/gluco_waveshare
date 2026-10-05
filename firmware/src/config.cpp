#include "app.hpp"
#include "net.hpp"
#include "providers.hpp"
#include "web_assets.hpp"
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <new>

using net::Doc;
std::atomic<uint32_t> configRevision{0};
std::atomic<bool> configUiActive{false};

namespace {
WebServer web(80);
DNSServer dns;
String apName, apPass, csrf;
std::atomic<PortalMode> mode{PortalMode::Off};
bool routesReady = false;
bool serverRunning = false;
const char *settingsLoadState="missing";
uint32_t started = 0, transitionAt = 0, closeAt = 0, rebootAt = 0, wifiDeadline = 0;
constexpr uint32_t kPortalMs = 10 * 60 * 1000;
constexpr uint32_t kWifiConnectMs = 25000;
// La tarea periódica HTTPS, estable en esta placa, usa CPU1/prioridad 0.
// Durante los ajustes se pausa; las consultas temporales usan la misma
// afinidad para liberar CPU0 a Wi-Fi y el barrido RGB. LVGL sigue en loopTask
// y conserva su prioridad superior a la de este trabajo.
constexpr BaseType_t kHttpsCore = 1;
enum class LibreJobState : uint8_t { Idle, Running, Ready, Failed };
std::atomic<LibreJobState> libreJobState{LibreJobState::Idle};
ConnectionChoice *libreJobChoices = nullptr;
size_t libreJobCount = 0;
char libreJobRegion[8]{};
char libreJobError[220]{};
enum class GeocodeJobState : uint8_t { Idle, Running, Ready, Failed };
std::atomic<GeocodeJobState> geocodeJobState{GeocodeJobState::Idle};
LocationChoice geocodeChoices[8]{};
size_t geocodeCount=0;
char geocodeError[160]{};
struct NearbyWifi { String ssid; int rssi = -100; bool secured = true; };
NearbyWifi nearbyWifi[16];
size_t nearbyWifiCount = 0;
bool wifiScanPending = false, wifiScanReady = false;
bool wifiScanHttpHeld = false;
uint32_t wifiScanStarted = 0, lastWifiScan = 0, lastWifiScanCheck = 0;
LibreCredentials stagedDeviceLogin;
enum class MutationKind:uint8_t { Wifi, RemoveWifi, Location, LibreUser };
struct Mutation {
    MutationKind kind; String ssid,password; bool connectNow=false;
    LocationChoice location{}; ConnectionChoice person{};
};
std::atomic<SetupJob> mutationState{SetupJob::Idle};
char mutationError[160]{};

void cancelWifiScan() {
    if (wifiScanPending) esp_wifi_scan_stop();
    WiFi.scanDelete();
    wifiScanPending = false;
    if (wifiScanHttpHeld) { xSemaphoreGive(httpMutex); wifiScanHttpHeld = false; }
}

void collectWifiScan(int found) {
    nearbyWifiCount = 0;
    for (int i = 0; i < found; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;
        const int rssi = WiFi.RSSI(i);
        size_t j = 0;
        while (j < nearbyWifiCount && nearbyWifi[j].ssid != ssid) ++j;
        if (j == nearbyWifiCount) {
            if (nearbyWifiCount >= 16) continue;
            ++nearbyWifiCount;
        } else if (nearbyWifi[j].rssi >= rssi) continue;
        nearbyWifi[j] = {ssid, rssi, WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
    }
    std::sort(nearbyWifi, nearbyWifi + nearbyWifiCount,
        [](const NearbyWifi &a, const NearbyWifi &b) { return a.rssi > b.rssi; });
    WiFi.scanDelete();
    wifiScanPending = false;
    wifiScanReady = true;
    if (wifiScanHttpHeld) { xSemaphoreGive(httpMutex); wifiScanHttpHeld = false; }
}

void fail(int code, const String &message) {
    Doc d(1000); d["message"] = message;
    String json; serializeJson(d, json);
    web.send(code, "application/json", json);
}

void headers() {
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("X-Frame-Options", "DENY");
    web.sendHeader("X-Content-Type-Options", "nosniff");
}

bool allowed(bool mutation = false) {
    const PortalMode current = mode.load();
    bool correctInterface = false;
    if (current == PortalMode::WifiAccessPoint)
        correctInterface = web.client().localIP() == WiFi.softAPIP();
    else if (current == PortalMode::LocalNetwork)
        correctInterface = web.client().localIP() == WiFi.localIP();
    if (!correctInterface) {
        fail(403, "El portal no está disponible desde esta interfaz de red");
        return false;
    }
    started = millis();
    if (mutation && web.header("X-Setup-Token") != csrf) {
        fail(403, "Formulario caducado; vuelve a escanear el QR");
        return false;
    }
    headers();
    return true;
}

bool body(Doc &d, size_t limit) {
    if (!allowed(true)) return false;
    String raw = web.arg("plain");
    if (raw.length() > limit || deserializeJson(d, raw) || !d.is<JsonObject>()) {
        fail(400, "Datos no válidos");
        return false;
    }
    return true;
}

void toJson(const Config &c, Doc &d, bool secrets) {
    d["ssid"] = c.ssid;
    JsonArray networks=d.createNestedArray("wifi_networks");
    for(size_t i=0;i<c.extraWifiCount;++i){
        JsonObject entry=networks.createNestedObject();
        entry["ssid"]=c.extraWifi[i].ssid;
        if(secrets)entry["password"]=c.extraWifi[i].password;
        else entry["has_password"]=!c.extraWifi[i].password.isEmpty();
    }
    d["libre_user"] = c.libreUser;
    d["libre_region"] = c.libreRegion;
    d["libre_version"] = c.libreVersion;
    d["patient_id"] = c.patientId;
    d["patient_name"] = c.patientName;
    d["city"] = c.city;
    d["timezone"] = c.timezone;
    d["latitude"] = c.latitude;
    d["longitude"] = c.longitude;
    d["location_set"] = c.locationSet;
    if (secrets) {
        d["wifi_password"] = c.wifiPass;
        d["libre_password"] = c.librePass;
    } else {
        d["has_wifi_password"] = !c.wifiPass.isEmpty();
        d["has_libre_password"] = !c.librePass.isEmpty();
    }
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
    if(d["wifi_networks"].is<JsonArrayConst>()){
        c.extraWifiCount=0;
        for(JsonObjectConst item:d["wifi_networks"].as<JsonArrayConst>()){
            if(c.extraWifiCount>=MAX_EXTRA_WIFI)break;
            c.extraWifi[c.extraWifiCount++]={String(item["ssid"] | ""),String(item["password"] | "")};
        }
    }
}

bool persist(const Config &next) {
    Doc stored(10000); toJson(next, stored, true);
    String json; serializeJson(stored, json);
    xSemaphoreTake(storageMutex, portMAX_DELAY);
    Preferences p;
    bool ok = p.begin("glucowave", false);
    if (ok) {
        ok = p.putString("settings", json) == json.length();
        p.end();
    }
    xSemaphoreGive(storageMutex);
    if(ok)displayRequestResync();
    return ok;
}

bool validRegion(const String &r) {
    for (const char *x : {"eu","eu2","us","de","fr","ae","ap","au","ca","jp","la","ru"})
        if (r == x) return true;
    return false;
}

bool validWifi(const String &ssid, const String &pass, String &error) {
    if (ssid.isEmpty() || ssid.length() > 32) {
        error = "SSID no válido"; return false;
    }
    if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 63)) {
        error = "La clave Wi-Fi debe tener entre 8 y 63 caracteres"; return false;
    }
    return true;
}

bool validate(const Config &c, String &error) {
    if (!validWifi(c.ssid, c.wifiPass, error)) return false;
    if(c.extraWifiCount>MAX_EXTRA_WIFI){error="Demasiadas redes Wi-Fi";return false;}
    for(size_t i=0;i<c.extraWifiCount;++i){
        if(!validWifi(c.extraWifi[i].ssid,c.extraWifi[i].password,error))return false;
        if(c.extraWifi[i].ssid==c.ssid){error="Una red adicional coincide con la principal";return false;}
        for(size_t j=0;j<i;++j)if(c.extraWifi[i].ssid==c.extraWifi[j].ssid){error="Hay redes Wi-Fi repetidas";return false;}
    }
    if (c.libreUser.isEmpty() || c.libreUser.length() > 160 ||
        c.librePass.isEmpty() || c.librePass.length() > 256) {
        error = "Introduce la cuenta y contraseña de LibreLinkUp"; return false;
    }
    if (!validRegion(c.libreRegion) || c.libreVersion.isEmpty() || c.libreVersion.length() > 20) {
        error = "Región o versión de LibreLinkUp no válida"; return false;
    }
    if (c.patientId.isEmpty() || c.patientId.length() > 99 || c.patientName.length() > 99) {
        error = "Inicia sesión y selecciona el usuario compartido"; return false;
    }
    if (c.timezone != "Europe/Madrid" && c.timezone != "Atlantic/Canary" && c.timezone != "UTC") {
        error = "Zona horaria no admitida en esta versión"; return false;
    }
    if (c.locationSet && (c.city.isEmpty() || c.city.length() > 96 ||
        !isfinite(c.latitude) || !isfinite(c.longitude) || c.latitude < -90 ||
        c.latitude > 90 || c.longitude < -180 || c.longitude > 180)) {
        error = "Busca y selecciona una ubicación válida"; return false;
    }
    return true;
}

void getConfig() {
    if (!allowed()) return;
    Doc d(6000); toJson(config, d, false);
    d["csrf"] = csrf;
    d["version"] = APP_VERSION;
    d["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    d["ip"] = WiFi.localIP().toString();
    d["portal_mode"] = mode.load() == PortalMode::WifiAccessPoint ? "wifi" : "online";
    d["last_reset_reason"] = appResetReason();
    d["uptime_seconds"] = millis() / 1000;
    const char *lastStage=appPreviousFaultStage(2);
    if(!lastStage[0])lastStage=appPreviousFaultStage(1);
    if(!lastStage[0])lastStage=appPreviousFaultStage(0);
    d["last_fault_stage"] = lastStage;
    d["settings_state"] = settingsLoadState;
    String json; serializeJson(d, json);
    web.send(200, "application/json", json);
}

void wifiConnect() {
    Doc d(2500); if (!body(d, 1800)) return;
    cancelWifiScan();
    String ssid = d["ssid"] | "", pass = d["password"] | "";
    ssid.trim();
    if (pass.isEmpty() && ssid == config.ssid) pass = config.wifiPass;
    if(pass.isEmpty())for(size_t i=0;i<config.extraWifiCount;++i)
        if(config.extraWifi[i].ssid==ssid){pass=config.extraWifi[i].password;break;}
    String error;
    if (!validWifi(ssid, pass, error)) { fail(400, error); return; }
    Config next = config;
    next.ssid = ssid; next.wifiPass = pass;
    // Promover una red conocida a principal no deja un SSID duplicado.
    for(size_t i=0;i<next.extraWifiCount;){
        if(next.extraWifi[i].ssid==ssid){
            for(size_t j=i+1;j<next.extraWifiCount;++j)next.extraWifi[j-1]=next.extraWifi[j];
            --next.extraWifiCount;
        }else ++i;
    }
    if (!persist(next)) { fail(500, "No se pudo guardar el Wi-Fi en la memoria"); return; }
    config = next;
    settingsLoadState="ok";
    configRevision.fetch_add(1, std::memory_order_release);
    web.send(200, "application/json",
        "{\"saved\":true,\"message\":\"Wi-Fi guardado. Vuelve a la red de casa y espera el segundo QR.\"}");
    transitionAt = millis() + 1500;
}

void saveWifiNetworks(){
    Doc d(2400);if(!body(d,2000))return;
    if(mode.load()!=PortalMode::LocalNetwork){fail(409,"Abre el segundo QR desde la red local");return;}
    JsonArrayConst list=d["networks"].as<JsonArrayConst>();
    if(list.isNull()||list.size()>MAX_EXTRA_WIFI){fail(400,"Se admiten hasta cuatro redes adicionales");return;}
    Config next=config;
    next.extraWifiCount=0;
    for(JsonObjectConst item:list){
        String ssid=item["ssid"] | "";ssid.trim();
        String pass=item["password"] | "";
        if(pass.isEmpty())for(size_t i=0;i<config.extraWifiCount;++i)
            if(config.extraWifi[i].ssid==ssid){pass=config.extraWifi[i].password;break;}
        String error;
        if(!validWifi(ssid,pass,error)||ssid==next.ssid){
            fail(400,ssid==next.ssid?"La red principal ya está guardada":error);return;
        }
        for(size_t i=0;i<next.extraWifiCount;++i)if(next.extraWifi[i].ssid==ssid){
            fail(400,"Red Wi-Fi duplicada");return;
        }
        next.extraWifi[next.extraWifiCount++]={ssid,pass};
    }
    if(!persist(next)){fail(500,"No se pudieron guardar las redes en NVS");return;}
    config=next;
    settingsLoadState="ok";
    configRevision.fetch_add(1,std::memory_order_release);
    web.send(200,"application/json","{\"message\":\"Redes adicionales guardadas. Se probarán si se pierde el Wi-Fi.\"}");
}

void beginWifiScan() {
    Doc d(128); if (!body(d, 100)) return;
    if (libreJobState.load(std::memory_order_acquire) == LibreJobState::Running ||
        geocodeJobState.load(std::memory_order_acquire) == GeocodeJobState::Running) {
        fail(409, "Espera a que termine la consulta anterior"); return;
    }
    if (wifiScanPending) {
        web.send(202, "application/json", "{\"state\":\"running\"}"); return;
    }
    const uint32_t now = millis();
    if (lastWifiScan && now - lastWifiScan < 15000) {
        if (wifiScanReady) { web.send(200, "application/json", "{\"state\":\"ready\"}"); return; }
        fail(429, "Espera unos segundos antes de repetir la búsqueda"); return;
    }
    if (httpMutex && xSemaphoreTake(httpMutex, 0) != pdTRUE) {
        fail(409, "Hay una consulta de red en curso; actualiza la lista en unos segundos"); return;
    }
    wifiScanHttpHeld = httpMutex != nullptr;
    nearbyWifiCount = 0;
    wifiScanReady = false;
    lastWifiScan = now;
    const int found = WiFi.scanNetworks(true, false, false, 150);
    if (found == WIFI_SCAN_RUNNING) {
        wifiScanPending = true;
        wifiScanStarted = now;
        lastWifiScanCheck = now;
        web.send(202, "application/json", "{\"state\":\"running\"}");
    } else if (found >= 0) {
        collectWifiScan(found);
        web.send(200, "application/json", "{\"state\":\"ready\"}");
    } else {
        cancelWifiScan();
        fail(503, "No se pudo buscar redes; introduce el SSID manualmente");
    }
}

void wifiScanStatus() {
    if (!allowed()) return;
    if (wifiScanPending) {
        const int found = WiFi.scanComplete();
        if (found == WIFI_SCAN_RUNNING && millis() - wifiScanStarted < 8000) {
            web.send(202, "application/json", "{\"state\":\"running\"}"); return;
        }
        if (found < 0) {
            cancelWifiScan();
            fail(503, "Búsqueda agotada; introduce el SSID manualmente o reintenta"); return;
        }
        collectWifiScan(found);
    }
    if (!wifiScanReady) { fail(409, "Inicia una búsqueda de redes"); return; }
    Doc out(5000); out["state"] = "ready";
    auto list = out.createNestedArray("networks");
    for (size_t i = 0; i < nearbyWifiCount; ++i) {
        auto item = list.createNestedObject();
        item["ssid"] = nearbyWifi[i].ssid;
        item["rssi"] = nearbyWifi[i].rssi;
        item["secure"] = nearbyWifi[i].secured;
    }
    String json; serializeJson(out, json); web.send(200, "application/json", json);
}

void runLibreLogin(LibreCredentials *credentials) {
    auto *choices = static_cast<ConnectionChoice *>(heap_caps_calloc(
        MAX_CONNECTIONS, sizeof(ConnectionChoice), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    size_t count = 0; String resolved, error;
    const bool ok = choices && libreListConnections(
        *credentials, choices, MAX_CONNECTIONS, count, resolved, error);
    delete credentials;
    if (!choices && error.isEmpty()) error = "Memoria insuficiente para listar usuarios";
    if (!ok) {
        if (choices) heap_caps_free(choices);
        strlcpy(libreJobError, error.c_str(), sizeof(libreJobError));
        libreJobState.store(LibreJobState::Failed, std::memory_order_release);
        Serial.printf("[PORTAL] Login LibreLinkUp finalizado con error: %s\n", libreJobError);
        return;
    }
    libreJobChoices = choices;
    libreJobCount = count;
    strlcpy(libreJobRegion, resolved.c_str(), sizeof(libreJobRegion));
    libreJobState.store(LibreJobState::Ready, std::memory_order_release);
    Serial.printf("[PORTAL] Login LibreLinkUp correcto; %u usuario(s)\n", unsigned(count));
}

void libreLoginTask(void *argument) {
    runLibreLogin(static_cast<LibreCredentials *>(argument));
    appDiagnosticStage("Libre: inactivo");
    // Tras retornar se han destruido los String y documentos temporales.
    vTaskDelete(nullptr);
}

void libreLogin() {
    Doc d(4000); if (!body(d, 3000)) return;
    if (mode.load() != PortalMode::LocalNetwork || WiFi.status() != WL_CONNECTED) {
        fail(409, "Esta función solo está disponible desde el segundo QR"); return;
    }
    if (libreJobState.load(std::memory_order_acquire) == LibreJobState::Running) {
        web.send(202, "application/json", "{\"state\":\"running\"}"); return;
    }
    if (geocodeJobState.load(std::memory_order_acquire) == GeocodeJobState::Running) {
        fail(409, "Espera a que termine la búsqueda de localidad"); return;
    }
    if (wifiScanPending) { fail(409, "Espera a que termine la búsqueda Wi-Fi"); return; }
    auto *credentials = new(std::nothrow) LibreCredentials;
    if (!credentials) { fail(500, "Memoria insuficiente para iniciar sesión"); return; }
    credentials->user = d["user"] | "";
    credentials->password = d["password"] | "";
    credentials->region = d["region"] | "eu";
    credentials->version = d["version"] | "5.1.1";
    credentials->user.trim(); credentials->version.trim();
    if (credentials->password.isEmpty() && credentials->user == config.libreUser &&
        credentials->region == config.libreRegion) credentials->password = config.librePass;
    if (credentials->user.isEmpty() || credentials->password.isEmpty()) {
        delete credentials; fail(400, "Introduce el correo y la contraseña de LibreLinkUp"); return;
    }
    libreJobState.store(LibreJobState::Running, std::memory_order_release);
    if (libreJobChoices) { heap_caps_free(libreJobChoices); libreJobChoices = nullptr; }
    libreJobCount = 0; libreJobRegion[0] = libreJobError[0] = 0;
    // El POST responde ya; HTTPS se hace fuera de su callback y a prioridad 0.
    if (xTaskCreatePinnedToCore(libreLoginTask, "libre-login", 18432, credentials, 0, nullptr, kHttpsCore) != pdPASS) {
        delete credentials; libreJobState.store(LibreJobState::Failed, std::memory_order_release);
        strlcpy(libreJobError, "No se pudo crear la tarea HTTPS", sizeof(libreJobError));
        fail(500, libreJobError); return;
    }
    web.send(202, "application/json", "{\"state\":\"running\"}");
}

void libreStatus() {
    if (!allowed()) return;
    const LibreJobState state = libreJobState.load(std::memory_order_acquire);
    if (state == LibreJobState::Idle) { fail(409, "No hay ningún inicio de sesión en curso"); return; }
    if (state == LibreJobState::Running) {
        web.send(202, "application/json", "{\"state\":\"running\"}"); return;
    }
    if (state == LibreJobState::Failed) { fail(422, libreJobError); return; }
    Doc out(8000); out["state"] = "ready"; out["region"] = libreJobRegion;
    auto patients = out.createNestedArray("patients");
    for (size_t i = 0; i < libreJobCount; ++i) {
        auto p = patients.createNestedObject();
        p["id"] = libreJobChoices[i].id; p["name"] = libreJobChoices[i].name;
    }
    String json; serializeJson(out, json);
    web.send(200, "application/json", json);
}

void runGeocode(const String &query) {
    // Open-Meteo devuelve muchos metadatos que no utiliza la pantalla.
    // Filtrar durante el parseo reduce la presión de PSRAM mientras el panel
    // RGB necesita leer continuamente su framebuffer desde ella.
    Doc filter(1024);
    filter["message"]=true;
    JsonObject place=filter.createNestedArray("results").createNestedObject();
    place["name"]=true;place["admin1"]=true;place["country"]=true;
    place["latitude"]=true;place["longitude"]=true;place["timezone"]=true;
    Doc result(12 * 1024);
    appDiagnosticStage("Portal: geocodificación HTTPS");
    const auto r=net::request("https://geocoding-api.open-meteo.com/v1/search?count=8&language=es&format=json&name="+
                              net::encode(query),result,"GET","",{},24*1024,filter.as<JsonVariantConst>());
    if(r.status!=200){
        strlcpy(geocodeError,r.error.c_str(),sizeof(geocodeError));
        geocodeJobState.store(GeocodeJobState::Failed,std::memory_order_release);
        return;
    }
    geocodeCount=0;
    for(JsonObjectConst p:result["results"].as<JsonArrayConst>()){
        if(geocodeCount>=8)break;
        if(!p["latitude"].is<double>()||!p["longitude"].is<double>())continue;
        auto &item=geocodeChoices[geocodeCount++];
        String name=String(p["name"]|"");
        if(*(p["admin1"]|""))name+=", "+String(p["admin1"]|"");
        if(*(p["country"]|""))name+=", "+String(p["country"]|"");
        strlcpy(item.name,name.c_str(),sizeof(item.name));
        item.latitude=p["latitude"];item.longitude=p["longitude"];
        String zone=p["timezone"]|"UTC";
        strlcpy(item.timezone,(zone=="Europe/Madrid"||zone=="Atlantic/Canary")?zone.c_str():"UTC",
                sizeof(item.timezone));
    }
    geocodeJobState.store(GeocodeJobState::Ready,std::memory_order_release);
}

void geocodeTask(void *argument) {
    auto *query=static_cast<String *>(argument);
    runGeocode(*query);
    delete query;
    appDiagnosticStage("Portal: inactivo");
    vTaskDelete(nullptr);
}

void geocode() {
    Doc d(1500); if (!body(d, 800)) return;
    if (mode.load() != PortalMode::LocalNetwork || WiFi.status() != WL_CONNECTED) {
        fail(409, "Esta función solo está disponible desde el segundo QR"); return;
    }
    String query = d["query"] | ""; query.trim();
    if (query.length() < 2 || query.length() > 80) { fail(400, "Escribe una localidad"); return; }
    if(geocodeJobState.load(std::memory_order_acquire)==GeocodeJobState::Running){
        web.send(202,"application/json","{\"state\":\"running\"}");return;
    }
    if(libreJobState.load(std::memory_order_acquire)==LibreJobState::Running){
        fail(409,"Espera a que termine el inicio de sesión LibreLinkUp");return;
    }
    if(wifiScanPending){fail(409,"Espera a que termine la búsqueda Wi-Fi");return;}
    String *job=new(std::nothrow) String(query);
    if(!job){fail(500,"Memoria insuficiente para buscar la localidad");return;}
    geocodeCount=0;geocodeError[0]=0;
    geocodeJobState.store(GeocodeJobState::Running,std::memory_order_release);
    if(xTaskCreatePinnedToCore(geocodeTask,"geocode",14336,job,0,nullptr,kHttpsCore)!=pdPASS){
        delete job;
        geocodeJobState.store(GeocodeJobState::Failed,std::memory_order_release);
        fail(500,"No se pudo crear la tarea meteorológica");return;
    }
    web.send(202,"application/json","{\"state\":\"running\"}");
}

void geocodeStatus(){
    if(!allowed())return;
    const auto state=geocodeJobState.load(std::memory_order_acquire);
    if(state==GeocodeJobState::Idle){fail(409,"No hay búsqueda de localidad en curso");return;}
    if(state==GeocodeJobState::Running){web.send(202,"application/json","{\"state\":\"running\"}");return;}
    if(state==GeocodeJobState::Failed){fail(422,geocodeError);return;}
    Doc out(6000);out["state"]="ready";auto list=out.createNestedArray("locations");
    for(size_t i=0;i<geocodeCount;++i){
        auto item=list.createNestedObject();
        item["name"]=geocodeChoices[i].name;
        item["latitude"]=geocodeChoices[i].latitude;
        item["longitude"]=geocodeChoices[i].longitude;
        item["timezone"]=geocodeChoices[i].timezone;
    }
    String json;serializeJson(out,json);web.send(200,"application/json",json);
}

void save() {
    Doc d(12000); if (!body(d, 10000)) return;
    if (mode.load() != PortalMode::LocalNetwork) {
        fail(409, "Completa primero la configuración Wi-Fi"); return;
    }
    Config next=config;fromJson(d.as<JsonVariantConst>(), next);
    next.ssid.trim(); next.libreUser.trim(); next.city.trim();
    if (next.wifiPass.isEmpty() && next.ssid == config.ssid) next.wifiPass = config.wifiPass;
    if (next.librePass.isEmpty() && next.libreUser == config.libreUser && next.libreRegion == config.libreRegion)
        next.librePass = config.librePass;
    String error;
    if (!validate(next, error)) { fail(400, error); return; }
    if (!persist(next)) { fail(500, "No se pudo guardar la configuración en NVS"); return; }
    config = next;
    settingsLoadState="ok";
    configRevision.fetch_add(1, std::memory_order_release);
    configTzTime(config.timezone=="Atlantic/Canary"?"WET0WEST,M3.5.0/1,M10.5.0":
                 config.timezone=="UTC"?"UTC0":"CET-1CEST,M3.5.0,M10.5.0/3",
                 "pool.ntp.org","time.cloudflare.com","time.google.com");
    web.send(200, "application/json",
        "{\"saved\":true,\"message\":\"Configuración guardada en memoria. Cerrando el portal.\"}");
    closeAt = millis() + 1800;
}

void reset() {
    Doc d(1000); if (!body(d, 500)) return;
    if (String(d["confirm"] | "") != "BORRAR") { fail(400, "Confirmación incorrecta"); return; }
    xSemaphoreTake(storageMutex, portMAX_DELAY);
    Preferences p;
    for (const char *ns : {"glucowave", "glucohist", "glucousers", "glucodiag"})
        if (p.begin(ns, false)) { p.clear(); p.end(); }
    xSemaphoreGive(storageMutex);
    web.send(200, "application/json", "{\"message\":\"Configuración eliminada. Reiniciando.\"}");
    rebootAt = millis() + 1200;
}

void registerRoutes() {
    if (routesReady) return;
    const char *wanted[]{"X-Setup-Token"}; web.collectHeaders(wanted, 1);
    web.on("/", HTTP_GET, [] {
        if (!allowed()) return;
        web.sendHeader("Content-Security-Policy", "default-src 'self'; style-src 'self'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'");
        web.send_P(200, "text/html; charset=utf-8", WEB_INDEX);
    });
    web.on("/app.js", HTTP_GET, [] { if (allowed()) web.send_P(200, "text/javascript; charset=utf-8", WEB_JS); });
    web.on("/style.css", HTTP_GET, [] { if (allowed()) web.send_P(200, "text/css; charset=utf-8", WEB_CSS); });
    web.on("/api/config", HTTP_GET, getConfig);
    web.on("/api/wifi", HTTP_POST, wifiConnect);
    web.on("/api/wifi/networks", HTTP_POST, saveWifiNetworks);
    web.on("/api/wifi/scan", HTTP_POST, beginWifiScan);
    web.on("/api/wifi/scan/status", HTTP_GET, wifiScanStatus);
    web.on("/api/libre/login", HTTP_POST, libreLogin);
    web.on("/api/libre/status", HTTP_GET, libreStatus);
    web.on("/api/geocode", HTTP_POST, geocode);
    web.on("/api/geocode/status", HTTP_GET, geocodeStatus);
    web.on("/api/save", HTTP_POST, save);
    web.on("/api/reset", HTTP_POST, reset);
    web.onNotFound([] {
        web.sendHeader("Location", portalUrl());
        web.send(302, "text/plain", "");
    });
    routesReady = true;
}

void stopServer() {
    cancelWifiScan();
    if (serverRunning) { web.stop(); serverRunning = false; }
    dns.stop();
}

void startServer() {
    registerRoutes(); web.begin(); serverRunning = true; started = millis();
}

void startWifiAccessPoint() {
    stopServer();
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apName.c_str(), apPass.c_str(), 1, false, 2);
    mode.store(PortalMode::WifiAccessPoint);
    dns.start(53, "*", WiFi.softAPIP());
    startServer();
    Serial.printf("[PORTAL 1/2] Wi-Fi temporal %s | http://192.168.4.1/\n", apName.c_str());
}

void startWaitingForWifi(bool restartConnection) {
    stopServer();
    WiFi.softAPdisconnect(true);
    mode.store(PortalMode::WaitingForWifi);
    WiFi.mode(WIFI_STA);
    if (restartConnection) {
        WiFi.disconnect(false, false); delay(80);
        WiFi.begin(config.ssid.c_str(), config.wifiPass.c_str());
    }
    wifiDeadline = millis() + kWifiConnectMs;
    started = millis();
    Serial.printf("[PORTAL] Conectando a %s para abrir la fase 2...\n", config.ssid.c_str());
}

void startLocalNetwork() {
    stopServer();
    WiFi.mode(WIFI_STA);
    mode.store(PortalMode::LocalNetwork);
    startServer();
    Serial.printf("[PORTAL 2/2] Configuración local en %s\n", portalUrl().c_str());
}
}

bool deviceWifiScan(String &error) {
    if (wifiScanPending) return true;
    if (libreJobState.load()==LibreJobState::Running ||
        geocodeJobState.load()==GeocodeJobState::Running) {
        error="Espera a que termine la consulta HTTPS"; return false;
    }
    const uint32_t now=millis();
    if (lastWifiScan && now-lastWifiScan<15000 && wifiScanReady) return true;
    if (lastWifiScan && now-lastWifiScan<15000) {
        error="Espera antes de buscar redes de nuevo"; return false;
    }
    if (httpMutex && xSemaphoreTake(httpMutex,0)!=pdTRUE) {
        error="La red está ocupada; vuelve a buscar en unos segundos"; return false;
    }
    wifiScanHttpHeld=httpMutex!=nullptr;
    wifiScanReady=false;nearbyWifiCount=0;lastWifiScan=now;
    const int found=WiFi.scanNetworks(true,false,false,150);
    if(found==WIFI_SCAN_RUNNING){wifiScanPending=true;wifiScanStarted=now;return true;}
    if(found>=0){collectWifiScan(found);return true;}
    cancelWifiScan();error="No se pudo escanear; escribe el SSID";return false;
}

SetupJob deviceWifiResults(WifiScanEntry *out,size_t capacity,size_t &count,String &error){
    count=0;
    if(wifiScanPending){
        const int found=WiFi.scanComplete();
        if(found==WIFI_SCAN_RUNNING && millis()-wifiScanStarted<8000)return SetupJob::Running;
        if(found<0){cancelWifiScan();error="Búsqueda Wi-Fi agotada";return SetupJob::Failed;}
        collectWifiScan(found);
    }
    if(!wifiScanReady)return SetupJob::Idle;
    for(size_t i=0;i<nearbyWifiCount&&count<capacity;++i){
        strlcpy(out[count].ssid,nearbyWifi[i].ssid.c_str(),sizeof(out[count].ssid));
        out[count].rssi=nearbyWifi[i].rssi;out[count].secure=nearbyWifi[i].secured;++count;
    }
    return SetupJob::Ready;
}

bool deviceSaveWifi(const String &name,const String &password,bool connectNow,String &error){
    String ssid=name;ssid.trim();
    String pass=password;
    if(pass.isEmpty()){
        if(ssid==config.ssid)pass=config.wifiPass;
        for(size_t i=0;i<config.extraWifiCount;++i)
            if(ssid==config.extraWifi[i].ssid)pass=config.extraWifi[i].password;
    }
    if(!validWifi(ssid,pass,error))return false;
    const bool alreadyConnected=WiFi.status()==WL_CONNECTED && WiFi.SSID()==ssid &&
                                config.ssid==ssid && config.wifiPass==pass;
    Config next=config;
    const bool makePrimary=connectNow || next.ssid.isEmpty();
    if(makePrimary){
        for(size_t i=0;i<next.extraWifiCount;){
            if(next.extraWifi[i].ssid==ssid){
                for(size_t j=i+1;j<next.extraWifiCount;++j)next.extraWifi[j-1]=next.extraWifi[j];
                --next.extraWifiCount;
            }else ++i;
        }
        if(next.ssid!=ssid && !next.ssid.isEmpty()){
            if(next.extraWifiCount==MAX_EXTRA_WIFI){error="Libera una red guardada antes de cambiar la principal";return false;}
            next.extraWifi[next.extraWifiCount++]={next.ssid,next.wifiPass};
        }
        next.ssid=ssid;next.wifiPass=pass;
    }else{
        if(ssid==next.ssid){next.wifiPass=pass;}
        else{
            size_t i=0;while(i<next.extraWifiCount&&next.extraWifi[i].ssid!=ssid)++i;
            if(i==next.extraWifiCount&&i==MAX_EXTRA_WIFI){error="Solo caben cinco redes en total";return false;}
            if(i==next.extraWifiCount)++next.extraWifiCount;
            next.extraWifi[i]={ssid,pass};
        }
    }
    if(!persist(next)){error="No se pudo guardar la red en NVS";return false;}
    config=next;settingsLoadState="ok";
    configRevision.fetch_add(1,std::memory_order_release);
    if(makePrimary&&!alreadyConnected){WiFi.mode(WIFI_STA);WiFi.begin(ssid.c_str(),pass.c_str());}
    return true;
}

bool deviceRemoveWifi(const String &ssid,String &error){
    Config next=config;
    bool reconnect=false;
    if(ssid==next.ssid){
        if(!next.extraWifiCount){error="Añade otra red antes de eliminar la principal";return false;}
        next.ssid=next.extraWifi[0].ssid;next.wifiPass=next.extraWifi[0].password;
        reconnect=true;
        for(size_t i=1;i<next.extraWifiCount;++i)next.extraWifi[i-1]=next.extraWifi[i];
        --next.extraWifiCount;
    }else{
        size_t i=0;while(i<next.extraWifiCount&&next.extraWifi[i].ssid!=ssid)++i;
        if(i==next.extraWifiCount){error="Red no guardada";return false;}
        for(size_t j=i+1;j<next.extraWifiCount;++j)next.extraWifi[j-1]=next.extraWifi[j];
        --next.extraWifiCount;
    }
    if(!persist(next)){error="No se pudo guardar el cambio en NVS";return false;}
    config=next;configRevision.fetch_add(1,std::memory_order_release);
    if(reconnect){WiFi.mode(WIFI_STA);WiFi.begin(config.ssid.c_str(),config.wifiPass.c_str());}
    return true;
}

bool deviceGeocode(const String &name,String &error){
    String query=name;query.trim();
    if(WiFi.status()!=WL_CONNECTED){error="Conecta primero el Wi-Fi";return false;}
    if(query.length()<2||query.length()>80){error="Escribe una localidad";return false;}
    if(geocodeJobState.load()==GeocodeJobState::Running||
       libreJobState.load()==LibreJobState::Running||wifiScanPending){
        error="Espera a que termine la consulta anterior";return false;
    }
    String *job=new(std::nothrow) String(query);
    if(!job){error="Memoria insuficiente";return false;}
    geocodeCount=0;geocodeError[0]=0;
    geocodeJobState.store(GeocodeJobState::Running,std::memory_order_release);
    if(xTaskCreatePinnedToCore(geocodeTask,"geocode",14336,job,0,nullptr,kHttpsCore)!=pdPASS){
        delete job;geocodeJobState.store(GeocodeJobState::Failed);
        error="No se pudo crear la tarea de búsqueda";return false;
    }
    return true;
}

SetupJob deviceGeocodeResults(LocationChoice *out,size_t capacity,size_t &count,String &error){
    count=0;const auto state=geocodeJobState.load(std::memory_order_acquire);
    if(state==GeocodeJobState::Idle)return SetupJob::Idle;
    if(state==GeocodeJobState::Running)return SetupJob::Running;
    if(state==GeocodeJobState::Failed){error=geocodeError;return SetupJob::Failed;}
    for(size_t i=0;i<geocodeCount&&count<capacity;++i)out[count++]=geocodeChoices[i];
    return SetupJob::Ready;
}

bool deviceSaveLocation(const LocationChoice &choice,String &error){
    Config next=config;next.city=choice.name;next.city.trim();
    next.latitude=choice.latitude;next.longitude=choice.longitude;
    next.timezone=choice.timezone;next.locationSet=true;
    if(next.city.isEmpty()||next.city.length()>96||!isfinite(next.latitude)||!isfinite(next.longitude)||
       next.latitude< -90||next.latitude>90||next.longitude< -180||next.longitude>180||
       (next.timezone!="Europe/Madrid"&&next.timezone!="Atlantic/Canary"&&next.timezone!="UTC")){
        error="Ubicación no válida";return false;
    }
    if(!persist(next)){error="No se pudo guardar la ubicación";return false;}
    config=next;configRevision.fetch_add(1,std::memory_order_release);
    configTzTime(config.timezone=="Atlantic/Canary"?"WET0WEST,M3.5.0/1,M10.5.0":
                 config.timezone=="UTC"?"UTC0":"CET-1CEST,M3.5.0,M10.5.0/3",
                 "pool.ntp.org","time.cloudflare.com","time.google.com");
    return true;
}

bool deviceLibreLogin(const String &name,const String &password,const String &region,String &error){
    if(WiFi.status()!=WL_CONNECTED){error="Conecta primero el Wi-Fi";return false;}
    if(libreJobState.load()==LibreJobState::Running||
       geocodeJobState.load()==GeocodeJobState::Running||wifiScanPending){
        error="Espera a que termine la consulta anterior";return false;
    }
    LibreCredentials next{name,password,region,config.libreVersion};next.user.trim();
    if(next.password.isEmpty()&&next.user==config.libreUser&&region==config.libreRegion)
        next.password=config.librePass;
    if(next.user.isEmpty()||next.user.length()>160||next.password.isEmpty()||
       next.password.length()>256||!validRegion(next.region)){
        error="Revisa el correo, la contraseña y la región";return false;
    }
    auto *job=new(std::nothrow) LibreCredentials(next);
    if(!job){error="Memoria insuficiente";return false;}
    stagedDeviceLogin=next;
    if(libreJobChoices){heap_caps_free(libreJobChoices);libreJobChoices=nullptr;}
    libreJobCount=0;libreJobError[0]=libreJobRegion[0]=0;
    libreJobState.store(LibreJobState::Running,std::memory_order_release);
    if(xTaskCreatePinnedToCore(libreLoginTask,"libre-login",18432,job,0,nullptr,kHttpsCore)!=pdPASS){
        delete job;libreJobState.store(LibreJobState::Failed);
        error="No se pudo crear la tarea LibreLinkUp";return false;
    }
    return true;
}

SetupJob deviceLibreResults(ConnectionChoice *out,size_t capacity,size_t &count,String &error){
    count=0;const auto state=libreJobState.load(std::memory_order_acquire);
    if(state==LibreJobState::Idle)return SetupJob::Idle;
    if(state==LibreJobState::Running)return SetupJob::Running;
    if(state==LibreJobState::Failed){error=libreJobError;return SetupJob::Failed;}
    for(size_t i=0;i<libreJobCount&&count<capacity;++i)out[count++]=libreJobChoices[i];
    return SetupJob::Ready;
}

bool deviceSaveLibreUser(const ConnectionChoice &choice,String &error){
    if(!choice.id[0]||!choice.name[0]||stagedDeviceLogin.user.isEmpty()||
       libreJobState.load()!=LibreJobState::Ready){error="Inicia sesión antes de elegir usuario";return false;}
    Config next=config;
    next.libreUser=stagedDeviceLogin.user;next.librePass=stagedDeviceLogin.password;
    next.libreRegion=libreJobRegion[0]?libreJobRegion:stagedDeviceLogin.region;
    next.libreVersion=stagedDeviceLogin.version;
    next.patientId=choice.id;next.patientName=choice.name;
    if(!persist(next)){error="No se pudo guardar LibreLinkUp en NVS";return false;}
    config=next;configRevision.fetch_add(1,std::memory_order_release);
    return true;
}

void mutationTask(void *argument){
    auto *item=static_cast<Mutation *>(argument);
    String error;bool ok=false;
    const bool locked=!httpMutex || xSemaphoreTake(httpMutex,pdMS_TO_TICKS(60000))==pdTRUE;
    if(!locked)error="Otra consulta sigue ocupando la red; repite el guardado";
    else{
        switch(item->kind){
            case MutationKind::Wifi:ok=deviceSaveWifi(item->ssid,item->password,item->connectNow,error);break;
            case MutationKind::RemoveWifi:ok=deviceRemoveWifi(item->ssid,error);break;
            case MutationKind::Location:ok=deviceSaveLocation(item->location,error);break;
            case MutationKind::LibreUser:ok=deviceSaveLibreUser(item->person,error);break;
        }
        if(httpMutex)xSemaphoreGive(httpMutex);
    }
    delete item;
    strlcpy(mutationError,error.c_str(),sizeof(mutationError));
    mutationState.store(ok?SetupJob::Ready:SetupJob::Failed,std::memory_order_release);
    vTaskDelete(nullptr);
}

bool queueMutation(Mutation *item,String &error){
    if(!item){error="Memoria insuficiente";return false;}
    if(strcmp(settingsLoadState,"invalid")==0 || strcmp(settingsLoadState,"unavailable")==0){
        delete item;error="Ajustes NVS dañados o inaccesibles; haz una copia antes de guardar";
        return false;
    }
    if(mutationState.load(std::memory_order_acquire)==SetupJob::Running){
        delete item;error="Espera a que termine el guardado";return false;
    }
    mutationError[0]=0;
    mutationState.store(SetupJob::Running,std::memory_order_release);
    if(xTaskCreatePinnedToCore(mutationTask,"settings-save",12288,item,0,nullptr,appWorkerCore())!=pdPASS){
        delete item;mutationState.store(SetupJob::Failed);
        error="No se pudo iniciar el guardado";return false;
    }
    return true;
}

bool deviceQueueWifi(const String &ssid,const String &password,bool connectNow,String &error){
    auto *item=new(std::nothrow) Mutation{};if(!item){error="Memoria insuficiente";return false;}
    item->kind=MutationKind::Wifi;item->ssid=ssid;item->password=password;item->connectNow=connectNow;
    return queueMutation(item,error);
}
bool deviceQueueRemoveWifi(const String &ssid,String &error){
    auto *item=new(std::nothrow) Mutation{};if(!item){error="Memoria insuficiente";return false;}
    item->kind=MutationKind::RemoveWifi;item->ssid=ssid;return queueMutation(item,error);
}
bool deviceQueueLocation(const LocationChoice &choice,String &error){
    auto *item=new(std::nothrow) Mutation{};if(!item){error="Memoria insuficiente";return false;}
    item->kind=MutationKind::Location;item->location=choice;return queueMutation(item,error);
}
bool deviceQueueLibreUser(const ConnectionChoice &choice,String &error){
    auto *item=new(std::nothrow) Mutation{};if(!item){error="Memoria insuficiente";return false;}
    item->kind=MutationKind::LibreUser;item->person=choice;return queueMutation(item,error);
}
SetupJob deviceMutationResult(String &error){
    const auto state=mutationState.load(std::memory_order_acquire);
    if(state==SetupJob::Failed)error=mutationError;
    return state;
}

void configLoad() {
    Preferences p; String raw;
    if (p.begin("glucowave", true)) { raw = p.getString("settings", ""); p.end(); }
    else settingsLoadState="unavailable";
    if (raw.isEmpty()) {
        Serial.printf("[NVS] Ajustes no cargados (%s); se abre configuración\n",settingsLoadState);
        return;
    }
    Doc d(10000);
    const auto error=deserializeJson(d,raw);
    if(error||!d.is<JsonObject>()){
        settingsLoadState="invalid";
        Serial.printf("[NVS] Ajustes presentes pero JSON no válido (%s); no se sobrescriben\n",error.c_str());
        return;
    }
    fromJson(d.as<JsonVariantConst>(),config);
    settingsLoadState="ok";
    Serial.printf("[NVS] Ajustes cargados: Wi-Fi %s, LibreLinkUp %s, usuario %s\n",
                  config.ssid.isEmpty()?"ausente":"presente",
                  config.libreUser.isEmpty()?"ausente":"presente",
                  config.patientId.isEmpty()?"ausente":"presente");
}

bool configSelectPatient(const char *id, const char *name) {
    if (!id || !*id || strlen(id) > 99 || !name || strlen(name) > 99) return false;
    if (config.patientId == id && config.patientName == name) return true;
    Config next = config;
    next.patientId = id;
    next.patientName = name;
    if (!persist(next)) return false;
    config = next;
    configRevision.fetch_add(1, std::memory_order_release);
    return true;
}

bool requestPatientSelection(const char *id, const char *name) {
    if (!patientQueue || !id || !*id || !name) return false;
    PatientSelection selection{};
    strlcpy(selection.id, id, sizeof(selection.id));
    strlcpy(selection.name, name, sizeof(selection.name));
    return xQueueOverwrite(patientQueue, &selection) == pdPASS;
}

bool configNeedsSetup() {
    return config.ssid.isEmpty() || config.libreUser.isEmpty() ||
           config.patientId.isEmpty();
}
const char *configStorageState(){return settingsLoadState;}

void portalStart() {
    if (mode.load() != PortalMode::Off) return;
    cancelWifiScan();
    char value[64];
    snprintf(value, sizeof(value), "GlucoWave-%04X", unsigned(ESP.getEfuseMac() & 0xffff)); apName = value;
    snprintf(value, sizeof(value), "GW%08lX", static_cast<unsigned long>(esp_random())); apPass = value;
    snprintf(value, sizeof(value), "%08lX%08lX%08lX", static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random())); csrf = value;
    transitionAt = closeAt = rebootAt = 0;
    lastWifiScan = 0;
    wifiScanReady = false;
    nearbyWifiCount = 0;
    displayWake();
    if (config.ssid.isEmpty()) startWifiAccessPoint();
    else if (WiFi.status() == WL_CONNECTED) startLocalNetwork();
    else startWaitingForWifi(true);
}

void portalLoop() {
    if (rebootAt && int32_t(millis() - rebootAt) >= 0) { markPlannedRestart(); ESP.restart(); }
    if (closeAt && int32_t(millis() - closeAt) >= 0) {
        closeAt=0;portalStop();uiShowHome();return;
    }
    PortalMode current = mode.load();
    if (current == PortalMode::Off) return;
    if (transitionAt && int32_t(millis() - transitionAt) >= 0) {
        transitionAt = 0; startWaitingForWifi(true); return;
    }
    current = mode.load();
    if (current == PortalMode::WaitingForWifi) {
        if (WiFi.status() == WL_CONNECTED) startLocalNetwork();
        else if (int32_t(millis() - wifiDeadline) >= 0) startWifiAccessPoint();
        return;
    }
    if (current == PortalMode::WifiAccessPoint) dns.processNextRequest();
    if (wifiScanPending && millis() - lastWifiScanCheck >= 250) {
        lastWifiScanCheck = millis();
        const int found = WiFi.scanComplete();
        if (found >= 0) collectWifiScan(found);
        else if (millis() - wifiScanStarted >= 8000) cancelWifiScan();
    }
    if (serverRunning) web.handleClient();
    if (millis() - started > kPortalMs) { portalStop(); uiShowHome(); }
}

void portalStop() {
    const PortalMode current = mode.load();
    if (current == PortalMode::Off) return;
    stopServer();
    // El segundo QR usa la STA existente. Al cerrarlo no tocar la radio:
    // cambiar modo o apagar un AP inexistente puede interrumpir su enlace.
    if (current == PortalMode::WifiAccessPoint) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }
    mode.store(PortalMode::Off);
    if (!config.ssid.isEmpty() && WiFi.status() != WL_CONNECTED)
        WiFi.begin(config.ssid.c_str(), config.wifiPass.c_str());
    Serial.println("[PORTAL] Cerrado");
}

bool portalActive() { return mode.load() != PortalMode::Off; }
PortalMode portalMode() { return mode.load(); }
String portalSsid() { return apName; }
String portalPassword() { return apPass; }
String portalUrl() {
    if (mode.load() == PortalMode::LocalNetwork && WiFi.status() == WL_CONNECTED) {
        String url("http://");
        url += WiFi.localIP().toString();
        url += '/';
        return url;
    }
    return "http://192.168.4.1/";
}
