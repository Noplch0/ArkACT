#include <cmath>
#include <cstdio>
#include <string>

#include "model.h"
#include "types.h"
#include "capture_policy.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                do {                                                                                    if (!(condition)) {                                                                     std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                   ++g_failures;                                                                   }                                                                               } while (0)

using ark::stronghold::BattlePhase;
using ark::stronghold::Coverage;
using ark::stronghold::Event;
using ark::stronghold::EventKind;
using ark::stronghold::PlayerStatus;
using ark::stronghold::StatsModel;
using ark::stronghold::View;

Event event(EventKind kind, std::uint64_t session = 1, std::uint32_t round = 1) {
    Event value;
    value.kind = kind;
    value.session_id = session;
    value.round = round;
    return value;
}

void roster(StatsModel& model, int index, const char* name, bool self = false) {
    Event value = event(EventKind::Roster);
    value.player_index = index;
    ark::stronghold::copy_text(value.player_uid, sizeof(value.player_uid),
                               "uid" + std::to_string(index));
    ark::stronghold::copy_text(value.player_name, sizeof(value.player_name), name);
    value.self = self ? 1u : 0u;
    model.apply(value);
}

void coverage(StatsModel& model, Coverage value, std::uint32_t simulated,
              std::uint32_t active) {
    Event event_value = event(EventKind::Coverage);
    event_value.coverage = static_cast<std::uint32_t>(value);
    event_value.simulated_mask = simulated;
    event_value.active_mask = active;
    model.apply(event_value);
}

void clock_tick(StatsModel& model, double delta, std::uint32_t mask,
                BattlePhase phase = BattlePhase::Main) {
    Event value = event(EventKind::Clock, model.session_id(), model.current_round());
    value.phase = phase;
    value.delta_time = delta;
    value.simulated_mask = mask;
    model.apply(value);
}

void phase_event(StatsModel& model, BattlePhase phase) {
    Event value = event(EventKind::Phase, model.session_id(), model.current_round());
    value.phase = phase;
    model.apply(value);
}

void damage(StatsModel& model, int player, const char* actor, std::uint32_t card_uid,
            double value, std::int32_t type = 1, bool continuous = false,
            BattlePhase phase = BattlePhase::Main) {
    Event event_value = event(EventKind::Damage, model.session_id(), model.current_round());
    event_value.phase = phase;
    event_value.player_index = player;
    event_value.card_uid = card_uid;
    event_value.actual_damage = value;
    event_value.calculated_damage = value * 1.5;
    event_value.damage_type = type;
    event_value.continuous = continuous ? 1u : 0u;
    ark::stronghold::copy_text(event_value.actor_id, sizeof(event_value.actor_id), actor);
    ark::stronghold::copy_text(event_value.actor_name, sizeof(event_value.actor_name), actor);
    model.apply(event_value);
}

Event unattributed_effect_damage(const StatsModel& model, BattlePhase phase,
                                 double actual, double calculated) {
    Event value = event(EventKind::Damage, model.session_id(), model.current_round());
    value.phase = phase;
    value.player_index = ark::stronghold::kUnknownPlayer;
    value.actual_damage = actual;
    value.calculated_damage = calculated;
    value.damage_type = 1;
    ark::stronghold::copy_text(value.actor_id, sizeof(value.actor_id), "unknown_effect");
    ark::stronghold::copy_text(value.actor_name, sizeof(value.actor_name), "无来源附加伤害");
    return value;
}

void check_unknown_effect_totals(const View& view, double actual, double calculated) {
    CHECK(view.unknown.index == ark::stronghold::kUnknownPlayer);
    CHECK(view.unknown.name == "未归属");
    CHECK(view.unknown.totals.actual == actual);
    CHECK(view.unknown.totals.calculated == calculated);
    const auto actor = view.unknown.operators.find("unknown_effect");
    CHECK(actor != view.unknown.operators.end());
    if (actor == view.unknown.operators.end()) return;
    CHECK(actor->second.id == "unknown_effect");
    CHECK(actor->second.name == "无来源附加伤害");
    CHECK(actor->second.totals.actual == actual);
    CHECK(actor->second.totals.calculated == calculated);
}

void test_four_players_and_duplicate_operators() {
    StatsModel model;
    model.apply(event(EventKind::SessionStart));
    for (int i = 0; i < 4; ++i) {
        roster(model, i, ("player" + std::to_string(i)).c_str(), i == 0);
    }
    coverage(model, Coverage::FullRoom, 0xFu, 0xFu);
    clock_tick(model, 10, 0xFu);
    damage(model, 0, "A", 100, 100);
    damage(model, 1, "A", 101, 200);
    damage(model, 2, "B", 102, 300, 2);
    damage(model, 3, "A", 103, 400, 3);
    damage(model, 0, "A", 104, 50, 1, true);

    CHECK(model.session().players.size() == 4);
    CHECK(model.session().actual_total() == 1050);
    CHECK(model.session().players.at(0).totals.actual == 150);
    CHECK(model.session().players.at(1).totals.actual == 200);
    CHECK(model.session().players.at(0).operators.at("A").instances.size() == 2);
    CHECK(model.session().players.at(1).operators.at("A").totals.actual == 200);
    CHECK(model.session().players.at(0).active_time == 10);
    CHECK(model.effective_mask() == 0xFu);
}

