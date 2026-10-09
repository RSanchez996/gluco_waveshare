#pragma once
#include "core.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
namespace gluco {
inline bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
inline int monthDays(int y, int m) {
    static const int d[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m >= 1 && m <= 12 ? d[m - 1] + (m == 2 && leap(y)) : 0;
}
inline int64_t utc(int y, int m, int d, int h, int mi, int s) {
    if (y < 1970 || y > 2100 || d < 1 || d > monthDays(y, m) || h < 0 || h > 23 || mi < 0 ||
        mi > 59 || s < 0 || s > 59)
        return 0;
    int yy = y - (m <= 2), era = yy / 400;
    unsigned yo = unsigned(yy - era * 400),
             doy = (153 * unsigned(m + (m > 2 ? -3 : 9)) + 2) / 5 + unsigned(d) - 1,
             doe = yo * 365 + yo / 4 - yo / 100 + doy;
    return (int64_t(era) * 146097 + doe - 719468) * 86400 + h * 3600 + mi * 60 + s;
}
inline int64_t factoryEpoch(const char *v) {
    if (!v || strlen(v) > 60)
        return 0;
    int y = 0, m = 0, d = 0, h = 0, mi = 0, s = 0, n = 0;
    if (sscanf(v, "%d/%d/%d %d:%d:%d%n", &m, &d, &y, &h, &mi, &s, &n) == 6) {
        std::string rest(v + n);
        while (!rest.empty() && rest.front() == ' ')
            rest.erase(0, 1);
        while (!rest.empty() && rest.back() == ' ')
            rest.pop_back();
        if (!rest.empty()) {
            if (h < 1 || h > 12 || (rest != "AM" && rest != "PM"))
                return 0;
            h = h % 12 + (rest == "PM" ? 12 : 0);
        }
        return utc(y, m, d, h, mi, s);
    }
    if (sscanf(v, "%d-%d-%dT%d:%d:%d%n", &y, &m, &d, &h, &mi, &s, &n) != 6)
        return 0;
    const char *p = v + n;
    if (*p == '.') {
        ++p;
        if (!isdigit((unsigned char)*p))
            return 0;
        while (isdigit((unsigned char)*p))
            ++p;
    }
    const int64_t t = utc(y, m, d, h, mi, s);
    if (!t)
        return 0;
    if (!strcmp(p, "Z"))
        return t;
    if ((*p == '+' || *p == '-') && strlen(p) == 6 && p[3] == ':') {
        int oh = 0, om = 0, k = 0;
        if (sscanf(p + 1, "%2d:%2d%n", &oh, &om, &k) != 2 || k != 5 || oh > 23 || om > 59)
            return 0;
        return t - (*p == '+' ? 1 : -1) * (oh * 3600 + om * 60);
    }
    return 0;
}
inline int libreMgdl(double mgdl, bool hasMgdl, double value, int units) {
    double g = hasMgdl ? mgdl : (units == 1 ? value : NAN);
    return range(g) == Range::Invalid ? -1 : int(std::lround(g));
}
inline const char *trend(int n) {
    static const char *t[]{"NONE",          "DoubleUp",      "SingleUp",   "FortyFiveUp",
                           "Flat",          "FortyFiveDown", "SingleDown", "DoubleDown",
                           "NotComputable", "RateOutOfRange"};
    return n >= 0 && n < 10 ? t[n] : t[0];
}
inline const char *libreTrend(int n) {
    static const int t[]{0, 6, 5, 4, 3, 2};
    return trend(n >= 0 && n < 6 ? t[n] : 0);
}
} // namespace gluco
