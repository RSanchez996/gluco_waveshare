#include "history_store.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace {
// 24 hours in 5-minute buckets = 288 buckets (300 capacity with margin).
// Stored 100% in RAM (zero NVS flash writes, zero flash wear).
constexpr size_t kMaxDayPoints = 300;
constexpr int64_t kWindowSeconds = 24 * 60 * 60;
constexpr uint32_t kBucketSeconds = 5 * 60;
constexpr int64_t kMaxGapSeconds = 25 * 60; // Max gap bridged for time-weighted TIR
constexpr size_t kMaxPatientSlots = 4;

struct CompactPoint {
    uint32_t epoch;
    int16_t glucose;
};

struct PatientHistorySlot {
    char patientId[100]{};
    CompactPoint points[kMaxDayPoints]{};
    size_t count = 0;
    uint32_t lastAccess = 0;
};

PatientHistorySlot slots[kMaxPatientSlots]{};
uint32_t accessCounter = 0;

PatientHistorySlot &slotFor(const char *patientId) {
    size_t emptyIdx = kMaxPatientSlots;
    size_t oldestIdx = 0;
    uint32_t oldestTick = UINT32_MAX;

    for (size_t i = 0; i < kMaxPatientSlots; ++i) {
        if (strcmp(slots[i].patientId, patientId) == 0) {
            slots[i].lastAccess = ++accessCounter;
            return slots[i];
        }
        if (slots[i].patientId[0] == 0 && emptyIdx == kMaxPatientSlots) {
            emptyIdx = i;
        }
        if (slots[i].lastAccess < oldestTick) {
            oldestTick = slots[i].lastAccess;
            oldestIdx = i;
        }
    }

    const size_t target = (emptyIdx < kMaxPatientSlots) ? emptyIdx : oldestIdx;
    memset(&slots[target], 0, sizeof(PatientHistorySlot));
    strlcpy(slots[target].patientId, patientId, sizeof(slots[target].patientId));
    slots[target].lastAccess = ++accessCounter;
    return slots[target];
}

void computeSummary(const CompactPoint *points, size_t count, int64_t nowEpoch, DailySummary &summary) {
    if (!points || !count) {
        summary = DailySummary{};
        return;
    }

    const int64_t cutoff = (nowEpoch >= 1700000000) ? (nowEpoch - kWindowSeconds) : 0;

    size_t validCount = 0;
    int minG = 9999, maxG = -9999;
    int64_t simpleSum = 0;
    size_t simpleGreen = 0, simpleLow = 0, simpleHigh = 0;

    double weightedGreenSec = 0.0;
    double weightedLowSec = 0.0;
    double weightedHighSec = 0.0;
    double weightedSumGlucoseSec = 0.0;
    double totalWeightSec = 0.0;

    int prevG = -1;
    int64_t prevEp = 0;

    for (size_t i = 0; i < count; ++i) {
        const int64_t ep = points[i].epoch;
        const int g = points[i].glucose;
        if (ep < cutoff || g < 20 || g > 1000)
            continue;

        ++validCount;
        simpleSum += g;
        if (g < minG)
            minG = g;
        if (g > maxG)
            maxG = g;
        if (g < 70)
            ++simpleLow;
        else if (g <= 180)
            ++simpleGreen;
        else
            ++simpleHigh;

        if (prevG >= 20 && ep > prevEp) {
            const int64_t dt = std::min<int64_t>(ep - prevEp, kMaxGapSeconds);
            if (dt > 0) {
                constexpr int kSteps = 8;
                const double stepSec = double(dt) / double(kSteps);
                for (int s = 0; s < kSteps; ++s) {
                    const double frac = (double(s) + 0.5) / double(kSteps);
                    const double val = double(prevG) + (double(g - prevG) * frac);
                    if (val < 70.0)
                        weightedLowSec += stepSec;
                    else if (val <= 180.0)
                        weightedGreenSec += stepSec;
                    else
                        weightedHighSec += stepSec;
                    weightedSumGlucoseSec += val * stepSec;
                    totalWeightSec += stepSec;
                }
            }
        }
        prevG = g;
        prevEp = ep;
    }

    if (!validCount) {
        summary = DailySummary{};
        return;
    }

    if (totalWeightSec >= 15.0 * 60.0) {
        summary.tirPercent = float((weightedGreenSec * 100.0) / totalWeightSec);
        summary.lowPercent = float((weightedLowSec * 100.0) / totalWeightSec);
        summary.highPercent = float((weightedHighSec * 100.0) / totalWeightSec);
        summary.averageGlucose = int(std::lround(weightedSumGlucoseSec / totalWeightSec));
    } else {
        summary.tirPercent = (float(simpleGreen) * 100.0f) / float(validCount);
        summary.lowPercent = (float(simpleLow) * 100.0f) / float(validCount);
        summary.highPercent = (float(simpleHigh) * 100.0f) / float(validCount);
        summary.averageGlucose = int(std::lround(double(simpleSum) / double(validCount)));
    }

    summary.minGlucose = minG;
    summary.maxGlucose = maxG;
    summary.totalReadings = validCount;
    summary.valid = true;
}

