#pragma once
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <initializer_list>
namespace net {
struct PsramAllocator {
    void *allocate(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void deallocate(void *p) { heap_caps_free(p); }
    void *reallocate(void *p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
using Doc = BasicJsonDocument<PsramAllocator>;
struct Header { const char *name; String value; };
struct Response { int status = 0; String error; uint32_t retrySeconds = 60; };
Response request(const String &url, Doc &doc, const char *method = "GET", const String &body = "",
                 std::initializer_list<Header> headers = {}, size_t responseLimit = 96 * 1024,
                 JsonVariantConst filter = JsonVariantConst());
String encode(const String &value);
String sha256(const String &value);
template <size_t N> void text(char (&dst)[N], const char *src) {
    strlcpy(dst, src ? src : "", N);
    for (char *p = dst; *p; ++p) if (uint8_t(*p) < 32) *p = ' ';
}
}
