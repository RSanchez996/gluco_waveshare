#include "net.hpp"
#include "app.hpp"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <mbedtls/sha256.h>
#include <algorithm>
extern const uint8_t caStart[] asm("_binary_certs_x509_crt_bundle_start");
extern const uint8_t caEnd[] asm("_binary_certs_x509_crt_bundle_end");
namespace net {
namespace {
constexpr size_t kMinimumLimit = 4 * 1024;
constexpr size_t kMaximumLimit = 768 * 1024;
constexpr time_t kMinimumTlsTime = 1704067200; // 2024-01-01 UTC
class BufferStream : public Stream {
public:
    explicit BufferStream(size_t requested) : limit(requested), capacity(std::min(requested + 1, size_t(16 * 1024))),
        data(static_cast<char *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))), deadline(millis()+30000) {
        if(data)data[0]=0;
    }
    const size_t limit;
    size_t capacity;
    char *data;
    const uint32_t deadline;
    size_t used = 0, bytesSinceSleep = 0; bool exceeded = false, timedOut = false, outOfMemory = false;
    ~BufferStream() { heap_caps_free(data); }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *p, size_t n) override {
        if (int32_t(millis() - deadline) >= 0) { timedOut = true; return 0; }
        if (!data) { outOfMemory = true; return 0; }
        if (used > limit || n > limit - used) { exceeded = true; return 0; }
        if (used + n + 1 > capacity) {
            size_t next = capacity;
            while (next < used + n + 1) next = std::min(limit + 1, next * 2);
            char *expanded = static_cast<char *>(heap_caps_realloc(data, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!expanded) { outOfMemory = true; return 0; }
            data = expanded; capacity = next;
        }
        memcpy(data + used, p, n); used += n; data[used] = 0;
        bytesSinceSleep += n;
        if (bytesSinceSleep >= 4 * 1024) {
            bytesSinceSleep = 0;
            // vTaskDelay(1), no delay(0): deja ejecutar IDLE0 y el controlador
            // Wi-Fi aun si el servidor entrega muchos fragmentos seguidos.
            vTaskDelay(1);
        }
        return n;
    }
    int available() override { return 0; } int read() override { return -1; }
    int peek() override { return -1; } void flush() override {}
};
bool safe(const String &s) { for (size_t i=0;i<s.length();++i) if ((uint8_t)s[i] < 32 || s[i] == 127) return false; return true; }
const char *timezoneRule() {
    if (config.timezone == "Atlantic/Canary") return "WET0WEST,M3.5.0/1,M10.5.0";
    if (config.timezone == "UTC") return "UTC0";
    return "CET-1CEST,M3.5.0,M10.5.0/3";
}
bool ensureTlsTime(String &error) {
    if (time(nullptr) >= kMinimumTlsTime) return true;
    Serial.println("[HTTPS] Esperando sincronización NTP antes de validar certificados...");
    configTzTime(timezoneRule(), "pool.ntp.org", "time.cloudflare.com", "time.google.com");
    const uint32_t deadline = millis() + 25000;
    while (time(nullptr) < kMinimumTlsTime && int32_t(millis() - deadline) < 0) delay(250);
    const time_t now = time(nullptr);
    if (now < kMinimumTlsTime) {
        error = "No se pudo sincronizar la hora por NTP; HTTPS no puede validar certificados";
        Serial.printf("[HTTPS] NTP fallo; epoch=%lld\n", static_cast<long long>(now));
        return false;
    }
    Serial.printf("[HTTPS] Hora sincronizada; epoch=%lld\n", static_cast<long long>(now));
    return true;
}
}
String encode(const String &s) {
    String out; out.reserve(s.length()*3); const char hex[]="0123456789ABCDEF";
    for (size_t i=0;i<s.length();++i) { const uint8_t c=s[i];
        if (isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') out+=char(c); else { out+='%';out+=hex[c>>4];out+=hex[c&15]; }
    } return out;
}
String sha256(const String &s) {
    uint8_t digest[32]; mbedtls_sha256(reinterpret_cast<const unsigned char *>(s.c_str()), s.length(), digest, 0);
    char value[65]; for (int i=0;i<32;++i) snprintf(value+i*2,3,"%02x",digest[i]); return String(value);
}
Response request(const String &url, Doc &doc, const char *method, const String &body,
                 std::initializer_list<Header> headers, size_t responseLimit, JsonVariantConst filter) {
    Response r; doc.clear();
    if (httpMutex) xSemaphoreTake(httpMutex, portMAX_DELAY);
    struct Unlock { ~Unlock(){ if(httpMutex)xSemaphoreGive(httpMutex); } } unlock;
    if (!url.startsWith("https://") || !safe(url)) { r.error="URL HTTPS no válida"; return r; }
    responseLimit=std::min(kMaximumLimit,std::max(kMinimumLimit,responseLimit));
    if (WiFi.status() != WL_CONNECTED) { r.error="Wi-Fi sin conexión a Internet"; return r; }
    if (!ensureTlsTime(r.error)) return r;
    const int slash = url.indexOf('/', 8);
    const String host = slash < 0 ? url.substring(8) : url.substring(8, slash);
    Serial.printf("[HTTPS] %s %s | RSSI %d | heap %u | PSRAM %u\n",
                  method, host.c_str(), WiFi.RSSI(), ESP.getFreeHeap(), ESP.getFreePsram());
    const bool libreHost = host.endsWith(".libreview.io");
    const uint16_t readTimeoutMs = libreHost ? 30000 : 20000;
    WiFiClientSecure client; client.setCACertBundle(caStart,size_t(caEnd-caStart)); client.setHandshakeTimeout(20); client.setTimeout(readTimeoutMs);
    HTTPClient http;
    if (!http.begin(client,url)) { r.error="No se pudo abrir la URL HTTPS"; return r; }
    http.setConnectTimeout(12000); http.setTimeout(readTimeoutMs); http.setReuse(false);
    String userAgent("GlucoWaveshare/");userAgent+=APP_VERSION;http.setUserAgent(userAgent);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char *keys[]{"Retry-After"}; http.collectHeaders(keys,1);
    http.addHeader("Accept","application/json"); http.addHeader("Accept-Encoding","identity");
    if (strcmp(method,"GET")) http.addHeader("Content-Type","application/json");
    for (const auto &h:headers) {
        if(!safe(h.value)){r.error="Cabecera no válida";http.end();return r;}
        if(String(h.name).equalsIgnoreCase("User-Agent")) http.setUserAgent(h.value);
        else http.addHeader(h.name,h.value);
    }
    const uint32_t requestStarted = millis();
    r.status=http.sendRequest(method,body);
    if(r.status<=0){
        const String detail=HTTPClient::errorToString(r.status);
        r.error="Conexión HTTPS con "+host+" falló ("+String(r.status)+": "+detail+")";
        if (r.status == HTTPC_ERROR_READ_TIMEOUT && libreHost) {
            // Solo metadatos locales: nunca registrar ni publicar contraseñas o tokens.
            const unsigned internal = unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
            const unsigned largest = unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
            r.error += " [" + String(millis() - requestStarted) + " ms; RSSI " +
                       String(WiFi.RSSI()) + " dBm; RAM " + String(internal) +
                       " KB; bloque " + String(largest) + " KB]";
        }
        Serial.printf("[HTTPS] Fallo %s: %d %s | epoch=%lld | heap=%u | psram=%u\n",host.c_str(),r.status,detail.c_str(),static_cast<long long>(time(nullptr)),ESP.getFreeHeap(),ESP.getFreePsram());
        http.end();return r;
    }
    Serial.printf("[HTTPS] %s %s -> HTTP %d | tamaño anunciado %d\n",method,host.c_str(),r.status,http.getSize());
    if(r.status==429){r.retrySeconds=std::min(3600L,std::max(300L,http.header("Retry-After").toInt()));r.error="Límite temporal del proveedor";http.end();return r;}
    if(r.status>=300&&r.status<400){r.error="Redirección no permitida";http.end();return r;}
    if(http.getSize()>int(responseLimit)){r.status=-2;r.error="Respuesta demasiado grande";http.end();return r;}
    BufferStream buffer(responseLimit);if(!buffer.data){r.status=-2;r.error="Memoria HTTP insuficiente";http.end();return r;}
    const int copied=http.writeToStream(&buffer);http.end();
    if(copied<0||buffer.exceeded||buffer.timedOut||buffer.outOfMemory){
        Serial.printf("[HTTPS] Cuerpo incompleto: copiados %d, recibidos %u, límite %u, timeout %d, overflow %d\n",
                      copied,unsigned(buffer.used),unsigned(responseLimit),buffer.timedOut,buffer.exceeded);
        r.status=-2;
        r.error=buffer.timedOut?"Tiempo de lectura HTTPS agotado":
                buffer.outOfMemory?"Memoria HTTP insuficiente":
                buffer.exceeded?"Respuesta HTTPS demasiado grande":"Respuesta HTTPS incompleta";
        return r;
    }
    if(buffer.used){
        if(url.indexOf("/graph")>=0)appDiagnosticStage("Libre: analizando JSON");
        DeserializationError e = filter.isNull()
            ? deserializeJson(doc,buffer.data,buffer.used)
            : deserializeJson(doc,buffer.data,buffer.used,DeserializationOption::Filter(filter));
        if(e&&r.status>=200&&r.status<300){
            r.status=-3;r.error="JSON no reconocido: "+String(e.c_str());
            Serial.printf("[HTTPS] JSON inválido (%s), %u bytes recibidos\n",e.c_str(),unsigned(buffer.used));
        }
    }
    if(r.status==430){
        r.retrySeconds=15*60;
        r.error="LibreLinkUp rechazó temporalmente la solicitud (HTTP 430); nuevo intento en 15 min";
    }else if(r.status==403&&host.endsWith(".libreview.io")){
        r.retrySeconds=15*60;
        r.error="LibreLinkUp denegó el acceso (HTTP 403). Verifica región y cuenta; espera 15 min antes de reintentar";
    }else if(r.status>=400){
        const char *message=doc["message"]|"";
        r.error="Proveedor HTTP "+String(r.status);
        if(*message)r.error+=" - "+String(message);
    }
    return r;
}
}
