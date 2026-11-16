#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "report_archive.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                 \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);            \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

using namespace ark::stronghold;

struct WriterCall {
    std::string filename;
    std::vector<WorkbookSheet> sheets;
    bool success = false;
};

struct WriterState {
    bool succeed = true;
    bool touch_files = true;
    int standard_exceptions_remaining = 0;
    int unknown_exceptions_remaining = 0;
    std::vector<WriterCall> calls;
};

WriterState* g_writer = nullptr;

bool test_writer(const std::string& root, const std::string& filename,
                 const std::vector<WorkbookSheet>& sheets, std::string* out_path) {
    WriterState* state = g_writer;
    if (state && state->standard_exceptions_remaining > 0) {
        --state->standard_exceptions_remaining;
        throw std::runtime_error("mock writer path failure");
    }
    if (state && state->unknown_exceptions_remaining > 0) {
        --state->unknown_exceptions_remaining;
        throw 17;
    }
    bool success = state && state->succeed;
    std::string path;
    if (success && state->touch_files) {
        const std::filesystem::path destination = utf8_path(root) / "exports" / utf8_path(filename);
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) {
            success = false;
        } else {
            std::ofstream stream(destination, std::ios::binary | std::ios::trunc);
            stream << "report fixture";
            success = static_cast<bool>(stream);
            if (success) path = path_utf8(destination);
        }
    } else if (success) {
        path = path_utf8(utf8_path(root) / "exports" / utf8_path(filename));
    }
    if (state) {
        state->calls.push_back({filename, sheets, success});
    }
    if (out_path) *out_path = success ? path : std::string{};
    return success;
}

std::filesystem::path fixture_root(const char* name) {
    const std::filesystem::path root = std::filesystem::current_path() / "fixture" / name;
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return root;
}

std::string path_for(const std::filesystem::path& root, const std::string& filename) {
    return path_utf8(root / "exports" / utf8_path(filename));
}

void begin_session(StatsModel& model, std::uint64_t session_id) {
    Event start;
    start.kind = EventKind::SessionStart;
    start.session_id = session_id;
    start.round = 1;
    model.apply(start);

    Event roster;
    roster.kind = EventKind::Roster;
    roster.session_id = session_id;
    roster.round = 1;
    roster.player_index = 1;
    roster.self = 1;
    copy_text(roster.player_name, sizeof(roster.player_name), "self");
    model.apply(roster);
    roster.player_index = 2;
    roster.self = 0;
    copy_text(roster.player_name, sizeof(roster.player_name), "mate");
    model.apply(roster);
}

void add_damage(StatsModel& model, double value, BattlePhase phase) {
    Event event;
    event.kind = EventKind::Damage;
    event.session_id = model.session_id();
    event.round = model.current_round();
    event.phase = phase;
    event.player_index = 1;
    event.actual_damage = value;
    event.calculated_damage = value;
    event.damage_type = 1;
    copy_text(event.actor_id, sizeof(event.actor_id), "unit");
    copy_text(event.actor_name, sizeof(event.actor_name), "干员");
    model.apply(event);
}

void add_clock(StatsModel& model, double seconds, BattlePhase phase) {
    Event event;
    event.kind = EventKind::Clock;
    event.session_id = model.session_id();
    event.round = model.current_round();
    event.phase = phase;
    event.delta_time = seconds;
    event.simulated_mask = player_bit(1) | player_bit(2);
    model.apply(event);
}

void start_round(StatsModel& model, std::uint32_t round) {
    Event event;
    event.kind = EventKind::RoundStart;
    event.session_id = model.session_id();
    event.round = round;
    model.apply(event);
}

void end_session(StatsModel& model, std::int64_t wall_time) {
    Event event;
    event.kind = EventKind::SessionEnd;
    event.session_id = model.session_id();
    event.round = model.current_round();
    event.wall_time = wall_time;
    model.apply(event);
}

