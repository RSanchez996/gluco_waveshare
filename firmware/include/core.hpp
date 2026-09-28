#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace gluco {
constexpr int64_t kHistorySeconds = 8 * 60 * 60;
constexpr int64_t kSampleSeconds = 5 * 60;
constexpr int64_t kStaleSeconds = 10 * 60;
constexpr int64_t kMaxGapSeconds = 12 * 60;
constexpr int64_t kGraphGapSeconds = 30 * 60;
constexpr size_t kMaxPoints = 120;
enum class Range : uint8_t { Invalid, LowRed, Green, HighYellow, VeryHighOrange };
inline Range range(double value) {
    if (!std::isfinite(value) || value < 20 || value > 1000) return Range::Invalid;
    if (value < 70) return Range::LowRed;
    if (value <= 180) return Range::Green;
    if (value <= 240) return Range::HighYellow;
    return Range::VeryHighOrange;
}
struct Point { int64_t epoch = 0; int16_t glucose = 0; };
inline bool validEpoch(int64_t t, int64_t now) {
    return now >= 1700000000 && t >= now - kHistorySeconds && t <= now + 60;
}
inline bool stale(int64_t t, int64_t now) {
    return t <= 0 || now < 1700000000 || t > now + 60 || now - t > kStaleSeconds;
}
inline bool connect(const Point &a, const Point &b) {
    return b.epoch > a.epoch && b.epoch - a.epoch <= kMaxGapSeconds;
}
inline bool graphConnect(const Point &a, const Point &b) {
    // El histórico compartido puede tener puntos cada 15 min; no unir
    // huecos mayores para no dibujar mediciones que nunca llegaron.
    return b.epoch > a.epoch && b.epoch - a.epoch <= kGraphGapSeconds;
}
inline size_t normalize(Point *p, size_t count, int64_t now) {
    std::sort(p, p + count, [](const Point &a, const Point &b) { return a.epoch < b.epoch; });
    size_t out = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!validEpoch(p[i].epoch, now) || range(p[i].glucose) == Range::Invalid) continue;
        if (out && p[i].epoch == p[out - 1].epoch) p[out - 1] = p[i];
        else p[out++] = p[i];
    }
    return out;
}
inline double graphX(int64_t epoch, int64_t now, int width) {
    return double(epoch - (now - kHistorySeconds)) * width / kHistorySeconds;
}
inline bool delta(const Point *p, size_t n, int &difference, int &minutes) {
    if (n < 2 || !connect(p[n - 2], p[n - 1])) return false;
    difference = p[n - 1].glucose - p[n - 2].glucose;
    minutes = int((p[n - 1].epoch - p[n - 2].epoch + 30) / 60);
    return minutes > 0;
}
}
