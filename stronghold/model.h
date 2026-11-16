#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "types.h"

namespace ark::stronghold {

inline constexpr std::size_t kDamageTypeCount = 4;
inline constexpr std::array<const char*, kDamageTypeCount> kDamageTypeNames = {
    "物理", "法术", "真实", "元素"};

inline int damage_type_index(std::int32_t type) {
    switch (type) {
    case 1: return 0;
    case 2: return 1;
    case 3: return 2;
    case 5: return 3;
    default: return -1;
    }
}

struct TypeTotals {
    double actual = 0;
    double calculated = 0;
};

struct Totals {
    double actual = 0;
    double calculated = 0;
    double heal = 0;
    double heal_calculated = 0;
    double elemental = 0;
    std::array<TypeTotals, kDamageTypeCount> types{};

    void add_damage(double real, double calculated_damage, std::int32_t type) {
        actual += std::abs(real);
        calculated += std::abs(calculated_damage);
        const int index = damage_type_index(type);
        if (index >= 0) {
            types[static_cast<std::size_t>(index)].actual += std::abs(real);
            types[static_cast<std::size_t>(index)].calculated += std::abs(calculated_damage);
        }
    }

    void add_heal(double real, double calculated_damage) {
        heal += std::abs(real);
        heal_calculated += std::abs(calculated_damage);
    }

    void add_elemental(double value) { elemental += std::abs(value); }
};

inline bool has_totals(const Totals& totals) {
    return totals.actual > 0 || totals.calculated > 0 || totals.heal > 0 ||
           totals.heal_calculated > 0 || totals.elemental > 0;
}

struct OperatorAggregate {
    std::string id;
    std::string name;
    std::set<std::uint32_t> instances;
    Totals totals;
};

struct PlayerAggregate {
    int index = kUnknownPlayer;
    std::string uid;
    std::string name;
    std::string nick_number;
    bool self = false;
    bool online = true;
    bool eliminated = false;
    PlayerStatus status = PlayerStatus::Online;
    double active_time = 0;
    Totals totals;
    std::map<std::string, OperatorAggregate> operators;

    std::string label() const {
        if (!name.empty()) {
            return name;
        }
        return player_slot_label(index);
    }
};

struct View {
    std::map<int, PlayerAggregate> players;
    PlayerAggregate unknown;
    double battle_seconds = 0;
    double last_game_time = 0;
    bool has_data = false;

    PlayerAggregate& ensure_player(int index) {
        if (index < 0) {
            unknown.index = kUnknownPlayer;
            unknown.name = "未归属";
            return unknown;
        }
        auto& player = players[index];
        player.index = index;
        return player;
    }

    const PlayerAggregate* find_player(int index) const {
        if (index < 0) {
            return &unknown;
        }
        const auto found = players.find(index);
        return found == players.end() ? nullptr : &found->second;
    }

    double actual_total() const {
        double total = unknown.totals.actual;
        for (const auto& [id, player] : players) {
            total += player.totals.actual;
        }
        return total;
    }

    double calculated_total() const {
        double total = unknown.totals.calculated;
        for (const auto& [id, player] : players) {
            total += player.totals.calculated;
        }
        return total;
    }

