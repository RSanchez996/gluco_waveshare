// Production providers and ArduinoJson with synthetic API replies; no live account.
#include "providers.hpp"
#include <Preferences.h>
#include <cassert>
#include <deque>
#include <iostream>
#include <sstream>
struct Reply {
    int status;
    String json;
};
std::deque<Reply> replies;
std::vector<String> urls;
Config cfg;
SemaphoreHandle_t stateMutex = nullptr, storageMutex = nullptr;
Config configSnapshot() { return cfg; }
void appDiagnosticStage(const char *) {}
namespace net {
String encode(const String &s) { return s; }
String sha256(const String &s) { return "mock-digest:" + s; }
Response request(const String &url, Doc &doc, const char *method, const String &,
                 std::initializer_list<Header> headers, size_t, JsonVariantConst filter) {
    assert(!replies.empty());
    urls.push_back(url);
    bool auth = false, account = false;
    for (const auto &h : headers) {
        if (std::string(h.name) == "Authorization")
            auth = h.value == "Bearer test-token";
        if (std::string(h.name) == "Account-Id")
            account = h.value == "mock-digest:account-id";
    }
    if (std::string(method) == "GET")
        assert(auth && account);
    const auto reply = replies.front();
    replies.pop_front();
    doc.clear();
    if (!reply.json.empty())
        assert(!deserializeJson(doc, reply.json.c_str(), DeserializationOption::Filter(filter)));
    Response r;
    r.status = reply.status;
    if (r.status != 200)
        r.error = "synthetic HTTP error";
    return r;
}
} // namespace net
String auth() {
    return R"({"status":0,"data":{"authTicket":{"token":"test-token"},"user":{"id":"account-id"},"ignoredProfile":"discard"}})";
}
String stamp(time_t t) {
    char text[40];
    tm utc{};
    gmtime_r(&t, &utc);
    strftime(text, sizeof(text), "%m/%d/%Y %I:%M:%S %p", &utc);
    return text;
}
String point(time_t t, int glucose) {
    return "{\"FactoryTimestamp\":\"" + stamp(t) + "\",\"ValueInMgPerDl\":" + String(glucose) +
           ",\"Value\":7.0,\"GlucoseUnits\":2,\"TrendArrow\":3}";
}
String graph() {
    auto now = time(nullptr);
    return "{\"status\":0,\"data\":{\"graphData\":[" + point(now - 600, 110) + "," +
           point(now - 300, 120) + "," + point(now - 300, 121) +
           "],\"connection\":{\"glucoseMeasurement\":" + point(now, 126) +
           "},\"ignoredSensorMetadata\":[1,2,3]}}";
}
void start(int n) {
    assert(replies.empty());
    cfg = {};
    cfg.libreUser = "case-" + String(n);
    cfg.librePass = "password";
    cfg.patientId = "patient-id";
    urls.clear();
}
int main() {
    start(1);
    LibreClient a;
    AppState s;
    replies = {{200, auth()}, {200, graph()}};
    assert(a.read(s) == 120 && s.currentValid && s.current.glucose == 126 && s.pointCount == 2);
    assert(s.points[1].glucose == 121 && !s.glucoseError[0] && String(s.direction) == "Flat" &&
           urls.size() == 2);
    auto epoch = s.current.epoch;
    replies = {{503, ""}};
    a.read(s);
    assert(s.current.epoch == epoch && s.pointCount == 2 && s.glucoseError[0]);
    replies = {
        {200,
         R"({"status":0,"data":{"graphData":[],"connection":{"glucoseMeasurement":{"Value":7,"GlucoseUnits":2,"FactoryTimestamp":"bad"}}}})"}};
    a.read(s);
    assert(s.current.epoch == epoch && s.glucoseError[0]);
    replies = {{401, ""}, {200, auth()}, {200, graph()}};
    a.read(s);
    assert(replies.empty() && s.currentValid && !s.glucoseError[0]);
    start(2);
    LibreClient b;
    AppState rejected;
    replies = {{200, R"({"status":2})"}};
    assert(b.read(rejected) == 900 && !rejected.currentValid && rejected.glucoseError[0]);
    start(3);
    LibreClient c;
    AppState redirected;
    replies = {{200, R"({"status":0,"data":{"redirect":true,"region":"eu2"}})"},
               {200, auth()},
               {200, graph()}};
    c.read(redirected);
    assert(redirected.currentValid && urls[1].find("api-eu2.") != String::npos &&
           urls[2].find("api-eu2.") != String::npos);
    start(4);
    LibreClient d;
    AppState limited;
    replies = {{200, auth()}, {401, ""}, {200, auth()}, {401, ""}};
    d.read(limited);
    assert(replies.empty() && !limited.currentValid && urls.size() == 4 && limited.glucoseError[0]);
    start(5);
    LibreClient e;
    AppState users;
    replies = {
        {200, auth()},
        {200,
         R"({"status":0,"data":[{"patientId":"p1","firstName":"Nombre","lastName":"Apellido","unneeded":123}]})"}};
    e.listConnections(users);
    assert(users.connectionCount == 1 && String(users.connections[0].id) == "p1");
    AppState cached;
    connectionCacheLoad(cached);
    assert(cached.connectionCount == 1);
    memset(preferencesBytes["items"].data(), 'X', preferencesBytes["items"].size());
    connectionCacheLoad(cached);
    assert(cached.connectionCount == 0);
    std::cout << "OK: provider filters, mg/dL, deduplication, failures preserve history, token "
                 "renewal bounded, region redirect, cache validation\n";
}
