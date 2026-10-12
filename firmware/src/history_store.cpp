#include "history_store.hpp"
#include <Preferences.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

extern SemaphoreHandle_t storageMutex;

namespace {
constexpr size_t kMaxDayPoints = 300;

struct __attribute__((packed)) DayHeader {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint16_t count;
};

struct __attribute__((packed)) CompactPoint {
    uint32_t epoch;
    int16_t glucose;
};

uint32_t hashId(const char *s) {
    uint32_t h = 2166136261u;
    if (!s)
        return h;
    while (*s) {
        h ^= uint8_t(*s++);
        h *= 16777619u;
    }
    return h;
}

void makeKey(const char *patientId, char *out, size_t outSize) {
    snprintf(out, outSize, "h_%08x", (unsigned)hashId(patientId));
}

bool getLocalDate(int64_t epoch, int &year, int &month, int &day) {
    if (epoch < 1700000000)
        return false;
    time_t t = epoch;
    tm local{};
    if (!localtime_r(&t, &local))
        return false;
    year = local.tm_year + 1900;
    month = local.tm_mon + 1;
    day = local.tm_mday;
    return true;
}

void computeSummary(const CompactPoint *points, size_t count, DailySummary &summary) {
    if (!points || !count) {
        summary = DailySummary{};
        return;
    }
    size_t green = 0, low = 0, high = 0, valid = 0;
    int32_t sum = 0;
    int minG = 9999, maxG = -9999;
    for (size_t i = 0; i < count; ++i) {
        const int g = points[i].glucose;
        if (g < 20 || g > 1000)
            continue;
        ++valid;
        sum += g;
        if (g < minG)
            minG = g;
        if (g > maxG)
            maxG = g;
        if (g < 70)
            ++low;
        else if (g <= 180)
            ++green;
        else
            ++high;
    }
    if (!valid) {
        summary = DailySummary{};
        return;
    }
    summary.tirPercent = (float(green) * 100.0f) / float(valid);
    summary.lowPercent = (float(low) * 100.0f) / float(valid);
    summary.highPercent = (float(high) * 100.0f) / float(valid);
    summary.averageGlucose = int(std::lround(double(sum) / double(valid)));
    summary.minGlucose = minG;
    summary.maxGlucose = maxG;
    summary.totalReadings = valid;
    summary.valid = true;
}
} // namespace

void historyStoreInit() {}

void historyStoreLoad(const char *patientId, DailySummary &summary, int64_t now) {
    summary = DailySummary{};
    if (!patientId || !*patientId)
        return;
    int curY = 0, curM = 0, curD = 0;
    const bool hasDate = getLocalDate(now, curY, curM, curD);

    char key[16];
    makeKey(patientId, key, sizeof(key));

    xSemaphoreTake(storageMutex, portMAX_DELAY);
    Preferences p;
    if (!p.begin("glucohist", false)) {
        xSemaphoreGive(storageMutex);
        return;
    }
    if (!p.isKey(key)) {
        p.end();
        xSemaphoreGive(storageMutex);
        return;
    }
    const size_t len = p.getBytesLength(key);
    if (len < sizeof(DayHeader)) {
        p.end();
        xSemaphoreGive(storageMutex);
        return;
    }

    auto *buffer = static_cast<uint8_t *>(malloc(len));
    if (!buffer) {
        p.end();
        xSemaphoreGive(storageMutex);
        return;
    }

    p.getBytes(key, buffer, len);
    p.end();
    xSemaphoreGive(storageMutex);

    DayHeader hdr{};
    memcpy(&hdr, buffer, sizeof(DayHeader));
    if (hdr.count > kMaxDayPoints || sizeof(DayHeader) + hdr.count * sizeof(CompactPoint) > len) {
        free(buffer);
        return;
    }

    if (hasDate && (hdr.year != curY || hdr.month != curM || hdr.day != curD)) {
        free(buffer);
        return;
    }

    const auto *points = reinterpret_cast<const CompactPoint *>(buffer + sizeof(DayHeader));
    computeSummary(points, hdr.count, summary);
    free(buffer);
}

void historyStoreMerge(const char *patientId, const gluco::Point *points, size_t count,
                       const gluco::Point &current, bool currentValid, DailySummary &summary,
                       int64_t now) {
    if (!patientId || !*patientId)
        return;
    int curY = 0, curM = 0, curD = 0;
    if (!getLocalDate(now, curY, curM, curD))
        return;

    char key[16];
    makeKey(patientId, key, sizeof(key));

    CompactPoint stored[kMaxDayPoints];
    size_t storedCount = 0;

    xSemaphoreTake(storageMutex, portMAX_DELAY);
    Preferences p;
    if (p.begin("glucohist", false)) {
        if (p.isKey(key)) {
            const size_t len = p.getBytesLength(key);
            if (len >= sizeof(DayHeader)) {
                auto *raw = static_cast<uint8_t *>(malloc(len));
                if (raw) {
                    p.getBytes(key, raw, len);
                    DayHeader hdr{};
                    memcpy(&hdr, raw, sizeof(DayHeader));
                    if (hdr.year == curY && hdr.month == curM && hdr.day == curD &&
                        hdr.count <= kMaxDayPoints &&
                        sizeof(DayHeader) + hdr.count * sizeof(CompactPoint) <= len) {
                        memcpy(stored, raw + sizeof(DayHeader), hdr.count * sizeof(CompactPoint));
                        storedCount = hdr.count;
                    }
                    free(raw);
                }
            }
        }

        // Merge incoming points
        auto addPoint = [&](int64_t ep, int16_t val) {
            if (ep < 1700000000 || val < 20 || val > 1000)
                return;
            int py = 0, pm = 0, pd = 0;
            if (!getLocalDate(ep, py, pm, pd) || py != curY || pm != curM || pd != curD)
                return;

            const uint32_t uepoch = uint32_t(ep);
            // Check if timestamp already exists (+/- 45s)
            for (size_t i = 0; i < storedCount; ++i) {
                if (std::abs(int32_t(stored[i].epoch - uepoch)) <= 45) {
                    stored[i].glucose = val; // update with fresher/smoothed reading
                    return;
                }
            }
            if (storedCount < kMaxDayPoints) {
                stored[storedCount++] = {uepoch, val};
            }
        };

        if (points) {
            for (size_t i = 0; i < count; ++i)
                addPoint(points[i].epoch, points[i].glucose);
        }
        if (currentValid)
            addPoint(current.epoch, current.glucose);

        // Sort by epoch ascending
        std::sort(stored, stored + storedCount, [](const CompactPoint &a, const CompactPoint &b) {
            return a.epoch < b.epoch;
        });

        // Save back to NVS
        DayHeader hdr{uint16_t(curY), uint8_t(curM), uint8_t(curD), uint16_t(storedCount)};
        const size_t saveBytes = sizeof(DayHeader) + storedCount * sizeof(CompactPoint);
        auto *saveBuf = static_cast<uint8_t *>(malloc(saveBytes));
        if (saveBuf) {
            memcpy(saveBuf, &hdr, sizeof(DayHeader));
            memcpy(saveBuf + sizeof(DayHeader), stored, storedCount * sizeof(CompactPoint));
            p.putBytes(key, saveBuf, saveBytes);
            free(saveBuf);
        }
        p.end();
    }
    xSemaphoreGive(storageMutex);

    computeSummary(stored, storedCount, summary);
}