void test_round_vs_cumulative_and_export_marker() {
    StatsModel model;
    model.apply(event(EventKind::SessionStart));
    roster(model, 0, "self", true);
    coverage(model, Coverage::SelfOnly, 1u, 1u);
    model.apply(event(EventKind::RoundStart, 1, 1));
    clock_tick(model, 5, 1u);
    damage(model, 0, "A", 1, 100);
    model.apply(event(EventKind::RoundStart, 1, 2));
    CHECK(model.pending_round_export() != nullptr);
    CHECK(model.pending_round_number() == 1);
    clock_tick(model, 7, 1u);
    damage(model, 0, "B", 2, 300);

    CHECK(model.session().actual_total() == 400);
    CHECK(model.round_view().actual_total() == 300);
    CHECK(model.session().battle_seconds == 12);
    CHECK(model.round_view().battle_seconds == 7);
    model.clear_pending_round_export();
}

void test_offline_freeze_and_self_only() {
    StatsModel model;
    model.apply(event(EventKind::SessionStart));
    roster(model, 0, "self", true);
    roster(model, 1, "mate");
    coverage(model, Coverage::FullRoom, 3u, 3u);
    clock_tick(model, 4, 3u);
    damage(model, 0, "A", 1, 100);
    damage(model, 1, "B", 2, 200);

    Event offline = event(EventKind::PlayerStatus);
    offline.player_index = 1;
    offline.online = 0;
    offline.status = static_cast<std::uint32_t>(PlayerStatus::Disconnected);
    model.apply(offline);
    clock_tick(model, 8, 1u);
    CHECK(model.session().players.at(1).totals.actual == 200);
    CHECK(model.session().players.at(1).active_time == 4);
    CHECK(model.session().players.at(0).active_time == 12);

    coverage(model, Coverage::SelfOnly, 1u, 1u);
    CHECK(model.coverage() == Coverage::SelfOnly);
    CHECK(model.effective_mask() == 3u);
    const auto visible = model.session().sorted_players(true, model.effective_mask(), false);
    CHECK(visible.size() == 1 && visible[0] == 0);
}

void test_heal_elemental_and_unknown() {
    StatsModel model;
    model.apply(event(EventKind::SessionStart));
    roster(model, 0, "self", true);
    coverage(model, Coverage::SelfOnly, 1u, 1u);
    clock_tick(model, 2, 1u);

    Event heal = event(EventKind::Damage);
    heal.player_index = 0;
    heal.damage_type = 4;
    heal.actual_damage = 60;
    heal.calculated_damage = 70;
    ark::stronghold::copy_text(heal.actor_id, sizeof(heal.actor_id), "healer");
    ark::stronghold::copy_text(heal.actor_name, sizeof(heal.actor_name), "healer");
    model.apply(heal);

    Event elemental = event(EventKind::ElementalBuildup);
    elemental.player_index = 0;
    elemental.calculated_damage = 30;
    ark::stronghold::copy_text(elemental.actor_id, sizeof(elemental.actor_id), "caster");
    ark::stronghold::copy_text(elemental.actor_name, sizeof(elemental.actor_name), "caster");
    model.apply(elemental);

    damage(model, -1, "orphan", 9, 90);
    CHECK(model.session().players.at(0).totals.heal == 60);
    CHECK(model.session().players.at(0).operators.at("healer").totals.heal == 60);
    CHECK(model.session().players.at(0).totals.elemental == 30);
    CHECK(model.session().unknown.totals.actual == 90);
    CHECK(model.session().actual_total() == 90);
}

