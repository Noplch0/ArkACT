#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "match_intel.h"

namespace ark::stronghold {

struct ShopFrequencyRow {
    std::string key;
    std::string label;
    int total = 0; // Count for the requested player scope.
    std::array<int, 4> players{};
};

struct PlayerOperatorRow {
    int player_index = -1;
    std::string key;
    std::string label;
    int gained = 0;
    int shop_gained = 0;
    int estimated_sold = 0;
    int held = 0;
    bool held_available = false;
};

class ShopCounter {
public:
    void reset(std::uint64_t session_id = 0) {
        session_id_ = session_id;
        incomplete_ = false;
        operator_frequency_.clear();
        bond_frequency_.clear();
        operator_labels_.clear();
        bond_labels_.clear();
        stores_.clear();
        gained_high_water_.clear();
        inventory_.clear();
        inventory_initialized_.clear();
        ownership_.clear();
    }

    void mark_incomplete() { incomplete_ = true; }
    bool incomplete() const { return incomplete_; }
    std::uint64_t session_id() const { return session_id_; }

    void apply(const ShopObservation& observation, bool queue_incomplete = false) {
        if (queue_incomplete) incomplete_ = true;
        if (!observation.session_id || !is_shop_player_slot(observation.player_index) ||
            observation.round <= 0) {
            incomplete_ = true;
            return;
        }
        if (!session_id_) session_id_ = observation.session_id;
        if (session_id_ != observation.session_id) {
            if (observation.session_id < session_id_) return;
            reset(observation.session_id);
        }

        const auto store_key = std::make_pair(observation.player_index, observation.round);
        StoreState& store = stores_[store_key];

        std::map<std::string, int> gained_delta;
        if (observation.gained_available) {
            auto& high_water = gained_high_water_[store_key];
            std::map<std::string, int> current;
            for (const auto& record : observation.gained) {
                if (record.count <= 0) continue;
                const std::string key = operator_key(record);
                current[key] += record.count;
                remember_operator(key, record);
            }
            for (const auto& [key, count] : current) {
                int& previous = high_water[key];
                if (count < previous) {
                    incomplete_ = true;
                    continue;
                }
                if (count > previous) gained_delta[key] = count - previous;
                previous = (std::max)(previous, count);
            }
        }

        std::map<std::string, int> current_inventory;
        if (observation.owned_available) {
            for (const auto& record : observation.owned) {
                if (record.count <= 0) continue;
                const std::string key = operator_key(record);
                current_inventory[key] += record.count;
                remember_operator(key, record);
            }
            auto& previous = inventory_[observation.player_index];
            const bool had_inventory = inventory_initialized_[observation.player_index];
            std::set<std::string> keys;
            for (const auto& [key, count] : previous) { (void)count; keys.insert(key); }
            for (const auto& [key, count] : current_inventory) { (void)count; keys.insert(key); }
            for (const auto& key : keys) {
                const int before = previous[key];
                const int after = current_inventory[key];
                if (had_inventory && after > before)
                    gained_delta[key] = (std::max)(gained_delta[key], after - before);
                if (after < before) {
                    auto& row = ownership_[{observation.player_index, key}];
                    row.estimated_sold += before - after;
                }
            }
            for (auto& [key, row] : ownership_) {
                if (key.first == observation.player_index) {
                    row.held = 0;
                    row.held_available = true;
                }
            }
            for (const auto& [key, count] : current_inventory) {
                auto& row = ownership_[{observation.player_index, key}];
                row.held = count;
                row.held_available = true;
            }
            previous = std::move(current_inventory);
            inventory_initialized_[observation.player_index] = true;
        }

        std::map<std::string, int> shop_gained;
        if (observation.goods_available) {
            const bool stale_rollback =
                store.initialized && observation.refresh_seq_available &&
                store.highest_refresh_seq_available &&
                observation.refresh_seq < store.highest_refresh_seq;
            if (stale_rollback) {
                // A delayed callback/poll must not move the shelf baseline back
                // and make the newer snapshot look like a second refresh.
                incomplete_ = true;
            } else {
                std::map<int, IntelShopGood> current_goods;
                for (std::size_t index = 0; index < observation.goods.size(); ++index) {
                    const auto& good = observation.goods[index];
                    const int slot = observation.slot_ids_available
                        ? good.slot_id
                        : static_cast<int>(index);
                    current_goods[slot] = good;
                    if (countable_shop_good(good)) remember_operator(operator_key(good), good);
                }

                if (!observation.refresh_seq_available || !observation.slot_ids_available)
                    incomplete_ = true;

                const bool had_known_sequence = store.last_refresh_seq_available;
                const bool sequence_advanced = store.initialized &&
                    observation.refresh_seq_available && had_known_sequence &&
                    observation.refresh_seq > store.last_refresh_seq;
                const bool same_sequence = store.initialized &&
                    observation.refresh_seq_available && had_known_sequence &&
                    observation.refresh_seq == store.last_refresh_seq;

                if (!store.initialized) {
                    // Keep the round boundary stable if the opening shelf is partial
                    // or another callback for the previous round arrives later.
                    const auto previous = stores_.find({observation.player_index, observation.round - 1});
                    if (previous != stores_.end()) {
                        for (const auto& [slot, good] : previous->second.goods) {
                            if (good.frozen && countable_shop_good(good))
                                store.opening_frozen_goods.emplace(slot, operator_key(good));
                        }
                    }
                } else if (observation.refresh_seq_available && store.highest_refresh_seq_available &&
                           observation.refresh_seq > store.highest_refresh_seq) {
                    store.opening_frozen_goods.clear();
                }

                std::vector<IntelShopGood> newly_visible;
                newly_visible.reserve(current_goods.size());
                if (sequence_advanced) {
                    if (observation.refresh_seq - store.last_refresh_seq > 1)
                        incomplete_ = true;
                    store.seen_appearances.clear();
                    for (const auto& [slot, good] : current_goods) {
                        const auto old = store.goods.find(slot);
                        const bool retained_frozen = good.frozen && old != store.goods.end() &&
                            old->second.frozen && operator_key(old->second) == operator_key(good);
                        store.seen_appearances.insert({slot, operator_key(good)});
                        if (!retained_frozen) newly_visible.push_back(good);
                    }
                } else {
                    if (store.initialized && !had_known_sequence)
                        incomplete_ = true;
                    for (const auto& [slot, good] : current_goods) {
                        const auto key = operator_key(good);
                        // A known sequence deduplicates repeats across partial snapshots.
                        bool is_new = store.seen_appearances.insert({slot, key}).second;
                        if (!observation.refresh_seq_available) {
                            // Without a sequence, fall back to adjacent shelf changes.
                            const auto old = store.goods.find(slot);
                            is_new = old == store.goods.end() || operator_key(old->second) != key;
                        }
                        if (is_new && !consume_opening_frozen_good(store, slot, good, key))
                            newly_visible.push_back(good);
                    }
                    if (same_sequence && !newly_visible.empty()) incomplete_ = true;
                }
                count_refresh(observation.player_index, newly_visible);

                if (same_sequence) {
                    std::map<std::string, int> removed_goods;
                    for (const auto& [slot, old_good] : store.goods) {
                        const auto now = current_goods.find(slot);
                        if (now == current_goods.end() ||
                            operator_key(now->second) != operator_key(old_good)) {
                            ++removed_goods[operator_key(old_good)];
                        }
                    }
                    for (const auto& [key, gained] : gained_delta) {
                        const int matched = (std::min)(gained, removed_goods[key]);
                        if (matched > 0) shop_gained[key] += matched;
                    }
                }

                store.initialized = true;
                store.goods = std::move(current_goods);
                store.last_refresh_seq = observation.refresh_seq;
                store.last_refresh_seq_available = observation.refresh_seq_available;
                if (observation.refresh_seq_available &&
                    (!store.highest_refresh_seq_available ||
                     observation.refresh_seq > store.highest_refresh_seq)) {
                    store.highest_refresh_seq = observation.refresh_seq;
                    store.highest_refresh_seq_available = true;
                }
            }
        } else {
            incomplete_ = true;
        }

        for (const auto& [key, count] : gained_delta) {
            auto& row = ownership_[{observation.player_index, key}];
            row.gained += count;
            row.shop_gained += shop_gained[key];
        }
    }

