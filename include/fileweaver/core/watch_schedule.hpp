#pragma once
#include <algorithm>
#include <chrono>
#include <optional>

namespace fileweaver {
// Caller supplies monotonic time. No wall-clock dependency or sleeping in domain logic.
class WatchSchedule {
  public:
    using Time = std::chrono::steady_clock::time_point;
    explicit WatchSchedule(Time now) : periodic_(now) {}
    void changed(Time now) {
        if (!first_) {
            first_ = now;
        }
        dirty_ = std::min(now + std::chrono::milliseconds(500), *first_ + std::chrono::seconds(2));
    }
    void urgent(Time now) {
        first_ = now;
        dirty_ = now;
    }
    [[nodiscard]] bool due(Time now) const {
        return now >= retry_ && (now >= periodic_ || (dirty_ && now >= *dirty_));
    }
    void completed(Time now, bool success) {
        first_.reset();
        dirty_.reset();
        if (success) {
            periodic_ = now + std::chrono::seconds(60);
            retry_ = now;
        } else {
            periodic_ = now;
            retry_ = now + std::chrono::seconds(5);
        }
    }

  private:
    Time periodic_;
    Time retry_ = Time::min();
    std::optional<Time> first_;
    std::optional<Time> dirty_;
};
} // namespace fileweaver
