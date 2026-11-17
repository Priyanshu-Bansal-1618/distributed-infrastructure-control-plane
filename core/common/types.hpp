#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>
#include <map>

namespace dicp {

using TimestampMs = uint64_t;
using TimestampSec = uint64_t;
using Incarnation = uint64_t;
using SequenceNumber = uint64_t;

inline TimestampMs CurrentTimeMs() {
    return static_cast<TimestampMs>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

inline TimestampSec CurrentTimeSec() {
    return static_cast<TimestampSec>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

inline TimestampMs SteadyClockMs() {
    return static_cast<TimestampMs>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace dicp
