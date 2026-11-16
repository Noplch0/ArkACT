#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "model.h"

namespace ark::stronghold {

// UI selection never mutates the captured model. Round 0 follows the latest
// round; selecting a completed round pins that snapshot as new rounds arrive.
struct ViewSelection {
    bool whole_session = true;
    BattlePhase phase = BattlePhase::Main;
    std::uint32_t selected_round = 0;

    void reset() { *this = ViewSelection{}; }
    void toggle_scope() { whole_session = !whole_session; }
    void toggle_phase() {
        phase = phase == BattlePhase::Main ? BattlePhase::Assist : BattlePhase::Main;
    }
    const char* scope_label() const { return whole_session ? "整局" : "本轮"; }
    const char* phase_label() const { return phase == BattlePhase::Assist ? "协防" : "战斗"; }
    std::string caption() const { return std::string(scope_label()) + "-" + phase_label(); }
    std::uint32_t round(const StatsModel& model) const {
        return selected_round ? selected_round : model.current_round();
    }
    bool latest(const StatsModel& model) const { return round(model) == model.current_round(); }
    bool live(const StatsModel& model, bool in_mode) const {
        return in_mode && !model.ended() && (whole_session || latest(model));
    }
    const View& view(const StatsModel& model) const {
        if (whole_session) return model.session(phase);
        const View* found = model.find_round_view(round(model), phase);
        static const View empty;
        return found ? *found : empty;
    }
    std::vector<std::uint32_t> rounds(const StatsModel& model) const {
        std::vector<std::uint32_t> values;
        for (const auto& [number, snapshot] : model.completed_rounds()) {
            (void)snapshot;
            values.push_back(number);
        }
        if (model.current_round() &&
            std::find(values.begin(), values.end(), model.current_round()) == values.end()) {
            values.push_back(model.current_round());
        }
        std::sort(values.begin(), values.end());
        return values;
    }
    void select(std::uint32_t number, const StatsModel& model) {
        if (!model.find_round_view(number)) return;
        selected_round = number == model.current_round() ? 0 : number;
        whole_session = false;
    }
    bool previous(const StatsModel& model) {
        const auto values = rounds(model);
        const auto number = whole_session ? model.current_round() : round(model);
        const auto current = std::lower_bound(values.begin(), values.end(), number);
        if (current == values.begin()) return false;
        select(*std::prev(current), model);
        return true;
    }
    bool next(const StatsModel& model) {
        const auto values = rounds(model);
        const auto number = whole_session ? model.current_round() : round(model);
        const auto following = std::upper_bound(values.begin(), values.end(), number);
        if (following == values.end()) return false;
        select(*following, model);
        return true;
    }
};

inline bool statistics_visible(const StatsModel& model, bool in_mode) {
    // The model keeps only the current / most recently completed simulation.
    return in_mode || model.session_id() != 0;
}

} // namespace ark::stronghold
