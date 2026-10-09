#include "core.hpp"
#include "protocol.hpp"
#include "runtime.hpp"
#include <cassert>
#include <limits>
#include <random>
int main() {
    using namespace gluco;
    const int64_t now = 1780000000;
    assert(range(69) == Range::LowRed && range(70) == Range::Green && range(180) == Range::Green);
    assert(range(181) == Range::HighYellow && range(240) == Range::HighYellow &&
           range(241) == Range::VeryHighOrange);
    assert(range(std::numeric_limits<double>::quiet_NaN()) == Range::Invalid);
    assert(stale(now - 301, now) && !stale(now - 300, now) && stale(now + 61, now));
    Point p[8] = {{now, 120},      {now - 10, 100},    {now - 10, 110},
                  {now + 61, 140}, {now - 50000, 100}, {now - 20, 0}};
    const auto n = normalize(p, 6, now);
    assert(n == 2 && p[0].epoch == now - 10 && p[0].glucose == 110 && p[1].epoch == now);
    assert(graphX(now, now, 800) == 800 && graphX(now - kHistorySeconds, now, 800) == 0);
    assert(!graphConnect({now - 1900, 100}, {now, 120}));
    assert(libreMgdl(126, true, 7.0, 2) == 126); // supplied mg/dL wins over mmol/L
    assert(libreMgdl(NAN, false, 126, 1) == 126);
    assert(factoryEpoch("10/08/2026 02:00:00 PM") == 1791468000);
    assert(factoryEpoch("garbage") == 0);
    assert(runtime::due(5, 0xfffffff0u) && !runtime::due(0xfffffff0u, 5));
    // Randomized corrupt/history payloads must preserve order and bounds.
    std::mt19937 random(43);
    for (int run = 0; run < 1000; ++run) {
        Point data[kMaxPoints];
        for (auto &x : data)
            x = {now - int(random() % 60000), int16_t(random() % 1100)};
        const auto count = normalize(data, kMaxPoints, now);
        assert(count <= kMaxPoints);
        for (size_t i = 0; i < count; ++i) {
            assert(validEpoch(data[i].epoch, now));
            assert(range(data[i].glucose) != Range::Invalid);
            if (i)
                assert(data[i].epoch > data[i - 1].epoch);
        }
    }
}
