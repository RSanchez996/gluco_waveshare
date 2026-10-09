#include "app.hpp"
#include "net.hpp"
#include "runtime.hpp"
#include <WiFi.h>
#include <algorithm>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <mbedtls/sha256.h>
namespace net {
namespace {
std::atomic<bool> active{false};
constexpr size_t kCapacity = 512 * 1024;
// One reusable allocation; no HTTP body String or geometric PSRAM reallocations.
char *bodyBuffer = nullptr;
struct Transfer {
    size_t used = 0, limit = kCapacity;
    uint32_t deadline = 0;
    bool failed = false;
};
bool safe(const String &s) {
    for (size_t i = 0; i < s.length(); ++i)
        if (uint8_t(s[i]) < 32 || uint8_t(s[i]) == 127)
            return false;
    return true;
}
esp_err_t event(esp_http_client_event_t *e) {
    auto *t = static_cast<Transfer *>(e->user_data);
    if (runtime::due(millis(), t->deadline)) {
        t->failed = true;
        return ESP_FAIL;
    }
    return ESP_OK;
}
// Cooperative JSON parsing: bounded byte budget between FreeRTOS yields.
// ArduinoJson owns all selected strings, so the reusable body may be overwritten.
class Reader {
    const char *data;
    size_t length, pos = 0, budget = 0;

