#include "history_store.hpp"
#include <Preferences.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

extern SemaphoreHandle_t storageMutex;

namespace {
constexpr size_t kMaxDayPoints = 300;
constexpr uint32_t kBucketSeconds = 300; // 5-minute buckets -> max 288 buckets in 24h
constexpr uint32_t kWindowSeconds = 24 * 60 * 60;
constexpr uint32_t kMaxSegmentSeconds = 20 * 60; // cap weight of a single point across gaps
constexpr int64_t kNvsSaveIntervalSeconds = 60 * 60; // save to NVS at most once per hour

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

// In-memory cache of the active patient's 24h points to avoid reading/writing NVS every 2 minutes
char ramKey[16]{};
CompactPoint ramPoints[kMaxDayPoints]{};
size_t ramCount = 0;
bool ramLoaded = false;
int64_t lastNvsSaveEpoch = 0;

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

// Compact and deduplicate an arbitrary list of points into 5-minute buckets within [now - 24h, now + 60]
size_t compactPoints(const CompactPoint *in, size_t inCount, CompactPoint *out, size_t maxOut,
                     int64_t now, bool &modified) {
    size_t outCount = 0;
    const uint32_t cutoff = (now >= 1700000000) ? uint32_t(now - kWindowSeconds) : 0;
    const uint32_t maxEpoch = (now >= 1700000000) ? uint32_t(now + 60) : UINT32_MAX;

    for (size_t i = 0; i < inCount; ++i) {
        const uint32_t ep = in[i].epoch;
        const int16_t g = in[i].glucose;
        if (ep < 1700000000u || ep < cutoff || ep > maxEpoch || g < 20 || g > 1000) {
            modified = true;
            continue;
        }
        const uint32_t bucket = ep / kBucketSeconds;
        bool merged = false;
        for (size_t j = 0; j < outCount; ++j) {
            if (out[j].epoch / kBucketSeconds == bucket) {
                if (ep >= out[j].epoch) {
                    if (out[j].epoch != ep || out[j].glucose != g)
                        modified = true;
                    out[j] = {ep, g};
                } else {
                    modified = true;
                }
                merged = true;
                break;
            }
        }
        if (!merged) {
            if (outCount < maxOut) {
                out[outCount++] = {ep, g};
            } else {
                size_t oldest = 0;
                for (size_t j = 1; j < outCount; ++j) {
                    if (out[j].epoch < out[oldest].epoch)
                        oldest = j;
                }
                if (ep > out[oldest].epoch) {
                    out[oldest] = {ep, g};
                    modified = true;
                }
            }
        }
    }
    std::sort(out, out + outCount, [](const CompactPoint &a, const CompactPoint &b) {
        return a.epoch < b.epoch;
    });
    if (outCount != inCount)
        modified = true;
    return outCount;
}

void computeSummary(const CompactPoint *points, size_t count, DailySummary &summary) {
    if (!points || !count) {
        summary = DailySummary{};
        return;
    }

    CompactPoint validPts[kMaxDayPoints];
    size_t valid = 0;
    int minG = 9999, maxG = -9999;

    for (size_t i = 0; i < count && valid < kMaxDayPoints; ++i) {
        const int g = points[i].glucose;
        if (g < 20 || g > 1000)
            continue;
        validPts[valid++] = points[i];
        if (g < minG)
            minG = g;
        if (g > maxG)
            maxG = g;
    }

    if (!valid) {
        summary = DailySummary{};
        return;
    }

    // Time-weighted integration: each sample i represents its surrounding time interval
    double lowSec = 0.0, greenSec = 0.0, highSec = 0.0, totalSec = 0.0, weightedSum = 0.0;
    for (size_t i = 0; i < valid; ++i) {
        double dtPrev = double(kBucketSeconds);
        if (i > 0 && validPts[i].epoch > validPts[i - 1].epoch) {
            dtPrev = std::min<double>(double(validPts[i].epoch - validPts[i - 1].epoch),
                                      double(kMaxSegmentSeconds));
        }
        double dtNext = double(kBucketSeconds);
        if (i + 1 < valid && validPts[i + 1].epoch > validPts[i].epoch) {
            dtNext = std::min<double>(double(validPts[i + 1].epoch - validPts[i].epoch),
                                      double(kMaxSegmentSeconds));
        }
        const double weight = std::max(60.0, 0.5 * (dtPrev + dtNext));
        const int g = validPts[i].glucose;

        totalSec += weight;
        weightedSum += double(g) * weight;
        if (g < 70)
            lowSec += weight;
        else if (g <= 180)
            greenSec += weight;
        else
            highSec += weight;
    }

    summary.tirPercent = float((greenSec * 100.0) / totalSec);
    summary.lowPercent = float((lowSec * 100.0) / totalSec);
    summary.highPercent = float((highSec * 100.0) / totalSec);
    summary.averageGlucose = int(std::lround(weightedSum / totalSec));
    summary.minGlucose = minG;
    summary.maxGlucose = maxG;
    summary.totalReadings = valid;
    summary.valid = true;
}

void ensureRamLoaded(const char *key, int64_t now) {
    if (ramLoaded && strcmp(ramKey, key) == 0) {
        if (now >= 1700000000) {
            CompactPoint tmp[kMaxDayPoints];
            bool mod = false;
            ramCount = compactPoints(ramPoints, ramCount, tmp, kMaxDayPoints, now, mod);
            memcpy(ramPoints, tmp, ramCount * sizeof(CompactPoint));
        }
        return;
    }

    strlcpy(ramKey, key, sizeof(ramKey));
    ramCount = 0;
    ramLoaded = true;
    lastNvsSaveEpoch = 0;

    Preferences p;
    if (!p.begin("glucohist", true))
        return;
    if (!p.isKey(key)) {
        p.end();
        return;
    }
    const size_t len = p.getBytesLength(key);
    if (len < sizeof(DayHeader)) {
        p.end();
        return;
    }
    auto *buffer = static_cast<uint8_t *>(malloc(len));
    if (!buffer) {
        p.end();
        return;
    }
    p.getBytes(key, buffer, len);
    p.end();

    DayHeader hdr{};
    memcpy(&hdr, buffer, sizeof(DayHeader));
    if (hdr.count <= kMaxDayPoints && sizeof(DayHeader) + hdr.count * sizeof(CompactPoint) <= len) {
        const auto *rawPoints = reinterpret_cast<const CompactPoint *>(buffer + sizeof(DayHeader));
        bool modified = false;
        ramCount = compactPoints(rawPoints, hdr.count, ramPoints, kMaxDayPoints, now, modified);
        if (!modified && now >= 1700000000)
            lastNvsSaveEpoch = now;
    }
    free(buffer);
}

void saveRamToNvs(const char *key, int64_t now) {
    int curY = 0, curM = 0, curD = 0;
    if (!getLocalDate(now, curY, curM, curD))
        return;
    Preferences p;
    if (!p.begin("glucohist", false))
        return;
    DayHeader hdr{uint16_t(curY), uint8_t(curM), uint8_t(curD), uint16_t(ramCount)};
    const size_t saveBytes = sizeof(DayHeader) + ramCount * sizeof(CompactPoint);
    auto *saveBuf = static_cast<uint8_t *>(malloc(saveBytes));
    if (saveBuf) {
        memcpy(saveBuf, &hdr, sizeof(DayHeader));
        memcpy(saveBuf + sizeof(DayHeader), ramPoints, ramCount * sizeof(CompactPoint));
        p.putBytes(key, saveBuf, saveBytes);
        free(saveBuf);
        lastNvsSaveEpoch = now;
    }
    p.end();
}

void upsertRamPoint(int64_t ep, int16_t val, bool isConsolidated, int64_t now, bool &changed) {
    if (ep < 1700000000 || val < 20 || val > 1000)
        return;
    const uint32_t cutoff = uint32_t(now - kWindowSeconds);
    const uint32_t maxEpoch = uint32_t(now + 60);
    const uint32_t uepoch = uint32_t(ep);
    if (uepoch < cutoff || uepoch > maxEpoch)
        return;

    const uint32_t bucket = uepoch / kBucketSeconds;
    for (size_t i = 0; i < ramCount; ++i) {
        if (ramPoints[i].epoch / kBucketSeconds == bucket) {
            if (isConsolidated || uepoch >= ramPoints[i].epoch) {
                if (ramPoints[i].glucose != val || ramPoints[i].epoch != uepoch) {
                    if (std::abs(int(ramPoints[i].glucose) - int(val)) >= 2)
                        changed = true;
                    ramPoints[i] = {uepoch, val};
                }
            }
            return;
        }
    }

    if (ramCount < kMaxDayPoints) {
        ramPoints[ramCount++] = {uepoch, val};
        changed = true;
    } else {
        size_t oldest = 0;
        for (size_t i = 1; i < ramCount; ++i) {
            if (ramPoints[i].epoch < ramPoints[oldest].epoch)
                oldest = i;
        }
        if (uepoch > ramPoints[oldest].epoch) {
            ramPoints[oldest] = {uepoch, val};
            changed = true;
        }
    }
}
} // namespace

