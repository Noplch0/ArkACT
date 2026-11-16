#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace ark::stronghold {

inline constexpr std::size_t kMaxId = 128;
inline constexpr std::size_t kMaxName = 96;
inline constexpr std::size_t kMaxPlayers = 8;
inline constexpr std::uint32_t kAllPlayersMask = (1u << kMaxPlayers) - 1u;

enum class EventKind : std::uint32_t {
    SessionStart = 1,
    RoundStart = 2,
    Roster = 3,
    Coverage = 4,
    Damage = 5,
    ElementalBuildup = 6,
    Clock = 7,
    PlayerStatus = 8,
    SessionEnd = 9,
    Warning = 10,
    BattleEnd = 11,
    ModeExit = 12,
    Phase = 13,
};

enum class BattlePhase : std::uint32_t {
    Main = 0,
    Assist = 1,
    Idle = 2,
};

enum class Coverage : std::uint32_t {
    Unknown = 0,
    SelfOnly = 1,
    Partial = 2,
    FullRoom = 3,
};

enum class PlayerStatus : std::uint32_t {
    Online = 0,
    Disconnected = 1,
    Quit = 2,
    Eliminated = 3,
    Unknown = 4,
};

inline constexpr int kUnknownPlayer = -1;

inline std::string player_slot_label(int raw_index) {
    if (raw_index > 0) return "P" + std::to_string(raw_index);
    return raw_index == 0 ? "P?" : "未归属";
}

// POD event shared by the game-thread capture and render-thread model. Character IDs and
// names are copied into fixed buffers so queue reads never touch managed strings.
struct Event {
    std::uint32_t size = sizeof(Event);
    EventKind kind = EventKind::Warning;
    std::uint64_t sequence = 0;
    std::uint64_t session_id = 0;
    std::uint32_t round = 1;
    BattlePhase phase = BattlePhase::Main;
    std::uint64_t battle = 0;
    std::int64_t wall_time = 0;
    double game_time = 0;
    double delta_time = 0;
    double calculated_damage = 0;
    double actual_damage = 0;
    std::int32_t damage_type = 0;
    std::int32_t player_index = kUnknownPlayer;
    std::uint32_t card_uid = 0;
    std::uint32_t coverage = static_cast<std::uint32_t>(Coverage::Unknown);
    std::uint32_t simulated_mask = 0;
    std::uint32_t active_mask = 0;
    std::uint32_t continuous = 0;
    std::uint32_t partial = 0;
    std::uint32_t self = 0;
    std::uint32_t online = 1;
    std::uint32_t eliminated = 0;
    std::uint32_t status = static_cast<std::uint32_t>(PlayerStatus::Online);
    char actor_id[kMaxId]{};
    char actor_name[kMaxName]{};
    char player_uid[kMaxId]{};
    char player_name[kMaxName]{};
    char nick_number[24]{};
    char message[kMaxName]{};
};

inline void copy_text(char* target, std::size_t size, const std::string& value) {
    if (!target || !size) {
        return;
    }
    const std::size_t count = (std::min)(size - 1, value.size());
    std::copy_n(value.data(), count, target);
    target[count] = '\0';
}

inline std::string text(const char* value) {
    return value ? std::string(value) : std::string{};
}

inline std::uint32_t player_bit(int index) {
    return index >= 0 && index < static_cast<int>(kMaxPlayers) ? (1u << index) : 0u;
}

// Thread-safe bounded history. Unlike a per-battle replay service, this history intentionally
// spans all rounds of one AutoChess simulation.
class EventQueue {
public:
    static constexpr std::size_t kDefaultCapacity = 32768;

    explicit EventQueue(std::size_t capacity = kDefaultCapacity) : capacity_(capacity) {}

    void clear() {
        std::lock_guard lock(mutex_);
        events_.clear();
        sequence_ = 0;
    }

    void publish(Event event) {
        std::lock_guard lock(mutex_);
        event.sequence = ++sequence_;
        events_.push_back(event);
        while (events_.size() > capacity_) {
            events_.pop_front();
        }
    }

    std::size_t read(std::uint64_t after, Event* out, std::size_t capacity,
                     bool* overrun, bool* partial) const {
        std::lock_guard lock(mutex_);
        const bool lost = !events_.empty() && after + 1 < events_.front().sequence;
        if (overrun) {
            *overrun = lost;
        }
        if (partial) {
            *partial = lost;
        }
        if (!out || !capacity) {
            return 0;
        }
        std::size_t written = 0;
        for (const Event& event : events_) {
            if (event.sequence > after && written < capacity) {
                out[written++] = event;
            }
        }
        return written;
    }

    std::uint64_t newest_sequence() const {
        std::lock_guard lock(mutex_);
        return sequence_;
    }

private:
    mutable std::mutex mutex_;
    std::deque<Event> events_;
    std::size_t capacity_ = kDefaultCapacity;
    std::uint64_t sequence_ = 0;
};

} // namespace ark::stronghold
