#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ark::stronghold {

inline constexpr bool is_shop_player_slot(int player_index) noexcept {
    return player_index >= 1 && player_index <= 4;
}

// ShopCounter receives only canonical P1-P4 slots.
inline constexpr int canonical_shop_player_slot(int raw_index, bool zero_based) noexcept {
    if (zero_based) return raw_index >= 0 && raw_index < 4 ? raw_index + 1 : -1;
    return is_shop_player_slot(raw_index) ? raw_index : -1;
}

// Copied, read-only match information. The capture thread fills this from the
// AutoChess data center; the render thread must never dereference game objects.
struct IntelSquadSlot {
    std::string char_id;
    std::string chess_id;
    std::string template_id;
    std::string cultivate_effect_id;
    std::string equip_id;
    std::string skin_id;
    std::vector<std::string> bond_ids;
    int type = -1;
    int potential_rank = -1;
    int skill_index = -1;
    bool banned = false;
    bool banned_available = false;
};

struct IntelBannedUnit {
    std::string bond_id;
    std::vector<int> chess_identifiers;
};

struct IntelShopGood {
    std::string chess_id;
    std::string operator_name;
    std::vector<std::string> bond_ids;
    std::vector<std::string> bond_names;
    int slot_id = 0;
    int price = 0;
    bool frozen = false;
    int chess_identifier = 0;
    bool operator_available = false;
};

struct IntelChessRecord {
    int chess_identifier = 0;
    int count = 0;
    std::string chess_id;
    std::string operator_name;
    std::vector<std::string> bond_ids;
    std::vector<std::string> bond_names;
    bool operator_available = false;
};

struct ShopObservation {
    std::uint64_t session_id = 0;
    int round = 0;
    int player_index = -1;
    int refresh_seq = 0;
    bool refresh_seq_available = false;
    bool slot_ids_available = true;
    bool goods_available = false;
    std::vector<IntelShopGood> goods;
    bool owned_available = false;
    std::vector<IntelChessRecord> owned;
    bool gained_available = false;
    std::vector<IntelChessRecord> gained;
};

struct IntelHiddenStore {
    bool available = false;
    bool refresh_seq_available = false;
    bool goods_available = false;
    int level = 0;
    int coin = 0;
    int upgrade_price = 0;
    int refresh_price = 0;
    int goods_type = 0;
    int free_refresh_count = 0;
    bool special_refresh = false;
    int refresh_seq = 0;
    std::vector<IntelShopGood> goods;
};

struct IntelHiddenPlayer {
    int index = -1;
    std::string band_id;
    std::vector<IntelBannedUnit> banned_units;
    bool banned_units_available = false;
    int hp = 0;
    bool hp_available = false;
    int shop_level = 0;
    bool shop_level_available = false;
    int game_state = -1;
    bool game_state_available = false;
    IntelHiddenStore store;
    bool owned_available = false;
    std::vector<IntelChessRecord> owned;
    bool gained_available = false;
    std::vector<IntelChessRecord> gained;
};

struct HiddenSyncSnapshot {
    bool available = false;
    std::uint64_t session_id = 0;
    std::uint64_t sequence = 0;
    std::uint64_t captured_tick = 0;
    int stage_seed = 0;
    bool stage_seed_available = false;
    std::string hidden_boss_id;
    bool hidden_boss_available = false;
    std::string hidden_boss_name;
    bool hidden_boss_name_available = false;
    int uid_index = -1;
    bool uid_index_available = false;
    int round = -1;
    bool round_available = false;
    int scene_state = -1;
    bool scene_state_available = false;
    std::vector<IntelHiddenPlayer> players;
};

struct IntelPlayer {
    int index = -1;
    bool self = false;
    std::string name;
    std::string band_id;
    bool band_available = false;
    std::vector<IntelSquadSlot> squad_slots;
    bool squad_available = false;
    int shop_level = 0;
    bool shop_level_available = false;
    int shop_coin = 0;
    bool shop_coin_available = false;
};

struct MatchIntelSnapshot {
    bool available = false;
    bool shop_available = false;
    std::uint64_t session_id = 0;
    std::uint32_t round = 1;
    int stage_seed = 0;
    bool stage_seed_available = false;
    std::string stage_id;
    bool stage_available = false;
    std::string mode_id;
    bool mode_available = false;
    std::string boss_id;
    bool boss_available = false;
    std::string hidden_boss_id;
    bool hidden_boss_available = false;
    std::string hidden_boss_name;
    bool hidden_boss_name_available = false;
    std::vector<std::string> enemy_types;
    bool enemy_types_available = false;
    std::vector<std::string> banned_bonds;
    bool banned_bonds_available = false;
    // The runtime schema exposes only the local player's ban-unit list. Other
    // players' static lineups are available, but their ban-unit details are not
    // retained in AutoChessDataCenter by the current client.
    std::vector<IntelBannedUnit> self_banned_units;
    bool self_banned_units_available = false;
    std::vector<IntelPlayer> players;
    int shop_level = 0;
    int shop_coin = 0;
    int shop_upgrade_price = 0;
    int shop_refresh_price = 0;
    int shop_goods_type = 0;
    int shop_free_refresh_count = 0;
    bool shop_special_refresh = false;
    int shop_refresh_seq = 0;
    bool shop_core_available = false;
    bool shop_goods_available = false;
    std::vector<IntelShopGood> shop_goods;
};

} // namespace ark::stronghold
