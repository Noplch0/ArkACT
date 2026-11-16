#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "xlsx_export.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                 do {                                                                                    if (!(condition)) {                                                                     std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                   ++g_failures;                                                                   }                                                                               } while (0)

using namespace ark::stronghold;

struct ZipItem {
    std::string name;
    std::string data;
    std::uint32_t flags = 0;
    std::uint32_t method = 0;
};

std::uint16_t read_u16(const std::string& data, std::size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<std::uint16_t>(static_cast<unsigned char>(data[offset]) |
                                      (static_cast<unsigned char>(data[offset + 1]) << 8));
}

std::uint32_t read_u32(const std::string& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset]) |
                                      (static_cast<unsigned char>(data[offset + 1]) << 8) |
                                      (static_cast<unsigned char>(data[offset + 2]) << 16) |
                                      (static_cast<unsigned char>(data[offset + 3]) << 24));
}

std::string file_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::map<std::string, ZipItem> unzip_store(const std::string& bytes) {
    std::map<std::string, ZipItem> result;
    CHECK(bytes.size() >= 22);
    if (bytes.size() < 22) return result;
    std::size_t eocd = std::string::npos;
    for (std::size_t pos = bytes.size() - 22 + 1; pos-- > 0;) {
        if (read_u32(bytes, pos) == 0x06054B50u) {
            eocd = pos;
            break;
        }
    }
    CHECK(eocd != std::string::npos);
    if (eocd == std::string::npos) return result;
    const std::uint16_t count = read_u16(bytes, eocd + 10);
    const std::uint32_t central_size = read_u32(bytes, eocd + 12);
    const std::uint32_t central_offset = read_u32(bytes, eocd + 16);
    CHECK(central_offset + central_size <= bytes.size());
    std::size_t pos = central_offset;
    for (std::uint16_t index = 0; index < count; ++index) {
        CHECK(read_u32(bytes, pos) == 0x02014B50u);
        const std::uint16_t flags = read_u16(bytes, pos + 8);
        const std::uint16_t method = read_u16(bytes, pos + 10);
        const std::uint32_t compressed_size = read_u32(bytes, pos + 20);
        const std::string::size_type name_size = read_u16(bytes, pos + 28);
        const std::string::size_type extra_size = read_u16(bytes, pos + 30);
        const std::string::size_type comment_size = read_u16(bytes, pos + 32);
        const std::uint32_t local_offset = read_u32(bytes, pos + 42);
        const std::string name = bytes.substr(pos + 46, name_size);
        CHECK(read_u32(bytes, local_offset) == 0x04034B50u);
        const std::string::size_type local_name = read_u16(bytes, local_offset + 26);
        const std::string::size_type local_extra = read_u16(bytes, local_offset + 28);
        const std::size_t data_offset = local_offset + 30 + local_name + local_extra;
        CHECK(data_offset + compressed_size <= bytes.size());
        result[name] = ZipItem{name, bytes.substr(data_offset, compressed_size), flags, method};
        pos += 46 + name_size + extra_size + comment_size;
    }
    return result;
}

const std::string& entry_data(const std::map<std::string, ZipItem>& archive,
                              const std::string& name) {
    static const std::string empty;
    const auto found = archive.find(name);
    return found == archive.end() ? empty : found->second.data;
}

PlayerAggregate& add_player(View& view, int index, const std::string& name, double damage,
                            double seconds, PlayerStatus status = PlayerStatus::Online) {
    PlayerAggregate& player = view.ensure_player(index);
    player.name = name;
    player.active_time = seconds;
    player.status = status;
    player.online = status == PlayerStatus::Online;
    player.totals.add_damage(damage, damage * 1.1, 1);
    view.battle_seconds = (std::max)(view.battle_seconds, seconds);
    view.has_data = true;
    return player;
}

void add_operator(PlayerAggregate& player, const std::string& id, const std::string& name,
                  double damage) {
    OperatorAggregate& actor = player.operators[id];
    actor.id = id;
    actor.name = name;
    actor.instances.insert(static_cast<std::uint32_t>(player.operators.size() + 100));
    actor.totals.add_damage(damage, damage * 1.2, 1);
}