void compactAndPrune(CompactPoint *pts, size_t &count, int64_t nowEpoch) {
    if (!count)
        return;

    std::sort(pts, pts + count, [](const CompactPoint &a, const CompactPoint &b) {
        return a.epoch < b.epoch;
    });

    const int64_t cutoff = (nowEpoch >= 1700000000) ? (nowEpoch - kWindowSeconds) : 0;

    CompactPoint out[kMaxDayPoints];
    size_t outCount = 0;
    for (size_t i = 0; i < count; ++i) {
        if (int64_t(pts[i].epoch) < cutoff || pts[i].glucose < 20 || pts[i].glucose > 1000)
            continue;
        if (nowEpoch >= 1700000000 && int64_t(pts[i].epoch) > nowEpoch + 600)
            continue;

        const uint32_t bucket = pts[i].epoch / kBucketSeconds;
        if (outCount > 0 && (out[outCount - 1].epoch / kBucketSeconds) == bucket) {
            out[outCount - 1] = pts[i];
        } else if (outCount < kMaxDayPoints) {
            out[outCount++] = pts[i];
        } else {
            memmove(out, out + 1, (kMaxDayPoints - 1) * sizeof(CompactPoint));
            out[kMaxDayPoints - 1] = pts[i];
        }
    }

    memcpy(pts, out, outCount * sizeof(CompactPoint));
    count = outCount;
}

void upsertPoint(CompactPoint *pts, size_t &count, int64_t ep, int16_t val, int64_t cutoff, int64_t maxEpoch) {
    if (ep < 1700000000 || ep < cutoff || ep > maxEpoch || val < 20 || val > 1000)
        return;
    const uint32_t uepoch = uint32_t(ep);
    const uint32_t bucket = uepoch / kBucketSeconds;

    for (size_t i = 0; i < count; ++i) {
        if ((pts[i].epoch / kBucketSeconds) == bucket) {
            if (uepoch >= pts[i].epoch) {
                pts[i].epoch = uepoch;
                pts[i].glucose = val;
            }
            return;
        }
    }

    if (count < kMaxDayPoints) {
        pts[count++] = {uepoch, val};
    } else {
        size_t oldestIdx = 0;
        for (size_t i = 1; i < count; ++i) {
            if (pts[i].epoch < pts[oldestIdx].epoch)
                oldestIdx = i;
        }
        if (uepoch > pts[oldestIdx].epoch) {
            pts[oldestIdx] = {uepoch, val};
        }
    }
}
} // namespace

void historyStoreInit() {}

void historyStoreLoad(const char *patientId, DailySummary &summary, int64_t now) {
    summary = DailySummary{};
    if (!patientId || !*patientId)
        return;

    auto &slot = slotFor(patientId);
    compactAndPrune(slot.points, slot.count, now);
    computeSummary(slot.points, slot.count, now, summary);
}

bool historyStoreHasFullDay(const char *patientId, int64_t now) {
    if (!patientId || !*patientId || now < 1700000000)
        return false;
    auto &slot = slotFor(patientId);
    compactAndPrune(slot.points, slot.count, now);
    if (slot.count < 100)
        return false;
    const int64_t span = int64_t(slot.points[slot.count - 1].epoch) - int64_t(slot.points[0].epoch);
    return span >= 18 * 3600;
}

void historyStoreImport(const char *patientId, const gluco::Point *points, size_t count,
                        DailySummary &summary, int64_t now) {
    if (!patientId || !*patientId || !points || !count || now < 1700000000)
        return;

    auto &slot = slotFor(patientId);
    const int64_t cutoff = now - kWindowSeconds;
    const int64_t maxEpoch = now + 600;

    for (size_t i = 0; i < count; ++i) {
        upsertPoint(slot.points, slot.count, points[i].epoch, points[i].glucose, cutoff, maxEpoch);
    }

    compactAndPrune(slot.points, slot.count, now);
    computeSummary(slot.points, slot.count, now, summary);
}

void historyStoreMerge(const char *patientId, const gluco::Point *points, size_t count,
                       const gluco::Point &current, bool currentValid, DailySummary &summary,
                       int64_t now) {
    if (!patientId || !*patientId)
        return;
    if (now < 1700000000) {
        if (currentValid && current.epoch >= 1700000000)
            now = current.epoch;
        else if (points && count > 0 && points[count - 1].epoch >= 1700000000)
            now = points[count - 1].epoch;
        else
            return;
    }

    auto &slot = slotFor(patientId);
    const int64_t cutoff = now - kWindowSeconds;
    const int64_t maxEpoch = now + 600;

    if (points) {
        for (size_t i = 0; i < count; ++i)
            upsertPoint(slot.points, slot.count, points[i].epoch, points[i].glucose, cutoff, maxEpoch);
    }
    if (currentValid)
        upsertPoint(slot.points, slot.count, current.epoch, current.glucose, cutoff, maxEpoch);

    compactAndPrune(slot.points, slot.count, now);
    computeSummary(slot.points, slot.count, now, summary);
}
