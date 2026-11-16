#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "csv_export.h"
#include "model.h"
#include "types.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                 do {                                                                                    if (!(condition)) {                                                                     std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                   ++g_failures;                                                                   }                                                                               } while (0)

using namespace ark::stronghold;

Event base(EventKind kind) {
    Event event;
    event.kind = kind;
    event.session_id = 7;
    event.round = 3;
    return event;
}

std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::string first_line(const std::string& text) {
    const std::size_t newline = text.find('\n');
    return newline == std::string::npos ? text : text.substr(0, newline);
}

StatsModel make_model() {
    StatsModel model;
    model.apply(base(EventKind::SessionStart));

    Event player = base(EventKind::Roster);
    player.player_index = 1;
    copy_text(player.player_uid, sizeof(player.player_uid), "hidden-uid");
    copy_text(player.player_name, sizeof(player.player_name), "测试玩家");
    copy_text(player.nick_number, sizeof(player.nick_number), "123456789");
    player.self = 1;
    model.apply(player);

    Event player2 = player;
    player2.player_index = 2;
    copy_text(player2.player_uid, sizeof(player2.player_uid), "remote-uid");
    copy_text(player2.player_name, sizeof(player2.player_name), "队友");
    copy_text(player2.nick_number, sizeof(player2.nick_number), "987654321");
    player2.self = 0;
    model.apply(player2);

    Event uncollected = player2;
    uncollected.player_index = 3;
    copy_text(uncollected.player_name, sizeof(uncollected.player_name), "未采集队友");
    model.apply(uncollected);

    Event coverage = base(EventKind::Coverage);
    coverage.coverage = static_cast<std::uint32_t>(Coverage::FullRoom);
    coverage.simulated_mask = player_bit(1) | player_bit(2);
    coverage.active_mask = player_bit(1) | player_bit(2);
    model.apply(coverage);

    Event clock = base(EventKind::Clock);
    clock.delta_time = 10;
    clock.simulated_mask = player_bit(1) | player_bit(2);
    model.apply(clock);

    Event damage = base(EventKind::Damage);
    damage.player_index = 1;
    damage.actual_damage = 1234.5;
    damage.calculated_damage = 1500;
    damage.damage_type = 1;
    damage.card_uid = 12;
    copy_text(damage.actor_id, sizeof(damage.actor_id), "char_1");
    copy_text(damage.actor_name, sizeof(damage.actor_name), "干员甲");
    model.apply(damage);

    damage.player_index = 2;
    damage.actual_damage = 2000;
    damage.calculated_damage = 2100;
    damage.card_uid = 13;
    copy_text(damage.actor_id, sizeof(damage.actor_id), "char_2");
    copy_text(damage.actor_name, sizeof(damage.actor_name), "干员乙");
    model.apply(damage);

    damage.player_index = 1;
    damage.actual_damage = 10;
    damage.calculated_damage = 10;
    damage.card_uid = 14;
    copy_text(damage.actor_id, sizeof(damage.actor_id), "secret-operator-id");
    copy_text(damage.actor_name, sizeof(damage.actor_name), "");
    model.apply(damage);

    return model;
}

void check_forbidden_columns(const std::string& text) {
    CHECK(text.find("玩家昵称编号") == std::string::npos);
    CHECK(text.find("干员ID") == std::string::npos);
    CHECK(text.find("统计范围") == std::string::npos);
    CHECK(text.find("hidden-uid") == std::string::npos);
    CHECK(text.find("remote-uid") == std::string::npos);
    CHECK(text.find("123456789") == std::string::npos);
    CHECK(text.find("987654321") == std::string::npos);
    CHECK(text.find("char_1") == std::string::npos);
    CHECK(text.find("char_2") == std::string::npos);
    CHECK(text.find("secret-operator-id") == std::string::npos);
}