WorkbookSheet make_round_sheet(const std::string& name, std::uint32_t round) {
    WorkbookSheet sheet;
    sheet.name = name;
    sheet.round = round;
    PlayerAggregate& remote = add_player(sheet.main, 2, "队友B", 300, 20);
    add_operator(remote, "sensitive-operator-id-b", "远程干员", 300);
    PlayerAggregate& self = add_player(sheet.main, 1, "测试玩家", 100, 10,
                                       PlayerStatus::Quit);
    add_operator(self, "sensitive-operator-id-a", "干员甲", 80);
    add_operator(self, "sensitive-operator-id-c", "干员乙", 20);
    add_operator(self, "sensitive-empty-operator-id", "", 1);

    PlayerAggregate& absent = sheet.main.ensure_player(3);
    absent.name = "未采集队友";
    absent.status = PlayerStatus::Online;

    PlayerAggregate& assist = add_player(sheet.assist, 2, "协防队友", 50, 5);
    add_operator(assist, "sensitive-assist-id", "协防干员", 50);
    sheet.assist.battle_seconds = 5;
    sheet.assist.has_data = true;
    return sheet;
}

std::filesystem::path find_workspace_root() {
    std::filesystem::path current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "stronghold" / "xlsx_export.h")) return current;
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return std::filesystem::current_path();
}

void check_forbidden_content(const std::string& text) {
    CHECK(text.find("玩家昵称编号") == std::string::npos);
    CHECK(text.find("干员ID") == std::string::npos);
    CHECK(text.find("统计范围") == std::string::npos);
    CHECK(text.find("hidden-uid") == std::string::npos);
    CHECK(text.find("sensitive-operator-id") == std::string::npos);
    CHECK(text.find("sensitive-empty-operator-id") == std::string::npos);
    CHECK(text.find("<f>") == std::string::npos);
}

std::string find_effect_row(const std::string& xml, const char* phase, const char* player,
                             const char* unit) {
    std::string result;
    int matches = 0;
    for (std::size_t pos = xml.find("<row "); pos != std::string::npos;) {
        const auto end = xml.find("</row>", pos);
        CHECK(end != std::string::npos);
        if (end == std::string::npos) break;
        const std::string row = xml.substr(pos, end + 6 - pos);
        const auto contains_text = [&](const char* text) {
            return row.find(std::string(">") + text + "</t>") != std::string::npos;
        };
        if (contains_text(phase) && contains_text(player) && contains_text(unit)) {
            ++matches;
            result = row;
        }
        pos = xml.find("<row ", end + 6);
    }
    CHECK(matches == 1);
    return result;
}

void check_effect_number(const std::string& row, const char* column, double expected,
                          bool percentage = false) {
    const auto pos = row.find(std::string("<c r=\"") + column);
    CHECK(pos != std::string::npos);
    if (pos == std::string::npos) return;
    const auto end = row.find("</c>", pos);
    CHECK(end != std::string::npos);
    if (end == std::string::npos) return;
    const std::string cell = row.substr(pos, end + 4 - pos);
    CHECK(cell.find("inlineStr") == std::string::npos);
    if (percentage) CHECK(cell.find("s=\"4\"") != std::string::npos);
    const auto begin_value = cell.find("<v>");
    const auto end_value = cell.find("</v>");
    CHECK(begin_value != std::string::npos && end_value != std::string::npos);
    if (begin_value == std::string::npos || end_value == std::string::npos) return;
    const std::string text = cell.substr(begin_value + 3, end_value - begin_value - 3);
    char* parsed_end = nullptr;
    const double actual = std::strtod(text.c_str(), &parsed_end);
    CHECK(!text.empty() && parsed_end == text.c_str() + text.size());
    CHECK(std::isfinite(actual));
    CHECK(std::abs(actual - expected) < 1e-12);
}

