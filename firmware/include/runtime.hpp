#pragma once
#include <cstdint>
namespace runtime {
constexpr int kGuiCore = 1;
constexpr int kDataCore = 0;
constexpr unsigned kGuiPriority = 2;
constexpr unsigned kDataPriority = 1;
constexpr unsigned kPortalPriority = 1;
constexpr unsigned kGuiStack = 12288;
constexpr unsigned kDataStack = 20480;
constexpr unsigned kPortalStack = 8192;
constexpr int kWidth = 800, kHeight = 480;
constexpr int kBounceLines = 20, kDrawLines = 12;
constexpr unsigned kPixelClock = 12000000;
constexpr int kGraphY = 160, kGraphHeight = 312;
static_assert((kWidth * kHeight) % (kWidth * kBounceLines) == 0);
static_assert(kHeight / kBounceLines % 2 == 0);
static_assert(kGraphY + kGraphHeight < 480);
inline bool due(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }
} // namespace runtime
