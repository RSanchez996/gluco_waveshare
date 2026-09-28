#include "app.hpp"
#include "net.hpp"
#include "providers.hpp"
#include "web_assets.hpp"
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <cmath>
#include <new>

using net::Doc;
std::atomic<uint32_t> configRevision{0};

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
enum class LibreJobState : uint8_t { Idle, Running, Ready, Failed };
std::atomic<LibreJobState> libreJobState{LibreJobState::Idle};
ConnectionChoice *libreJobChoices = nullptr;
size_t libreJobCount = 0;
char libreJobRegion[8]{};
char libreJobError[220]{};
enum class GeocodeJobState : uint8_t { Idle, Running, Ready, Failed };
std::atomic<GeocodeJobState> geocodeJobState{GeocodeJobState::Idle};
struct GeocodeChoice { char name[140]{}; float latitude=0, longitude=0; char timezone[32]{}; };
GeocodeChoice geocodeChoices[8]{};
size_t geocodeCount=0;
char geocodeError[160]{};

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
    if(ok)displayResync();
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
    String ssid = d["ssid"] | "", pass = d["password"] | "";
    ssid.trim();
    if (pass.isEmpty() && ssid == config.ssid) pass = config.wifiPass;
    String error;
    if (!validWifi(ssid, pass, error)) { fail(400, error); return; }
    Config next = config;
    next.ssid = ssid; next.wifiPass = pass;
    if (!persist(next)) { fail(500, "No se pudo guardar el Wi-Fi en la memoria"); return; }
    config = next;
    settingsLoadState="ok";
    configRevision.fetch_add(1, std::memory_order_release);
    web.send(200, "application/json",
        "{\"saved\":true,\"message\":\"Wi-Fi guardado. Vuelve a la red de casa y espera el segundo QR.\"}");
    transitionAt = millis() + 1500;
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
    // HTTPS corre en el otro núcleo y con prioridad 0; la interfaz conserva
    // su núcleo y las tareas del sistema pueden adelantar a este trabajo.
    if (xTaskCreatePinnedToCore(libreLoginTask, "libre-login", 18432, credentials, 0, nullptr, appWorkerCore()) != pdPASS) {
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
    Doc result(28 * 1024);
    appDiagnosticStage("Portal: geocodificación HTTPS");
    const auto r=net::request("https://geocoding-api.open-meteo.com/v1/search?count=8&language=es&format=json&name="+
                              net::encode(query),result);
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
    String *job=new(std::nothrow) String(query);
    if(!job){fail(500,"Memoria insuficiente para buscar la localidad");return;}
    geocodeCount=0;geocodeError[0]=0;
    geocodeJobState.store(GeocodeJobState::Running,std::memory_order_release);
    if(xTaskCreatePinnedToCore(geocodeTask,"geocode",14336,job,0,nullptr,appWorkerCore())!=pdPASS){
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
    Config next; fromJson(d.as<JsonVariantConst>(), next);
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

void portalStart() {
    if (mode.load() != PortalMode::Off) return;
    char value[64];
    snprintf(value, sizeof(value), "GlucoWave-%04X", unsigned(ESP.getEfuseMac() & 0xffff)); apName = value;
    snprintf(value, sizeof(value), "GW%08lX", static_cast<unsigned long>(esp_random())); apPass = value;
    snprintf(value, sizeof(value), "%08lX%08lX%08lX", static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random())); csrf = value;
    transitionAt = closeAt = rebootAt = 0;
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