void historyStoreInit() {}

void historyStoreLoad(const char *patientId, DailySummary &summary, int64_t now) {
    summary = DailySummary{};
    if (!patientId || !*patientId)
        return;

    char key[16];
    makeKey(patientId, key, sizeof(key));

    if (storageMutex)
        xSemaphoreTake(storageMutex, portMAX_DELAY);
    ensureRamLoaded(key, now);
    computeSummary(ramPoints, ramCount, summary);
    if (storageMutex)
        xSemaphoreGive(storageMutex);
}

size_t historyStoreCount24h(const char *patientId, int64_t now) {
    if (!patientId || !*patientId)
        return 0;
    char key[16];
    makeKey(patientId, key, sizeof(key));
    if (storageMutex)
        xSemaphoreTake(storageMutex, portMAX_DELAY);
    ensureRamLoaded(key, now);
    const size_t c = ramCount;
    if (storageMutex)
        xSemaphoreGive(storageMutex);
    return c;
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

    if (storageMutex)
        xSemaphoreTake(storageMutex, portMAX_DELAY);
    ensureRamLoaded(key, now);

    bool changed = false;
    if (currentValid)
        upsertRamPoint(current.epoch, current.glucose, false, now, changed);
    if (points) {
        for (size_t i = 0; i < count; ++i)
            upsertRamPoint(points[i].epoch, points[i].glucose, true, now, changed);
    }

    std::sort(ramPoints, ramPoints + ramCount, [](const CompactPoint &a, const CompactPoint &b) {
        return a.epoch < b.epoch;
    });

    // Save to NVS only if never saved in this session or at least 1 hour has elapsed since last NVS write
    if (changed && (lastNvsSaveEpoch == 0 || (now - lastNvsSaveEpoch) >= kNvsSaveIntervalSeconds)) {
        saveRamToNvs(key, now);
    }

    computeSummary(ramPoints, ramCount, summary);
    Serial.printf("[HIST] TIR=%.1f%% (rango=%.0f%% bajo=%.0f%% alto=%.0f%%) media=%d min=%d max=%d muestras=%u\n",
                  summary.tirPercent, summary.tirPercent, summary.lowPercent, summary.highPercent,
                  summary.averageGlucose, summary.minGlucose, summary.maxGlucose, unsigned(summary.totalReadings));
    if (storageMutex)
        xSemaphoreGive(storageMutex);
}

void historyStoreImport(const char *patientId, const gluco::Point *points, size_t count,
                        DailySummary &summary, int64_t now) {
    if (!patientId || !*patientId || !points || !count)
        return;
    int curY = 0, curM = 0, curD = 0;
    if (!getLocalDate(now, curY, curM, curD))
        return;

    char key[16];
    makeKey(patientId, key, sizeof(key));

    if (storageMutex)
        xSemaphoreTake(storageMutex, portMAX_DELAY);
    ensureRamLoaded(key, now);

    bool changed = false;
    for (size_t i = 0; i < count; ++i) {
        upsertRamPoint(points[i].epoch, points[i].glucose, true, now, changed);
    }

    std::sort(ramPoints, ramPoints + ramCount, [](const CompactPoint &a, const CompactPoint &b) {
        return a.epoch < b.epoch;
    });

    if (changed) {
        saveRamToNvs(key, now);
    }

    computeSummary(ramPoints, ramCount, summary);
    if (storageMutex)
        xSemaphoreGive(storageMutex);
}