WorkbookSheet make_unknown_effect_sheet() {
    WorkbookSheet sheet;
    sheet.name = "无来源附加伤害";
    sheet.round = 3;
    const auto populate = [](View& view, double ordinary, double effect, double unknown,
                             double player_seconds, double battle_seconds) {
        const auto add_actor = [](PlayerAggregate& player, const char* id, const char* name,
                                  double actual) {
            OperatorAggregate& actor = player.operators[id];
            actor.id = id;
            actor.name = name;
            actor.totals.add_damage(actual, actual * 1.5, 1);
            player.totals.add_damage(actual, actual * 1.5, 1);
        };
        PlayerAggregate& player = view.ensure_player(1);
        player.name = "测试玩家";
        player.uid = "hidden-uid";
        player.nick_number = "123456789";
        player.active_time = player_seconds;
        add_actor(player, "char_1", "普通干员", ordinary);
        add_actor(player, "unknown_effect", "无来源附加伤害", effect);
        player.totals.add_heal(8, 8);
        player.operators.at("char_1").totals.add_heal(8, 8);
        PlayerAggregate& bucket = view.ensure_player(kUnknownPlayer);
        bucket.uid = "remote-uid";
        bucket.nick_number = "987654321";
        add_actor(bucket, "unknown_effect", "无来源附加伤害", unknown);
        view.battle_seconds = battle_seconds;
        view.has_data = true;
    };
    populate(sheet.main, 40, 20, 40, 4, 10);
    populate(sheet.assist, 10, 30, 40, 2, 5);
    return sheet;
}

void test_unknown_effect_rows_privacy_and_phase_rates() {
    const auto root = std::filesystem::temp_directory_path() / "arkf-stronghold-unknown-effect-csv";
    for (bool zero_time : {false, true}) {
        WorkbookSheet sheet = make_unknown_effect_sheet();
        if (zero_time) {
            sheet.main.players.at(1).active_time = 0;
            sheet.assist.players.at(1).active_time = 0;
            sheet.main.battle_seconds = 0;
            sheet.assist.battle_seconds = 0;
        }
        const auto rows = build_export_rows(sheet);
        CHECK(rows.size() == 10);
        int effect_rows = 0;
        int unknown_totals = 0;
        for (const ExportRow& row : rows) {
            const bool assist = row.phase == "协防";
            const bool unknown = row.player_index == kUnknownPlayer;
            const double total = assist ? 80 : 100;
            const double duration = zero_time ? 0 : unknown ? (assist ? 5 : 10) : (assist ? 2 : 4);
            CHECK(row.phase == "主战" || assist);
            CHECK(row.round == 3);
            CHECK(row.duration == duration);
            CHECK(std::isfinite(row.dps));
            CHECK(std::isfinite(row.hps));
            CHECK(std::abs(row.share - row.actual / total) < 1e-12);
            CHECK(row.dps == (duration > 0 ? row.actual / duration : 0));
            CHECK(row.hps == (duration > 0 ? row.heal / duration : 0));
            if (row.unit == "无来源附加伤害") {
                ++effect_rows;
                CHECK(row.dimension == "干员");
                CHECK(row.actual == (unknown ? 40 : assist ? 30 : 20));
                CHECK(row.calculated == row.actual * 1.5);
                CHECK(row.player == (unknown ? "未归属" : "测试玩家(在线)"));
                CHECK(row.slot == (unknown ? "?" : "P1"));
                CHECK(row.hps == 0);
            }
            if (row.unit == "未归属合计") {
                ++unknown_totals;
                CHECK(unknown);
                CHECK(row.dimension == "玩家");
                CHECK(row.actual == 40);
                CHECK(row.calculated == 60);
            }
        }
        CHECK(effect_rows == 4);
        CHECK(unknown_totals == 2);

        std::string path;
        CHECK(write_csv_file(path_utf8(root), zero_time ? "zero.csv" : "timed.csv",
                             sheet, true, &path));
        const std::string text = read_all(utf8_path(path));
        check_forbidden_columns(text);
        CHECK(text.find("unknown_effect") == std::string::npos);
        CHECK(text.find("nan") == std::string::npos);
        CHECK(text.find("inf") == std::string::npos);
        const std::string known_main_rates = zero_time ? "0.0000,0.0000" : "4.0000,5.0000";
        const std::string known_assist_rates = zero_time ? "0.0000,0.0000" : "2.0000,15.0000";
        const std::string unknown_main_rates = zero_time ? "0.0000,0.0000" : "10.0000,4.0000";
        const std::string unknown_assist_rates = zero_time ? "0.0000,0.0000" : "5.0000,8.0000";
        CHECK(text.find("主战,干员,3,P1,测试玩家(在线),无来源附加伤害,1,20.0000," +
                        known_main_rates + ",20.0000%,30.0000,0.0000,0.0000,0.0000") != std::string::npos);
        CHECK(text.find("协防,干员,3,P1,测试玩家(在线),无来源附加伤害,1,30.0000," +
                        known_assist_rates + ",37.5000%,45.0000,0.0000,0.0000,0.0000") != std::string::npos);
        for (const char* dimension : {"玩家", "干员"}) {
            const std::string unit = std::string(dimension) == "玩家" ? "未归属合计" : "无来源附加伤害";
            const std::string prefix = std::string(dimension) + ",3,?,未归属," + unit + ",1,40.0000,";
            CHECK(text.find("主战," + prefix + unknown_main_rates +
                            ",40.0000%,60.0000,0.0000,0.0000,0.0000") != std::string::npos);
            CHECK(text.find("协防," + prefix + unknown_assist_rates +
                            ",50.0000%,60.0000,0.0000,0.0000,0.0000") != std::string::npos);
        }
    }
    std::filesystem::remove_all(root);
}

