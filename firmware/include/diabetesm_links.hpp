#pragma once
#include "app.hpp"
#include <cstddef>

void dmLinksInit();
bool dmLinkGet(const char *patientId, DiabetesMLink &out);
bool dmLinkSave(const DiabetesMLink &link);
size_t dmLinkList(DiabetesMLink *out, size_t maxCount);
bool dmLinkApply(const char *patientId, Config &config);
bool dmLinkHasAccount(const char *patientId, char *outUser = nullptr, size_t maxLen = 0);