  public:
    Reader(const char *p, size_t n) : data(p), length(n) {}
    int read() {
        if (pos >= length)
            return -1;
        if (++budget >= 1024) {
            budget = 0;
            vTaskDelay(1);
        }
        return uint8_t(data[pos++]);
    }
    size_t readBytes(char *dst, size_t n) {
        size_t count = 0;
        for (; count < n && pos < length; ++count)
            dst[count] = char(read());
        return count;
    }
};
} // namespace
bool busy() { return active.load(); }
String encode(const String &s) {
    String out;
    out.reserve(s.length() * 3);
    const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < s.length(); ++i) {
        const uint8_t c = s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += char(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
String sha256(const String &s) {
    uint8_t digest[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char *>(s.c_str()), s.length(), digest, 0);
    char text[65];
    for (int i = 0; i < 32; ++i)
        snprintf(text + i * 2, 3, "%02x", digest[i]);
    return String(text);
}
Response request(const String &url, Doc &doc, const char *method, const String &body,
                 std::initializer_list<Header> headers, size_t limit, JsonVariantConst filter) {
    Response r;
    doc.clear();
    if (xPortGetCoreID() != runtime::kDataCore) {
        r.error = "HTTP fuera del trabajador de datos";
        return r;
    }
    const bool isHttps = url.startsWith("https://");
    const bool isHttp = url.startsWith("http://");
    if ((!isHttps && !isHttp) || !safe(url)) {
        r.error = "URL HTTP no válida";
        return r;
    }
    if (WiFi.status() != WL_CONNECTED) {
        r.error = "Wi-Fi sin conexión";
        return r;
    }
    if (isHttps && time(nullptr) < 1704067200) {
        Serial.println("[HTTP] Esperando sincronización NTP previa a conexión TLS...");
        const uint32_t ntpWaitDeadline = millis() + 8000;
        while (time(nullptr) < 1704067200 && !runtime::due(millis(), ntpWaitDeadline)) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (time(nullptr) < 1704067200) {
            Serial.println("[HTTP] Error: Reloj sin hora válida tras espera NTP");
            r.error = "Esperando hora NTP para validar certificados";
            return r;
        }
        Serial.println("[HTTP] Hora NTP sincronizada correctamente");
    }
    if (xSemaphoreTake(httpMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        r.error = "Red ocupada";
        return r;
    }
    active.store(true);
    struct Guard {
        ~Guard() {
            active.store(false);
            xSemaphoreGive(httpMutex);
        }
    } guard;
    if (!bodyBuffer)
        bodyBuffer = static_cast<char *>(
            heap_caps_malloc(kCapacity + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!bodyBuffer) {
        r.error = "Memoria HTTP insuficiente";
        return r;
    }
    Transfer t;
    t.limit = std::min(kCapacity, std::max<size_t>(4096, limit));
    t.deadline = millis() + 50000;
    esp_http_client_config_t options{};
    options.url = url.c_str();
    if (isHttps) {
        options.crt_bundle_attach = esp_crt_bundle_attach;
    }
    options.event_handler = event;
    options.user_data = &t;
    options.timeout_ms = 25000;
    options.buffer_size = 8192;
    options.buffer_size_tx = 4096;
    options.addr_type = HTTP_ADDR_TYPE_INET;
    options.disable_auto_redirect = true;
    options.keep_alive_enable = false;
    auto client = esp_http_client_init(&options);
    if (!client) {
        r.error = "No se pudo crear HTTP";
        return r;
    }
    struct Cleanup {
        esp_http_client_handle_t h;
        ~Cleanup() { esp_http_client_cleanup(h); }
    } cleanup{client};
    esp_http_client_set_method(client,
                               strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Connection", "close");
    const String userAgent = String("GlucoWaveshare/") + APP_VERSION;
    esp_http_client_set_header(client, "User-Agent", userAgent.c_str());
    if (!body.isEmpty()) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body.c_str(), body.length());
    }
    for (const auto &h : headers) {
        if (!safe(h.value)) {
            r.error = "Cabecera no válida";
            return r;
        }
        esp_http_client_set_header(client, h.name, h.value.c_str());
    }
    const uint32_t started = millis();
    Serial.printf("[HTTP] Abriendo conexion a %s\n", url.c_str());
    esp_err_t result = esp_http_client_open(client, body.length());
    if (result != ESP_OK) {
        const int err = esp_http_client_get_errno(client);
        Serial.printf("[HTTP] Reintentando tras fallo conexion %s a %s: result=%s (%d), errno=%d\n",
                      isHttps ? "HTTPS" : "HTTP", url.c_str(), esp_err_to_name(result), int(result), err);
        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(1500));
        t.deadline = millis() + 45000;
        t.failed = false;
        client = esp_http_client_init(&options);
        if (client) {
            cleanup.h = client;
            esp_http_client_set_method(client,
                                       strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET);
            esp_http_client_set_header(client, "Accept", "application/json");
            esp_http_client_set_header(client, "Accept-Encoding", "identity");
            esp_http_client_set_header(client, "Connection", "close");
            esp_http_client_set_header(client, "User-Agent", userAgent.c_str());
            if (!body.isEmpty()) {
                esp_http_client_set_header(client, "Content-Type", "application/json");
                esp_http_client_set_post_field(client, body.c_str(), body.length());
            }
            for (const auto &h : headers) {
                esp_http_client_set_header(client, h.name, h.value.c_str());
            }
            result = esp_http_client_open(client, body.length());
        }
    }
    if (result != ESP_OK) {
        const int err = esp_http_client_get_errno(client);
        Serial.printf("[HTTP] Error conexion %s a %s: result=%s (%d), errno=%d\n",
                      isHttps ? "HTTPS" : "HTTP", url.c_str(), esp_err_to_name(result), int(result), err);
        r.error = "No se pudo conectar " + String(isHttps ? "HTTPS: " : "HTTP: ") + String(esp_err_to_name(result));
        return r;
    }
    size_t sent = 0;
    while (sent < body.length() && !runtime::due(millis(), t.deadline)) {
        const int n = esp_http_client_write(client, body.c_str() + sent, body.length() - sent);
        if (n <= 0) {
            r.error = "POST HTTP incompleto";
            return r;
        }
        sent += n;
        vTaskDelay(1);
    }
    if (sent != body.length()) {
        r.error = "Tiempo del POST HTTP agotado";
        return r;
    }
    esp_http_client_set_timeout_ms(client, 25000);
    const int64_t declared = esp_http_client_fetch_headers(client);
    r.status = esp_http_client_get_status_code(client);
    if (declared < 0) {
        const int err = esp_http_client_get_errno(client);
        Serial.printf("[HTTP] Timeout esperando cabeceras de %s: status=%d, errno=%d\n",
                      url.c_str(), r.status, err);
        r.error = "Tiempo de espera HTTPS agotado esperando respuesta";
        return r;
    }
    if (declared > int64_t(t.limit)) {
        r.error = "Respuesta HTTPS demasiado grande";
        return r;
    }
    t.used = 0;
    while (t.used < t.limit) {
        if (declared > 0 && int64_t(t.used) >= declared)
            break;
        if (runtime::due(millis(), t.deadline)) {
            Serial.printf("[HTTP] Timeout leyendo body de %s (usados: %u bytes)\n",
                          url.c_str(), unsigned(t.used));
            t.failed = true;
            break;
        }
        const size_t toRead = std::min<size_t>(4096, t.limit - t.used);
        const int n = esp_http_client_read(client, bodyBuffer + t.used, toRead);
        if (n <= 0) {
            // Fin de stream, socket cerrado o fin de chunks
            break;
        }
        t.used += n;
        if (esp_http_client_is_chunked_response(client) &&
            esp_http_client_is_complete_data_received(client))
            break;
    }
    if (t.failed) {
        r.status = -2;
        r.error = "Tiempo de espera HTTP agotado";
        return r;
    }
    if (declared > 0 && int64_t(t.used) < declared) {
        Serial.printf("[HTTP] Respuesta truncada de %s: %u de %lld bytes\n",
                      url.c_str(), unsigned(t.used), declared);
        r.status = -2;
        r.error = "Respuesta HTTP truncada";
        return r;
    }
    if (t.used == 0 && r.status == 200) {
        Serial.printf("[HTTP] Respuesta vacía de %s\n", url.c_str());
        r.status = -2;
        r.error = "Respuesta HTTP vacía";
        return r;
    }
    bodyBuffer[t.used] = 0;
    Serial.printf(
        "[HTTP] %s %d %u bytes %lu ms internal=%u largest=%u\n", isHttps ? "HTTPS" : "HTTP", r.status, unsigned(t.used),
        static_cast<unsigned long>(millis() - started),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    if (r.status >= 300 && r.status < 400) {
        r.error = "Redirección HTTP no permitida";
        return r;
    }
    if (r.status == 429) {
        r.retrySeconds = 900;
        r.error = "Límite temporal del proveedor; reintento en 15 min";
        return r;
    }
    if (r.status == 403 || r.status == 430) {
        r.retrySeconds = 900;
        r.error = "Acceso temporalmente rechazado (HTTP " + String(r.status) + ")";
        return r;
    }
    if (t.used) {
        Reader reader(bodyBuffer, t.used);
        const auto error =
            filter.isNull() ? deserializeJson(doc, reader)
                            : deserializeJson(doc, reader, DeserializationOption::Filter(filter));
        if (error && r.status >= 200 && r.status < 300) {
            r.status = -3;
            r.error = "JSON no reconocido: " + String(error.c_str());
            return r;
        }
    }
    if (r.status >= 400) {
        r.error = "Error HTTP " + String(r.status);
        const char *message = doc["message"] | "";
        if (!*message)
            message = doc["error"]["message"] | "";
        if (*message)
            r.error += " - " + String(message);
    }
    return r;
}
} // namespace net