void test_csv_export() {
    StatsModel model = make_model();
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "arkf-stronghold-csv";
    std::filesystem::remove_all(root);
    std::string path;
    CHECK(write_csv_file(path_utf8(root), "stronghold-test-summary.csv", model.session(), 3,
                         model.coverage(), true, &path));
    const std::string data = read_all(utf8_path(path));
    CHECK(data.size() > 3);
    CHECK(static_cast<unsigned char>(data[0]) == 0xEF);
    CHECK(static_cast<unsigned char>(data[1]) == 0xBB);
    CHECK(static_cast<unsigned char>(data[2]) == 0xBF);

    const std::string header = first_line(data);
    CHECK(header.size() >= 3);
    if (header.size() >= 3) {
        CHECK(header.substr(3) == "阶段,维度,轮次,玩家槽位,玩家,干员,实例数,实际伤害,战斗时间(秒),DPS,伤害占比,计算伤害,治疗量,HPS,元素损伤");
    }
    CHECK(header.find("状态") == std::string::npos);
    check_forbidden_columns(data);
    CHECK(data.find("测试玩家(在线)") != std::string::npos);
    CHECK(data.find("队友(在线)") != std::string::npos);
    CHECK(data.find("未采集队友") == std::string::npos);
    CHECK(data.find("干员甲") != std::string::npos);
    CHECK(data.find("未知干员") != std::string::npos);
    CHECK(data.find("1234.5000") != std::string::npos);
    CHECK(data.find("2000.0000") != std::string::npos);
    CHECK(data.find('%') != std::string::npos);
    CHECK(data.find("nan") == std::string::npos);
    CHECK(data.find("inf") == std::string::npos);
    std::filesystem::remove_all(root);
}