void test_unknown_effect_rows_privacy_and_phase_rates() {
    WorkbookSheet sheet;
    sheet.name = "无来源附加伤害";
    sheet.round = 3;
    const auto populate = [](View& view, double ordinary, double effect, double unknown,
                             double player_seconds, double battle_seconds) {
        PlayerAggregate& player = add_player(view, 1, "测试玩家", ordinary + effect, player_seconds);
        player.uid = "hidden-uid";
        player.nick_number = "123456789";
        add_operator(player, "sensitive-operator-id", "普通干员", ordinary);
        add_operator(player, "unknown_effect", "无来源附加伤害", effect);
        player.totals.calculated = (ordinary + effect) * 1.2;
        player.totals.add_heal(8, 8);
        player.operators.at("sensitive-operator-id").totals.add_heal(8, 8);
        PlayerAggregate& bucket = add_player(view, kUnknownPlayer, "未归属", unknown, 0);
        bucket.uid = "unknown-bucket-uid";
        bucket.nick_number = "987654321";
        add_operator(bucket, "unknown_effect", "无来源附加伤害", unknown);
        bucket.totals.calculated = unknown * 1.2;
        view.battle_seconds = battle_seconds;
    };
    populate(sheet.main, 40, 20, 40, 4, 10);
    populate(sheet.assist, 10, 30, 40, 2, 5);
    WorkbookSheet zero = sheet;
    zero.name = "无计时附加伤害";
    zero.main.players.at(1).active_time = 0;
    zero.assist.players.at(1).active_time = 0;
    zero.main.battle_seconds = 0;
    zero.assist.battle_seconds = 0;

    const auto root = std::filesystem::temp_directory_path() / "arkf-stronghold-unknown-effect-xlsx";
    std::string path;
    CHECK(write_workbook_file(path_utf8(root), "无来源附加伤害.xlsx", {sheet, zero}, &path));
    const auto archive = unzip_store(file_bytes(utf8_path(path)));
    for (const auto& [name, entry] : archive) {
        (void)name;
        check_forbidden_content(entry.data);
        CHECK(entry.data.find("unknown-bucket-uid") == std::string::npos);
        CHECK(entry.data.find("123456789") == std::string::npos);
        CHECK(entry.data.find("987654321") == std::string::npos);
        CHECK(entry.data.find("unknown_effect") == std::string::npos);
    }
    CHECK(entry_data(archive, "xl/styles.xml").find("formatCode=\"0.00%\"") != std::string::npos);
    for (int index = 1; index <= 2; ++index) {
        const std::string xml = entry_data(archive, "xl/worksheets/sheet" + std::to_string(index) + ".xml");
        CHECK(!xml.empty());
        for (bool assist : {false, true}) {
            const char* phase = assist ? "协防" : "主战";
            for (bool unknown : {false, true}) {
                const std::string row = find_effect_row(xml, phase,
                    unknown ? "未归属" : "测试玩家(在线)", "无来源附加伤害");
                const double actual = unknown ? 40 : assist ? 30 : 20;
                const double seconds = index == 2 ? 0 : unknown ? (assist ? 5 : 10) : (assist ? 2 : 4);
                CHECK(row.find(unknown ? ">?</t>" : ">P1</t>") != std::string::npos);
                CHECK(row.find(">干员</t>") != std::string::npos);
                check_effect_number(row, "H", actual);
                check_effect_number(row, "I", seconds);
                check_effect_number(row, "J", seconds > 0 ? actual / seconds : 0);
                check_effect_number(row, "K", actual / (assist ? 80 : 100), true);
                check_effect_number(row, "L", actual * 1.2);
                check_effect_number(row, "M", 0);
                check_effect_number(row, "N", 0);
            }
            const std::string total = find_effect_row(xml, phase, "未归属", "未归属合计");
            check_effect_number(total, "H", 40);
            check_effect_number(total, "K", assist ? 0.5 : 0.4, true);
            check_effect_number(total, "L", 48);
            const std::string player = find_effect_row(xml, phase, "测试玩家(在线)", "玩家合计");
            check_effect_number(player, "H", assist ? 40 : 60);
            check_effect_number(player, "K", assist ? 0.5 : 0.6, true);
            check_effect_number(player, "M", 8);
            check_effect_number(player, "N", index == 2 ? 0 : assist ? 4 : 2);
        }
    }
    std::filesystem::remove_all(root);
}

