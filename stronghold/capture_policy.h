#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "types.h"

namespace ark::stronghold {

struct PlayerState {
    bool online = false;
    bool eliminated = false;
    PlayerStatus status = PlayerStatus::Unknown;
};

// Death is supplied by ScenePlayerData.isDead. hp == 0 can be an uninitialised
// roster, and gameFinished alone is not the game's death predicate.
inline PlayerState player_state(int connection, int online, int missing, int quit, bool dead) {
    if (connection == quit) return {false, false, PlayerStatus::Quit};
    if (dead) return {connection == online, true, PlayerStatus::Eliminated};
    if (connection == missing) return {false, false, PlayerStatus::Disconnected};
    if (connection == online) return {true, false, PlayerStatus::Online};
    return {};
}

inline bool player_frozen(bool online, bool eliminated, PlayerStatus status) {
    return eliminated || (!online && status != PlayerStatus::Unknown);
}

// Roster entries can disappear during round changes. A missing entry does not
// change identity; a different nonempty UID at an existing slot does.
class SessionTracker {
public:
    bool update(int seed, std::uintptr_t center,
                const std::vector<std::pair<int, std::string>>& roster, bool manual_reset) {
        bool changed = manual_reset || !present_ || seed != seed_ || center != center_;
        for (const auto& [slot, uid] : roster) {
            const auto previous = uids_.find(slot);
            if (previous != uids_.end() && !uid.empty() && previous->second != uid) changed = true;
        }
        if (changed) uids_.clear();
        for (const auto& [slot, uid] : roster) if (!uid.empty()) uids_[slot] = uid;
        present_ = true;
        seed_ = seed;
        center_ = center;
        return changed;
    }

    void leave() { present_ = false; uids_.clear(); }

private:
    bool present_ = false;
    int seed_ = 0;
    std::uintptr_t center_ = 0;
    std::map<int, std::string> uids_;
};

// Only concrete player identities present in local battle data / captured
// events count as coverage evidence. List lengths are not player identities.
inline Coverage coverage_for_masks(std::uint32_t active, std::uint32_t simulated, int self) {
    simulated &= kAllPlayersMask;
    active &= kAllPlayersMask;
    if (!simulated) return Coverage::Unknown;
    if (simulated == player_bit(self)) return Coverage::SelfOnly;
    if (active && (simulated & active) == active) return Coverage::FullRoom;
    return Coverage::Partial;
}

} // namespace ark::stronghold