    std::vector<ShopFrequencyRow> frequencies(bool by_bond, int player_filter = -1) const {
        const auto& source = by_bond ? bond_frequency_ : operator_frequency_;
        const auto& labels = by_bond ? bond_labels_ : operator_labels_;
        const char* fallback = by_bond ? "盟约名称未读取" : "干员名称未读取";
        const bool single_player = is_shop_player_slot(player_filter);
        std::vector<ShopFrequencyRow> result;
        result.reserve(source.size());
        for (const auto& [key, row] : source) {
            const int total = single_player ? row.players[player_filter - 1] : row.total;
            if (total == 0) continue;
            result.push_back({key, label_for(labels, key, fallback), total, row.players});
        }
        std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
            if (left.total != right.total) return left.total > right.total;
            return left.label < right.label;
        });
        return result;
    }

    std::vector<PlayerOperatorRow> player_operators(int player_filter = -1) const {
        std::vector<PlayerOperatorRow> result;
        for (const auto& [key, row] : ownership_) {
            if (player_filter >= 0 && key.first != player_filter) continue;
            result.push_back({key.first, key.second,
                              label_for(operator_labels_, key.second, "干员名称未读取"),
                              row.gained, row.shop_gained,
                              row.estimated_sold, row.held, row.held_available});
        }
        std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
            if (left.player_index != right.player_index) return left.player_index < right.player_index;
            if (left.held != right.held) return left.held > right.held;
            return left.label < right.label;
        });
        return result;
    }