void test_shared_row_builder_and_assist_phase() {
    StatsModel model = make_model();
    WorkbookSheet sheet;
    sheet.name = "第 3 轮";
    sheet.round = 3;
    sheet.main = model.round_view();
    sheet.assist.ensure_player(2).name = "协防玩家";
    PlayerAggregate& assist = sheet.assist.ensure_player(2);
    assist.active_time = 5;
    assist.status = PlayerStatus::Quit;
    assist.totals.add_damage(100, 120, 1);
    OperatorAggregate& actor = assist.operators["assist-operator"];
    actor.id = "assist-operator";
    actor.name = "协防干员";
    actor.instances.insert(99);
    actor.totals.add_damage(100, 120, 1);
    sheet.assist.battle_seconds = 5;
    sheet.assist.has_data = true;

    const auto rows = build_export_rows(sheet);
    CHECK(!rows.empty());
    bool saw_main = false;
    bool saw_assist = false;
    for (const ExportRow& row : rows) {
        if (row.phase == "主战") saw_main = true;
        if (row.phase == "协防") {
            saw_assist = true;
            CHECK(row.round == 3);
            CHECK(row.player == "协防玩家(中途退出)");
            CHECK(row.duration == 5);
            CHECK(row.share == 1);
            CHECK(row.dps == 20);
        }
    }
    CHECK(saw_main);
    CHECK(saw_assist);

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "arkf-stronghold-assist-csv";
    std::filesystem::remove_all(root);
    std::string path;
    CHECK(write_csv_file(path_utf8(root), "assist.csv", sheet, true, &path));
    const std::string text = read_all(utf8_path(path));
    CHECK(text.find("主战") != std::string::npos);
    CHECK(text.find("协防") != std::string::npos);
    CHECK(text.find("协防玩家(中途退出)") != std::string::npos);
    CHECK(text.find("协防干员") != std::string::npos);
    CHECK(text.find("100.0000%") != std::string::npos);
    check_forbidden_columns(text);
    std::filesystem::remove_all(root);
}

void test_filenames() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "arkf-stronghold-csv2";
    std::filesystem::remove_all(root);
    const StatsModel model = make_model();
    const auto paths = export_reports(path_utf8(root), 42, 9, 4, model.session(),
                                      model.round_view(), Coverage::FullRoom);
    CHECK(paths.has_value());
    if (paths) {
        CHECK(paths->summary.find("stronghold-42-9-summary.csv") != std::string::npos);
        CHECK(paths->round.find("stronghold-42-9-r04.csv") != std::string::npos);
    }
    std::filesystem::remove_all(root);
}

void test_summary_and_round_slot_columns() {
    StatsModel model;
    model.apply(base(EventKind::SessionStart));
    const std::uint32_t raw_slots = player_bit(1) | player_bit(2) | player_bit(3) | player_bit(4);
    for (int index = 1; index <= 4; ++index) {
        Event player = base(EventKind::Roster);
        player.player_index = index;
        player.self = index == 1 ? 1u : 0u;
        const std::string name = "玩家" + std::to_string(index);
        copy_text(player.player_name, sizeof(player.player_name), name);
        model.apply(player);
    }

    Event coverage = base(EventKind::Coverage);
    coverage.coverage = static_cast<std::uint32_t>(Coverage::FullRoom);
    coverage.simulated_mask = raw_slots;
    coverage.active_mask = raw_slots;
    model.apply(coverage);

    Event clock = base(EventKind::Clock);
    clock.delta_time = 4;
    clock.simulated_mask = raw_slots;
    model.apply(clock);

    for (int index = 1; index <= 4; ++index) {
        Event damage = base(EventKind::Damage);
        damage.player_index = index;
        damage.actual_damage = index * 10;
        damage.calculated_damage = damage.actual_damage;
        damage.damage_type = 1;
        damage.card_uid = static_cast<std::uint32_t>(index);
        copy_text(damage.actor_id, sizeof(damage.actor_id), "operator");
        copy_text(damage.actor_name, sizeof(damage.actor_name), "干员");
        model.apply(damage);
    }

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "arkf-stronghold-slot-csv";
    std::filesystem::remove_all(root);
    const auto paths = export_reports(path_utf8(root), 42, 9, 4, model.session(),
                                      model.round_view(), model.coverage());
    CHECK(paths.has_value());
    if (paths) {
        const std::string summary = read_all(utf8_path(paths->summary));
        const std::string round = read_all(utf8_path(paths->round));
        const auto check_slots = [](const std::string& text) {
            CHECK(text.find(",P1,") != std::string::npos);
            CHECK(text.find(",P2,") != std::string::npos);
            CHECK(text.find(",P3,") != std::string::npos);
            CHECK(text.find(",P4,") != std::string::npos);
            CHECK(text.find(",P5,") == std::string::npos);
        };
        check_slots(summary);
        check_slots(round);
        CHECK(player_label(model.session().players.at(1)) == "P1 玩家1");
    }
    std::filesystem::remove_all(root);
}