void test_unattributed_effect_damage_retention_and_isolation() {
    StatsModel model;
    Event begin = event(EventKind::SessionStart, 901, 1);
    begin.sequence = 1;
    model.apply(begin);
    roster(model, 0, "self", true);
    roster(model, 1, "mate");
    coverage(model, Coverage::FullRoom, 3u, 3u);
    clock_tick(model, 4, 3u, BattlePhase::Main);
    clock_tick(model, 2, 3u, BattlePhase::Assist);

    damage(model, 0, "A", 100, 100, 1, false, BattlePhase::Main);
    damage(model, 1, "B", 200, 40, 1, false, BattlePhase::Assist);
    Event main_unknown = unattributed_effect_damage(model, BattlePhase::Main, 30, 45);
    main_unknown.sequence = 2;
    model.apply(main_unknown);
    Event assist_unknown = unattributed_effect_damage(model, BattlePhase::Assist, 20, 25);
    assist_unknown.sequence = 3;
    model.apply(assist_unknown);

    check_unknown_effect_totals(model.session(BattlePhase::Main), 30, 45);
    check_unknown_effect_totals(model.round_view(BattlePhase::Main), 30, 45);
    check_unknown_effect_totals(model.session(BattlePhase::Assist), 20, 25);
    check_unknown_effect_totals(model.round_view(BattlePhase::Assist), 20, 25);
    CHECK(model.session(BattlePhase::Main).actual_total() == 130);
    CHECK(model.session(BattlePhase::Main).calculated_total() == 195);
    CHECK(model.session(BattlePhase::Assist).actual_total() == 60);
    CHECK(model.session(BattlePhase::Assist).calculated_total() == 85);
    CHECK(model.session(BattlePhase::Main).players.at(0).totals.actual == 100);
    CHECK(model.session(BattlePhase::Main).players.at(0).operators.count("unknown_effect") == 0);
    CHECK(model.session(BattlePhase::Main).unknown.operators.count("A") == 0);
    CHECK(model.session(BattlePhase::Assist).players.at(1).totals.actual == 40);
    CHECK(model.session(BattlePhase::Assist).players.at(1).operators.count("unknown_effect") == 0);
    CHECK(model.session(BattlePhase::Assist).unknown.operators.count("B") == 0);

    Event next_round = event(EventKind::RoundStart, 901, 2);
    next_round.sequence = 4;
    model.apply(next_round);
    const auto* first_main = model.find_round_view(1, BattlePhase::Main);
    const auto* first_assist = model.find_round_view(1, BattlePhase::Assist);
    CHECK(first_main != nullptr && first_main->actual_total() == 130);
    CHECK(first_main != nullptr && first_main->calculated_total() == 195);
    CHECK(first_main != nullptr && first_main->unknown.totals.actual == 30);
    CHECK(first_main != nullptr && first_main->unknown.totals.calculated == 45);
    CHECK(first_assist != nullptr && first_assist->actual_total() == 60);
    CHECK(first_assist != nullptr && first_assist->calculated_total() == 85);
    CHECK(first_assist != nullptr && first_assist->unknown.totals.actual == 20);
    CHECK(first_assist != nullptr && first_assist->unknown.totals.calculated == 25);

    Event round_two_unknown = unattributed_effect_damage(model, BattlePhase::Main, 7, 9);
    round_two_unknown.sequence = 5;
    model.apply(round_two_unknown);
    check_unknown_effect_totals(model.session(BattlePhase::Main), 37, 54);
    check_unknown_effect_totals(model.round_view(BattlePhase::Main), 7, 9);
    CHECK(model.round_view(BattlePhase::Main).actual_total() == 7);
    CHECK(model.round_view(BattlePhase::Main).calculated_total() == 9);
    CHECK(model.round_view(BattlePhase::Assist).unknown.totals.actual == 0);
    CHECK(model.round_view(BattlePhase::Assist).unknown.totals.calculated == 0);
    CHECK(model.session(BattlePhase::Main).actual_total() == 137);
    CHECK(model.session(BattlePhase::Main).calculated_total() == 204);
    CHECK(first_main != nullptr && first_main->unknown.totals.actual == 30);
    CHECK(first_main != nullptr && first_main->unknown.totals.calculated == 45);

    Event fresh_session = event(EventKind::SessionStart, 902, 1);
    fresh_session.sequence = 6;
    model.apply(fresh_session);
    CHECK(model.session_id() == 902);
    CHECK(model.current_round() == 1);
    CHECK(model.completed_rounds().empty());
    CHECK(model.find_round_view(2, BattlePhase::Main) == nullptr);
    CHECK(model.find_round_view(2, BattlePhase::Assist) == nullptr);
    for (BattlePhase phase : {BattlePhase::Main, BattlePhase::Assist}) {
        CHECK(model.session(phase).actual_total() == 0);
        CHECK(model.session(phase).calculated_total() == 0);
        CHECK(model.session(phase).unknown.totals.actual == 0);
        CHECK(model.session(phase).unknown.totals.calculated == 0);
        CHECK(model.round_view(phase).actual_total() == 0);
        CHECK(model.round_view(phase).calculated_total() == 0);
    }
}

void test_unattributed_effect_damage_sequence_and_same_value_dot() {
    StatsModel model;
    Event begin = event(EventKind::SessionStart, 911, 1);
    begin.sequence = 1;
    model.apply(begin);

    Event first = unattributed_effect_damage(model, BattlePhase::Main, 12, 18);
    first.sequence = 2;
    model.apply(first);
    check_unknown_effect_totals(model.session(BattlePhase::Main), 12, 18);
    check_unknown_effect_totals(model.round_view(BattlePhase::Main), 12, 18);

    model.apply(first);
    check_unknown_effect_totals(model.session(BattlePhase::Main), 12, 18);
    check_unknown_effect_totals(model.round_view(BattlePhase::Main), 12, 18);

    Event same_value_dot = first;
    same_value_dot.sequence = 3;
    same_value_dot.continuous = 1;
    model.apply(same_value_dot);
    check_unknown_effect_totals(model.session(BattlePhase::Main), 24, 36);
    check_unknown_effect_totals(model.round_view(BattlePhase::Main), 24, 36);
}

