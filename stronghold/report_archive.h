#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "xlsx_export.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace ark::stronghold {

enum class ReportCaptureReason : std::uint32_t {
    RoundStart = 0,
    Manual = 1,
    SessionEnd = 2,
    NewSession = 3,
    Clear = 4,
    Unload = 5,
};

enum class ReportSnapshotKind : std::uint32_t {
    InProgress = 0,
    Interrupted = 1,
    Complete = 2,
};

enum class ReportFlushResult : std::uint32_t {
    NoPending = 0,
    Written = 1,
    Throttled = 2,
    Failed = 3,
};

using WorkbookWriter = bool (*)(const std::string& root, const std::string& filename,
                                const std::vector<WorkbookSheet>& sheets,
                                std::string* out_path);

inline std::uint32_t current_process_id() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

inline bool local_time_from_unix(std::int64_t unix_seconds, std::tm& out) {
    const std::time_t value = static_cast<std::time_t>(unix_seconds);
#if defined(_WIN32)
    return ::localtime_s(&out, &value) == 0;
#else
    return ::localtime_r(&value, &out) != nullptr;
#endif
}

inline std::string final_report_filename(std::int64_t ended_at, std::int64_t now_ms) {
    std::int64_t seconds = ended_at;
    if (seconds <= 0) seconds = now_ms / 1000;
    if (seconds < 0) seconds = 0;
    std::tm local{};
    if (!local_time_from_unix(seconds, local)) {
        return "1970-01-01 00-00.xlsx";
    }
    char buffer[32]{};
    const int written = std::snprintf(
        buffer, sizeof(buffer), "%04d-%02d-%02d %02d-%02d.xlsx",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
    return written > 0 ? std::string(buffer, static_cast<std::size_t>(written))
                       : std::string("1970-01-01 00-00.xlsx");
}

inline std::string in_progress_report_filename(std::uint32_t process_id,
                                               std::uint64_t session_id) {
    return "进行中-" + std::to_string(process_id) + "-" + std::to_string(session_id) +
           ".xlsx";
}

inline std::string interrupted_report_filename(std::uint32_t process_id,
                                               std::uint64_t session_id) {
    return "中断-" + std::to_string(process_id) + "-" + std::to_string(session_id) + ".xlsx";
}

inline std::filesystem::path report_export_path(const std::string& root,
                                                const std::string& filename) {
    return utf8_path(root) / "exports" / utf8_path(filename);
}

class ReportArchive {
public:
    explicit ReportArchive(std::uint32_t process_id = current_process_id(),
                           WorkbookWriter writer = &write_workbook_file)
        : process_id_(process_id), writer_(writer) {}

    void set_retry_delay_ms(std::int64_t delay_ms) {
        retry_delay_ms_ = delay_ms > 0 ? delay_ms : 0;
    }

    std::uint32_t process_id() const { return process_id_; }
    std::size_t pending_count() const { return pending_.size(); }

    // Captures an immutable report snapshot. It never opens, writes, renames or deletes files.
    // An uninitialized session or a model without any reportable combat data is ignored.
    void capture(const StatsModel& model, ReportCaptureReason reason) {
        const std::uint64_t session_id = model.session_id();
        if (!session_id || !has_report_data(model)) return;
        ReportSnapshotKind kind = snapshot_kind(model, reason);
        std::vector<WorkbookSheet> sheets = collect_sheets(model);
        for (PendingReport& pending : pending_) {
            if (pending.session_id != session_id) continue;
            if (pending.kind != kind) pending.filename.clear();
            pending.kind = kind;
            pending.ended_at = model.ended_at();
            pending.sheets = std::move(sheets);
            return;
        }

        PendingReport pending;
        pending.session_id = session_id;
        pending.kind = kind;
        pending.ended_at = model.ended_at();
        pending.sheets = std::move(sheets);
        pending_.push_back(std::move(pending));
    }

    // unix_now_ms must be Unix-epoch milliseconds because it is the fallback when ended_at is 0.
    // At most one pending report is attempted per call. A failed report is throttled and kept.
    ReportFlushResult flush(const std::string& root, std::int64_t unix_now_ms, bool force,
                            std::string& status) {
        status.clear();
        if (pending_.empty()) {
            status = "没有待写报告";
            return ReportFlushResult::NoPending;
        }

        bool throttled = false;
        for (std::size_t index = 0; index < pending_.size(); ++index) {
            PendingReport& pending = pending_[index];
            if (!force && unix_now_ms < pending.retry_after_ms) {
                throttled = true;
                continue;
            }

            ensure_filename(root, pending, unix_now_ms);
            std::string out_path;
            bool written = false;
            bool writer_exception = false;
            try {
                if (writer_) {
                    written = writer_(root, pending.filename, pending.sheets, &out_path);
                }
            } catch (const std::exception&) {
                writer_exception = true;
            } catch (...) {
                writer_exception = true;
            }
            if (!written) {
                ++pending.failed_attempts;
                pending.retry_after_ms = unix_now_ms + retry_delay_ms_;
                status = (writer_exception ? "写入异常: " : "写入失败: ") +
                    pending.filename + "，第" +
                    std::to_string(pending.failed_attempts) + "次，将在" +
                    std::to_string(retry_delay_ms_) + "ms后重试";
                return ReportFlushResult::Failed;
            }

            const std::string filename = pending.filename;
            const std::uint64_t session_id = pending.session_id;
            const ReportSnapshotKind kind = pending.kind;
            const std::string written_path = out_path.empty() ? filename : out_path;
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(index));
            if (kind != ReportSnapshotKind::InProgress) {
                remove_in_progress_file(root, session_id);
            }
            status = "报告已写入: " + written_path;
            return ReportFlushResult::Written;
        }

        if (throttled) {
            status = "写入失败，仍在重试冷却期";
            return ReportFlushResult::Throttled;
        }
        status = "没有可写报告";
        return ReportFlushResult::NoPending;
    }

private:
    struct PendingReport {
        std::uint64_t session_id = 0;
        ReportSnapshotKind kind = ReportSnapshotKind::InProgress;
        std::int64_t ended_at = 0;
        std::vector<WorkbookSheet> sheets;
        std::string filename;
        std::int64_t retry_after_ms = 0;
        std::uint32_t failed_attempts = 0;
    };

    static ReportSnapshotKind snapshot_kind(const StatsModel& model,
                                            ReportCaptureReason reason) {
        // A manual clear can arrive after SessionEnd was applied with partial data. Clear is
        // deliberately stronger than model.ended(): such a report is still an interruption.
        if (reason == ReportCaptureReason::Clear) return ReportSnapshotKind::Interrupted;
        if (model.ended()) return ReportSnapshotKind::Complete;
        switch (reason) {
        case ReportCaptureReason::NewSession:
        case ReportCaptureReason::Unload:
        case ReportCaptureReason::SessionEnd:
            return ReportSnapshotKind::Interrupted;
        default:
            return ReportSnapshotKind::InProgress;
        }
    }

    static bool view_has_report_data(const View& view) {
        if (view.has_data || view.battle_seconds > 0 || has_totals(view.unknown.totals)) {
            return true;
        }
        for (const auto& [index, player] : view.players) {
            (void)index;
            if (player.active_time > 0 || has_totals(player.totals)) return true;
        }
        return false;
    }

    static bool has_report_data(const StatsModel& model) {
        if (view_has_report_data(model.session(BattlePhase::Main)) ||
            view_has_report_data(model.session(BattlePhase::Assist)) ||
            view_has_report_data(model.round_view(BattlePhase::Main)) ||
            view_has_report_data(model.round_view(BattlePhase::Assist))) {
            return true;
        }
        for (const auto& [round, views] : model.completed_rounds()) {
            (void)round;
            if (view_has_report_data(views.main) || view_has_report_data(views.assist)) {
                return true;
            }
        }
        return false;
    }

    static std::string round_sheet_name(std::uint32_t round) {
        char buffer[32]{};
        const int written = std::snprintf(buffer, sizeof(buffer), "第%02u轮", round);
        return written > 0 ? std::string(buffer, static_cast<std::size_t>(written))
                           : std::string("第") + std::to_string(round) + "轮";
    }

    static std::vector<WorkbookSheet> collect_sheets(const StatsModel& model) {
        std::vector<WorkbookSheet> sheets;
        WorkbookSheet summary;
        summary.name = "整局累计";
        summary.round = 0;
        summary.main = model.session(BattlePhase::Main);
        summary.assist = model.session(BattlePhase::Assist);
        sheets.push_back(std::move(summary));

        for (const auto& [round, views] : model.completed_rounds()) {
            WorkbookSheet sheet;
            sheet.name = round_sheet_name(round);
            sheet.round = round;
            sheet.main = views.main;
            sheet.assist = views.assist;
            sheets.push_back(std::move(sheet));
        }

        const std::uint32_t current_round = model.current_round();
        if (current_round && model.completed_rounds().find(current_round) ==
                                 model.completed_rounds().end()) {
            WorkbookSheet sheet;
            sheet.name = round_sheet_name(current_round);
            sheet.round = current_round;
            sheet.main = model.round_view(BattlePhase::Main);
            sheet.assist = model.round_view(BattlePhase::Assist);
            sheets.push_back(std::move(sheet));
        }
        return sheets;
    }

    static std::string suffixed_filename(const std::string& base, unsigned suffix) {
        constexpr const char* extension = ".xlsx";
        const std::string ext(extension);
        if (base.size() < ext.size() || base.compare(base.size() - ext.size(), ext.size(), ext) != 0) {
            return base + "(" + std::to_string(suffix) + ")";
        }
        return base.substr(0, base.size() - ext.size()) + "(" + std::to_string(suffix) + ")" +
               ext;
    }

    static bool path_exists(const std::filesystem::path& path) {
        std::error_code error;
        return std::filesystem::exists(path, error);
    }

    void ensure_filename(const std::string& root, PendingReport& pending,
                         std::int64_t unix_now_ms) {
        if (!pending.filename.empty()) return;
        if (pending.kind == ReportSnapshotKind::InProgress) {
            pending.filename = in_progress_report_filename(process_id_, pending.session_id);
            return;
        }
        if (pending.kind == ReportSnapshotKind::Interrupted) {
            pending.filename = interrupted_report_filename(process_id_, pending.session_id);
            return;
        }

        const auto locked = final_names_.find(pending.session_id);
        if (locked != final_names_.end()) {
            pending.filename = locked->second;
            return;
        }

        const std::string base = final_report_filename(pending.ended_at, unix_now_ms);
        std::string candidate = base;
        for (unsigned suffix = 2; suffix < 100000; ++suffix) {
            if (reserved_final_names_.count(candidate) == 0 &&
                !path_exists(report_export_path(root, candidate))) {
                break;
            }
            candidate = suffixed_filename(base, suffix);
        }
        pending.filename = candidate;
        final_names_[pending.session_id] = candidate;
        reserved_final_names_.insert(candidate);
    }

    void remove_in_progress_file(const std::string& root, std::uint64_t session_id) const {
        std::error_code error;
        std::filesystem::remove(
            report_export_path(root, in_progress_report_filename(process_id_, session_id)),
            error);
    }

    std::uint32_t process_id_ = 0;
    WorkbookWriter writer_ = nullptr;
    std::int64_t retry_delay_ms_ = 5000;
    std::deque<PendingReport> pending_;
    std::map<std::uint64_t, std::string> final_names_;
    std::set<std::string> reserved_final_names_;
};

} // namespace ark::stronghold