    std::vector<int> sorted_players(bool self_only, std::uint32_t allowed_mask,
                                    bool show_unknown) const {
        std::vector<int> result;
        for (const auto& [id, player] : players) {
            if (self_only && !player.self) {
                continue;
            }
            if (!self_only && !(allowed_mask & player_bit(id))) {
                continue;
            }
            result.push_back(id);
        }
        std::sort(result.begin(), result.end(), [&](int left, int right) {
            const auto& a = players.at(left);
            const auto& b = players.at(right);
            if (a.totals.actual == b.totals.actual) {
                return a.index < b.index;
            }
            return a.totals.actual > b.totals.actual;
        });
        if (show_unknown && has_totals(unknown.totals)) {
            result.push_back(kUnknownPlayer);
        }
        return result;
    }
};

struct RoundViews {
    View main;
    View assist;
};

struct RosterPlayer {
    int index = kUnknownPlayer;
    std::string uid;
    std::string name;
    std::string nick_number;
    bool self = false;
    bool online = true;
    bool eliminated = false;
    PlayerStatus status = PlayerStatus::Online;
};

class StatsModel {
public:
    void apply(const Event& event) {
        if (event.sequence && event.sequence <= last_sequence_) return;
        if (event.sequence) last_sequence_ = event.sequence;
        if (event.kind == EventKind::SessionStart) {
            if (session_id_ == event.session_id && session_id_) return;
            const auto sequence = last_sequence_;
            reset(event.session_id, event.round);
            last_sequence_ = sequence;
            partial_ = event.partial != 0;
            return;
        }
        if (!session_id_ || event.session_id != session_id_) return;
        if ((event.kind == EventKind::Damage || event.kind == EventKind::ElementalBuildup ||
             event.kind == EventKind::Clock) && (ended_ || event.round != current_round_)) return;
        switch (event.kind) {
        case EventKind::SessionStart:
            break;
        case EventKind::RoundStart:
            if (!ended_) start_round(event.round);
            break;
        case EventKind::Roster:
            upsert_roster(event);
            break;
        case EventKind::Coverage:
            coverage_ = static_cast<Coverage>(event.coverage);
            simulated_mask_ = event.simulated_mask;
            collected_mask_ |= simulated_mask_;
            active_mask_ = event.active_mask;
            partial_ = partial_ || event.partial != 0;
            break;
        case EventKind::PlayerStatus:
            update_status(event);
            break;
        case EventKind::Damage:
            add_damage(event, false);
            break;
        case EventKind::ElementalBuildup:
            add_damage(event, true);
            break;
        case EventKind::Clock:
            add_clock(event);
            break;
        case EventKind::Phase:
            if (!ended_ && event.round == current_round_) {
                const std::size_t index = phase_index(event.phase);
                current_phase_ = index < kPhaseCount ? event.phase : BattlePhase::Idle;
                if (current_phase_ == BattlePhase::Idle) battle_active_ = false;
            }
            break;
        case EventKind::BattleEnd:
            battle_active_ = false;
            break;
        case EventKind::SessionEnd:
            if (!ended_) ended_at_ = (std::max)(std::int64_t{0}, event.wall_time);
            ended_ = true;
            battle_active_ = false;
            current_phase_ = BattlePhase::Idle;
            break;
        case EventKind::ModeExit:
            battle_active_ = false;
            current_phase_ = BattlePhase::Idle;
            break;
        case EventKind::Warning:
            warning_ = text(event.message);
            partial_ = true;
            break;
        }
    }

    void clear_pending_round_export() { if (!pending_rounds_.empty()) pending_rounds_.pop_front(); }

    std::uint64_t session_id() const { return session_id_; }
    std::uint32_t current_round() const { return current_round_; }
    Coverage coverage() const { return coverage_; }
    std::uint32_t simulated_mask() const { return simulated_mask_; }
    std::uint32_t active_mask() const { return active_mask_; }
    bool partial() const { return partial_; }
    bool ended() const { return ended_; }
    std::int64_t ended_at() const { return ended_at_; }
    bool battle_active() const { return battle_active_; }
    BattlePhase current_phase() const { return current_phase_; }
    int self_index() const { return self_index_; }
    const std::string& warning() const { return warning_; }

    const View& session(BattlePhase phase = BattlePhase::Main) const {
        const std::size_t index = phase_index(phase);
        return index < kPhaseCount ? session_views_[index] : idle_view_;
    }

    const View& round_view(BattlePhase phase = BattlePhase::Main) const {
        const std::size_t index = phase_index(phase);
        return index < kPhaseCount ? round_views_[index] : idle_view_;
    }

    const View* find_round_view(std::uint32_t round,
                                BattlePhase phase = BattlePhase::Main) const {
        if (!round) return nullptr;
        const std::size_t index = phase_index(phase);
        if (index >= kPhaseCount) return nullptr;
        if (round == current_round_) {
            return &round_views_[index];
        }
        const auto found = completed_rounds_.find(round);
        if (found == completed_rounds_.end()) return nullptr;
        return index == 0 ? &found->second.main : &found->second.assist;
    }