void test_coverage_detector() {
    using ark::stronghold::coverage_for_masks;
    CHECK(coverage_for_masks(1, 1, 0) == Coverage::SelfOnly);
    CHECK(coverage_for_masks(15, 1, 0) == Coverage::SelfOnly);
    CHECK(coverage_for_masks(15, 15, 0) == Coverage::FullRoom);
    CHECK(coverage_for_masks(15, 3, 0) == Coverage::Partial);
    CHECK(coverage_for_masks(15, 0, 0) == Coverage::Unknown);
    CHECK(coverage_for_masks(15, 6, 0) == Coverage::Partial);
    CHECK(coverage_for_masks(4, 4, 2) == Coverage::SelfOnly);
    CHECK(coverage_for_masks(3, 7, 0) == Coverage::FullRoom);
    CHECK(coverage_for_masks(0, 3, 0) == Coverage::Partial);
}

void test_session_identity() {
    ark::stronghold::SessionTracker tracker;
    using Roster = std::vector<std::pair<int, std::string>>;
    const Roster roster = {{0, "self"}, {1, "mate"}};
    CHECK(tracker.update(42, 100, roster, false));
    CHECK(!tracker.update(42, 100, roster, false)); // next battle/round
    CHECK(!tracker.update(42, 100, {{0, "self"}}, false)); // teammate disappears
    CHECK(!tracker.update(42, 100, roster, false)); // reconnect
    CHECK(tracker.update(42, 100, roster, true)); // manual reset
    CHECK(tracker.update(42, 101, roster, false)); // new data center, same seed
    CHECK(tracker.update(43, 101, roster, false));
    CHECK(tracker.update(43, 101, {{0, "self"}, {1, "different"}}, false));
    tracker.leave();
    CHECK(tracker.update(43, 101, roster, false));
}

void test_confirmed_visibility_and_unknown_status() {
    using ark::stronghold::player_state;
    CHECK(!player_state(1, 1, 2, 3, false).eliminated);
    CHECK(player_state(1, 1, 2, 3, true).eliminated);
    CHECK(player_state(3, 1, 2, 3, true).status == PlayerStatus::Quit);
    CHECK(player_state(2, 1, 2, 3, false).status == PlayerStatus::Disconnected);
    CHECK(player_state(0, 1, 2, 3, false).status == PlayerStatus::Unknown);
    StatsModel model;
    model.apply(event(EventKind::SessionStart));
    for (int i = 0; i < 4; ++i) roster(model, i, "player", i == 0);
    coverage(model, Coverage::Partial, 3, 15);
    CHECK(model.effective_mask() == 3); // Confirmed teammate is visible before first hit.
    coverage(model, Coverage::FullRoom, 15, 15);
    CHECK(model.effective_mask() == 15);
    coverage(model, Coverage::SelfOnly, 1, 15);
    CHECK(model.effective_mask() == 15); // Keep players confirmed earlier in this session.
    StatsModel low_graphics;
    low_graphics.apply(event(EventKind::SessionStart));
    for (int i = 0; i < 4; ++i) roster(low_graphics, i, "player", i == 0);
    coverage(low_graphics, Coverage::SelfOnly, 1, 15);
    CHECK(low_graphics.effective_mask() == 1); // Never confirmed teammates remain excluded.
    Event pending = event(EventKind::PlayerStatus);
    pending.player_index = 0;
    pending.online = 0;
    pending.status = static_cast<unsigned>(PlayerStatus::Unknown);
    model.apply(pending);
    damage(model, 0, "A", 1, 50);
    clock_tick(model, 2, 1);
    CHECK(model.session().players.at(0).totals.actual == 50);
    CHECK(model.session().players.at(0).active_time == 0);
    pending.status = static_cast<unsigned>(PlayerStatus::Quit);
    model.apply(pending);
    damage(model, 0, "A", 1, 100);
    CHECK(model.session().players.at(0).totals.actual == 50);
}

void test_player_slot_labels_and_empty_mask() {
    CHECK(ark::stronghold::player_slot_label(1) == "P1");
    CHECK(ark::stronghold::player_slot_label(2) == "P2");
    CHECK(ark::stronghold::player_slot_label(3) == "P3");
    CHECK(ark::stronghold::player_slot_label(4) == "P4");
    CHECK(ark::stronghold::player_slot_label(0) == "P?");
    CHECK(ark::stronghold::player_slot_label(-1) == "未归属");

    ark::stronghold::View view;
    for (int index = 1; index <= 4; ++index) {
        view.ensure_player(index);
        CHECK(view.players.at(index).label() == ark::stronghold::player_slot_label(index));
    }

    // A zero display mask must show no unconfirmed roster rows.
    CHECK(view.sorted_players(false, 0, false).empty());
    const std::uint32_t raw_slots = ark::stronghold::player_bit(1) |
        ark::stronghold::player_bit(2) | ark::stronghold::player_bit(3) |
        ark::stronghold::player_bit(4);
    CHECK(raw_slots == 0x1Eu);
    const auto visible = view.sorted_players(false, raw_slots, false);
    CHECK(visible.size() == 4 && visible[0] == 1 && visible[3] == 4);
}

