#include "ota.hpp"
#include "net.hpp"
#include "runtime.hpp"
#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <esp_app_format.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>

using net::Doc;

namespace {
constexpr const char *kGithubManifestUrl =
    "https://raw.githubusercontent.com/RSanchez996/gluco_waveshare/main/bin/manifest.json";
constexpr const char *kGithubFirmwareUrl =
    "https://raw.githubusercontent.com/RSanchez996/gluco_waveshare/main/bin/gluco_waveshare.bin";

SemaphoreHandle_t otaMutex = nullptr;
OtaStatus currentStatus{};
char localElfSha256[65]{};
std::atomic<uint8_t> pendingCommand{0}; // 0 = none, 1 = check, 2 = install
std::atomic<bool> running{false};

class OtaLock {
  public:
    OtaLock() {
        if (otaMutex)
            xSemaphoreTake(otaMutex, portMAX_DELAY);
    }
    ~OtaLock() {
        if (otaMutex)
            xSemaphoreGive(otaMutex);
    }
};

void setStatus(OtaState state, const char *msg, uint8_t progress = 0, uint32_t dl = 0,
               uint32_t total = 0) {
    OtaLock lock;
    currentStatus.state = state;
    currentStatus.progressPercent = progress;
    currentStatus.downloadedBytes = dl;
    currentStatus.totalBytes = total;
    if (msg)
        strlcpy(currentStatus.message, msg, sizeof(currentStatus.message));
}

void formatHex(const uint8_t *bytes, size_t len, char *out, size_t outSize) {
    if (outSize < len * 2 + 1)
        return;
    for (size_t i = 0; i < len; ++i)
        snprintf(out + i * 2, 3, "%02x", bytes[i]);
    out[len * 2] = 0;
}

struct ManifestInfo {
    String version;
    String elfSha256;
    String buildDate;
    String binSha256;
    String changes;
    uint32_t binSize = 0;
};

ManifestInfo cachedManifest{};
bool hasCachedManifest = false;

constexpr const char *kDefaultChanges =
    "- Cálculo de TIR 24 h desde Diabetes:M en RAM cada 15 min (sin desgaste de Flash)\n"
    "- Actualización OTA desde Ajustes con confirmación previa y resumen de cambios\n"
    "- Lecturas de glucosa constantes cada 2 min y resumen de Ajustes corregido";

// Reads the first 288 bytes of gluco_waveshare.bin over HTTPS if manifest.json
// does not explicitly include elf_sha256.
bool fetchRemoteHeaderDescriptor(String &outVersion, String &outElfSha256, String &outBuildDate) {
    if (xSemaphoreTake(httpMutex, pdMS_TO_TICKS(5000)) != pdTRUE)
        return false;
    struct Guard {
        ~Guard() { xSemaphoreGive(httpMutex); }
    } guard;

    const String userAgent = String("GlucoWaveshare/") + APP_VERSION;
    for (int attempt = 0; attempt < 3; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(attempt == 0 ? 750 : 1500));
        const String url = String(kGithubFirmwareUrl) + "?t=" + String(static_cast<unsigned long>(time(nullptr)));
        esp_http_client_config_t cfg{};
        cfg.url = url.c_str();
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms = 20000;
        cfg.buffer_size = 4096;
        cfg.buffer_size_tx = 2048;
        cfg.addr_type = HTTP_ADDR_TYPE_INET;
        cfg.disable_auto_redirect = false;
        cfg.max_redirection_count = 3;
        cfg.keep_alive_enable = false;

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client)
            continue;
        struct Cleanup {
            esp_http_client_handle_t h;
            ~Cleanup() {
                if (h) {
                    esp_http_client_close(h);
                    esp_http_client_cleanup(h);
                }
            }
        } cleanup{client};

        esp_http_client_set_method(client, HTTP_METHOD_GET);
        esp_http_client_set_header(client, "Range", "bytes=0-287");
        esp_http_client_set_header(client, "Accept-Encoding", "identity");
        esp_http_client_set_header(client, "Cache-Control", "no-cache");
        esp_http_client_set_header(client, "Connection", "close");
        esp_http_client_set_header(client, "User-Agent", userAgent.c_str());

        if (esp_http_client_open(client, 0) != ESP_OK)
            continue;
        if (esp_http_client_fetch_headers(client) < 0)
            continue;
        const int status = esp_http_client_get_status_code(client);
        if (status != 200 && status != 206)
            continue;

