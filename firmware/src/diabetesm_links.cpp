#include "diabetesm_links.hpp"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstring>

extern SemaphoreHandle_t storageMutex;

namespace {
constexpr size_t kMaxLinks = MAX_CONNECTIONS;

void loadLinksDoc(ArduinoJson::JsonDocument &doc) {
    xSemaphoreTake(storageMutex, portMAX_DELAY);
    Preferences p;
    if (p.begin("glucodm", false)) {
        String json = p.getString("links", "[]");
        p.end();
        deserializeJson(doc, json);
    }
    xSemaphoreGive(storageMutex);
    if (!doc.is<JsonArray>())
        doc.to<JsonArray>();
}

void saveLinksDoc(const ArduinoJson::JsonDocument &doc) {
    String json;
    serializeJson(doc, json);
    xSemaphoreTake(storageMutex, portMAX_DELAY);
    Preferences p;
    if (p.begin("glucodm", false)) {
        p.putString("links", json);
        p.end();
    }
    xSemaphoreGive(storageMutex);
}
} // namespace

void dmLinksInit() {}

bool dmLinkGet(const char *patientId, DiabetesMLink &out) {
    if (!patientId || !*patientId)
        return false;
    StaticJsonDocument<4096> doc;
    loadLinksDoc(doc);
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        const char *id = obj["id"] | "";
        if (strcmp(id, patientId) == 0) {
            strlcpy(out.patientId, id, sizeof(out.patientId));
            strlcpy(out.patientName, obj["name"] | "", sizeof(out.patientName));
            strlcpy(out.user, obj["user"] | "", sizeof(out.user));
            strlcpy(out.pass, obj["pass"] | "", sizeof(out.pass));
            out.enabled = obj["enabled"] | false;
            out.syncFiltered = obj["syncFiltered"] | false;
            return true;
        }
    }
    return false;
}

bool dmLinkSave(const DiabetesMLink &link) {
    if (!link.patientId[0])
        return false;
    StaticJsonDocument<4096> doc;
    loadLinksDoc(doc);
    JsonArray arr = doc.as<JsonArray>();
    bool found = false;
    for (JsonObject obj : arr) {
        if (strcmp(obj["id"] | "", link.patientId) == 0) {
            if (link.patientName[0])
                obj["name"] = link.patientName;
            obj["user"] = link.user;
            if (link.pass[0])
                obj["pass"] = link.pass;
            obj["enabled"] = link.enabled;
            obj["syncFiltered"] = link.syncFiltered;
            found = true;
            break;
        }
    }
    if (!found) {
        if (arr.size() >= kMaxLinks)
            return false;
        JsonObject obj = arr.createNestedObject();
        obj["id"] = link.patientId;
        obj["name"] = link.patientName;
        obj["user"] = link.user;
        obj["pass"] = link.pass;
        obj["enabled"] = link.enabled;
        obj["syncFiltered"] = link.syncFiltered;
    }
    saveLinksDoc(doc);
    return true;
}

size_t dmLinkList(DiabetesMLink *out, size_t maxCount) {
    if (!out || !maxCount)
        return 0;
    StaticJsonDocument<4096> doc;
    loadLinksDoc(doc);
    JsonArray arr = doc.as<JsonArray>();
    size_t count = 0;
    for (JsonObject obj : arr) {
        if (count >= maxCount)
            break;
        strlcpy(out[count].patientId, obj["id"] | "", sizeof(out[count].patientId));
        strlcpy(out[count].patientName, obj["name"] | "", sizeof(out[count].patientName));
        strlcpy(out[count].user, obj["user"] | "", sizeof(out[count].user));
        strlcpy(out[count].pass, obj["pass"] | "", sizeof(out[count].pass));
        out[count].enabled = obj["enabled"] | false;
        out[count].syncFiltered = obj["syncFiltered"] | false;
        ++count;
    }
    return count;
}

bool dmLinkApply(const char *patientId, Config &config) {
    if (!patientId || !*patientId) {
        config.diabetesmUser = "";
        config.diabetesmPass = "";
        config.diabetesmEnabled = false;
        config.diabetesmSyncFiltered = false;
        return false;
    }
    DiabetesMLink link{};
    if (dmLinkGet(patientId, link)) {
        config.diabetesmUser = link.user;
        config.diabetesmPass = link.pass;
        config.diabetesmEnabled = link.enabled;
        config.diabetesmSyncFiltered = link.syncFiltered;
        return true;
    }
    config.diabetesmUser = "";
    config.diabetesmPass = "";
    config.diabetesmEnabled = false;
    config.diabetesmSyncFiltered = false;
    return false;
}

bool dmLinkHasAccount(const char *patientId, char *outUser, size_t maxLen) {
    DiabetesMLink link{};
    if (dmLinkGet(patientId, link) && link.user[0]) {
        if (outUser && maxLen > 0)
            strlcpy(outUser, link.user, maxLen);
        return true;
    }
    return false;
}