private:
    struct Frequency {
        int total = 0;
        std::array<int, 4> players{};
    };
    struct StoreState {
        bool initialized = false;
        bool last_refresh_seq_available = false;
        int last_refresh_seq = 0;
        bool highest_refresh_seq_available = false;
        int highest_refresh_seq = 0;
        std::map<int, IntelShopGood> goods;
        // Prior-round frozen slots awaiting their first observation this round.
        std::map<int, std::string> opening_frozen_goods;
        // Slot/operator identities already observed in the current refresh.
        std::set<std::pair<int, std::string>> seen_appearances;
    };
    struct Ownership {
        int gained = 0;
        int shop_gained = 0;
        int estimated_sold = 0;
        int held = 0;
        bool held_available = false;
    };

    static std::string operator_key(const IntelChessRecord& record) {
        if (!record.chess_id.empty()) return record.chess_id;
        return "#" + std::to_string(record.chess_identifier);
    }
    static std::string operator_key(const IntelShopGood& good) {
        if (!good.chess_id.empty()) return good.chess_id;
        return "#" + std::to_string(good.chess_identifier);
    }
    static std::string label_for(const std::map<std::string, std::string>& labels,
                                 const std::string& key, const char* fallback) {
        const auto it = labels.find(key);
        return it != labels.end() ? it->second : fallback;
    }

    template <typename Record>
    void remember_operator(const std::string& key, const Record& record) {
        if (!record.operator_name.empty() && record.operator_name != key &&
            !record.operator_name.starts_with("char_"))
            operator_labels_[key] = record.operator_name;
        for (std::size_t i = 0; i < record.bond_ids.size() && i < record.bond_names.size(); ++i) {
            const auto& id = record.bond_ids[i];
            const auto& name = record.bond_names[i];
            if (!id.empty() && !name.empty() && name != id) bond_labels_[id] = name;
        }
    }
    static bool countable_shop_good(const IntelShopGood& good) {
        return good.operator_available || good.chess_identifier > 0 || !good.chess_id.empty();
    }
    static bool consume_opening_frozen_good(StoreState& store, int slot,
                                            const IntelShopGood& good, const std::string& key) {
        const auto previous = store.opening_frozen_goods.find(slot);
        if (previous == store.opening_frozen_goods.end() || !countable_shop_good(good))
            return false;
        const bool retained = good.frozen && previous->second == key;
        store.opening_frozen_goods.erase(previous);
        return retained;
    }
    void count_refresh(int player, const std::vector<IntelShopGood>& goods) {
        for (const auto& good : goods) {
            if (!countable_shop_good(good)) {
                incomplete_ = true;
                continue;
            }
            const std::string key = operator_key(good);
            auto& operator_row = operator_frequency_[key];
            ++operator_row.total;
            ++operator_row.players[player - 1];
            for (const auto& bond : good.bond_ids) {
                if (bond.empty()) continue;
                auto& bond_row = bond_frequency_[bond];
                ++bond_row.total;
                ++bond_row.players[player - 1];
            }
        }
    }

    std::uint64_t session_id_ = 0;
    bool incomplete_ = false;
    std::map<std::string, Frequency> operator_frequency_;
    std::map<std::string, Frequency> bond_frequency_;
    std::map<std::string, std::string> operator_labels_;
    std::map<std::string, std::string> bond_labels_;
    std::map<std::pair<int, int>, StoreState> stores_;
    std::map<std::pair<int, int>, std::map<std::string, int>> gained_high_water_;
    std::map<int, std::map<std::string, int>> inventory_;
    std::map<int, bool> inventory_initialized_;
    std::map<std::pair<int, std::string>, Ownership> ownership_;
};

} // namespace ark::stronghold