        uint8_t header[288]{};
        size_t readTotal = 0;
        int zeroReads = 0;
        const uint32_t deadline = millis() + 12000;
        while (readTotal < sizeof(header) && !runtime::due(millis(), deadline)) {
            const int n = esp_http_client_read(client, reinterpret_cast<char *>(header + readTotal),
                                               sizeof(header) - readTotal);
            if (n < 0)
                break;
            if (n == 0) {
                if (++zeroReads > 20)
                    break;
                vTaskDelay(pdMS_TO_TICKS(25));
                continue;
            }
            zeroReads = 0;
            readTotal += size_t(n);
        }
        if (readTotal < sizeof(header))
            continue;

        esp_app_desc_t desc{};
        memcpy(&desc, header + 32, sizeof(esp_app_desc_t));
        if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD)
            continue;

        char ver[33]{}, dateStr[17]{}, timeStr[17]{}, shaHex[65]{};
        memcpy(ver, desc.version, 32);
        memcpy(dateStr, desc.date, 16);
        memcpy(timeStr, desc.time, 16);
        formatHex(desc.app_elf_sha256, 32, shaHex, sizeof(shaHex));

        outVersion = ver;
        outElfSha256 = shaHex;
        outBuildDate = String(dateStr) + " " + String(timeStr);
        outBuildDate.trim();
        return true;
    }
    return false;
}

bool fetchManifest(ManifestInfo &info, String &error) {
    if (WiFi.status() != WL_CONNECTED) {
        error = "Conecta el Wi-Fi antes de buscar actualizaciones";
        return false;
    }
    const String url = String(kGithubManifestUrl) + "?t=" + String(static_cast<unsigned long>(time(nullptr)));
    Doc d(12288);
    net::Response r = net::request(url, d, "GET", "", {{"Cache-Control", "no-cache"}}, 24 * 1024);
    if (r.status != 200 || !d["images"].is<JsonArray>()) {
        error = r.error.isEmpty() ? ("No se pudo leer manifest.json (HTTP " + String(r.status) + ")")
                                  : r.error;
        return false;
    }

    info.version = d["version"] | "";
    info.elfSha256 = d["elf_sha256"] | "";
    info.buildDate = d["build_date"] | "";
    info.binSize = 0;
    info.binSha256 = "";
    info.changes = "";

    if (d["changes"].is<JsonArray>()) {
        for (JsonVariantConst item : d["changes"].as<JsonArrayConst>()) {
            const char *line = item | "";
            if (!line || !*line)
                continue;
            while (*line == ' ' || *line == '-' || *line == '*')
                ++line;
            if (strncmp(line, "\xE2\x80\xA2", 3) == 0) {
                line += 3;
                while (*line == ' ')
                    ++line;
            }
            if (!*line)
                continue;
            if (!info.changes.isEmpty())
                info.changes += "\n";
            info.changes += "- ";
            info.changes += line;
        }
    } else if (d["changelog"].is<const char *>()) {
        info.changes = d["changelog"] | "";
    }
    info.changes.replace("\xE2\x80\xA2", "-");
    info.changes.replace("\xC2\xAB", "'");
    info.changes.replace("\xC2\xBB", "'");
    if (info.changes.isEmpty()) {
        info.changes = kDefaultChanges;
    }

    for (JsonObjectConst img : d["images"].as<JsonArrayConst>()) {
        const char *file = img["file"] | "";
        if (strcmp(file, "gluco_waveshare.bin") == 0) {
            info.binSize = img["size"] | 0U;
            info.binSha256 = img["sha256"] | "";
            break;
        }
    }

    if (info.binSize < 256 * 1024 || info.binSha256.length() != 64) {
        error = "manifest.json en GitHub no contiene gluco_waveshare.bin válido";
        return false;
    }

    if (info.elfSha256.length() != 64) {
        String hdrVer, hdrSha, hdrDate;
        if (fetchRemoteHeaderDescriptor(hdrVer, hdrSha, hdrDate)) {
            if (!hdrVer.isEmpty())
                info.version = hdrVer;
            info.elfSha256 = hdrSha;
            info.buildDate = hdrDate;
        }
    }

    cachedManifest = info;
    hasCachedManifest = true;

    {
        OtaLock lock;
        strlcpy(currentStatus.remoteVersion, info.version.c_str(), sizeof(currentStatus.remoteVersion));
        String buildTxt = info.buildDate;
        if (info.elfSha256.length() >= 8) {
            if (!buildTxt.isEmpty())
                buildTxt += " ";
            buildTxt += "(" + info.elfSha256.substring(0, 8) + ")";
        }
        strlcpy(currentStatus.remoteBuild, buildTxt.c_str(), sizeof(currentStatus.remoteBuild));
        strlcpy(currentStatus.changes, info.changes.c_str(), sizeof(currentStatus.changes));
    }

    return true;
}

