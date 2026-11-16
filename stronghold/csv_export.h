#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "model.h"

namespace ark::stronghold {

inline constexpr int kCsvDecimalPlaces = 4;
inline constexpr std::array<std::string_view, 15> kExportHeaders = {
    "阶段", "维度", "轮次", "玩家槽位", "玩家", "干员", "实例数",
    "实际伤害", "战斗时间(秒)", "DPS", "伤害占比", "计算伤害",
    "治疗量", "HPS", "元素损伤"};

// A report sheet owns independent main-battle and assist-battle views. Keeping the
// views separate is important: assist damage, elapsed time and DPS must never leak
// into the ordinary battle denominator.
struct WorkbookSheet {
    std::string name;
    std::uint32_t round = 0;
    View main;
    View assist;
};

struct ExportRow {
    std::string phase;
    std::string dimension;
    std::uint32_t round = 0;
    int player_index = kUnknownPlayer;
    std::string slot;
    std::string player;
    std::string unit;
    std::size_t instances = 1;
    double actual = 0;
    double duration = 0;
    double dps = 0;
    double share = 0;
    double calculated = 0;
    double heal = 0;
    double hps = 0;
    double elemental = 0;
};

inline std::string utf8_from_path_checked(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

inline std::string csv_escape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }
    std::string out = "\"";
    for (char c : value) {
        out += c == '"' ? "\"\"" : std::string(1, c);
    }
    out += '"';
    return out;
}