void test_duplicate_stale_and_ended_events() {
    StatsModel model;
    Event begin = event(EventKind::SessionStart, 11, 3);
    begin.sequence = 1;
    model.apply(begin);
    CHECK(model.current_round() == 3);
    Event value = event(EventKind::Damage, 11, 3);
    value.actual_damage = 100;
    value.sequence = 2;
    model.apply(value);
    model.apply(value);
    CHECK(model.session().actual_total() == 100);
    model.apply(begin);
    CHECK(model.session().actual_total() == 100);
    value.sequence = 3;
    value.session_id = 10;
    model.apply(value);
    value.sequence = 4;
    value.session_id = 11;
    value.round = 2;
    model.apply(value);
    CHECK(model.session().actual_total() == 100);
    model.apply(event(EventKind::SessionEnd, 11, 3));
    value.sequence = 5;
    value.round = 3;
    model.apply(value);
    CHECK(model.session().actual_total() == 100);
    model.apply(event(EventKind::SessionStart, 12, 1));
    CHECK(model.session().actual_total() == 0);
    CHECK(!model.ended());
    CHECK(ark::stronghold::per_second(10, 0) == 0);
    CHECK(ark::stronghold::share(10, 0) == 0);
}

void test_offline_events_cannot_mutate_totals_or_timer() {
    StatsModel model;
    model.apply(event(EventKind::SessionStart));
    roster(model, 0, "self", true);
    roster(model, 1, "mate");
    clock_tick(model, 2, 3);
    damage(model, 1, "A", 100, 50);
    Event offline = event(EventKind::PlayerStatus);
    offline.player_index = 1;
    offline.online = 0;
    model.apply(offline);
    // Even if an old capture mask arrives, the model must freeze this player.
    clock_tick(model, 10, 3);
    damage(model, 1, "A", 100, 500);
    CHECK(model.session().players.at(1).active_time == 2);
    CHECK(model.session().players.at(1).totals.actual == 50);
    CHECK(model.session().players.at(0).active_time == 12);
}

void test_multiple_pending_rounds_and_duplicate_round_start() {
    StatsModel model;
    model.apply(event(EventKind::SessionStart));
    damage(model, 0, "A", 1, 10);
    model.apply(event(EventKind::RoundStart, 1, 1));
    CHECK(model.round_view().actual_total() == 10);
    model.apply(event(EventKind::RoundStart, 1, 2));
    damage(model, 0, "A", 1, 20);
    model.apply(event(EventKind::RoundStart, 1, 3));
    CHECK(model.pending_round_number() == 1);
    CHECK(model.pending_round_export()->actual_total() == 10);
    model.clear_pending_round_export();
    CHECK(model.pending_round_number() == 2);
    CHECK(model.pending_round_export()->actual_total() == 20);
    model.clear_pending_round_export();
    CHECK(!model.pending_round_export());
    CHECK(model.session().actual_total() == 30);
}

void test_phase_timing_wait_and_two_player_assist() {
    CHECK(Event{}.phase == BattlePhase::Main);
    CHECK(Event{}.wall_time == 0);

    StatsModel model;
    model.apply(event(EventKind::SessionStart));
    for (int index = 0; index < 4; ++index) {
        roster(model, index, ("player" + std::to_string(index)).c_str(), index == 0);
    }
    coverage(model, Coverage::FullRoom, 0xFu, 0xFu);
    phase_event(model, BattlePhase::Main);
    clock_tick(model, 5, 0xFu, BattlePhase::Main);
    // Slot zero has finished its own wave, so only the other three continue on the clock.
    clock_tick(model, 9, 0xEu, BattlePhase::Main);
    clock_tick(model, 7, 0, BattlePhase::Main);
    CHECK(model.session(BattlePhase::Main).battle_seconds == 14);
    CHECK(model.round_view(BattlePhase::Main).battle_seconds == 14);
    CHECK(model.session(BattlePhase::Main).players.at(0).active_time == 5);
    for (int index = 1; index < 4; ++index) {
        CHECK(model.session(BattlePhase::Main).players.at(index).active_time == 14);
    }

    phase_event(model, BattlePhase::Assist);
    CHECK(model.current_phase() == BattlePhase::Assist);
    const std::uint32_t assist_mask = ark::stronghold::player_bit(0) |
        ark::stronghold::player_bit(2);
    clock_tick(model, 3, assist_mask, BattlePhase::Assist);
    clock_tick(model, 11, 0, BattlePhase::Assist);
    CHECK(model.session(BattlePhase::Assist).battle_seconds == 3);
    CHECK(model.session(BattlePhase::Assist).players.at(0).active_time == 3);
    CHECK(model.session(BattlePhase::Assist).players.at(1).active_time == 0);
    CHECK(model.session(BattlePhase::Assist).players.at(2).active_time == 3);
    CHECK(model.session(BattlePhase::Assist).players.at(3).active_time == 0);
    CHECK(model.round_view(BattlePhase::Assist).battle_seconds == 3);
    CHECK(model.session(BattlePhase::Main).battle_seconds == 14);

    phase_event(model, BattlePhase::Idle);
    CHECK(model.current_phase() == BattlePhase::Idle);
    CHECK(!model.battle_active());
    clock_tick(model, 4, 0xFu, BattlePhase::Idle);
    CHECK(model.session(BattlePhase::Idle).battle_seconds == 0);
    CHECK(model.session(BattlePhase::Main).battle_seconds == 14);
}

