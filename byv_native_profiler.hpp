#pragma once

// BYV native profiler
// Debug-only instrumentation. It does NOT change the BYV wire format.
// Build with: -DBYV_ENABLE_PROFILE
//
// Usage:
//   BYV_PROFILE_SCOPE("serialize.convert");
//   BYV_PROFILE_SCOPE("serialize.dictionary");
//   BYV_PROFILE_SCOPE("serialize.encode");
//   BYV_PROFILE_SCOPE("serialize.buffer_copy");
//   BYV_PROFILE_SCOPE("deserialize.decode");
//   BYV_PROFILE_SCOPE("deserialize.to_js");
//
// The profiler is thread-local and reports one aggregate sample per
// serialize()/deserialize() call. Nested scopes accumulate independently.

#ifdef BYV_ENABLE_PROFILE

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace byv_profile {

using Clock = std::chrono::steady_clock;

struct Stats {
    uint64_t calls = 0;
    uint64_t ns = 0;
};

inline thread_local std::unordered_map<std::string, Stats> stats;
inline thread_local bool active = false;

class Scope {
public:
    explicit Scope(const char* name)
        : name_(name), start_(Clock::now()) {}

    ~Scope() {
        if (!active) return;
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - start_).count();

        auto& s = stats[std::string(name_)];
        s.calls++;
        s.ns += static_cast<uint64_t>(elapsed);
    }

private:
    const char* name_;
    Clock::time_point start_;
};

inline void reset() {
    stats.clear();
}

inline void begin() {
    reset();
    active = true;
}

inline void end() {
    active = false;
}

inline void report(const char* title) {
    std::fprintf(stderr, "\n===== %s =====\n", title);

    uint64_t total = 0;
    for (const auto& [name, s] : stats)
        total += s.ns;

    for (const auto& [name, s] : stats) {
        const double ms = static_cast<double>(s.ns) / 1e6;
        const double pct =
            total ? (100.0 * static_cast<double>(s.ns) /
                     static_cast<double>(total)) : 0.0;

        std::fprintf(
            stderr,
            "%-32s calls=%llu  total=%10.3f ms  share=%6.2f%%\n",
            name.c_str(),
            static_cast<unsigned long long>(s.calls),
            ms,
            pct
        );
    }

    std::fprintf(stderr, "========================\n\n");
}

} // namespace byv_profile

#define BYV_PROFILE_SCOPE(name) \
    byv_profile::Scope byv_profile_scope_##__LINE__(name)

#else

#define BYV_PROFILE_SCOPE(name) do {} while (0)

#endif
