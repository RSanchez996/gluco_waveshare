#pragma once
#include "app.hpp"
#include "theme.hpp"
#include <cstdint>

inline uint32_t tirColor(float tirPercent) {
    if (tirPercent >= 90.0f)
        return theme::GreenBold;
    if (tirPercent >= 70.0f)
        return theme::Green;
    return theme::Orange;
}

void historyStoreInit();
void historyStoreLoad(const char *patientId, DailySummary &summary, int64_t now);
void historyStoreMerge(const char *patientId, const gluco::Point *points, size_t count,
                       const gluco::Point &current, bool currentValid, DailySummary &summary,
                       int64_t now);
void historyStoreImport(const char *patientId, const gluco::Point *points, size_t count,
                        DailySummary &summary, int64_t now);
bool historyStoreHasFullDay(const char *patientId, int64_t now);
