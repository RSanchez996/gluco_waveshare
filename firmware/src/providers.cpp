#include "providers.hpp"
#include "protocol.hpp"
#include <Preferences.h>
#include <algorithm>
using net::Doc;
using net::text;
namespace {
String base(const String &region) { return "https://api-" + region + ".libreview.io"; }
bool validRegion(const String &r) {
    for (const char *x : {"eu", "eu2", "us", "de", "fr", "ae", "ap", "au", "ca", "jp", "la", "ru"})
        if (r == x)
            return true;
    return false;
}
struct Session {
    String token, account, region;
};
Session stagedSession;
String stagedIdentity;
String credentialIdentity(const LibreCredentials &c) {
    return net::sha256(c.user + "\n" + c.password + "\n" + c.region + "\n" + c.version);
}
void clearStagedSession() {
    if (stateMutex)
        xSemaphoreTake(stateMutex, portMAX_DELAY);
    stagedSession.token = "";
    stagedSession.account = "";
    stagedSession.region = "";
    stagedIdentity = "";
    if (stateMutex)
        xSemaphoreGive(stateMutex);
}
void stageSession(const LibreCredentials &c, const Session &s) {
    if (stateMutex)
        xSemaphoreTake(stateMutex, portMAX_DELAY);
    stagedSession = s;
    stagedIdentity = credentialIdentity(c);
    if (stateMutex)
        xSemaphoreGive(stateMutex);
}
bool takeStagedSession(const LibreCredentials &c, Session &s) {
    if (stateMutex)
        xSemaphoreTake(stateMutex, portMAX_DELAY);
    const bool valid = !stagedSession.token.isEmpty() && stagedIdentity == credentialIdentity(c);
    if (valid)
        s = stagedSession;
    if (stateMutex)
        xSemaphoreGive(stateMutex);
    if (!valid)
        return false;
    Serial.println("[LIBRE] Reutilizando la sesión validada en RAM");
    return true;
}
bool loginCredentials(const LibreCredentials &c, Session &s, String &error, uint32_t &retry) {
    s = {};
    s.region = c.region;
    if (!validRegion(s.region)) {
        error = "Región LibreLinkUp no admitida";
        return false;
    }
    String visited;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (visited.indexOf("|" + s.region + "|") >= 0) {
            error = "Bucle de región LibreLinkUp";
            return false;
        }
        visited += "|" + s.region + "|";
        // El login puede devolver perfiles y metadatos grandes. Conservar solo
        // los campos necesarios reduce la presión de PSRAM durante TLS.
        Doc filter(1024);
        filter["status"] = true;
        filter["message"] = true;
        JsonObject data = filter.createNestedObject("data");
        data["redirect"] = true;
        data["region"] = true;
        data.createNestedObject("authTicket")["token"] = true;
        data.createNestedObject("user")["id"] = true;
        Doc body(1800), response(8 * 1024);
        body["email"] = c.user;
        body["password"] = c.password;
        String json;
        serializeJson(body, json);
        Serial.printf("[LIBRE] Login: región %s (sin mostrar credenciales)\n", s.region.c_str());
        appDiagnosticStage("Libre: login HTTPS");
        auto r = net::request(base(s.region) + "/llu/auth/login", response, "POST", json,
                              {{"product", "llu.android"},
                               {"version", c.version},
                               {"User-Agent", "okhttp/4.10.0"},
                               {"Accept-Language", "es-ES,es;q=0.9"},
                               {"Cache-Control", "no-cache"},
                               {"Pragma", "no-cache"}},
                              64 * 1024, filter.as<JsonVariantConst>());
        retry = r.retrySeconds;
        if (r.status != 200) {
            // Un 403 puede proceder del filtro anti-bots. No probamos otra
            // región a ciegas: duplicaría los intentos y podría activar 430.
            error = "Login LibreLinkUp (" + s.region + "): " + r.error;
            Serial.printf("[LIBRE] Login rechazado: HTTP/HTTPS %d, reintento %u s\n", r.status,
                          unsigned(retry));
            return false;
        }
        const int status = response["status"] | -1;
        if (status == 2) {
            error = "Correo o contraseña de LibreLinkUp incorrectos (o cuenta de LibreView en vez de LibreLinkUp)";
            retry = 900;
            return false;
        }
        if (status == 4) {
            error = "Abre la app LibreLinkUp y acepta las condiciones pendientes";
            retry = 900;
            return false;
        }
        if (response["data"]["redirect"] | false) {
            String next = response["data"]["region"] | "";
            if (!validRegion(next)) {
                error = "Región redirigida no soportada";
                return false;
            }
            Serial.printf("[LIBRE] Redirección declarada por el proveedor: %s\n", next.c_str());
            s.region = next;
            continue;
        }
        const char *token = response["data"]["authTicket"]["token"] | "",
                   *id = response["data"]["user"]["id"] | "";
        if (status || !*token || !*id) {
            error = "Login LibreLinkUp: respuesta no reconocida (estado " + String(status) + ")";
            return false;
        }
        s.token = token;
        s.account = net::sha256(id);
        Serial.printf("[LIBRE] Token obtenido en región %s\n", s.region.c_str());
        return true;
    }
    error = "Demasiados cambios de región";
    return false;
}
net::Response sessionGet(Session &s, const LibreCredentials &c, const String &route, Doc &doc,
                         size_t responseLimit = 96 * 1024,
                         JsonVariantConst filter = JsonVariantConst()) {
    return net::request(base(s.region) + route, doc, "GET", "",
                        {{"product", "llu.android"},
                         {"version", c.version},
                         {"User-Agent", "okhttp/4.10.0"},
                         {"Accept-Language", "es-ES,es;q=0.9"},
                         {"Cache-Control", "no-cache"},
                         {"Pragma", "no-cache"},
                         {"Authorization", "Bearer " + s.token},
                         {"Account-Id", s.account}},
                        responseLimit, filter);
}
size_t parseConnections(JsonArrayConst source, ConnectionChoice *out, size_t capacity) {
    size_t count = 0;
    for (JsonObjectConst item : source) {
        const char *id = item["patientId"] | "";
        if (!*id || strlen(id) > 99 || count >= capacity)
            continue;
        strlcpy(out[count].id, id, sizeof(out[count].id));
        String name = String(item["firstName"] | "") + " " + String(item["lastName"] | "");
        name.trim();
        strlcpy(out[count].name, name.isEmpty() ? id : name.c_str(), sizeof(out[count].name));
        ++count;
    }
    return count;
}
uint64_t connectionSignature(const ConnectionChoice *items, size_t count) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < count; ++i) {
        for (const char *p = items[i].id; *p; ++p) {
            hash ^= uint8_t(*p);
            hash *= 1099511628211ULL;
        }
        hash ^= 0xff;
        hash *= 1099511628211ULL;
        for (const char *p = items[i].name; *p; ++p) {
            hash ^= uint8_t(*p);
            hash *= 1099511628211ULL;
        }
        hash ^= 0xfe;
        hash *= 1099511628211ULL;
    }
    return hash;
}
bool parseLibrePoint(JsonObjectConst p, int64_t now, gluco::Point &out) {
    const bool has = p["ValueInMgPerDl"].is<double>();
    const int g =
        gluco::libreMgdl(p["ValueInMgPerDl"] | NAN, has, p["Value"] | NAN, p["GlucoseUnits"] | 0);
    const int64_t epoch = gluco::factoryEpoch(p["FactoryTimestamp"] | "");
    if (!gluco::validEpoch(epoch, now) || gluco::range(g) == gluco::Range::Invalid)
        return false;
    out = {epoch, int16_t(g)};
    return true;
}
} // namespace
bool libreListConnections(const LibreCredentials &credentials, ConnectionChoice *out,
                          size_t capacity, size_t &count, String &resolvedRegion, String &error) {
    count = 0;
    uint32_t retry = 60;
    Session s;
    bool reused = takeStagedSession(credentials, s);
    if (!reused && !loginCredentials(credentials, s, error, retry))
        return false;
    // Dos negociaciones TLS seguidas compiten por memoria y radio con el panel
    // RGB. Separarlas un instante deja ejecutar las tareas Idle y Wi-Fi.
    if (!reused)
        vTaskDelay(pdMS_TO_TICKS(500));
    Serial.println("[LIBRE] Consultando usuarios compartidos");
    Doc filter(1024);
    filter["status"] = true;
    filter["message"] = true;
    JsonArray list = filter.createNestedArray("data");
    JsonObject item = list.createNestedObject();
    item["patientId"] = true;
    item["firstName"] = true;
    item["lastName"] = true;
    Doc d(16 * 1024);
    appDiagnosticStage("Libre: usuarios HTTPS");
    auto r =
        sessionGet(s, credentials, "/llu/connections", d, 64 * 1024, filter.as<JsonVariantConst>());
    if (reused && (r.status == 401 || (r.status == 200 && (d["status"] | -1) == 2))) {
        Serial.println("[LIBRE] La sesión en RAM ha caducado; renovando una vez");
        clearStagedSession();
        d.clear();
        if (!loginCredentials(credentials, s, error, retry))
            return false;
        vTaskDelay(pdMS_TO_TICKS(500));
        appDiagnosticStage("Libre: usuarios HTTPS");
        r = sessionGet(s, credentials, "/llu/connections", d, 64 * 1024,
                       filter.as<JsonVariantConst>());
    }
    if (r.status != 200) {
        error = "Usuarios LibreLinkUp (" + s.region + "): " + r.error;
        return false;
    }
    const int status = d["status"] | -1;
    if (status != 0 || !d["data"].is<JsonArray>()) {
        error = "Usuarios LibreLinkUp: respuesta no reconocida (estado " + String(status) + ")";
        return false;
    }
    count = parseConnections(d["data"].as<JsonArrayConst>(), out, capacity);
    resolvedRegion = s.region;
    if (!count) {
        error = "La cuenta no tiene usuarios compartidos. Acepta la invitación en LibreLinkUp.";
        return false;
    }
    LibreCredentials resolvedCredentials = credentials;
    resolvedCredentials.region = s.region;
    stageSession(resolvedCredentials, s);
    return true;
}
bool LibreClient::login(String &error, uint32_t &retry) {
    const Config config = configSnapshot();
    LibreCredentials c{config.libreUser, config.librePass, config.libreRegion, config.libreVersion};
    Session s;
    if (!takeStagedSession(c, s) && !loginCredentials(c, s, error, retry))
        return false;
    stageSession(c, s);
    token = s.token;
    account = s.account;
    region = s.region;
    identity = credentialIdentity(c);
    return true;
}
net::Response LibreClient::get(const String &route, Doc &doc, size_t responseLimit,
                               JsonVariantConst filter) {
    const Config config = configSnapshot();
    net::Response result;
    uint32_t retry = 60;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool neededLogin = token.isEmpty();
        if (neededLogin && !login(result.error, retry)) {
            result.retrySeconds = retry;
            return result;
        }
        if (neededLogin) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        LibreCredentials c{config.libreUser, config.librePass, region, config.libreVersion};
        Session s{token, account, region};
        appDiagnosticStage(route.endsWith("/graph") ? "Libre: gráfica HTTPS"
                                                    : "Libre: usuarios HTTPS");
        result = sessionGet(s, c, route, doc, responseLimit, filter);
        if (result.status == 401 || (result.status == 200 && (doc["status"] | -1) == 2)) {
            clearStagedSession();
            token = "";
            if (!attempt)
                continue;
        }
        return result;
    }
    return result;
}
void LibreClient::resetSession() {
    token = "";
    account = "";
    region = "";
    identity = "";
}
uint32_t LibreClient::listConnections(AppState &state) {
    const Config config = configSnapshot();
    if (config.libreUser.isEmpty()) {
        text(state.connectionsError, "Configura primero la cuenta de LibreLinkUp");
        return 300;
    }
    Doc filter(768);
    filter["status"] = true;
    filter["message"] = true;
    JsonArray list = filter.createNestedArray("data");
    JsonObject item = list.createNestedObject();
    item["patientId"] = true;
    item["firstName"] = true;
    item["lastName"] = true;
    Doc d(28 * 1024);
    auto r = get("/llu/connections", d, 64 * 1024, filter.as<JsonVariantConst>());
    if (r.status != 200 || !d["data"].is<JsonArray>()) {
        text(state.connectionsError, r.error.isEmpty()
                                         ? "No se pudieron actualizar los usuarios compartidos"
                                         : r.error.c_str());
        return std::max<uint32_t>(120, r.retrySeconds);
    }
    const size_t previousCount = state.connectionCount;
    const uint64_t previousSignature = connectionSignature(state.connections, previousCount);
    const size_t count =
        parseConnections(d["data"].as<JsonArrayConst>(), state.connections, MAX_CONNECTIONS);
    if (!count) {
        text(state.connectionsError, "La cuenta no tiene usuarios compartidos en LibreLinkUp");
        return 300;
    }
    state.connectionCount = count;
    state.connectionsFetched = time(nullptr);
    state.connectionsError[0] = 0;
    if (count != previousCount ||
        connectionSignature(state.connections, count) != previousSignature)
        connectionCacheSave(state);
    return 6 * 60 * 60;
}
uint32_t LibreClient::read(AppState &state) {
    const Config config = configSnapshot();
    if (config.libreUser.isEmpty() || config.patientId.isEmpty()) {
        text(state.glucoseError, "Configura LibreLinkUp y selecciona un usuario");
        return 300;
    }
    LibreCredentials credentials{config.libreUser, config.librePass, config.libreRegion,
                                 config.libreVersion};
    if (identity != credentialIdentity(credentials)) {
        token = "";
        account = "";
        region = "";
        identity = "";
    }
    Doc filter(1400);
    filter["status"] = true;
    filter["message"] = true;
    JsonObject data = filter.createNestedObject("data");
    JsonArray graph = data.createNestedArray("graphData");
    JsonObject point = graph.createNestedObject();
    for (const char *key :
         {"FactoryTimestamp", "ValueInMgPerDl", "Value", "GlucoseUnits", "TrendArrow"})
        point[key] = true;
    JsonObject connection = data.createNestedObject("connection");
    JsonObject measurementFilter = connection.createNestedObject("glucoseMeasurement");
    for (const char *key :
         {"FactoryTimestamp", "ValueInMgPerDl", "Value", "GlucoseUnits", "TrendArrow"})
        measurementFilter[key] = true;
    Doc d(128 * 1024);
    auto r = get("/llu/connections/" + net::encode(config.patientId) + "/graph", d, 512 * 1024,
                 filter.as<JsonVariantConst>());
    appDiagnosticStage("Libre: procesando datos");
    if (r.status != 200 || !d["data"]["graphData"].is<JsonArray>()) {
        text(state.glucoseError,
             r.error.isEmpty() ? "Histórico LibreLinkUp no reconocido" : r.error.c_str());
        return r.retrySeconds;
    }
    // /graph ya incluye el histórico. Reemplazarlo entero en cada consulta:
    // los puntos almacenados antes pueden haber sido corregidos por el servicio.
    const int64_t now = time(nullptr);
    // Reservar fuera de la pila de la tarea HTTPS: 160 puntos son 2,5 KiB.
    auto *fresh = static_cast<gluco::Point *>(heap_caps_malloc(
        gluco::kMaxPoints * sizeof(gluco::Point), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!fresh) {
        text(state.glucoseError, "Memoria insuficiente para la gráfica");
        return 300;
    }
    size_t count = 0;
    const size_t received = d["data"]["graphData"].size();
    for (JsonObjectConst p : d["data"]["graphData"].as<JsonArrayConst>()) {
        gluco::Point point{};
        if (!parseLibrePoint(p, now, point))
            continue;
        if (count < gluco::kMaxPoints)
            fresh[count++] = point;
        else {
            // Si llegan más de 160 muestras, conservar las más recientes.
            size_t oldest = 0;
            for (size_t i = 1; i < count; ++i)
                if (fresh[i].epoch < fresh[oldest].epoch)
                    oldest = i;
            if (point.epoch > fresh[oldest].epoch)
                fresh[oldest] = point;
        }
    }
    count = gluco::normalize(fresh, count, now);
    gluco::Point current{};
    const auto reading = d["data"]["connection"]["glucoseMeasurement"].as<JsonObjectConst>();
    const bool currentValid = parseLibrePoint(reading, now, current);
    Serial.printf("[LIBRE] Histórico: %u recibidos, %u válidos / 12 h; actual: %s\n",
                  unsigned(received), unsigned(count), currentValid ? "sí" : "no");
    if (!count && !currentValid) {
        heap_caps_free(fresh);
        text(state.glucoseError, "LibreLinkUp respondió sin lecturas válidas");
        return 300;
    }
    // El servicio puede devolver solo la lectura actual mientras recompone
    // el histórico. Conservar la última curva hasta recibir otra válida.
    if (count)
        memcpy(state.points, fresh, count * sizeof(gluco::Point));
    heap_caps_free(fresh);
    if (count)
        state.pointCount = count;
    state.current = current;
    state.currentValid = currentValid;
    text(state.direction, currentValid ? gluco::libreTrend(reading["TrendArrow"] | 0) : "");
    // Solo RAM. Ninguna escritura periódica a NVS ni trabajo adicional en HTTPS.
    state.glucoseFetched = time(nullptr);
    state.glucoseError[0] = 0;
    return 120;
}
void connectionCacheLoad(AppState &state) {
    const Config config = configSnapshot();
    xSemaphoreTake(storageMutex, portMAX_DELAY);
    Preferences p;
    if (!p.begin("glucousers", true)) {
        xSemaphoreGive(storageMutex);
        return;
    }
    LibreCredentials c{config.libreUser, config.librePass, config.libreRegion, config.libreVersion};
    const String owner = p.getString("owner", "");
    const size_t bytes = p.getBytesLength("items");
    if (owner == credentialIdentity(c) && bytes && bytes <= sizeof(state.connections) &&
        bytes % sizeof(ConnectionChoice) == 0) {
        state.connectionCount =
            p.getBytes("items", state.connections, bytes) / sizeof(ConnectionChoice);
        for (size_t i = 0; i < state.connectionCount; ++i)
            if (!memchr(state.connections[i].id, 0, sizeof(state.connections[i].id)) ||
                !memchr(state.connections[i].name, 0, sizeof(state.connections[i].name)) ||
                !state.connections[i].id[0]) {
                state.connectionCount = 0;
                break;
            }
        if (state.connectionCount > 0)
            state.connectionsFetched = time(nullptr);
    }
    p.end();
    xSemaphoreGive(storageMutex);
}
void connectionCacheSave(const AppState &state) {
    const Config config = configSnapshot();
    if (!state.connectionCount)
        return;
    LibreCredentials c{config.libreUser, config.librePass, config.libreRegion, config.libreVersion};
    xSemaphoreTake(storageMutex, portMAX_DELAY);
    Preferences p;
    if (p.begin("glucousers", false)) {
        p.putString("owner", credentialIdentity(c));
        p.putBytes("items", state.connections, state.connectionCount * sizeof(ConnectionChoice));
        p.end();
    }
    xSemaphoreGive(storageMutex);
}

void DiabetesMClient::resetSession() {
    token = "";
    cookies = "";
    lastUploadedEpoch = 0;
}

bool DiabetesMClient::login(const Config &config, String &error) {
    if (config.diabetesmUser.isEmpty() || config.diabetesmPass.isEmpty()) {
        error = "Faltan credenciales de Diabetes:M";
        return false;
    }
    Serial.println("[DIABETES-M] Conectando con Diabetes:M...");
    Doc req(512);
    req["username"] = config.diabetesmUser;
    req["password"] = config.diabetesmPass;
    req["device"] = "web";
    req["client"] = "web";
    String body;
    serializeJson(req, body);

    Doc res(32768);
    net::Response r = net::request(
        "https://analytics.diabetes-m.com/api/v1/user/authentication/login_v2",
        res, "POST", body, {
            {"Origin", "https://analytics.diabetes-m.com"},
            {"Referer", "https://analytics.diabetes-m.com/"}
        }, 16 * 1024);

    if (r.status != 200) {
        token = "";
        cookies = "";
        error = "Diabetes:M login: " + (r.error.isEmpty() ? String("HTTP ") + String(r.status) : r.error);
        Serial.printf("[DIABETES-M] Error de login HTTP %d: %s\n", r.status, r.error.c_str());
        return false;
    }

    token = res["token"].as<String>();
    if (token.isEmpty() && res.containsKey("data")) {
        token = res["data"]["token"].as<String>();
    }
    if (token.isEmpty()) {
        error = "Diabetes:M: token ausente en respuesta";
        return false;
    }
    cookies = r.cookie;
    Serial.printf("[DIABETES-M] Sesión iniciada con éxito (cookies=%s)\n",
                  cookies.isEmpty() ? "ninguna" : cookies.c_str());
    return true;
}

bool DiabetesMClient::uploadGlucose(const gluco::Point &point, const Config &config, String &error) {
    if (!config.diabetesmEnabled || config.diabetesmUser.isEmpty() || config.diabetesmPass.isEmpty())
        return true;

    if (point.epoch <= 0 || point.glucose <= 0)
        return true;

    if (point.epoch <= lastUploadedEpoch)
        return true;

    if (token.isEmpty()) {
        if (!login(config, error))
            return false;
    }

    Doc req(512);
    req["entry_time"] = int64_t(point.epoch) * 1000LL;
    double mmol = double(point.glucose) / 18.0182;
    req["glucose"] = double(round(mmol * 100.0) / 100.0);
    req["glucoseInCurrentUnit"] = int(point.glucose);
    req["is_sensor"] = 1;
    req["category"] = 0;
    req["notes"] = "GlucoWaveshare";
    const String tz = config.timezone.isEmpty() ? "Europe/Madrid" : config.timezone;
    req["timezone"] = tz;
    String body;
    serializeJson(req, body);

    std::initializer_list<net::Header> hdrs = {
        {"Authorization", "Bearer " + token},
        {"Origin", "https://analytics.diabetes-m.com"},
        {"Referer", "https://analytics.diabetes-m.com/"},
        {"Cookie", cookies}
    };

    Doc res(16384);
    net::Response r = net::request(
        "https://analytics.diabetes-m.com/api/v1/diary/entries/save_as_new",
        res, "POST", body, hdrs, 16 * 1024);

    if (r.status == 401) {
        Serial.println("[DIABETES-M] Token caducado; reintentando login");
        token = "";
        cookies = "";
        if (!login(config, error))
            return false;
        res.clear();
        std::initializer_list<net::Header> retryHdrs = {
            {"Authorization", "Bearer " + token},
            {"Origin", "https://analytics.diabetes-m.com"},
            {"Referer", "https://analytics.diabetes-m.com/"},
            {"Cookie", cookies}
        };
        r = net::request(
            "https://analytics.diabetes-m.com/api/v1/diary/entries/save_as_new",
            res, "POST", body, retryHdrs, 16 * 1024);
    }

    if (r.status != 200) {
        error = "Diabetes:M subida: " + (r.error.isEmpty() ? String("HTTP ") + String(r.status) : r.error);
        Serial.printf("[DIABETES-M] Error al subir lectura (%d): %s\n", r.status, r.error.c_str());
        return false;
    }

    lastUploadedEpoch = point.epoch;
    Serial.printf("[DIABETES-M] Lectura sincronizada correctamente: %d mg/dL (%.2f mmol/L) [%s]\n",
                  int(point.glucose), mmol, tz.c_str());
    return true;
}
