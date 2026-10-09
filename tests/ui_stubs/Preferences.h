#pragma once
#include "Arduino.h"
#include <map>
#include <vector>
inline std::map<std::string, String> preferencesStrings;
inline std::map<std::string, std::vector<char>> preferencesBytes;
struct Preferences {
    bool begin(const char *, bool) { return true; }
    void end() {}
    String getString(const char *key, const char *fallback) {
        auto i = preferencesStrings.find(key);
        return i == preferencesStrings.end() ? String(fallback) : i->second;
    }
    size_t getBytesLength(const char *key) { return preferencesBytes[key].size(); }
    size_t getBytes(const char *key, void *out, size_t n) {
        auto &v = preferencesBytes[key];
        n = std::min(n, v.size());
        memcpy(out, v.data(), n);
        return n;
    }
    size_t putString(const char *key, const String &s) {
        preferencesStrings[key] = s;
        return s.size();
    }
    size_t putBytes(const char *key, const void *in, size_t n) {
        auto *p = static_cast<const char *>(in);
        preferencesBytes[key] = {p, p + n};
        return n;
    }
};