void test_phase_views_round_snapshots_and_exit_freeze() {
    StatsModel model;
    model.apply(event(EventKind::SessionStart));
    for (int index = 1; index <= 4; ++index) {
        roster(model, index, ("player" + std::to_string(index)).c_str(), index == 1);
    }
    const std::uint32_t room_mask = 0x1Eu;
    coverage(model, Coverage::FullRoom, room_mask, room_mask);
    clock_tick(model, 4, room_mask, BattlePhase::Main);
    damage(model, 1, "A", 101, 100, 1, false, BattlePhase::Main);
    damage(model, 2, "B", 102, 40, 2, false, BattlePhase::Main);
    damage(model, 3, "A", 103, 15, 3, false, BattlePhase::Main);
    damage(model, 4, "C", 104, 5, 1, false, BattlePhase::Main);
    damage(model, 1, "idle", 199, 999, 1, false, BattlePhase::Idle);

    Event main_elemental = event(EventKind::ElementalBuildup, model.session_id(),
                                 model.current_round());
    main_elemental.player_index = 1;
    main_elemental.calculated_damage = 10;
    ark::stronghold::copy_text(main_elemental.actor_id, sizeof(main_elemental.actor_id), "A");
    model.apply(main_elemental);

    phase_event(model, BattlePhase::Assist);
    const std::uint32_t assist_mask = ark::stronghold::player_bit(1) |
        ark::stronghold::player_bit(3);
    clock_tick(model, 2, assist_mask, BattlePhase::Assist);
    damage(model, 1, "A", 101, 70, 1, false, BattlePhase::Assist);
    damage(model, 3, "A", 103, 30, 1, false, BattlePhase::Assist);
    Event assist_elemental = event(EventKind::ElementalBuildup, model.session_id(),
                                   model.current_round());
    assist_elemental.phase = BattlePhase::Assist;
    assist_elemental.player_index = 3;
    assist_elemental.calculated_damage = 25;
    ark::stronghold::copy_text(assist_elemental.actor_id, sizeof(assist_elemental.actor_id), "A");
    model.apply(assist_elemental);

    Event p3_quit = event(EventKind::PlayerStatus);
    p3_quit.player_index = 3;
    p3_quit.online = 0;
    p3_quit.status = static_cast<std::uint32_t>(PlayerStatus::Quit);
    model.apply(p3_quit);
    // A stale assist mask must not add time after the participant has left.
    clock_tick(model, 5, assist_mask, BattlePhase::Assist);
    CHECK(model.session(BattlePhase::Assist).players.at(1).active_time == 7);
    CHECK(model.session(BattlePhase::Assist).players.at(3).active_time == 2);
    CHECK(model.session(BattlePhase::Assist).battle_seconds == 7);

    phase_event(model, BattlePhase::Main);
    Event p2_disconnect = event(EventKind::PlayerStatus);
    p2_disconnect.player_index = 2;
    p2_disconnect.online = 0;
    p2_disconnect.status = static_cast<std::uint32_t>(PlayerStatus::Disconnected);
    model.apply(p2_disconnect);
    clock_tick(model, 3, room_mask, BattlePhase::Main);
    CHECK(model.session(BattlePhase::Main).players.at(1).active_time == 7);
    CHECK(model.session(BattlePhase::Main).players.at(2).active_time == 4);
    CHECK(model.session(BattlePhase::Main).players.at(3).active_time == 4);
    CHECK(model.session(BattlePhase::Main).players.at(4).active_time == 7);

    CHECK(model.session(BattlePhase::Main).actual_total() == 160);
    CHECK(model.session(BattlePhase::Assist).actual_total() == 100);
    CHECK(model.round_view(BattlePhase::Main).actual_total() == 160);
    CHECK(model.round_view(BattlePhase::Assist).actual_total() == 100);
    CHECK(model.session(BattlePhase::Main).players.at(1).totals.elemental == 10);
    CHECK(model.session(BattlePhase::Assist).players.at(3).totals.elemental == 25);
    CHECK(model.session(BattlePhase::Idle).actual_total() == 0);
    CHECK(model.round_view(BattlePhase::Idle).actual_total() == 0);

    model.apply(event(EventKind::RoundStart, model.session_id(), 2));
    const auto first = model.completed_rounds().find(1);
    CHECK(first != model.completed_rounds().end());
    CHECK(first->second.main.actual_total() == 160);
    CHECK(first->second.assist.actual_total() == 100);
    CHECK(first->second.main.battle_seconds == 7);
    CHECK(first->second.assist.battle_seconds == 7);
    CHECK(first->second.main.players.at(2).active_time == 4);
    CHECK(first->second.assist.players.at(3).active_time == 2);
    CHECK(model.pending_round_export(BattlePhase::Main) != nullptr);
    CHECK(model.pending_round_export(BattlePhase::Main)->actual_total() == 160);
    CHECK(model.pending_assist_round_export() != nullptr);
    CHECK(model.pending_assist_round_export()->actual_total() == 100);
    CHECK(model.pending_round_views() != nullptr);
    CHECK(model.pending_round_export(BattlePhase::Idle) == nullptr);
    CHECK(model.round_view(BattlePhase::Main).actual_total() == 0);
    CHECK(model.round_view(BattlePhase::Assist).actual_total() == 0);
    CHECK(model.current_phase() == BattlePhase::Main);

    // A timing-only round still produces a completed snapshot.
    clock_tick(model, 2, room_mask, BattlePhase::Main);
    model.apply(event(EventKind::RoundStart, model.session_id(), 3));
    const auto second = model.completed_rounds().find(2);
    CHECK(second != model.completed_rounds().end());
    CHECK(second->second.main.battle_seconds == 2);
    CHECK(second->second.main.actual_total() == 0);
    CHECK(second->second.assist.battle_seconds == 0);
}