void test_zero_rates_unknown_heal_and_formula_protection() {
    StatsModel model;
    model.apply(base(EventKind::SessionStart));
    Event own = base(EventKind::Roster);
    own.player_index = 0;
    own.self = 1;
    copy_text(own.player_name, sizeof(own.player_name), "=HYPERLINK(\"https://example.invalid\")");
    model.apply(own);

    Event heal = base(EventKind::Damage);
    heal.player_index = 0;
    heal.damage_type = 4;
    heal.actual_damage = 10;
    heal.calculated_damage = 12;
    model.apply(heal);

    Event unknown_heal = heal;
    unknown_heal.player_index = kUnknownPlayer;
    unknown_heal.actual_damage = 5;
    model.apply(unknown_heal);

    const auto root = std::filesystem::temp_directory_path() / utf8_path("arkf-stronghold-中文路径");
    std::string file;
    CHECK(write_csv_file(path_utf8(root), "unknown.csv", model.session(), 3,
                          Coverage::SelfOnly, true, &file));
    const auto text = read_all(utf8_path(file));
    CHECK(text.find("未归属合计") != std::string::npos);
    CHECK(text.find("未知干员") != std::string::npos);
    CHECK(text.find("'=HYPERLINK") != std::string::npos);
    CHECK(text.find("nan") == std::string::npos);
    CHECK(text.find("inf") == std::string::npos);
    CHECK(csv_percent(0.25) == "25.0000%");
    CHECK(csv_text_cell("+SUM(A1)") == "'+SUM(A1)");
    CHECK(csv_text_cell("-2+3") == "'-2+3");
    CHECK(csv_text_cell("@cmd") == "'@cmd");
    CHECK(csv_text_cell("普通文本") == "普通文本");
    CHECK(csv_text_cell("玩家,\"甲\"\n乙") == "\"玩家,\"\"甲\"\"\n乙\"");
    std::filesystem::remove_all(root);

    const auto blocked = std::filesystem::temp_directory_path() / "arkf-stronghold-blocked-export";
    std::filesystem::remove_all(blocked);
    std::filesystem::create_directories(blocked);
    const auto exports = blocked / "exports";
    { std::ofstream stream(exports); stream << "file occupies export directory"; }
    CHECK(!write_csv_file(path_utf8(blocked), "fail.csv", model.session(), 3,
                           Coverage::SelfOnly, true, nullptr));
    std::filesystem::remove_all(blocked);
}
} // namespace

int main() {
    test_unknown_effect_rows_privacy_and_phase_rates();
    std::printf("%s unknown effect rows / privacy / independent phase rates\n",
                g_failures == 0 ? "PASS" : "FAIL");
    const int prior_export = g_failures;
    test_csv_export();
    std::printf("%s csv export\n", g_failures == prior_export ? "PASS" : "FAIL");
    const int prior_rows = g_failures;
    test_shared_row_builder_and_assist_phase();
    std::printf("%s shared rows and assist phase\n", g_failures == prior_rows ? "PASS" : "FAIL");
    const int prior_names = g_failures;
    test_filenames();
    std::printf("%s filenames\n", g_failures == prior_names ? "PASS" : "FAIL");
    const int prior_slots = g_failures;
    test_summary_and_round_slot_columns();
    std::printf("%s summary and round slot columns\n", g_failures == prior_slots ? "PASS" : "FAIL");
    const int prior_zero = g_failures;
    test_zero_rates_unknown_heal_and_formula_protection();
    std::printf("%s zero rates / unknown heal / UTF-8 paths / formula injection / export failures\n",
                g_failures == prior_zero ? "PASS" : "FAIL");
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("all stronghold csv tests passed");
    return 0;
}