inline std::filesystem::path utf8_path(const std::string& value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

inline std::string path_utf8(const std::filesystem::path& path) {
    return utf8_from_path_checked(path);
}

inline const char* coverage_name(Coverage coverage) {
    switch (coverage) {
    case Coverage::FullRoom: return "全房间";
    case Coverage::SelfOnly: return "仅本机";
    case Coverage::Partial: return "部分采集";
    default: return "未知";
    }
}

inline std::string status_name(const PlayerAggregate& player) {
    switch (player.status) {
    case PlayerStatus::Disconnected: return "掉线";
    case PlayerStatus::Quit: return "中途退出";
    case PlayerStatus::Eliminated: return "淘汰";
    case PlayerStatus::Unknown: return "状态待确认";
    default: return player.online ? "在线" : "掉线";
    }
}

inline std::string player_export_name(const PlayerAggregate& player) {
    if (player.index < 0) return "未归属";
    std::string name = player.name;
    if (name.empty()) name = player.label();
    if (name.empty()) name = player.index < 0 ? "未归属" : player_slot_label(player.index);
    return name + "(" + status_name(player) + ")";
}

inline std::string player_slot_export(const PlayerAggregate& player) {
    return player.index < 0 ? "?" : player_slot_label(player.index);
}

inline std::string player_label(const PlayerAggregate& player) {
    if (player.index < 0) return "未归属";
    std::string label = player_slot_label(player.index);
    if (!player.name.empty()) label += " " + player.name;
    return label;
}

inline bool include_player_in_export(const PlayerAggregate& player) {
    // Roster rows that were never simulated or observed are intentionally omitted.
    // A real participant that dealt zero damage is still retained when the game
    // supplied a clock sample for that player.
    return player.active_time > 0 || has_totals(player.totals) ||
           !player.operators.empty();
}

inline std::string operator_export_name(const OperatorAggregate& actor) {
    if (actor.name.empty() || actor.name == actor.id) return "未知干员";
    return actor.name;
}

inline std::vector<const PlayerAggregate*> sorted_export_players(const View& view) {
    std::vector<const PlayerAggregate*> players;
    players.reserve(view.players.size());
    for (const auto& [index, player] : view.players) {
        (void)index;
        if (include_player_in_export(player)) players.push_back(&player);
    }
    std::sort(players.begin(), players.end(), [](const auto* left, const auto* right) {
        if (left->totals.actual != right->totals.actual) {
            return left->totals.actual > right->totals.actual;
        }
        return left->index < right->index;
    });
    return players;
}

inline void append_phase_rows(std::vector<ExportRow>& rows, const char* phase, std::uint32_t round,
                              const View& view, bool include_operators) {
    const double damage_total = view.actual_total();
    auto make_base = [&](const PlayerAggregate& player) {
        ExportRow row;
        row.phase = phase;
        row.round = round;
        row.player_index = player.index;
        row.slot = player_slot_export(player);
        row.player = player_export_name(player);
        row.duration = player.index < 0 ? view.battle_seconds : player.active_time;
        return row;
    };

    for (const PlayerAggregate* player : sorted_export_players(view)) {
        ExportRow total = make_base(*player);
        total.dimension = "玩家";
        total.unit = "玩家合计";
        total.instances = 1;
        total.actual = player->totals.actual;
        total.dps = per_second(total.actual, total.duration);
        total.share = share(total.actual, damage_total);
        total.calculated = player->totals.calculated;
        total.heal = player->totals.heal;
        total.hps = per_second(total.heal, total.duration);
        total.elemental = player->totals.elemental;
        rows.push_back(std::move(total));

        if (!include_operators) continue;
        std::vector<const OperatorAggregate*> operators;
        operators.reserve(player->operators.size());
        for (const auto& [id, actor] : player->operators) {
            (void)id;
            operators.push_back(&actor);
        }
        std::sort(operators.begin(), operators.end(), [](const auto* left, const auto* right) {
            if (left->totals.actual != right->totals.actual) {
                return left->totals.actual > right->totals.actual;
            }
            if (left->name != right->name) return left->name < right->name;
            return left->id < right->id;
        });
        for (const OperatorAggregate* actor : operators) {
            ExportRow row = make_base(*player);
            row.dimension = "干员";
            row.unit = operator_export_name(*actor);
            row.instances = (std::max)(std::size_t{1}, actor->instances.size());
            row.actual = actor->totals.actual;
            row.dps = per_second(row.actual, row.duration);
            row.share = share(row.actual, damage_total);
            row.calculated = actor->totals.calculated;
            row.heal = actor->totals.heal;
            row.hps = per_second(row.heal, row.duration);
            row.elemental = actor->totals.elemental;
            rows.push_back(std::move(row));
        }
    }

    if (has_totals(view.unknown.totals) || !view.unknown.operators.empty()) {
        ExportRow total = make_base(view.unknown);
        total.dimension = "玩家";
        total.unit = "未归属合计";
        total.instances = 1;
        total.actual = view.unknown.totals.actual;
        total.dps = per_second(total.actual, total.duration);
        total.share = share(total.actual, damage_total);
        total.calculated = view.unknown.totals.calculated;
        total.heal = view.unknown.totals.heal;
        total.hps = per_second(total.heal, total.duration);
        total.elemental = view.unknown.totals.elemental;
        rows.push_back(std::move(total));
        if (include_operators) {
            for (const auto& [id, actor] : view.unknown.operators) {
                (void)id;
                ExportRow row = make_base(view.unknown);
                row.dimension = "干员";
                row.unit = operator_export_name(actor);
                row.instances = (std::max)(std::size_t{1}, actor.instances.size());
                row.actual = actor.totals.actual;
                row.dps = per_second(row.actual, row.duration);
                row.share = share(row.actual, damage_total);
                row.calculated = actor.totals.calculated;
                row.heal = actor.totals.heal;
                row.hps = per_second(row.heal, row.duration);
                row.elemental = actor.totals.elemental;
                rows.push_back(std::move(row));
            }
        }
    }
}

inline std::vector<ExportRow> build_export_rows(const WorkbookSheet& sheet,
                                                bool include_operators = true) {
    std::vector<ExportRow> rows;
    append_phase_rows(rows, "主战", sheet.round, sheet.main, include_operators);
    append_phase_rows(rows, "协防", sheet.round, sheet.assist, include_operators);
    return rows;
}

inline bool csv_formula_prefix_required(const std::string& value) {
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char ch = static_cast<unsigned char>(value[index]);
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++index;
            continue;
        }
        return ch == '=' || ch == '+' || ch == '-' || ch == '@';
    }
    return false;
}