    const RosterPlayer* self() const {
        const auto found = roster_.find(self_index_);
        return found == roster_.end() ? nullptr : &found->second;
    }
    const std::map<int, RosterPlayer>& roster() const { return roster_; }
    const std::map<std::uint32_t, RoundViews>& completed_rounds() const { return completed_rounds_; }
    const RoundViews* pending_round_views() const {
        return pending_rounds_.empty() ? nullptr : &pending_rounds_.front().views;
    }
    const View* pending_round_export(BattlePhase phase = BattlePhase::Main) const {
        if (pending_rounds_.empty()) return nullptr;
        switch (phase) {
        case BattlePhase::Main: return &pending_rounds_.front().views.main;
        case BattlePhase::Assist: return &pending_rounds_.front().views.assist;
        default: return nullptr;
        }
    }
    const View* pending_assist_round_export() const {
        return pending_round_export(BattlePhase::Assist);
    }
    std::uint32_t pending_round_number() const { return pending_rounds_.empty() ? 0 : pending_rounds_.front().round; }
    Coverage pending_round_coverage() const { return pending_rounds_.empty() ? Coverage::Unknown : pending_rounds_.front().coverage; }

    std::uint32_t effective_mask() const {
        return collected_mask_ | player_bit(self_index_);
    }

private:
    static constexpr std::size_t kPhaseCount = 2;

    static constexpr std::size_t phase_index(BattlePhase phase) {
        switch (phase) {
        case BattlePhase::Main: return 0;
        case BattlePhase::Assist: return 1;
        default: return kPhaseCount;
        }
    }

    static View make_view() {
        View view;
        view.ensure_player(kUnknownPlayer);
        return view;
    }

    RoundViews round_views_snapshot() const {
        return {round_views_[0], round_views_[1]};
    }

    void reset(std::uint64_t session, std::uint32_t round) {
        *this = StatsModel{};
        session_id_ = session;
        session_views_[0] = make_view();
        session_views_[1] = make_view();
        start_round(round);
    }

    void start_round(std::uint32_t round) {
        if (ended_ || (current_round_ && round <= current_round_)) return;
        if (current_round_) {
            const RoundViews completed = round_views_snapshot();
            completed_rounds_[current_round_] = completed;
            pending_rounds_.push_back({current_round_, coverage_, completed});
        }
        round_views_[0] = make_view();
        round_views_[1] = make_view();
        current_round_ = round ? round : 1;
        current_phase_ = BattlePhase::Main;
        battle_active_ = false;
        copy_roster_to_round_views();
    }

    void upsert_roster(const Event& event) {
        if (event.player_index < 0) {
            return;
        }
        RosterPlayer row;
        row.index = event.player_index;
        row.uid = text(event.player_uid);
        row.name = text(event.player_name);
        row.nick_number = text(event.nick_number);
        row.self = event.self != 0;
        row.online = event.online != 0;
        row.eliminated = event.eliminated != 0;
        row.status = static_cast<PlayerStatus>(event.status);
        roster_[row.index] = row;
        if (row.self) {
            self_index_ = row.index;
        }
        apply_roster(session_views_[0], row);
        apply_roster(session_views_[1], row);
        apply_roster(round_views_[0], row);
        apply_roster(round_views_[1], row);
    }

    void apply_roster(View& view, const RosterPlayer& row) {
        auto& player = view.ensure_player(row.index);
        player.uid = row.uid;
        player.name = row.name;
        player.nick_number = row.nick_number;
        player.self = row.self;
        player.online = row.online;
        player.eliminated = row.eliminated;
        player.status = row.status;
    }

    void copy_roster_to_round_views() {
        for (const auto& [id, row] : roster_) {
            apply_roster(round_views_[0], row);
            apply_roster(round_views_[1], row);
        }
    }

    void update_status(const Event& event) {
        if (event.player_index < 0) {
            return;
        }
        auto found = roster_.find(event.player_index);
        if (found == roster_.end()) {
            found = roster_.emplace(event.player_index, RosterPlayer{}).first;
            found->second.index = event.player_index;
        }
        RosterPlayer& row = found->second;
        row.online = event.online != 0;
        row.eliminated = event.eliminated != 0;
        row.status = static_cast<PlayerStatus>(event.status);
        apply_roster(session_views_[0], row);
        apply_roster(session_views_[1], row);
        apply_roster(round_views_[0], row);
        apply_roster(round_views_[1], row);
    }