std::string expected_final_filename(std::int64_t unix_seconds) {
    const std::time_t value = static_cast<std::time_t>(unix_seconds);
    std::tm local{};
#if defined(_WIN32)
    if (::localtime_s(&local, &value) != 0) return {};
#else
    if (::localtime_r(&value, &local) == nullptr) return {};
#endif
    char buffer[32]{};
    const int written = std::snprintf(
        buffer, sizeof(buffer), "%04d-%02d-%02d %02d-%02d.xlsx",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
    return written > 0 ? std::string(buffer, static_cast<std::size_t>(written)) : std::string{};
}

std::string with_number_suffix(const std::string& base, unsigned number) {
    const auto extension = base.rfind(".xlsx");
    if (extension == std::string::npos) return base;
    return base.substr(0, extension) + "(" + std::to_string(number) + ").xlsx";
}

const WorkbookSheet* sheet_for_round(const std::vector<WorkbookSheet>& sheets,
                                     std::uint32_t round) {
    for (const WorkbookSheet& sheet : sheets) {
        if (sheet.round == round) return &sheet;
    }
    return nullptr;
}

const WriterCall* successful_call(const WriterState& state, const std::string& filename) {
    for (const WriterCall& call : state.calls) {
        if (call.success && call.filename == filename) return &call;
    }
    return nullptr;
}

void test_multiround_dual_phase_collection() {
    const std::filesystem::path root = fixture_root("multiround");
    WriterState writer;
    g_writer = &writer;
    ReportArchive archive(4242, &test_writer);

    StatsModel model;
    begin_session(model, 9001);
    add_clock(model, 4, BattlePhase::Main);
    add_damage(model, 100, BattlePhase::Main);
    add_clock(model, 2, BattlePhase::Assist);
    add_damage(model, 40, BattlePhase::Assist);
    start_round(model, 2);
    add_clock(model, 6, BattlePhase::Main);
    add_damage(model, 200, BattlePhase::Main);
    add_clock(model, 3, BattlePhase::Assist);
    add_damage(model, 60, BattlePhase::Assist);
    start_round(model, 3);
    add_clock(model, 8, BattlePhase::Main);
    add_damage(model, 300, BattlePhase::Main);
    add_clock(model, 4, BattlePhase::Assist);
    add_damage(model, 80, BattlePhase::Assist);

    archive.capture(model, ReportCaptureReason::Manual);
    CHECK(writer.calls.empty());
    CHECK(archive.pending_count() == 1);
    CHECK(model.completed_rounds().size() == 2);

    std::string status;
    CHECK(archive.flush(path_utf8(root), 1000, false, status) == ReportFlushResult::Written);
    CHECK(status.find("报告已写入") != std::string::npos);
    CHECK(archive.flush(path_utf8(root), 1000, false, status) == ReportFlushResult::NoPending);
    CHECK(writer.calls.size() == 1);
    if (writer.calls.size() == 1) {
        CHECK(writer.calls[0].filename == "进行中-4242-9001.xlsx");
        const auto& sheets = writer.calls[0].sheets;
        CHECK(sheets.size() == 4);
        if (sheets.size() == 4) {
            const WorkbookSheet* summary = sheet_for_round(sheets, 0);
            const WorkbookSheet* first = sheet_for_round(sheets, 1);
            const WorkbookSheet* second = sheet_for_round(sheets, 2);
            const WorkbookSheet* third = sheet_for_round(sheets, 3);
            CHECK(summary && first && second && third);
            if (summary && first && second && third) {
                CHECK(summary->name == "整局累计");
                CHECK(summary->main.actual_total() == 600);
                CHECK(summary->assist.actual_total() == 180);
                CHECK(summary->main.battle_seconds == 18);
                CHECK(summary->assist.battle_seconds == 9);
                CHECK(first->name == "第01轮" && first->main.actual_total() == 100 &&
                      first->assist.actual_total() == 40);
                CHECK(second->name == "第02轮" && second->main.actual_total() == 200 &&
                      second->assist.actual_total() == 60);
                CHECK(third->name == "第03轮" && third->main.actual_total() == 300 &&
                      third->assist.actual_total() == 80);
                CHECK(first->main.battle_seconds == 4 && first->assist.battle_seconds == 2);
                CHECK(second->main.battle_seconds == 6 && second->assist.battle_seconds == 3);
                CHECK(third->main.battle_seconds == 8 && third->assist.battle_seconds == 4);
            }
        }
    }

    g_writer = nullptr;
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_failure_backoff_ended_name_and_zero_fallback() {
    const std::filesystem::path root = fixture_root("ended");
    WriterState writer;
    writer.succeed = false;
    g_writer = &writer;
    ReportArchive archive(5000, &test_writer);
    archive.set_retry_delay_ms(1000);

    StatsModel model;
    begin_session(model, 9002);
    add_damage(model, 123, BattlePhase::Main);
    const std::int64_t ended_at = 1763201400;
    end_session(model, ended_at);
    const std::string expected = expected_final_filename(ended_at);
    CHECK(expected.size() == 21);
    CHECK(expected.find(':') == std::string::npos);

    archive.capture(model, ReportCaptureReason::SessionEnd);
    CHECK(writer.calls.empty());
    CHECK(archive.pending_count() == 1);

    std::string status;
    CHECK(archive.flush(path_utf8(root), 1000, false, status) == ReportFlushResult::Failed);
    CHECK(writer.calls.size() == 1);
    if (!writer.calls.empty()) CHECK(writer.calls[0].filename == expected);
    CHECK(archive.flush(path_utf8(root), 1500, false, status) ==
          ReportFlushResult::Throttled);
    CHECK(writer.calls.size() == 1);
    CHECK(archive.flush(path_utf8(root), 1999, false, status) ==
          ReportFlushResult::Throttled);
    CHECK(writer.calls.size() == 1);

    writer.succeed = true;
    CHECK(archive.flush(path_utf8(root), 2000, false, status) == ReportFlushResult::Written);
    CHECK(writer.calls.size() == 2);
    if (writer.calls.size() == 2) {
        CHECK(writer.calls[0].filename == writer.calls[1].filename);
        CHECK(writer.calls[1].filename == expected);
    }
    CHECK(archive.pending_count() == 0);

    WriterState fallback_writer;
    g_writer = &fallback_writer;
    ReportArchive fallback_archive(5001, &test_writer);
    StatsModel fallback;
    begin_session(fallback, 9003);
    add_damage(fallback, 7, BattlePhase::Main);
    end_session(fallback, 0);
    fallback_archive.capture(fallback, ReportCaptureReason::SessionEnd);
    const std::int64_t fallback_now_ms = ended_at * 1000 + 45000;
    CHECK(fallback_archive.flush(path_utf8(root), fallback_now_ms, false, status) ==
          ReportFlushResult::Written);
    CHECK(fallback_writer.calls.size() == 1);
    if (!fallback_writer.calls.empty()) {
        // The first report already owns the unsuffixed name; fallback time still resolves
        // to the same minute and therefore must use the collision suffix.
        CHECK(fallback_writer.calls[0].filename ==
              with_number_suffix(expected_final_filename(ended_at), 2));
    }

    g_writer = nullptr;
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_failure_survives_model_reset_and_interruption_names() {
    const std::filesystem::path root = fixture_root("cross-session");
    WriterState writer;
    writer.succeed = false;
    writer.touch_files = false;
    g_writer = &writer;
    ReportArchive archive(777, &test_writer);
    archive.set_retry_delay_ms(5000);

    StatsModel first;
    begin_session(first, 11);
    add_damage(first, 100, BattlePhase::Main);
    archive.capture(first, ReportCaptureReason::RoundStart);
    std::string status;
    CHECK(archive.flush(path_utf8(root), 1000, false, status) == ReportFlushResult::Failed);
    CHECK(archive.pending_count() == 1);

    // Plugin captures the old model immediately before replacing it with a new session.
    archive.capture(first, ReportCaptureReason::NewSession);
    CHECK(archive.pending_count() == 1);
    CHECK(writer.calls.size() == 1);

    StatsModel second;
    begin_session(second, 22);
    add_damage(second, 200, BattlePhase::Main);
    archive.capture(second, ReportCaptureReason::RoundStart);
    CHECK(archive.pending_count() == 2);
    CHECK(writer.calls.size() == 1);

    writer.succeed = true;
    writer.touch_files = true;
    CHECK(archive.flush(path_utf8(root), 5000, true, status) == ReportFlushResult::Written);
    CHECK(archive.pending_count() == 1);
    CHECK(archive.flush(path_utf8(root), 5000, true, status) == ReportFlushResult::Written);
    CHECK(archive.pending_count() == 0);
    CHECK(writer.calls.size() == 3);
    if (writer.calls.size() == 3) {
        CHECK(writer.calls[1].filename == "中断-777-11.xlsx");
        CHECK(writer.calls[2].filename == "进行中-777-22.xlsx");
        const auto& old_sheets = writer.calls[1].sheets;
        const auto& new_sheets = writer.calls[2].sheets;
        CHECK(!old_sheets.empty() && old_sheets[0].main.actual_total() == 100);
        CHECK(!new_sheets.empty() && new_sheets[0].main.actual_total() == 200);
    }

    StatsModel cleared;
    begin_session(cleared, 33);
    add_damage(cleared, 33, BattlePhase::Main);
    archive.capture(cleared, ReportCaptureReason::Clear);
    CHECK(archive.flush(path_utf8(root), 6000, true, status) == ReportFlushResult::Written);
    StatsModel unloaded;
    begin_session(unloaded, 44);
    add_damage(unloaded, 44, BattlePhase::Main);
    archive.capture(unloaded, ReportCaptureReason::Unload);
    CHECK(archive.flush(path_utf8(root), 7000, true, status) == ReportFlushResult::Written);
    StatsModel false_end;
    begin_session(false_end, 55);
    add_damage(false_end, 55, BattlePhase::Main);
    // A SessionEnd reason without model.ended() must never claim a completed-game name.
    archive.capture(false_end, ReportCaptureReason::SessionEnd);
    CHECK(archive.flush(path_utf8(root), 8000, true, status) == ReportFlushResult::Written);
    CHECK(writer.calls.size() == 6);
    if (writer.calls.size() == 6) {
        CHECK(writer.calls[3].filename == "中断-777-33.xlsx");
        CHECK(writer.calls[4].filename == "中断-777-44.xlsx");
        CHECK(writer.calls[5].filename == "中断-777-55.xlsx");
    }

    g_writer = nullptr;
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_same_minute_conflict_and_in_progress_cleanup() {
    const std::filesystem::path root = fixture_root("collision");
    WriterState writer;
    g_writer = &writer;
    ReportArchive archive(9000, &test_writer);
    const std::int64_t ended_at = 1763201400;
    const std::string base = expected_final_filename(ended_at);

    StatsModel first;
    begin_session(first, 51);
    add_damage(first, 10, BattlePhase::Main);
    end_session(first, ended_at);
    archive.capture(first, ReportCaptureReason::SessionEnd);
    std::string status;
    CHECK(archive.flush(path_utf8(root), ended_at * 1000, false, status) ==
          ReportFlushResult::Written);

    StatsModel second;
    begin_session(second, 52);
    add_damage(second, 20, BattlePhase::Main);
    end_session(second, ended_at);
    archive.capture(second, ReportCaptureReason::SessionEnd);
    CHECK(archive.flush(path_utf8(root), ended_at * 1000, false, status) ==
          ReportFlushResult::Written);

    CHECK(writer.calls.size() == 2);
    if (writer.calls.size() == 2) {
        CHECK(writer.calls[0].filename == base);
        CHECK(writer.calls[1].filename == with_number_suffix(base, 2));
        CHECK(std::filesystem::exists(utf8_path(path_for(root, base))));
        CHECK(std::filesystem::exists(utf8_path(path_for(root, with_number_suffix(base, 2)))));
    }

    StatsModel live;
    begin_session(live, 53);
    add_damage(live, 30, BattlePhase::Main);
    archive.capture(live, ReportCaptureReason::Manual);
    CHECK(archive.flush(path_utf8(root), ended_at * 1000, false, status) ==
          ReportFlushResult::Written);
    const std::string in_progress = "进行中-9000-53.xlsx";
    CHECK(std::filesystem::exists(utf8_path(path_for(root, in_progress))));

    end_session(live, ended_at + 60);
    const std::string final_name = expected_final_filename(ended_at + 60);
    archive.capture(live, ReportCaptureReason::SessionEnd);
    CHECK(archive.flush(path_utf8(root), (ended_at + 60) * 1000, false, status) ==
          ReportFlushResult::Written);
    CHECK(!std::filesystem::exists(utf8_path(path_for(root, in_progress))));
    const WriterCall* final_call = successful_call(writer, final_name);
    CHECK(final_call != nullptr);
    if (!final_call) {
        // A same-minute collision may legitimately add a numeric suffix.
        for (const WriterCall& call : writer.calls) {
            if (call.success && call.filename.find(".xlsx") != std::string::npos &&
                call.filename.find("进行中-") == std::string::npos) {
                final_call = &call;
            }
        }
    }
    CHECK(final_call != nullptr);
    if (final_call) {
        CHECK(std::filesystem::exists(utf8_path(path_for(root, final_call->filename))));
    }

    g_writer = nullptr;
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_writer_exceptions_are_retryable() {
    const std::filesystem::path root = fixture_root("writer-exceptions");
    WriterState writer;
    g_writer = &writer;
    ReportArchive archive(8080, &test_writer);
    archive.set_retry_delay_ms(100);

    StatsModel model;
    begin_session(model, 700);
    add_damage(model, 70, BattlePhase::Main);
    archive.capture(model, ReportCaptureReason::Manual);
    writer.standard_exceptions_remaining = 1;

    std::string status;
    CHECK(archive.flush(path_utf8(root), 1000, false, status) == ReportFlushResult::Failed);
    CHECK(archive.pending_count() == 1);
    CHECK(status.find("写入异常") != std::string::npos);
    CHECK(writer.calls.empty());
    CHECK(archive.flush(path_utf8(root), 1100, false, status) == ReportFlushResult::Written);
    CHECK(archive.pending_count() == 0);
    CHECK(writer.calls.size() == 1);

    StatsModel second;
    begin_session(second, 701);
    add_damage(second, 71, BattlePhase::Main);
    archive.capture(second, ReportCaptureReason::Manual);
    writer.unknown_exceptions_remaining = 1;
    CHECK(archive.flush(path_utf8(root), 2000, false, status) == ReportFlushResult::Failed);
    CHECK(archive.pending_count() == 1);
    CHECK(status.find("写入异常") != std::string::npos);
    CHECK(writer.calls.size() == 1);
    CHECK(archive.flush(path_utf8(root), 2100, false, status) == ReportFlushResult::Written);
    CHECK(archive.pending_count() == 0);
    CHECK(writer.calls.size() == 2);

    g_writer = nullptr;
    std::error_code error;
    std::filesystem::remove_all(root, error);
}
void test_empty_capture_and_clear_precedence() {
    const std::filesystem::path root = fixture_root("empty-clear");
    WriterState writer;
    g_writer = &writer;
    ReportArchive archive(6161, &test_writer);

    StatsModel uninitialized;
    archive.capture(uninitialized, ReportCaptureReason::Manual);
    CHECK(archive.pending_count() == 0);

    StatsModel empty;
    begin_session(empty, 100);
    archive.capture(empty, ReportCaptureReason::Clear);
    CHECK(archive.pending_count() == 0);
    CHECK(writer.calls.empty());

    const std::int64_t ended_at = 1763201400;
    StatsModel cleared;
    begin_session(cleared, 200);
    add_damage(cleared, 50, BattlePhase::Main);
    end_session(cleared, ended_at);
    archive.capture(cleared, ReportCaptureReason::SessionEnd);
    CHECK(archive.pending_count() == 1);
    // Manual clear after an ended/partial model is still an interrupted report.
    archive.capture(cleared, ReportCaptureReason::Clear);
    CHECK(archive.pending_count() == 1);
    StatsModel empty_again;
    archive.capture(empty_again, ReportCaptureReason::Manual);
    CHECK(archive.pending_count() == 1);

    std::string status;
    CHECK(archive.flush(path_utf8(root), ended_at * 1000, true, status) ==
          ReportFlushResult::Written);
    CHECK(writer.calls.size() == 1);
    if (!writer.calls.empty()) {
        CHECK(writer.calls[0].filename == "中断-6161-200.xlsx");
    }

    StatsModel new_session;
    begin_session(new_session, 201);
    add_damage(new_session, 60, BattlePhase::Main);
    end_session(new_session, ended_at + 120);
    archive.capture(new_session, ReportCaptureReason::NewSession);
    CHECK(archive.flush(path_utf8(root), (ended_at + 120) * 1000, true, status) ==
          ReportFlushResult::Written);

    StatsModel unloaded;
    begin_session(unloaded, 202);
    add_damage(unloaded, 70, BattlePhase::Main);
    end_session(unloaded, ended_at + 240);
    archive.capture(unloaded, ReportCaptureReason::Unload);
    CHECK(archive.flush(path_utf8(root), (ended_at + 240) * 1000, true, status) ==
          ReportFlushResult::Written);

    CHECK(writer.calls.size() == 3);
    if (writer.calls.size() == 3) {
        // Once a model is genuinely ended, NewSession/Unload keep the completed timestamp name.
        CHECK(writer.calls[1].filename == expected_final_filename(ended_at + 120));
        CHECK(writer.calls[2].filename == expected_final_filename(ended_at + 240));
    }

    g_writer = nullptr;
    std::error_code error;
    std::filesystem::remove_all(root, error);
}
void test_default_writer_integration() {
    const std::filesystem::path root = fixture_root("default-writer");
    g_writer = nullptr;
    StatsModel model;
    begin_session(model, 9100);
    add_damage(model, 42, BattlePhase::Main);
    add_damage(model, 8, BattlePhase::Assist);
    ReportArchive archive(5555);
    archive.capture(model, ReportCaptureReason::Manual);
    std::string status;
    CHECK(archive.flush(path_utf8(root), 1000, false, status) == ReportFlushResult::Written);
    const std::filesystem::path report =
        utf8_path(path_for(root, "进行中-5555-9100.xlsx"));
    CHECK(std::filesystem::exists(report));
    CHECK(std::filesystem::file_size(report) > 22);
    std::ifstream stream(report, std::ios::binary);
    char signature[2]{};
    stream.read(signature, 2);
    CHECK(signature[0] == 'P' && signature[1] == 'K');
    stream.close();

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

} // namespace

int main() {
    test_multiround_dual_phase_collection();
    std::printf("%s multi-round dual-phase collection\n", g_failures == 0 ? "PASS" : "FAIL");
    const int after_multiround = g_failures;
    test_failure_backoff_ended_name_and_zero_fallback();
    std::printf("%s failure backoff / ended name / fallback\n",
                g_failures == after_multiround ? "PASS" : "FAIL");
    const int after_failure = g_failures;
    test_failure_survives_model_reset_and_interruption_names();
    std::printf("%s cross-session retention / interruption names\n",
                g_failures == after_failure ? "PASS" : "FAIL");
    const int after_cross_session = g_failures;
    test_same_minute_conflict_and_in_progress_cleanup();
    std::printf("%s same-minute conflict / in-progress cleanup\n",
                g_failures == after_cross_session ? "PASS" : "FAIL");
    const int after_collision = g_failures;
    test_writer_exceptions_are_retryable();
    std::printf("%s writer exceptions / retry\n",
                g_failures == after_collision ? "PASS" : "FAIL");
    const int after_exceptions = g_failures;
    test_empty_capture_and_clear_precedence();
    std::printf("%s empty capture / Clear precedence\n",
                g_failures == after_exceptions ? "PASS" : "FAIL");
    const int after_empty = g_failures;
    test_default_writer_integration();
    std::printf("%s default xlsx writer integration\n",
                g_failures == after_empty ? "PASS" : "FAIL");
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("all stronghold report archive tests passed");
    return 0;
}