inline std::string csv_safe_text(const std::string& value) {
    return csv_formula_prefix_required(value) ? "'" + value : value;
}

inline std::string csv_text_cell(const std::string& value) {
    return csv_escape(csv_safe_text(value));
}

inline std::string csv_number(double value) {
    if (!std::isfinite(value)) value = 0;
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(kCsvDecimalPlaces) << value;
    return stream.str();
}

inline std::string csv_percent(double fraction) {
    if (!std::isfinite(fraction)) fraction = 0;
    return csv_number(fraction * 100.0) + "%";
}

struct CsvExportPaths {
    std::string summary;
    std::string round;
};

inline std::string round_csv_suffix(std::uint32_t round) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "-r%02u.csv", round);
    return buffer;
}

inline bool write_csv_file(const std::string& root, const std::string& filename,
                           const WorkbookSheet& sheet, bool include_operators,
                           std::string* out_path) {
    if (filename.empty()) return false;
    const std::filesystem::path relative = utf8_path(filename);
    if (relative.is_absolute() || relative.has_root_path() || relative.has_parent_path()) return false;

    std::error_code error;
    const std::filesystem::path directory = utf8_path(root) / "exports";
    std::filesystem::create_directories(directory, error);
    if (error) return false;
    const std::filesystem::path path = directory / relative;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream << "\xEF\xBB\xBF";
    for (std::size_t index = 0; index < kExportHeaders.size(); ++index) {
        if (index) stream << ',';
        stream << kExportHeaders[index];
    }
    stream << '\n';

    for (const ExportRow& row : build_export_rows(sheet, include_operators)) {
        stream << csv_text_cell(row.phase) << ',' << csv_text_cell(row.dimension) << ','
               << row.round << ',' << csv_text_cell(row.slot)
               << ',' << csv_text_cell(row.player) << ',' << csv_text_cell(row.unit) << ','
               << row.instances << ',' << csv_number(row.actual) << ','
               << csv_number(row.duration) << ',' << csv_number(row.dps) << ','
               << csv_percent(row.share) << ',' << csv_number(row.calculated) << ','
               << csv_number(row.heal) << ',' << csv_number(row.hps) << ','
               << csv_number(row.elemental) << '\n';
    }
    stream.flush();
    if (!stream) return false;
    stream.close();
    if (!stream) return false;
    if (out_path) *out_path = path_utf8(path);
    return true;
}

inline bool write_csv_file(const std::string& root, const std::string& filename,
                           const View& view, std::uint32_t round, Coverage,
                           bool include_operators, std::string* out_path) {
    WorkbookSheet sheet;
    sheet.name = "第" + std::to_string(round) + "轮";
    sheet.round = round;
    sheet.main = view;
    return write_csv_file(root, filename, sheet, include_operators, out_path);
}

inline std::optional<CsvExportPaths> export_reports(const std::string& root, std::uint64_t pid,
                                                    std::uint64_t session, std::uint32_t round,
                                                    const View& summary, const View& current_round,
                                                    Coverage coverage) {
    (void)coverage;
    const std::string base = "stronghold-" + std::to_string(pid) + "-" + std::to_string(session);
    CsvExportPaths paths;
    if (!write_csv_file(root, base + "-summary.csv", summary, round, coverage, true,
                        &paths.summary)) {
        return std::nullopt;
    }
    if (!write_csv_file(root, base + round_csv_suffix(round), current_round, round,
                        coverage, true, &paths.round)) {
        return std::nullopt;
    }
    return paths;
}

} // namespace ark::stronghold