void test_round_history_lookup_and_lifecycle() {
    StatsModel model;
    constexpr std::uint64_t session = 900;
    model.apply(event(EventKind::SessionStart, session, 1));
    roster(model, 0, "self", true);
    roster(model, 1, "mate");
    coverage(model, Coverage::FullRoom, 3u, 3u);

    damage(model, 0, "A", 101, 100, 1, false, BattlePhase::Main);
    damage(model, 1, "B", 102, 40, 1, false, BattlePhase::Assist);
    model.apply(event(EventKind::RoundStart, session, 2));

    damage(model, 0, "A", 201, 200, 1, false, BattlePhase::Main);
    damage(model, 1, "B", 202, 70, 1, false, BattlePhase::Assist);
    model.apply(event(EventKind::RoundStart, session, 3));

    damage(model, 0, "A", 301, 300, 1, false, BattlePhase::Main);
    phase_event(model, BattlePhase::Assist);
    damage(model, 1, "B", 302, 90, 1, false, BattlePhase::Assist);

    const auto check_history = [&] {
        const auto* first_main = model.find_round_view(1, BattlePhase::Main);
        const auto* first_assist = model.find_round_view(1, BattlePhase::Assist);
        const auto* second_main = model.find_round_view(2, BattlePhase::Main);
        const auto* second_assist = model.find_round_view(2, BattlePhase::Assist);
        const auto* current_main = model.find_round_view(3, BattlePhase::Main);
        const auto* current_assist = model.find_round_view(3, BattlePhase::Assist);
        CHECK(first_main != nullptr && first_main->actual_total() == 100);
        CHECK(first_assist != nullptr && first_assist->actual_total() == 40);
        CHECK(second_main != nullptr && second_main->actual_total() == 200);
        CHECK(second_assist != nullptr && second_assist->actual_total() == 70);
        CHECK(current_main != nullptr && current_main->actual_total() == 300);
        CHECK(current_assist != nullptr && current_assist->actual_total() == 90);
    };

    check_history();
    CHECK(model.current_round() == 3);
    CHECK(model.session(BattlePhase::Main).actual_total() == 600);
    CHECK(model.session(BattlePhase::Assist).actual_total() == 200);
    CHECK(model.round_view(BattlePhase::Main).actual_total() == 300);
    CHECK(model.round_view(BattlePhase::Assist).actual_total() == 90);

    CHECK(model.find_round_view(0, BattlePhase::Main) == nullptr);
    CHECK(model.find_round_view(0, BattlePhase::Assist) == nullptr);
    CHECK(model.find_round_view(4, BattlePhase::Main) == nullptr);
    CHECK(model.find_round_view(99, BattlePhase::Assist) == nullptr);
    CHECK(model.find_round_view(1, BattlePhase::Idle) == nullptr);
    CHECK(model.find_round_view(3, BattlePhase::Idle) == nullptr);

    model.apply(event(EventKind::ModeExit, session, 3));
    CHECK(model.current_phase() == BattlePhase::Idle);
    check_history();

    Event end = event(EventKind::SessionEnd, session, 3);
    end.wall_time = 1763201400;
    model.apply(end);
    CHECK(model.ended());
    CHECK(model.current_round() == 3);
    check_history();

    model.apply(event(EventKind::SessionStart, session + 1, 1));
    CHECK(!model.ended());
    CHECK(model.current_round() == 1);
    CHECK(model.completed_rounds().empty());
    const auto* fresh_main = model.find_round_view(1, BattlePhase::Main);
    const auto* fresh_assist = model.find_round_view(1, BattlePhase::Assist);
    CHECK(fresh_main != nullptr && fresh_main->actual_total() == 0 && !fresh_main->has_data);
    CHECK(fresh_assist != nullptr && fresh_assist->actual_total() == 0 && !fresh_assist->has_data);
    CHECK(model.find_round_view(2, BattlePhase::Main) == nullptr);
    CHECK(model.find_round_view(3, BattlePhase::Assist) == nullptr);
    CHECK(model.session(BattlePhase::Main).actual_total() == 0);
    CHECK(model.session(BattlePhase::Assist).actual_total() == 0);
}

