#include "app.hpp"
#include "net.hpp"
#include "runtime.hpp"
#include "web_assets.hpp"
#include <DNSServer.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <lwip/sockets.h>
using net::Doc;
namespace {
std::atomic<bool> wanted{false};
std::atomic<PortalMode> mode{PortalMode::Off};
std::atomic<uint32_t> lastAccess{0};
httpd_handle_t server = nullptr;
DNSServer dns;
char apName[32]{}, apPass[32]{}, csrf[32]{};
std::atomic<int> afterSave{0}; // 1 = reconnect Wi-Fi, 2 = close portal
uint32_t wifiDeadline = 0, transitionAt = 0;
void response(httpd_req_t *req, int code, const char *type, const String &data) {
    const char *status = code == 200   ? "200 OK"
                         : code == 202 ? "202 Accepted"
                         : code == 400 ? "400 Bad Request"
                         : code == 403 ? "403 Forbidden"
                         : code == 409 ? "409 Conflict"
                         : code == 422 ? "422 Unprocessable Entity"
                                       : "500 Internal Server Error";
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_send(req, data.c_str(), data.length());
}
void error(httpd_req_t *req, int code, const String &message) {
    Doc d(1024);
    d["message"] = message;
    String json;
    serializeJson(d, json);
    response(req, code, "application/json", json);
}
void json(httpd_req_t *req, Doc &d) {
    String out;
    serializeJson(d, out);
    response(req, 200, "application/json", out);
}
bool allowed(httpd_req_t *req, bool mutation = false) {
    const auto current = mode.load();
    if (!wanted.load() ||
        (current != PortalMode::WifiAccessPoint && current != PortalMode::LocalNetwork)) {
        error(req, 403, "Portal cerrado");
        return false;
    }
    if (mutation) {
        char token[40]{};
        if (httpd_req_get_hdr_value_str(req, "X-Setup-Token", token, sizeof(token)) != ESP_OK ||
            strcmp(token, csrf) != 0) {
            error(req, 403, "Formulario caducado; vuelve a cargar la página");
            return false;
        }
    }
    lastAccess.store(millis());
    return true;
}
bool input(httpd_req_t *req, Doc &d) {
    if (!allowed(req, true))
        return false;
    if (req->content_len <= 0 || req->content_len > 8192) {
        error(req, 400, "Formulario demasiado grande");
        return false;
    }
    auto *raw = static_cast<char *>(
        heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!raw) {
        error(req, 500, "Memoria insuficiente");
        return false;
    }
    size_t count = 0;
    const uint32_t deadline = millis() + 6000;
    while (count < req->content_len && !runtime::due(millis(), deadline)) {
        int n = httpd_req_recv(req, raw + count, std::min<size_t>(1024, req->content_len - count));
        if (n <= 0)
            break;
        count += n;
        vTaskDelay(1);
    }
    raw[count] = 0;
    // const input duplicates strings into the document before freeing raw.
    bool ok = count == req->content_len &&
              !deserializeJson(d, static_cast<const char *>(raw), count) && d.is<JsonObject>();
    heap_caps_free(raw);
    if (!ok)
        error(req, 400, "Formulario incompleto o no válido");
    return ok;
}
void accepted(httpd_req_t *req) {
    response(req, 202, "application/json", "{\"state\":\"running\"}");
}
esp_err_t asset(httpd_req_t *req) {
    if (!allowed(req))
        return ESP_OK;
    const char *data = strcmp(req->uri, "/app.js") == 0      ? WEB_JS
                       : strcmp(req->uri, "/style.css") == 0 ? WEB_CSS
                                                             : WEB_INDEX;
    const char *type = strcmp(req->uri, "/app.js") == 0      ? "text/javascript; charset=utf-8"
                       : strcmp(req->uri, "/style.css") == 0 ? "text/css; charset=utf-8"
                                                             : "text/html; charset=utf-8";
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; style-src 'self'; script-src 'self'; connect-src "
                       "'self'; frame-ancestors 'none'");
    httpd_resp_send(req, data, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
esp_err_t get(httpd_req_t *req) {
    if (!allowed(req))
        return ESP_OK;
    const String path = req->uri;
    Doc d(12000);
    if (path == "/api/config") {
        const Config c = configSnapshot();
        d["ssid"] = c.ssid;
        d["has_wifi_password"] = !c.wifiPass.isEmpty();
        auto list = d.createNestedArray("wifi_networks");
        for (size_t i = 0; i < c.extraWifiCount; ++i) {
            auto n = list.createNestedObject();
            n["ssid"] = c.extraWifi[i].ssid;
            n["has_password"] = !c.extraWifi[i].password.isEmpty();
        }
        d["libre_user"] = c.libreUser;
        d["has_libre_password"] = !c.librePass.isEmpty();
        d["libre_region"] = c.libreRegion;
        d["libre_version"] = c.libreVersion;
        d["diabetesm_user"] = c.diabetesmUser;
        d["has_diabetesm_password"] = !c.diabetesmPass.isEmpty();
        d["diabetesm_enabled"] = c.diabetesmEnabled;
        d["patient_id"] = c.patientId;
        d["patient_name"] = c.patientName;
        d["city"] = c.city;
        d["latitude"] = c.latitude;
        d["longitude"] = c.longitude;
        d["timezone"] = c.timezone;
        d["location_set"] = c.locationSet;
        d["csrf"] = csrf;
        d["version"] = APP_VERSION;
        d["settings_state"] = configStorageState();
        d["last_reset_reason"] = appResetReason();
        d["last_fault_stage"] = appPreviousFaultStage(0);
        d["uptime_seconds"] = millis() / 1000;
        d["wifi_connected"] = WiFi.status() == WL_CONNECTED;
        d["ip"] = WiFi.localIP().toString();
        d["portal_mode"] = mode.load() == PortalMode::WifiAccessPoint ? "wifi" : "online";
    } else if (path == "/api/diagnostics") {
        // Print adapter owns an ordinary short string, never credentials.
        class Output : public Print {
          public:
            String s;
            size_t write(uint8_t c) override {
                s += char(c);
                return 1;
            }
        } out;
        appDiagnosticsJson(out);
        response(req, 200, "application/json", out.s);
        return ESP_OK;
    } else {
        String message;
        size_t count = 0;
        SetupJob state = SetupJob::Idle;
        if (path == "/api/wifi/scan/status") {
            WifiScanEntry items[16]{};
            state = deviceWifiResults(items, 16, count, message);
            if (state == SetupJob::Ready) {
                auto list = d.createNestedArray("networks");
                for (size_t i = 0; i < count; ++i) {
                    auto n = list.createNestedObject();
                    n["ssid"] = items[i].ssid;
                    n["rssi"] = items[i].rssi;
                    n["secure"] = items[i].secure;
                }
            }
        } else if (path == "/api/libre/status") {
            ConnectionChoice items[MAX_CONNECTIONS]{};
            state = deviceLibreResults(items, MAX_CONNECTIONS, count, message);
            if (state == SetupJob::Ready) {
                d["region"] = deviceLibreRegion();
                auto list = d.createNestedArray("patients");
                for (size_t i = 0; i < count; ++i) {
                    auto p = list.createNestedObject();
                    p["id"] = items[i].id;
                    p["name"] = items[i].name;
                }
            }
        } else if (path == "/api/geocode/status") {
            LocationChoice items[8]{};
            state = deviceGeocodeResults(items, 8, count, message);
            if (state == SetupJob::Ready) {
                auto list = d.createNestedArray("locations");
                for (size_t i = 0; i < count; ++i) {
                    auto p = list.createNestedObject();
                    p["name"] = items[i].name;
                    p["latitude"] = items[i].latitude;
                    p["longitude"] = items[i].longitude;
                    p["timezone"] = items[i].timezone;
                }
            }
        } else if (path == "/api/mutation/status")
            state = deviceMutationResult(message);
        else {
            error(req, 400, "Ruta no válida");
            return ESP_OK;
        }
        if (state == SetupJob::Idle) {
            error(req, 409, "No hay operación pendiente");
            return ESP_OK;
        }
        if (state == SetupJob::Running) {
            accepted(req);
            return ESP_OK;
        }
        if (state == SetupJob::Failed) {
            if (path == "/api/mutation/status")
                afterSave.store(0);
            error(req, 422, message);
            return ESP_OK;
        }
        d["state"] = "ready";
        d["message"] = "Configuración guardada";
    }
    json(req, d);
    return ESP_OK;
}
esp_err_t post(httpd_req_t *req) {
    if (afterSave.load() != 0) {
        error(req, 409, "Espera a que termine el guardado anterior");
        return ESP_OK;
    }
    Doc d(16000);
    if (!input(req, d))
        return ESP_OK;
    const String path = req->uri;
    String message;
    bool ok = false;
    if (path == "/api/wifi/scan")
        ok = deviceWifiScan(message);
    else if (path == "/api/libre/login")
        ok = deviceLibreLogin(d["user"] | "", d["password"] | "", d["region"] | "eu", message);
    else if (path == "/api/geocode")
        ok = deviceGeocode(d["query"] | "", message);
    else if (path == "/api/wifi") {
        ok = deviceQueueWifi(d["ssid"] | "", d["password"] | "", true, message);
        if (ok)
            afterSave.store(1);
    } else if (path == "/api/wifi/networks") {
        Config next = configSnapshot();
        const Config old = next;
        next.extraWifiCount = 0;
        auto list = d["networks"].as<JsonArrayConst>();
        if (list.size() > MAX_EXTRA_WIFI) {
            error(req, 400, "Solo caben cuatro redes adicionales");
            return ESP_OK;
        }
        for (JsonObjectConst p : list) {
            String ssid = p["ssid"] | "", pass = p["password"] | "";
            if (pass.isEmpty())
                for (size_t i = 0; i < old.extraWifiCount; ++i)
                    if (old.extraWifi[i].ssid == ssid)
                        pass = old.extraWifi[i].password;
            next.extraWifi[next.extraWifiCount++] = {ssid, pass};
        }
        ok = configQueueFull(next, message, true);
    } else if (path == "/api/save") {
        Config next = configSnapshot();
        next.ssid = d["ssid"] | next.ssid;
        next.libreUser = d["libre_user"] | next.libreUser;
        next.libreRegion = d["libre_region"] | next.libreRegion;
        next.libreVersion = d["libre_version"] | next.libreVersion;
        const String wifi = d["wifi_password"] | "", libre = d["libre_password"] | "",
                     diabetesm = d["diabetesm_password"] | "";
        const Config old = configSnapshot();
        if (!wifi.isEmpty())
            next.wifiPass = wifi;
        else if (next.ssid != old.ssid)
            next.wifiPass = "";
        if (!libre.isEmpty())
            next.librePass = libre;
        else if (next.libreUser != old.libreUser || next.libreRegion != old.libreRegion)
            next.librePass = "";
        if (d.containsKey("diabetesm_user"))
            next.diabetesmUser = d["diabetesm_user"] | next.diabetesmUser;
        next.diabetesmUser.trim();
        if (!diabetesm.isEmpty())
            next.diabetesmPass = diabetesm;
        else if (next.diabetesmUser != old.diabetesmUser)
            next.diabetesmPass = "";
        if (d.containsKey("diabetesm_enabled"))
            next.diabetesmEnabled = d["diabetesm_enabled"].as<bool>();
        next.patientId = d["patient_id"] | "";
        next.patientName = d["patient_name"] | "";
        next.city = d["city"] | "";
        next.latitude = d["latitude"] | 0.0f;
        next.longitude = d["longitude"] | 0.0f;
        next.timezone = d["timezone"] | "Europe/Madrid";
        next.locationSet = d["location_set"] | false;
        ok = configQueueFull(next, message);
        if (ok)
            afterSave.store(2);
    } else if (path == "/api/reset") {
        if (String(d["confirm"] | "") != "BORRAR") {
            error(req, 400, "Confirmación incorrecta");
            return ESP_OK;
        }
        ok = configQueueReset(message);
    } else
        message = "Ruta no válida";
    if (ok)
        accepted(req);
    else
        error(req, 409, message);
    return ESP_OK;
}
void stopServer() {
    if (server) {
        httpd_stop(server);
        server = nullptr;
    }
    dns.stop();
}
esp_err_t favicon(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}
esp_err_t http404Handler(httpd_req_t *req, httpd_err_code_t err) {
    if (mode.load() == PortalMode::WifiAccessPoint && req->method == HTTP_GET) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_OK;
}
void startServer() {
    if (server)
        return;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.core_id = runtime::kDataCore;
    c.task_priority = runtime::kPortalPriority;
    c.stack_size = runtime::kPortalStack;
    c.max_uri_handlers = 32;
    c.max_open_sockets = 6;
    c.lru_purge_enable = true;
    c.recv_wait_timeout = 10;
    c.send_wait_timeout = 10;
    const esp_err_t err = httpd_start(&server, &c);
    if (err != ESP_OK) {
        Serial.printf("[PORTAL] Error iniciando servidor HTTP: %d (%s)\n", err, esp_err_to_name(err));
        return;
    }
    Serial.printf("[PORTAL] Servidor HTTP activo en puerto %d (IP: %s)\n",
                  c.server_port, WiFi.localIP().toString().c_str());
    for (const char *url : {"/", "/index.html", "/app.js", "/style.css"}) {
        httpd_uri_t route{};
        route.uri = url;
        route.method = HTTP_GET;
        route.handler = asset;
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    }
    httpd_uri_t favRoute{};
    favRoute.uri = "/favicon.ico";
    favRoute.method = HTTP_GET;
    favRoute.handler = favicon;
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &favRoute));
    for (const char *url : {"/api/config", "/api/diagnostics", "/api/wifi/scan/status",
                            "/api/libre/status", "/api/geocode/status", "/api/mutation/status"}) {
        httpd_uri_t route{};
        route.uri = url;
        route.method = HTTP_GET;
        route.handler = get;
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    }
    for (const char *url : {"/api/wifi", "/api/wifi/networks", "/api/wifi/scan", "/api/libre/login",
                            "/api/geocode", "/api/save", "/api/reset"}) {
        httpd_uri_t route{};
        route.uri = url;
        route.method = HTTP_POST;
        route.handler = post;
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http404Handler);
    lastAccess.store(millis());
}
void waiting(const Config &c) {
    stopServer();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(c.ssid.c_str(), c.wifiPass.c_str());
    wifiDeadline = millis() + 25000;
    mode.store(PortalMode::WaitingForWifi);
}
void accessPoint() {
    stopServer();
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apName, apPass, 1, false, 2);
    mode.store(PortalMode::WifiAccessPoint);
    dns.start(53, "*", WiFi.softAPIP());
    startServer();
}
} // namespace
void portalStart() {
    wanted.store(true);
    lastAccess.store(millis());
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        mode.store(PortalMode::LocalNetwork);
    }
    displayRequestWake();
}
void portalStop() { wanted.store(false); }
bool portalActive() { return wanted.load() || mode.load() == PortalMode::WifiAccessPoint; }
PortalMode portalMode() { return mode.load(); }
String portalSsid() { return apName; }
String portalPassword() { return apPass; }
String portalUrl() {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        return String("http://") + WiFi.localIP().toString() + "/";
    }
    return "http://192.168.4.1/";
}
void portalRuntimeStart() {
    snprintf(apName, sizeof(apName), "GlucoWave-%04X", unsigned(ESP.getEfuseMac() & 0xffff));
    snprintf(apPass, sizeof(apPass), "GW%08lX", static_cast<unsigned long>(esp_random()));
    snprintf(csrf, sizeof(csrf), "%08lX%08lX%08lX", static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
}
void portalLoop() {
    if (net::busy() || configBusy())
        return; // never alter radio during TLS or a scan
    const auto current = mode.load();
    const uint32_t now = millis();
    const Config c = configSnapshot();

    if (!wanted.load()) {
        if (current != PortalMode::Off || server != nullptr) {
            stopServer();
            if (current == PortalMode::WifiAccessPoint) {
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (sta)
                    esp_netif_set_default_netif(sta);
            }
            const Config saved = configSnapshot();
            if (!saved.ssid.isEmpty() &&
                (WiFi.status() != WL_CONNECTED || WiFi.SSID() != saved.ssid))
                WiFi.begin(saved.ssid.c_str(), saved.wifiPass.c_str());
            afterSave.store(0);
            transitionAt = 0;
            mode.store(PortalMode::Off);
            Serial.println("[PORTAL] Servidor HTTP detenido (portal inactivo)");
        }
        return;
    }

    if (current == PortalMode::Off) {
        if (c.ssid.isEmpty())
            accessPoint();
        else if (WiFi.status() == WL_CONNECTED) {
            mode.store(PortalMode::LocalNetwork);
            startServer();
        } else
            waiting(c);
        return;
    }

    if (current == PortalMode::LocalNetwork) {
        if (WiFi.status() != WL_CONNECTED) {
            stopServer();
            waiting(c);
            return;
        }
        if (!server) {
            startServer();
        }
    }

    const int saved = afterSave.load();
    if (saved) {
        String message;
        const SetupJob result = deviceMutationResult(message);
        if (result == SetupJob::Failed) {
            afterSave.store(0);
            transitionAt = 0;
        }
        if (result == SetupJob::Ready && !transitionAt)
            transitionAt = now + 2500;
        if (transitionAt && runtime::due(now, transitionAt)) {
            transitionAt = 0;
            afterSave.store(0);
            if (saved == 1)
                waiting(c);
            else {
                wanted.store(false);
                uiRequestHome();
            }
            return;
        }
    }

    if (current == PortalMode::WaitingForWifi) {
        if (WiFi.status() == WL_CONNECTED) {
            mode.store(PortalMode::LocalNetwork);
            startServer();
        } else if (runtime::due(now, wifiDeadline))
            accessPoint();
        return;
    }

    if (current == PortalMode::WifiAccessPoint)
        dns.processNextRequest();

    if (wanted.load() && (now - lastAccess.load() > 600000)) {
        wanted.store(false);
        uiRequestHome();
    }
}
