#include "app.hpp"
#include "net.hpp"
#include "providers.hpp"
#include "runtime.hpp"
#include <WiFi.h>
#include <algorithm>
#include <esp_netif.h>
#include <esp_wifi.h>

using net::Doc;
using net::text;

namespace {
constexpr uint32_t kLibreGapMs = 45000;
constexpr uint32_t kTlsGapMs = 3000;
constexpr uint32_t kWifiStableMs = 8000;
constexpr uint32_t kReconnectFallbackMs = 60000;
constexpr uint32_t kExtraWifiFallbackMs = 30000;
AppState *work = nullptr;

bool due(uint32_t now, uint32_t at) { return at && int32_t(now - at) >= 0; }

void publish() {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    memcpy(sharedState, work, sizeof(AppState));
    xSemaphoreGive(stateMutex);
}

void publishGlucoseStatus(const char *status) {
    if (strcmp(work->glucoseError, status) == 0)
        return;
    text(work->glucoseError, status);
    publish();
}

uint32_t weather(const Config &config) {
    if (!config.locationSet) {
        text(work->weatherError, "Configura una ubicación en Ajustes");
        return 300;
    }
    const String tz = config.timezone.isEmpty() ? "auto" : net::encode(config.timezone);
    const String url =
        "http://api.open-meteo.com/v1/forecast?latitude=" + String(config.latitude, 5) +
        "&longitude=" + String(config.longitude, 5) +
        "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m,is_day"
        "&hourly=temperature_2m,precipitation_probability,weather_code,is_day"
        "&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max"
        "&forecast_hours=8&forecast_days=4&timezone=" +
        tz + "&timeformat=unixtime";
    // Seis horas visibles y cuatro días agregados en una única petición.
    Doc d(16 * 1024);
    const auto r = net::request(url, d, "GET", "", {}, 32 * 1024);
    if (r.status != 200 || !d["current"]["temperature_2m"].is<double>()) {
        text(work->weatherError,
             r.error.isEmpty() ? "Respuesta meteorológica incompleta" : r.error.c_str());
        return std::max<uint32_t>(45, r.retrySeconds);
    }
    work->temperature = d["current"]["temperature_2m"];
    work->apparent = d["current"]["apparent_temperature"] | work->temperature;
    work->weatherCode = d["current"]["weather_code"] | 0;
    work->weatherIsDay = (d["current"]["is_day"] | 1) != 0;
    work->wind = d["current"]["wind_speed_10m"] | 0.0f;
    work->high = d["daily"]["temperature_2m_max"][0] | work->temperature;
    work->low = d["daily"]["temperature_2m_min"][0] | work->temperature;
    work->hourCount = 0;
    const auto times = d["hourly"]["time"].as<JsonArrayConst>();
    for (size_t i = 0; i < times.size() && work->hourCount < 6; ++i) {
        const int64_t t = times[i].as<int64_t>();
        if (t < time(nullptr) || !d["hourly"]["temperature_2m"][i].is<double>())
            continue;
        auto &h = work->hours[work->hourCount++];
        h.epoch = t;
        h.temperature = d["hourly"]["temperature_2m"][i];
        h.rain = d["hourly"]["precipitation_probability"][i] | 0;
        h.code = d["hourly"]["weather_code"][i] | 0;
        h.isDay = (d["hourly"]["is_day"][i] | 1) != 0;
    }
    work->dayCount = 0;
    const auto dates = d["daily"]["time"].as<JsonArrayConst>();
    for (size_t i = 0; i < dates.size() && work->dayCount < 4; ++i) {
        if (dates[i].isNull() || d["daily"]["temperature_2m_max"][i].isNull() ||
            d["daily"]["temperature_2m_min"][i].isNull())
            continue;
        auto &day = work->days[work->dayCount++];
        day.epoch = dates[i].as<int64_t>();
        day.high = d["daily"]["temperature_2m_max"][i];
        day.low = d["daily"]["temperature_2m_min"][i];
        day.code = d["daily"]["weather_code"][i] | 0;
        day.rain = d["daily"]["precipitation_probability_max"][i] | -1;
    }
    work->weatherFetched = time(nullptr);
    work->weatherValid = true;
    work->weatherError[0] = 0;
    return 30 * 60;
}

void task(void *) {
    Config config = configSnapshot();
    work = static_cast<AppState *>(
        heap_caps_calloc(1, sizeof(AppState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!work)
        abort();
    strlcpy(work->activePatientId, config.patientId.c_str(), sizeof(work->activePatientId));
    connectionCacheLoad(*work);
    publish();

    LibreClient libre;
    uint32_t glucoseDue = 0, weatherDue = 0, connectionsDue = 0;
    uint32_t connectedAt = 0, disconnectedAt = 0, lastTlsRequest = 0, lastLibreRequest = 0,
             reconnectAt = 0, libreBlockedUntil = 0;
    bool scheduleReady = false, portalWasActive = false;
    uint8_t glucoseShortRetries = 0;
    String accountKey = net::sha256(config.libreUser + "\n" + config.librePass + "\n" +
                                    config.libreRegion + "\n" + config.libreVersion);
    String locationKey = net::sha256(config.city + "\n" + String(config.latitude, 5) + "\n" +
                                     String(config.longitude, 5));
    uint32_t knownRevision = configRevision.load(std::memory_order_acquire);
    int lastWifi = -1;
    size_t fallbackIndex = 0;

    while (true) {
        configService();
        // Radio changes and TLS share this owner: no cross-task Wi-Fi race.
        portalLoop();
        config = configSnapshot();
        if (strcmp(work->activePatientId, config.patientId.c_str()) != 0) {
            strlcpy(work->activePatientId, config.patientId.c_str(), sizeof(work->activePatientId));
            work->pointCount = 0;
            work->currentValid = false;
            work->current = {};
            work->direction[0] = 0;
            work->glucoseFetched = 0;
            work->glucoseRequestStartedMs = 0;
            text(work->glucoseError, "Usuario cambiado; esperando una lectura nueva...");
            glucoseDue = millis() + 1000;
            libreBlockedUntil = 0;
            glucoseShortRetries = 0;
            publish();
        }
        PatientSelection selection{};
        if (patientQueue && xQueueReceive(patientQueue, &selection, 0) == pdTRUE) {
            const bool changedPatient = config.patientId != selection.id;
            if (configSelectPatient(selection.id, selection.name)) {
                config = configSnapshot();
                strlcpy(work->activePatientId, selection.id, sizeof(work->activePatientId));
                if (changedPatient) {
                    glucoseShortRetries = 0;
                    work->pointCount = 0;
                    work->currentValid = false;
                    work->current = {};
                    work->direction[0] = 0;
                    work->glucoseFetched = 0;
                    // El histórico está solo en RAM; no escribir flash al
                    // cambiar, ni mostrar la lectura del usuario anterior.
                }
                text(work->glucoseError, changedPatient
                                             ? "Usuario cambiado; esperando una lectura nueva..."
                                             : "Actualizando la lectura del usuario actual...");
                // La autenticación es de la cuenta, no de cada paciente. Reutilizar
                // el token evita repetir un POST TLS al cambiar de usuario.
                libreBlockedUntil = 0;
                glucoseDue = millis() + 1000;
                Serial.printf("[LIBRE] Usuario activo: %s\n", selection.name);
                publish();
            } else {
                text(work->glucoseError, "No se pudo guardar el usuario seleccionado");
                publish();
            }
        }

        if (portalActive() || configUiActive.load(std::memory_order_acquire)) {
            portalWasActive = true;
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (portalWasActive) {
            portalWasActive = false;
            scheduleReady = false;
            connectedAt = 0;
            lastTlsRequest = 0;
            lastLibreRequest = 0;
        }
        const uint32_t revision = configRevision.load(std::memory_order_acquire);
        if (revision != knownRevision) {
            knownRevision = revision;
            const String currentAccountKey =
                net::sha256(config.libreUser + "\n" + config.librePass + "\n" + config.libreRegion +
                            "\n" + config.libreVersion);
            if (currentAccountKey != accountKey) {
                glucoseShortRetries = 0;
                accountKey = currentAccountKey;
                strlcpy(work->activePatientId, config.patientId.c_str(),
                        sizeof(work->activePatientId));
                work->pointCount = 0;
                work->currentValid = false;
                work->current = {};
                work->glucoseFetched = 0;
                work->glucoseRequestStartedMs = 0;
                work->direction[0] = 0;
                work->connectionCount = 0;
                work->connectionsFetched = 0;
                work->connectionsError[0] = 0;
                libre.resetSession();
                libreBlockedUntil = 0;
                scheduleReady = false;
                publish();
            }
            const String currentLocationKey =
                net::sha256(config.city + "\n" + String(config.latitude, 5) + "\n" +
                            String(config.longitude, 5));
            if (currentLocationKey != locationKey) {
                locationKey = currentLocationKey;
                work->weatherValid = false;
                work->weatherFetched = 0;
                work->hourCount = 0;
                work->dayCount = 0;
                work->weatherError[0] = 0;
                weatherDue = millis() + 3000;
                publish();
            }
        }

        const uint32_t now = millis();
        const int wifi = int(WiFi.status());
        if (wifi != lastWifi) {
            lastWifi = wifi;
            if (wifi == WL_CONNECTED) {
                esp_wifi_set_ps(WIFI_PS_NONE);
                WiFi.setSleep(false);
                esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (sta)
                    esp_netif_set_default_netif(sta);
                IPAddress dns0 = WiFi.dnsIP(0);
                if (dns0 == IPAddress(0, 0, 0, 0) || dns0 == WiFi.softAPIP()) {
                    WiFi.setDNS(IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));
                    Serial.println("[WIFI] DNS configurado a servidores publicos (8.8.8.8, 1.1.1.1)");
                } else {
                    WiFi.setDNS(dns0, IPAddress(8, 8, 8, 8));
                }
                Serial.printf("[WIFI] Conectado: %s | GW: %s | DNS: %s | RSSI %d dBm\n",
                              WiFi.localIP().toString().c_str(),
                              WiFi.gatewayIP().toString().c_str(),
                              WiFi.dnsIP(0).toString().c_str(), WiFi.RSSI());
            } else
                Serial.printf("[WIFI] Estado %d; la reconexión automática sigue activa\n", wifi);
        }
        if (wifi != WL_CONNECTED) {
            if (!disconnectedAt) {
                disconnectedAt = now;
                reconnectAt = now + (config.extraWifiCount ? 15000 : kReconnectFallbackMs);
                fallbackIndex = 0;
            }
            connectedAt = 0;
            scheduleReady = false;
            appDiagnosticStage("Esperando conexión Wi-Fi");
            publishGlucoseStatus("Wi-Fi desconectado; mostrando los últimos datos guardados");
            if (!config.ssid.isEmpty() && (!reconnectAt || int32_t(now - reconnectAt) >= 0)) {
                if (config.extraWifiCount) {
                    // Dejar primero 15 s al reintento automático; después probar
                    // las redes guardadas de una en una, solo sin conexión.
                    const size_t index = fallbackIndex++ % (config.extraWifiCount + 1);
                    const String &ssid =
                        index < config.extraWifiCount ? config.extraWifi[index].ssid : config.ssid;
                    const String &pass = index < config.extraWifiCount
                                             ? config.extraWifi[index].password
                                             : config.wifiPass;
                    Serial.printf("[WIFI] Probando red guardada %s\n", ssid.c_str());
                    WiFi.begin(ssid.c_str(), pass.c_str());
                    reconnectAt = now + kExtraWifiFallbackMs;
                } else {
                    WiFi.reconnect();
                    reconnectAt = now + kReconnectFallbackMs;
                    Serial.println(
                        "[WIFI] Reintento de respaldo sin borrar ni reiniciar la interfaz");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        disconnectedAt = 0;
        if (!connectedAt) {
            connectedAt = now;
            reconnectAt = now + kReconnectFallbackMs;
        }
        if (now - connectedAt < kWifiStableMs) {
            appDiagnosticStage("Estabilizando Wi-Fi");
            publishGlucoseStatus("Wi-Fi conectado; estabilizando la red...");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (time(nullptr) < 1700000000) {
            appDiagnosticStage("Sincronizando reloj");
            publishGlucoseStatus("Sincronizando el reloj por Internet...");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (!scheduleReady) {
            scheduleReady = true;
            const uint32_t start = millis();
            const bool hasPatient = !config.patientId.isEmpty();
            const bool usersFresh = work->connectionCount && (hasPatient || (work->connectionsFetched > 0 &&
                                    time(nullptr) - work->connectionsFetched < 6 * 60 * 60));
            connectionsDue = (usersFresh || hasPatient) ? start + 6 * 60 * 60 * 1000UL
                                                        : start + 1000;
            glucoseDue = hasPatient ? start + 1000 : start + 15000;
            weatherDue = config.locationSet ? (work->weatherValid ? start + 30 * 60 * 1000 : start + 4000) : 0;
            if (!config.locationSet)
                text(work->weatherError, "Configura una ubicación en Ajustes");
            appDiagnosticStage("Esperando consulta Libre");
            Serial.println(
                "[RED] Cola iniciada: usuarios y glucosa LibreLinkUp separados en el tiempo");
        }

        const uint32_t tick = millis();
        const bool tlsGapReady = !lastTlsRequest || tick - lastTlsRequest >= kTlsGapMs;
        const bool libreGapReady = !lastLibreRequest || tick - lastLibreRequest >= kLibreGapMs;
        const bool libreReady = !libreBlockedUntil || due(tick, libreBlockedUntil);
        if (tlsGapReady && libreGapReady && libreReady && due(tick, glucoseDue) && !config.patientId.isEmpty()) {
            appDiagnosticStage("Preparando gráfica Libre");
            Serial.println("[RED 1/1] Consultando glucosa LibreLinkUp");
            text(work->glucoseError, "Consultando gráfica LibreLinkUp...");
            work->glucoseRequestStartedMs = millis();
            publish();
            const uint32_t wait = libre.read(*work);
            work->glucoseRequestStartedMs = 0;
            publish();
            appDiagnosticStage("Esperando siguiente consulta");
            lastTlsRequest = millis();
            lastLibreRequest = lastTlsRequest;
            const bool transient = work->glucoseError[0] && wait <= 120;
            const uint32_t seconds = transient && glucoseShortRetries == 0
                                         ? std::max<uint32_t>(45, wait)
                                         : std::max<uint32_t>(120, wait);
            glucoseShortRetries = transient ? std::min<uint8_t>(2, glucoseShortRetries + 1) : 0;
            glucoseDue = lastTlsRequest + seconds * 1000;
            if (work->glucoseError[0] && wait > 120)
                libreBlockedUntil = lastTlsRequest + wait * 1000;
            else if (!work->glucoseError[0])
                libreBlockedUntil = 0;
        } else if (tlsGapReady && libreGapReady && libreReady && due(tick, connectionsDue)) {
            appDiagnosticStage("Preparando usuarios Libre");
            Serial.println("[RED 1/1] Actualizando la lista de usuarios LibreLinkUp");
            const uint32_t wait = libre.listConnections(*work);
            publish();
            appDiagnosticStage("Esperando siguiente consulta");
            lastTlsRequest = millis();
            lastLibreRequest = lastTlsRequest;
            connectionsDue = lastTlsRequest + std::max<uint32_t>(300, wait) * 1000;
            // Solo el rechazo explícito del proveedor limita también glucosa.
            // Un fallo de la lista de usuarios no detiene el paciente ya elegido.
            if (work->connectionsError[0] && wait >= 900)
                libreBlockedUntil = lastTlsRequest + wait * 1000;
        } else if (tlsGapReady && due(tick, weatherDue) && config.locationSet) {
            Serial.println("[RED 1/1] Consultando clima Open-Meteo");
            const uint32_t wait = weather(config);
            publish();
            lastTlsRequest = millis();
            weatherDue = lastTlsRequest + std::max<uint32_t>(work->weatherValid ? 1800 : 45, wait) * 1000;
        }
        vTaskDelay(pdMS_TO_TICKS(portalActive() ? 50 : 250));
    }
}
} // namespace

void networkStart() {
    TaskHandle_t handle = nullptr;
    if (xTaskCreatePinnedToCore(task, "data", runtime::kDataStack, nullptr, runtime::kDataPriority,
                                &handle, runtime::kDataCore) != pdPASS)
        abort();
    appRecordDataTask(handle);
}

const char *weatherText(int code) {
    if (code == 0)
        return "Despejado";
    if (code <= 3)
        return "Nubes y claros";
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