void performCheck() {
    appDiagnosticStage("OTA: comprobando GitHub");
    setStatus(OtaState::Checking, "Buscando actualizaciones en GitHub...");
    Serial.println("[OTA] Comprobando versión en GitHub...");

    ManifestInfo info;
    String error;
    if (!fetchManifest(info, error)) {
        Serial.printf("[OTA] Error al comprobar: %s\n", error.c_str());
        setStatus(OtaState::Failed, error.c_str());
        return;
    }

    const bool sameBuild = !info.elfSha256.isEmpty() &&
                           strcasecmp(info.elfSha256.c_str(), localElfSha256) == 0;
    if (sameBuild) {
        String msg = "No hay actualizaciones pendientes. Ya tienes instalada la versión v" +
                     info.version + " (" + info.elfSha256.substring(0, 8) + ").";
        Serial.printf("[OTA] %s\n", msg.c_str());
        setStatus(OtaState::UpToDate, msg.c_str());
    } else {
        String msg = "¡Nueva actualización disponible (v" + info.version;
        if (!info.buildDate.isEmpty())
            msg += " · " + info.buildDate;
        msg += ")! ¿Quieres instalarla?";
        Serial.printf("[OTA] %s\n", msg.c_str());
        setStatus(OtaState::UpdateAvailable, msg.c_str());
    }
}