void test_workbook_structure_and_fixture() {
    const std::filesystem::path root = find_workspace_root() / ".build" / "export-2.1.0-fixture";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    std::vector<WorkbookSheet> sheets;
    sheets.push_back(make_round_sheet("整局汇总", 0));
    sheets.push_back(make_round_sheet("第1轮", 1));

    WorkbookSheet empty_assist;
    empty_assist.name = "无效/名字:*?[]";
    empty_assist.round = 2;
    add_player(empty_assist.main, 1, "=SUM(A1)", 10, 2);
    PlayerAggregate& roster_only = empty_assist.assist.ensure_player(1);
    roster_only.name = "名单中的自己";
    roster_only.self = true;
    roster_only.status = PlayerStatus::Online;
    sheets.push_back(empty_assist);

    WorkbookSheet duplicate = make_round_sheet("第1轮", 3);
    sheets.push_back(duplicate);

    std::string path;
    CHECK(write_workbook_file(path_utf8(root), "报告.xlsx", sheets, &path));
    CHECK(!path.empty());
    const std::filesystem::path actual = utf8_path(path);
    CHECK(actual == root / "exports" / utf8_path("报告.xlsx"));
    CHECK(std::filesystem::exists(actual));

    const std::string bytes = file_bytes(actual);
    CHECK(read_u32(bytes, 0) == 0x04034B50u);
    const auto archive = unzip_store(bytes);
    CHECK(archive.size() >= 10);
    CHECK(archive.count("[Content_Types].xml") == 1);
    CHECK(archive.count("_rels/.rels") == 1);
    CHECK(archive.count("xl/workbook.xml") == 1);
    CHECK(archive.count("xl/_rels/workbook.xml.rels") == 1);
    CHECK(archive.count("xl/styles.xml") == 1);
    CHECK(archive.count("xl/worksheets/sheet1.xml") == 1);
    CHECK(archive.count("xl/worksheets/sheet2.xml") == 1);
    CHECK(archive.count("xl/worksheets/sheet3.xml") == 1);
    CHECK(archive.count("xl/worksheets/sheet4.xml") == 1);
    for (const auto& [name, item] : archive) {
        (void)name;
        CHECK((item.flags & 0x0800u) != 0);
        CHECK(item.method == 0);
    }

    const std::string workbook = entry_data(archive, "xl/workbook.xml");
    CHECK(workbook.find("整局汇总") != std::string::npos);
    CHECK(workbook.find("第1轮") != std::string::npos);
    CHECK(workbook.find("第1轮(2)") != std::string::npos);
    CHECK(workbook.find("无效名字") != std::string::npos);
    CHECK(workbook.find("无效/名字") == std::string::npos);
    CHECK(workbook.find(":*?[]") == std::string::npos);

    const std::string styles = entry_data(archive, "xl/styles.xml");
    CHECK(styles.find("formatCode=\"0.00%\"") != std::string::npos);
    CHECK(styles.find("numFmtId=\"164\"") != std::string::npos);
    CHECK(styles.find("applyNumberFormat=\"1\"") != std::string::npos);

    const std::string sheet1 = entry_data(archive, "xl/worksheets/sheet1.xml");
    CHECK(sheet1.find("阶段") != std::string::npos);
    CHECK(sheet1.find("维度") != std::string::npos);
    CHECK(sheet1.find("战斗时间(秒)") != std::string::npos);
    CHECK(sheet1.find("伤害占比") != std::string::npos);
    CHECK(sheet1.find("元素损伤") != std::string::npos);
    CHECK(sheet1.find("主战") != std::string::npos);
    CHECK(sheet1.find("协防") != std::string::npos);
    CHECK(sheet1.find("测试玩家(中途退出)") != std::string::npos);
    CHECK(sheet1.find("队友B(在线)") != std::string::npos);
    CHECK(sheet1.find("协防队友(在线)") != std::string::npos);
    CHECK(sheet1.find("未采集队友") == std::string::npos);
    CHECK(sheet1.find("未知干员") != std::string::npos);
    CHECK(sheet1.find("ySplit=\"1\"") != std::string::npos);
    CHECK(sheet1.find("state=\"frozen\"") != std::string::npos);
    CHECK(sheet1.find("<autoFilter ref=\"A1:O") != std::string::npos);
    check_forbidden_content(sheet1);

    const std::size_t b_pos = sheet1.find("队友B(在线)");
    const std::size_t a_pos = sheet1.find("测试玩家(中途退出)");
    CHECK(b_pos != std::string::npos && a_pos != std::string::npos && b_pos < a_pos);
    const std::size_t op_a = sheet1.find("干员甲");
    const std::size_t op_c = sheet1.find("干员乙");
    CHECK(op_a != std::string::npos && op_c != std::string::npos && op_a < op_c);

    const std::size_t k2 = sheet1.find("<c r=\"K2\"");
    CHECK(k2 != std::string::npos);
    if (k2 != std::string::npos) {
        const std::size_t value = sheet1.find("<v>", k2);
        const std::size_t value_end = value == std::string::npos ? value : sheet1.find("</v>", value);
        CHECK(value != std::string::npos && value_end != std::string::npos);
        if (value != std::string::npos && value_end != std::string::npos) {
            const std::string number = sheet1.substr(value + 3, value_end - value - 3);
            CHECK(number.find('%') == std::string::npos);
            CHECK(number == "0.75");
        }
        CHECK(sheet1.find("s=\"164\"", k2) != std::string::npos ||
              sheet1.find("s=\"4\"", k2) != std::string::npos);
    }

    const std::string sheet3 = entry_data(archive, "xl/worksheets/sheet3.xml");
    CHECK(sheet3.find("<t xml:space=\"preserve\">=SUM(A1)(在线)</t>") != std::string::npos);
    CHECK(sheet3.find("协防") == std::string::npos);
    CHECK(sheet3.find("名单中的自己") == std::string::npos);
    CHECK(sheet3.find("<f>") == std::string::npos);

    const std::filesystem::path fixture = root / utf8_path("报告.xlsx");
    std::filesystem::copy_file(actual, fixture, std::filesystem::copy_options::overwrite_existing);
    CHECK(std::filesystem::exists(fixture));
}