void test_session_end_wall_time_lock_and_reset() {
    StatsModel model;
    model.apply(event(EventKind::SessionStart, 77, 5));
    CHECK(model.ended_at() == 0);
    phase_event(model, BattlePhase::Assist);
    CHECK(model.current_phase() == BattlePhase::Assist);

    Event end = event(EventKind::SessionEnd, 77, 5);
    end.wall_time = 1763201400;
    model.apply(end);
    CHECK(model.ended());
    CHECK(model.ended_at() == 1763201400);
    CHECK(model.current_phase() == BattlePhase::Idle);
    end.wall_time = 1763201999;
    model.apply(end);
    CHECK(model.ended_at() == 1763201400);

    // Late phase/round events cannot reopen a closed session.
    phase_event(model, BattlePhase::Main);
    CHECK(model.current_phase() == BattlePhase::Idle);
    model.apply(event(EventKind::RoundStart, 77, 6));
    CHECK(model.current_round() == 5);

    model.apply(event(EventKind::SessionStart, 78, 1));
    CHECK(!model.ended());
    CHECK(model.ended_at() == 0);
    CHECK(model.current_round() == 1);
    CHECK(model.current_phase() == BattlePhase::Main);
    CHECK(model.completed_rounds().empty());

    Event zero_end = event(EventKind::SessionEnd, 78, 1);
    model.apply(zero_end);
    CHECK(model.ended());
    CHECK(model.ended_at() == 0);
    zero_end.wall_time = 123;
    model.apply(zero_end);
    CHECK(model.ended_at() == 0);

    model.apply(event(EventKind::SessionStart, 79, 1));
    Event next_end = event(EventKind::SessionEnd, 79, 1);
    next_end.wall_time = 456;
    model.apply(next_end);
    CHECK(model.ended_at() == 456);
}
void test_event_queue_overrun_and_sequence() {
    ark::stronghold::EventQueue queue(2);
    Event first = event(EventKind::Damage);
    first.actual_damage = 1;
    queue.publish(first);
    queue.publish(first);
    queue.publish(first);
    Event out[2]{};
    bool overrun = false;
    bool partial = false;
    const std::size_t count = queue.read(0, out, 2, &overrun, &partial);
    CHECK(count == 2);
    CHECK(overrun);
    CHECK(partial);
    CHECK(out[0].sequence + 1 == out[1].sequence);
    queue.read(2, out, 2, &overrun, &partial);
    CHECK(!overrun && !partial); // Evicting already consumed events is not data loss.
}

struct TestCase {
    const char* name;
    void (*run)();
};

const TestCase kCases[] = {
    {"four players / duplicate operators", test_four_players_and_duplicate_operators},
    {"round vs cumulative", test_round_vs_cumulative_and_export_marker},
    {"offline freeze / self only", test_offline_freeze_and_self_only},
    {"heal / elemental / unknown", test_heal_elemental_and_unknown},
    {"unattributed effect retention / isolation", test_unattributed_effect_damage_retention_and_isolation},
    {"unattributed effect sequence / same-value DoT", test_unattributed_effect_damage_sequence_and_same_value_dot},
    {"coverage detector", test_coverage_detector},
    {"session identity / round changes", test_session_identity},
    {"confirmed visibility / player death / unknown status", test_confirmed_visibility_and_unknown_status},
    {"player slot labels / empty mask", test_player_slot_labels_and_empty_mask},
    {"duplicate / stale / ended events", test_duplicate_stale_and_ended_events},
    {"offline rejects stale damage / clock", test_offline_events_cannot_mutate_totals_or_timer},
    {"pending round snapshots", test_multiple_pending_rounds_and_duplicate_round_start},
    {"phase timing / wait / assist participants", test_phase_timing_wait_and_two_player_assist},
    {"phase snapshots / elemental separation / exit freeze", test_phase_views_round_snapshots_and_exit_freeze},
    {"round history lookup / lifecycle", test_round_history_lookup_and_lifecycle},
    {"ended_at first lock / session reset", test_session_end_wall_time_lock_and_reset},
    {"event queue overrun", test_event_queue_overrun_and_sequence},
};

} // namespace

int main() {
    for (const TestCase& test : kCases) {
        const int before = g_failures;
        test.run();
        std::printf("%s %s\n", g_failures == before ? "PASS" : "FAIL", test.name);
    }
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("all stronghold model tests passed");
    return 0;
}
