#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#ifdef __cplusplus
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
class String : public std::string {
  public:
    using std::string::string;
    String() = default;
    String(const std::string &s) : std::string(s) {}
    String(int n) : std::string(std::to_string(n)) {}
    String(unsigned n) : std::string(std::to_string(n)) {}
    bool isEmpty() const { return empty(); }
    bool concat(const char *s) {
        append(s);
        return true;
    }
    bool concat(const char *s, size_t n) {
        append(s, n);
        return true;
    }
    int indexOf(const std::string &s) const {
        auto i = find(s);
        return i == npos ? -1 : int(i);
    }
    bool endsWith(const std::string &s) const {
        return size() >= s.size() && compare(size() - s.size(), s.size(), s) == 0;
    }
    int indexOf(char c) const {
        auto i = find(c);
        return i == npos ? -1 : int(i);
    }
    void remove(size_t n) { erase(n); }
    void trim() {
        auto a = find_first_not_of(" \t\r\n"), b = find_last_not_of(" \t\r\n");
        if (a == npos)
            clear();
        else
            *this = substr(a, b - a + 1);
    }
};
class SerialStub {
  public:
    void println(const char *s) { puts(s); }
    void flush() {}
    template <typename... A> void printf(const char *s, A... a) { std::printf(s, a...); }
};
inline SerialStub Serial;
inline size_t gluco_test_strlcpy(char *d, const char *s, size_t n) {
    size_t len = strlen(s);
    if (n) {
        size_t c = len < n - 1 ? len : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return len;
}
#define strlcpy gluco_test_strlcpy
extern "C" {
#endif
uint32_t millis(void);
void delay(uint32_t ms);
#ifdef __cplusplus
}
#endif