void test_atomic_replacement_and_failures() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "arkf-stronghold-xlsx-atomic";
    std::filesystem::remove_all(root);
    WorkbookSheet first;
    first.name = "第一版";
    first.round = 1;
    add_player(first.main, 1, "测试玩家", 10, 1);
    WorkbookSheet second;
    second.name = "第二版";
    second.round = 2;
    add_player(second.main, 1, "替换玩家", 20, 2);

    std::string path;
    CHECK(write_workbook_file(path_utf8(root), "原子替换.xlsx", {first}, &path));
    const std::string before = file_bytes(utf8_path(path));
    CHECK(!before.empty());
    CHECK(write_workbook_file(path_utf8(root), "原子替换.xlsx", {second}, &path));
    const std::string after = file_bytes(utf8_path(path));
    CHECK(!after.empty());
    CHECK(before != after);
    const auto archive = unzip_store(after);
    CHECK(entry_data(archive, "xl/workbook.xml").find("第二版") != std::string::npos);

    CHECK(!write_workbook_file(path_utf8(root), "..\\outside.xlsx", {first}, nullptr));
    CHECK(!write_workbook_file(path_utf8(root), "C:outside.xlsx", {first}, nullptr));
    const std::string unchanged = file_bytes(utf8_path(path));
    CHECK(unchanged == after);
    CHECK(!write_workbook_file(path_utf8(root), "", {first}, nullptr));
    CHECK(!write_workbook_file(path_utf8(root), "none.xlsx", {}, nullptr));

    const auto blocked = std::filesystem::temp_directory_path() / "arkf-stronghold-xlsx-blocked";
    std::filesystem::remove_all(blocked);
    std::filesystem::create_directories(blocked);
    { std::ofstream stream(blocked / "exports"); stream << "not a directory"; }
    CHECK(!write_workbook_file(path_utf8(blocked), "blocked.xlsx", {first}, nullptr));
    std::filesystem::remove_all(blocked);
    std::filesystem::remove_all(root);
}

void test_xml_escaping_and_sheet_limits() {
    std::string name = "A&B<测试>\"'";
    for (int index = 0; index < 40; ++index) name += "超";
    const auto names = unique_sheet_names({WorkbookSheet{name, 1, {}, {}},
                                           WorkbookSheet{name, 2, {}, {}}});
    CHECK(names.size() == 2);
    CHECK(names[0] != names[1]);
    CHECK(names[0].find('&') != std::string::npos);
    CHECK(names[0].find('<') != std::string::npos);
    CHECK(names[0].find('>') != std::string::npos);
    CHECK(names[0].find('"') != std::string::npos);
    CHECK(names[0].find("'") != std::string::npos);
    CHECK(utf16_units('A') == 1);
    CHECK(utf16_units(0x1F600u) == 2);
    CHECK(xml_escape("A&B<C>\x01") == "A&amp;B&lt;C&gt;");
    CHECK(xml_escape("\xEF\xBF\xBD") == "\xEF\xBF\xBD");
}

} // namespace

int main() {
    test_unknown_effect_rows_privacy_and_phase_rates();
    std::printf("%s unknown effect rows / privacy / independent phase rates\n",
                g_failures == 0 ? "PASS" : "FAIL");
    const int prior_structure = g_failures;
    test_workbook_structure_and_fixture();
    std::printf("%s workbook structure / fixture\n", g_failures == prior_structure ? "PASS" : "FAIL");
    const int prior_atomic = g_failures;
    test_atomic_replacement_and_failures();
    std::printf("%s atomic replacement / failures\n", g_failures == prior_atomic ? "PASS" : "FAIL");
    const int prior_xml = g_failures;
    test_xml_escaping_and_sheet_limits();
    std::printf("%s XML escaping / sheet limits\n", g_failures == prior_xml ? "PASS" : "FAIL");
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("all stronghold xlsx tests passed");
    return 0;
}
