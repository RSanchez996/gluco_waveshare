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
    String cookie;
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
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value) {
        if (strcasecmp(e->header_key, "Set-Cookie") == 0) {
            String c = e->header_value;
            int semi = c.indexOf(';');
            if (semi > 0)
                c = c.substring(0, semi);
            c.trim();
            if (!t->cookie.isEmpty())
                t->cookie += "; ";
            t->cookie += c;
        }
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
    if (xSemaphoreTake(httpMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        r.error = "Red ocupada";
        return r;
    }
    active.store(true);
    static uint32_t lastHttpEndMs = 0;
    struct Guard {
        ~Guard() {
            lastHttpEndMs = millis();
            active.store(false);
            xSemaphoreGive(httpMutex);
        }
    } guard;
    if (lastHttpEndMs != 0) {
        const uint32_t elapsed = millis() - lastHttpEndMs;
        if (elapsed < 750) {
            vTaskDelay(pdMS_TO_TICKS(750 - elapsed));
        }
    }
    if (!bodyBuffer)
        bodyBuffer = static_cast<char *>(
            heap_caps_malloc(kCapacity + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!bodyBuffer) {
        r.error = "Memoria HTTP insuficiente";
        return r;
    }
    for (const auto &h : headers) {
        if (!safe(h.value)) {
            r.error = "Cabecera no válida";
            return r;
        }
    }

    Transfer t;
    t.limit = std::min(kCapacity, std::max<size_t>(4096, limit));
    const String userAgent = String("GlucoWaveshare/") + APP_VERSION;
    const uint32_t started = millis();
    esp_err_t lastOpenErr = ESP_OK;
    int64_t declared = -1;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            Serial.printf("[HTTP] Reintento %d/2 para %s tras fallo previo (%s)\n", attempt,
                          url.c_str(), r.error.c_str());
            vTaskDelay(pdMS_TO_TICKS(1500));
        }

        t.used = 0;
        t.deadline = millis() + 45000;
        t.failed = false;
        t.cookie = "";
        r.status = 0;
        r.error = "";
        r.cookie = "";

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
        options.disable_auto_redirect = false;
        options.max_redirection_count = 3;
        options.keep_alive_enable = false;

        auto client = esp_http_client_init(&options);
        if (!client) {
            r.error = "No se pudo crear HTTP";
            continue;
        }
        struct Cleanup {
            esp_http_client_handle_t h;
            ~Cleanup() {
                if (h) {
                    esp_http_client_close(h);
                    esp_http_client_cleanup(h);
                }
            }
        } cleanup{client};

        esp_http_client_set_method(client,
                                   strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET);
        esp_http_client_set_header(client, "Accept", "application/json, text/plain, */*");
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

        if (attempt == 0) {
            Serial.printf("[HTTP] Abriendo conexion a %s\n", url.c_str());
        }
        lastOpenErr = esp_http_client_open(client, body.length());
        if (lastOpenErr != ESP_OK) {
            const int err = esp_http_client_get_errno(client);
            Serial.printf("[HTTP] Fallo conexion %s a %s (intento %d): result=%s (%d), errno=%d\n",
                          isHttps ? "HTTPS" : "HTTP", url.c_str(), attempt + 1,
                          esp_err_to_name(lastOpenErr), int(lastOpenErr), err);
            r.error = "No se pudo conectar " + String(isHttps ? "HTTPS: " : "HTTP: ") +
                      String(esp_err_to_name(lastOpenErr));
            continue;
        }

        size_t sent = 0;
        bool writeFailed = false;
        while (sent < body.length() && !runtime::due(millis(), t.deadline)) {
            const int n = esp_http_client_write(client, body.c_str() + sent, body.length() - sent);
            if (n <= 0) {
                writeFailed = true;
                break;
            }
            sent += n;
            vTaskDelay(1);
        }
        if (writeFailed || sent != body.length()) {
            r.error = writeFailed ? "POST HTTP incompleto" : "Tiempo del POST HTTP agotado";
            continue;
        }

        esp_http_client_set_timeout_ms(client, 25000);
        declared = esp_http_client_fetch_headers(client);
        if (declared == -28679 /* -ESP_ERR_HTTP_EAGAIN */) {
            const uint32_t fetchDeadline = millis() + 5000;
            while (declared == -28679 && !runtime::due(millis(), fetchDeadline)) {
                vTaskDelay(pdMS_TO_TICKS(100));
                declared = esp_http_client_fetch_headers(client);
            }
        }
        r.status = esp_http_client_get_status_code(client);
        r.cookie = t.cookie;
        if (declared < 0) {
            const int err = esp_http_client_get_errno(client);
            Serial.printf("[HTTP] Timeout esperando cabeceras de %s (intento %d): status=%d, errno=%d\n",
                          url.c_str(), attempt + 1, r.status, err);
            r.error = "Tiempo de espera HTTPS agotado esperando respuesta";
            continue;
        }
        if (declared > int64_t(t.limit)) {
            r.error = "Respuesta HTTPS demasiado grande";
            return r;
        }

        t.used = 0;
        bool readError = false;
        int zeroReads = 0;
        size_t chunksRead = 0;
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
            if (n < 0) {
                const int err = esp_http_client_get_errno(client);
                if (err == 11 /* EAGAIN */ || err == 115 /* EINPROGRESS */ ||
                    n == -28679 /* -ESP_ERR_HTTP_EAGAIN */) {
                    if (t.used > 2 && (bodyBuffer[t.used - 1] == '}' || bodyBuffer[t.used - 1] == '\n'))
                        break;
                    vTaskDelay(pdMS_TO_TICKS(40));
                    continue;
                }
                Serial.printf("[HTTP] Error leyendo body de %s: n=%d, errno=%d\n", url.c_str(), n, err);
                readError = true;
                break;
            }
            if (n == 0) {
                if (esp_http_client_is_chunked_response(client)) {
                    if (esp_http_client_is_complete_data_received(client))
                        break;
                    if (t.used > 2 && (bodyBuffer[t.used - 1] == '}' || bodyBuffer[t.used - 1] == '\n'))
                        break;
                    if (++zeroReads <= 25) {
                        vTaskDelay(pdMS_TO_TICKS(30));
                        continue;
                    }
                    Serial.printf("[HTTP] Conexión cerrada antes de completar chunked en %s (usados: %u bytes)\n",
                                  url.c_str(), unsigned(t.used));
                    readError = true;
                    break;
                } else if (declared > 0 && int64_t(t.used) < declared) {
                    if (++zeroReads <= 25) {
                        vTaskDelay(pdMS_TO_TICKS(30));
                        continue;
                    }
                    Serial.printf("[HTTP] Conexión cerrada antes de recibir todo el body en %s (%u de %lld bytes)\n",
                                  url.c_str(), unsigned(t.used), declared);
                    readError = true;
                    break;
                } else {
                    break;
                }
            }
            zeroReads = 0;
            t.used += n;
            if (++chunksRead % 4 == 0)
                vTaskDelay(1);
            if (esp_http_client_is_chunked_response(client) &&
                esp_http_client_is_complete_data_received(client))
                break;
        }

        if (readError || t.failed) {
            r.status = -2;
            r.error = "Conexión HTTPS interrumpida o incompleta";
            continue;
        }
        if (esp_http_client_is_chunked_response(client) &&
            !esp_http_client_is_complete_data_received(client) &&
            !(t.used > 2 && (bodyBuffer[t.used - 1] == '}' || bodyBuffer[t.used - 1] == '\n'))) {
            Serial.printf("[HTTP] Respuesta chunked incompleta de %s: %u bytes\n",
                          url.c_str(), unsigned(t.used));
            r.status = -2;
            r.error = "Respuesta HTTPS incompleta";
            continue;
        }
        if (declared > 0 && int64_t(t.used) < declared) {
            Serial.printf("[HTTP] Respuesta truncada de %s: %u de %lld bytes\n",
                          url.c_str(), unsigned(t.used), declared);
            r.status = -2;
            r.error = "Respuesta HTTP truncada";
            continue;
        }
        if (t.used == 0 && r.status == 200) {
            Serial.printf("[HTTP] Respuesta vacía de %s\n", url.c_str());
            r.status = -2;
            r.error = "Respuesta HTTP vacía";
            continue;
        }
        break;
    }

    if (!r.error.isEmpty() && (r.status <= 0)) {
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
            Serial.printf("[HTTP] Error deserializando JSON de %s: %s (usados: %u bytes, declarados: %lld)\n",
                          url.c_str(), error.c_str(), unsigned(t.used), declared);
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