void performInstall() {
    appDiagnosticStage("OTA: preparando descarga");
    Serial.println("[OTA] Iniciando actualización OTA desde GitHub...");

    ManifestInfo info;
    String error;
    if (hasCachedManifest && cachedManifest.binSize >= 256 * 1024 &&
        cachedManifest.binSha256.length() == 64) {
        info = cachedManifest;
    } else {
        setStatus(OtaState::Checking, "Verificando manifest.json en GitHub antes de descargar...");
        if (!fetchManifest(info, error)) {
            Serial.printf("[OTA] Error previo a descarga: %s\n", error.c_str());
            setStatus(OtaState::Failed, error.c_str());
            return;
        }
    }

    const esp_partition_t *updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (!updatePartition) {
        setStatus(OtaState::Failed, "No se encontró partición OTA de destino (app0/app1)");
        return;
    }
    if (info.binSize > updatePartition->size) {
        setStatus(OtaState::Failed, "El binario supera el tamaño de la partición OTA");
        return;
    }

    Serial.printf("[OTA] Destino: partición %s (offset 0x%lx, tam %lu bytes). Esperado: %u bytes, sha256=%s\n",
                  updatePartition->label, static_cast<unsigned long>(updatePartition->address),
                  static_cast<unsigned long>(updatePartition->size), unsigned(info.binSize),
                  info.binSha256.c_str());

    if (xSemaphoreTake(httpMutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
        setStatus(OtaState::Failed, "Conexión ocupada; inténtalo de nuevo en unos segundos");
        return;
    }
    struct HttpGuard {
        ~HttpGuard() { xSemaphoreGive(httpMutex); }
    } httpGuard;

    const String userAgent = String("GlucoWaveshare/") + APP_VERSION;
    esp_http_client_handle_t client = nullptr;
    for (int attempt = 0; attempt < 3; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(attempt == 0 ? 750 : 1500));
        const String url = String(kGithubFirmwareUrl) + "?t=" + String(static_cast<unsigned long>(time(nullptr)));
        esp_http_client_config_t cfg{};
        cfg.url = url.c_str();
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms = 30000;
        cfg.buffer_size = 8192;
        cfg.buffer_size_tx = 4096;
        cfg.addr_type = HTTP_ADDR_TYPE_INET;
        cfg.disable_auto_redirect = false;
        cfg.max_redirection_count = 3;
        cfg.keep_alive_enable = false;

        client = esp_http_client_init(&cfg);
        if (!client)
            continue;

        esp_http_client_set_method(client, HTTP_METHOD_GET);
        esp_http_client_set_header(client, "Accept-Encoding", "identity");
        esp_http_client_set_header(client, "Cache-Control", "no-cache");
        esp_http_client_set_header(client, "Connection", "close");
        esp_http_client_set_header(client, "User-Agent", userAgent.c_str());

        setStatus(OtaState::Downloading,
                  attempt == 0 ? "Conectando con GitHub para descargar gluco_waveshare.bin..."
                               : "Reintentando conexión HTTPS con GitHub...",
                  0, 0, info.binSize);

        if (esp_http_client_open(client, 0) != ESP_OK) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            client = nullptr;
            continue;
        }

        const int64_t contentLength = esp_http_client_fetch_headers(client);
        const int httpStatus = esp_http_client_get_status_code(client);
        if (contentLength < 0 || httpStatus != 200) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            client = nullptr;
            continue;
        }
        if (contentLength > 0 && uint32_t(contentLength) != info.binSize) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            client = nullptr;
            char msg[140];
            snprintf(msg, sizeof(msg), "Tamaño HTTP (%lld) no coincide con manifest.json (%u)",
                     static_cast<long long>(contentLength), unsigned(info.binSize));
            setStatus(OtaState::Failed, msg);
            return;
        }
        break;
    }

    if (!client) {
        setStatus(OtaState::Failed, "No se pudo conectar por HTTPS con GitHub tras 3 intentos");
        return;
    }
    struct ClientCleanup {
        esp_http_client_handle_t h;
        ~ClientCleanup() {
            if (h) {
                esp_http_client_close(h);
                esp_http_client_cleanup(h);
            }
        }
    } clientCleanup{client};

    constexpr size_t kChunkSize = 4096;
    auto *chunk = static_cast<uint8_t *>(
        heap_caps_malloc(kChunkSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!chunk) {
        setStatus(OtaState::Failed, "Sin memoria PSRAM para el buffer de descarga OTA");
        return;
    }
    struct ChunkGuard {
        uint8_t *p;
        ~ChunkGuard() { heap_caps_free(p); }
    } chunkGuard{chunk};

    esp_ota_handle_t otaHandle = 0;
    // Sequential writes erase 4 KB sectors on demand rather than blocking for 15s up front.
    esp_err_t err = esp_ota_begin(updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
    if (err != ESP_OK) {
        char msg[140];
        snprintf(msg, sizeof(msg), "No se pudo abrir la partición OTA (%s)", esp_err_to_name(err));
        setStatus(OtaState::Failed, msg);
        return;
    }

    mbedtls_sha256_context shaCtx;
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts(&shaCtx, 0);

    uint32_t downloaded = 0;
    uint8_t lastReportedPct = 255;
    uint32_t lastProgressMs = millis();

    appDiagnosticStage("OTA: descargando y grabando");
    while (downloaded < info.binSize) {
        if (millis() - lastProgressMs > 45000) {
            mbedtls_sha256_free(&shaCtx);
            esp_ota_abort(otaHandle);
            setStatus(OtaState::Failed, "Tiempo agotado durante la descarga OTA");
            return;
        }

        const size_t toRead = std::min<size_t>(kChunkSize, info.binSize - downloaded);
        const int n = esp_http_client_read(client, reinterpret_cast<char *>(chunk), toRead);
        if (n < 0) {
            mbedtls_sha256_free(&shaCtx);
            esp_ota_abort(otaHandle);
            setStatus(OtaState::Failed, "Error de lectura HTTPS durante la descarga OTA");
            return;
        }
        if (n == 0) {
            if (esp_http_client_is_complete_data_received(client))
                break;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        lastProgressMs = millis();

        if (downloaded == 0 && n >= 288) {
            esp_app_desc_t hdrDesc{};
            memcpy(&hdrDesc, chunk + 32, sizeof(esp_app_desc_t));
            if (hdrDesc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
                mbedtls_sha256_free(&shaCtx);
                esp_ota_abort(otaHandle);
                setStatus(OtaState::Failed, "El archivo descargado no es un firmware ESP32-S3 válido");
                return;
            }
        }

        mbedtls_sha256_update(&shaCtx, chunk, size_t(n));
        err = esp_ota_write(otaHandle, chunk, size_t(n));
        if (err != ESP_OK) {
            mbedtls_sha256_free(&shaCtx);
            esp_ota_abort(otaHandle);
            char msg[140];
            snprintf(msg, sizeof(msg), "Error grabando flash OTA (%s)", esp_err_to_name(err));
            setStatus(OtaState::Failed, msg);
            return;
        }

        downloaded += uint32_t(n);
        const uint8_t pct = uint8_t((uint64_t(downloaded) * 100ULL) / uint64_t(info.binSize));
        if (pct != lastReportedPct) {
            lastReportedPct = pct;
            char msg[140];
            snprintf(msg, sizeof(msg), "Descargando e instalando en %s: %u%% (%u / %u KB)",
                     updatePartition->label, unsigned(pct), unsigned(downloaded / 1024),
                     unsigned(info.binSize / 1024));
            setStatus(OtaState::Downloading, msg, pct, downloaded, info.binSize);
            if (pct % 10 == 0)
                Serial.printf("[OTA] %s\n", msg);
        }

        // Cooperative yield so IDLE0, Wi-Fi, and CPU1 RGB LCD DMA run smoothly.
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    uint8_t digest[32]{};
    mbedtls_sha256_finish(&shaCtx, digest);
    mbedtls_sha256_free(&shaCtx);

    if (downloaded != info.binSize) {
        esp_ota_abort(otaHandle);
        char msg[140];
        snprintf(msg, sizeof(msg), "Descarga incompleta (%u de %u bytes)", unsigned(downloaded),
                 unsigned(info.binSize));
        setStatus(OtaState::Failed, msg);
        return;
    }


    char computedSha[65]{};
    formatHex(digest, sizeof(digest), computedSha, sizeof(computedSha));
    if (strcasecmp(computedSha, info.binSha256.c_str()) != 0) {
        esp_ota_abort(otaHandle);
        Serial.printf("[OTA] SHA-256 inválido: esperado=%s recibido=%s\n", info.binSha256.c_str(),
                      computedSha);
        setStatus(OtaState::Failed, "Verificación SHA-256 fallida; partición actual intacta");
        return;
    }

    setStatus(OtaState::Installing, "Verificando imagen y activando partición de arranque...", 100,
              downloaded, info.binSize);

    err = esp_ota_end(otaHandle);
    if (err != ESP_OK) {
        char msg[140];
        snprintf(msg, sizeof(msg), "Validación de imagen OTA falló (%s)", esp_err_to_name(err));
        setStatus(OtaState::Failed, msg);
        return;
    }

    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK) {
        char msg[140];
        snprintf(msg, sizeof(msg), "No se pudo cambiar la partición de arranque (%s)",
                 esp_err_to_name(err));
        setStatus(OtaState::Failed, msg);
        return;
    }

    Serial.printf("[OTA] Actualización completada con éxito en %s. Reiniciando...\n",
                  updatePartition->label);
    setStatus(OtaState::Rebooting, "¡Firmware actualizado correctamente! Reiniciando pantalla...",
              100, downloaded, info.binSize);
    markPlannedRestart();
    vTaskDelay(pdMS_TO_TICKS(1800));
    ESP.restart();
}
} // namespace

void otaInit() {
    if (!otaMutex)
        otaMutex = xSemaphoreCreateMutex();

    esp_ota_mark_app_valid_cancel_rollback();

    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *runningPart = esp_ota_get_running_partition();

    OtaLock lock;
    if (desc) {
        strlcpy(currentStatus.currentVersion, desc->version, sizeof(currentStatus.currentVersion));
        formatHex(desc->app_elf_sha256, 32, localElfSha256, sizeof(localElfSha256));
        char shortSha[9]{};
        memcpy(shortSha, localElfSha256, 8);
        snprintf(currentStatus.currentBuild, sizeof(currentStatus.currentBuild), "%s %s (%s)",
                 desc->date, desc->time, shortSha);
    } else {
        strlcpy(currentStatus.currentVersion, APP_VERSION, sizeof(currentStatus.currentVersion));
        strlcpy(currentStatus.currentBuild, "Compilación local", sizeof(currentStatus.currentBuild));
    }
    strlcpy(currentStatus.activePartition, runningPart ? runningPart->label : "app0",
            sizeof(currentStatus.activePartition));
    strlcpy(currentStatus.message, "Pulsa 'Comprobar actualizaciones' para buscar nuevas versiones.",
            sizeof(currentStatus.message));
    strlcpy(currentStatus.changes, kDefaultChanges, sizeof(currentStatus.changes));
}

bool otaBusy() { return running.load() || pendingCommand.load() != 0; }

bool otaRequestCheck(String &error) {
    if (otaBusy()) {
        error = "Ya hay una operación OTA en curso";
        return false;
    }
    setStatus(OtaState::Checking, "Solicitando comprobación a GitHub...");
    pendingCommand.store(1);
    return true;
}

bool otaRequestInstall(String &error) {
    if (otaBusy()) {
        error = "Ya hay una operación OTA en curso";
        return false;
    }
    setStatus(OtaState::Checking, "Preparando descarga e instalación desde GitHub...");
    pendingCommand.store(2);
    return true;
}

OtaStatus otaGetStatus() {
    OtaLock lock;
    return currentStatus;
}

void otaService() {
    const uint8_t cmd = pendingCommand.exchange(0);
    if (!cmd)
        return;
    running.store(true);
    if (cmd == 1)
        performCheck();
    else if (cmd == 2)
        performInstall();
    running.store(false);
}