    void add_damage(const Event& event, bool elemental_buildup) {
        const std::size_t index = phase_index(event.phase);
        if (index >= kPhaseCount) return;
        const double real = event.actual_damage;
        const double calculated = event.calculated_damage;
        if (!std::isfinite(real) || !std::isfinite(calculated)) {
            return;
        }
        if (event.player_index >= 0) {
            const auto player = roster_.find(event.player_index);
            if (player != roster_.end() && (player->second.eliminated ||
                (!player->second.online && player->second.status != PlayerStatus::Unknown))) return;
            collected_mask_ |= player_bit(event.player_index);
        }
        add_to(session_views_[index], round_views_[index], event, elemental_buildup);
    }

    void add_to(View& session_view, View& round, const Event& event, bool elemental_buildup) {
        auto add = [&](View& view) {
            PlayerAggregate& player = view.ensure_player(event.player_index);
            if (event.player_index >= 0 && player.name.empty()) {
                const auto roster = roster_.find(event.player_index);
                if (roster != roster_.end()) {
                    apply_roster(view, roster->second);
                }
            }
            std::string id = text(event.actor_id);
            if (id.empty()) {
                id = "unknown";
            }
            PlayerAggregate& updated_player = view.ensure_player(event.player_index);
            OperatorAggregate& actor = updated_player.operators[id];
            actor.id = id;
            actor.name = text(event.actor_name).empty() ? id : text(event.actor_name);
            if (event.card_uid) {
                actor.instances.insert(event.card_uid);
            }
            if (elemental_buildup) {
                actor.totals.add_elemental(event.calculated_damage);
                updated_player.totals.add_elemental(event.calculated_damage);
            } else if (event.damage_type == 4) {
                actor.totals.add_heal(event.actual_damage, event.calculated_damage);
                updated_player.totals.add_heal(event.actual_damage, event.calculated_damage);
            } else {
                actor.totals.add_damage(event.actual_damage, event.calculated_damage, event.damage_type);
                updated_player.totals.add_damage(event.actual_damage, event.calculated_damage,
                                                 event.damage_type);
            }
            view.has_data = true;
        };
        add(session_view);
        add(round);
    }

    void add_clock(const Event& event) {
        if (!(event.delta_time > 0) || !std::isfinite(event.delta_time)) {
            return;
        }
        if (!event.simulated_mask) {
            return;
        }
        const std::size_t index = phase_index(event.phase);
        if (index >= kPhaseCount) {
            return;
        }
        battle_active_ = true;
        View& session_view = session_views_[index];
        View& round = round_views_[index];
        session_view.battle_seconds += event.delta_time;
        round.battle_seconds += event.delta_time;
        for (int player_index = 0; player_index < static_cast<int>(kMaxPlayers); ++player_index) {
            if (!(event.simulated_mask & player_bit(player_index))) {
                continue;
            }
            const auto player = roster_.find(player_index);
            if (player == roster_.end() || !player->second.online || player->second.eliminated) continue;
            auto add_time = [&](View& view) {
                auto& aggregate = view.ensure_player(player_index);
                aggregate.active_time += event.delta_time;
            };
            add_time(session_view);
            add_time(round);
        }
        session_view.last_game_time = event.game_time;
        round.last_game_time = event.game_time;
    }

    std::uint64_t session_id_ = 0;
    std::uint64_t last_sequence_ = 0;
    std::uint32_t current_round_ = 0;
    Coverage coverage_ = Coverage::Unknown;
    std::uint32_t simulated_mask_ = 0;
    std::uint32_t collected_mask_ = 0;
    std::uint32_t active_mask_ = 0;
    bool partial_ = false;
    bool ended_ = false;
    std::int64_t ended_at_ = 0;
    bool battle_active_ = false;
    BattlePhase current_phase_ = BattlePhase::Main;
    int self_index_ = kUnknownPlayer;
    std::string warning_;
    std::map<int, RosterPlayer> roster_;
    std::array<View, kPhaseCount> session_views_{};
    std::array<View, kPhaseCount> round_views_{};
    View idle_view_{};
    struct RoundSnapshot {
        std::uint32_t round;
        Coverage coverage;
        RoundViews views;
    };
    std::deque<RoundSnapshot> pending_rounds_;
    std::map<std::uint32_t, RoundViews> completed_rounds_;
};
inline double per_second(double value, double duration) {
    return duration > 0 ? value / duration : 0.0;
}

inline double share(double value, double total) {
    return total > 0 ? value / total : 0.0;
}

} // namespace ark::stronghold
