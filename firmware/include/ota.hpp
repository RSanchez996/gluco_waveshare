#pragma once
#include "app.hpp"
#include <cstdint>

enum class OtaState : uint8_t {
    Idle = 0,
    Checking,
    UpToDate,
    UpdateAvailable,
    Downloading,
    Installing,
    Rebooting,
    Failed
};

struct OtaStatus {
    OtaState state = OtaState::Idle;
    uint8_t progressPercent = 0;
    uint32_t downloadedBytes = 0;
    uint32_t totalBytes = 0;
    char currentVersion[32]{};
    char currentBuild[64]{};
    char activePartition[16]{};
    char remoteVersion[32]{};
    char remoteBuild[64]{};
    char message[160]{};
    char changes[512]{};
};

void otaInit();
void otaService();
bool otaRequestCheck(String &error);
bool otaRequestInstall(String &error);
OtaStatus otaGetStatus();
bool otaBusy();
