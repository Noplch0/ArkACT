#include <ark/plugin.h>
#include <imgui.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <cstdio>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "capture.h"
#include "unity_input.h"
#include "report_archive.h"
#include "model.h"
#include "types.h"
#include "metadata_report.h"
#include "stronghold_version.h"
#include "view_state.h"

namespace ark::stronghold {
namespace {

constexpr std::uint32_t kLogInfo = 1;
constexpr std::uint32_t kLogWarning = 2;
constexpr std::uint32_t kLogError = 3;

const ArkPluginApiV1* g_host = nullptr;
StatsModel g_model;
ReportArchive g_reports;
std::uint64_t g_cursor = 0;
bool g_mode_active = false;
ViewSelection g_selection;
std::uint32_t g_timing_mask = 0;
bool g_interrupted_report = false;
bool g_queue_partial = false;
bool g_reset_pending = false;
std::string g_export_status;
std::string g_runtime_error;
std::string g_capture_error;
std::string g_input_error;
std::string g_last_connect_log;
std::unique_ptr<ark::UnityInput> g_input;
bool g_input_ready = false;
bool g_connection_reported = false;
ULONGLONG g_plugin_start_tick = 0;
ULONGLONG g_next_connect_tick = 0;
std::set<int> g_expanded_players;

void export_diagnostics(const char* reason) {
    std::ostringstream snapshot;
    snapshot << capture_diagnostics() << "\nRENDER MODEL\n"
        << "session=" << g_model.session_id() << " round=" << g_model.current_round()
        << " ended=" << g_model.ended() << " queue_cursor=" << g_cursor
        << " queue_partial=" << g_queue_partial << '\n'
        << "session_actual=" << g_model.session().actual_total()
        << " round_actual=" << g_model.round_view().actual_total()
        << " assist_actual=" << g_model.session(BattlePhase::Assist).actual_total()
        << " assist_round_actual=" << g_model.round_view(BattlePhase::Assist).actual_total() << '\n';
    for (const auto phase : {BattlePhase::Main, BattlePhase::Assist}) {
      for (const auto& [slot, player] : g_model.session(phase).players) {
        snapshot << "model phase=" << static_cast<unsigned>(phase) << " slot=" << slot << " actual=" << player.totals.actual
            << " calculated=" << player.totals.calculated << " seconds=" << player.active_time
            << " status=" << static_cast<unsigned>(player.status)
            << " operators=" << player.operators.size() << '\n';
      }
    }
    g_export_status = diagnostics::write_metadata_report(g_host->root_directory, reason, snapshot.str());
}

void set_input_capture(void* user, bool mouse, bool keyboard) {
    static_cast<ark::UnityInput*>(user)->set_capture(mouse, keyboard);
}

bool route_input_message(void* user, std::uint32_t message, std::uintptr_t wparam,
                         std::intptr_t lparam) {
    return static_cast<ark::UnityInput*>(user)->route_message(message, wparam, lparam);
}

bool input_protected(void* user) {
    return static_cast<ark::UnityInput*>(user)->protected_input();
}

void log_once(std::string& last, std::uint32_t level, const std::string& message) {
    if (last == message || !g_host) {
        return;
    }
    last = message;
    g_host->log(g_host->owner, level, message.c_str());
}

void retry_runtime_connection() {
    if (!g_host || !g_input) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < g_next_connect_tick) {
        return;
    }
    g_next_connect_tick = now + 250;

    if (!g_input_ready) {
        try {
            g_input_ready = g_input->initialize();
            if (g_input_ready) {
                g_input_error.clear();
            }
        } catch (const std::exception& error) {
            g_input_error = error.what();
            log_once(g_last_connect_log, kLogWarning,
                     std::string("Unity input adapter pending: ") + g_input_error);
        }
    }
    if (!capture_ready() && !capture_incompatible() && g_runtime_error.empty()) {
        if (initialize_capture(g_host)) {
            g_capture_error.clear();
        } else {
            const std::string current = capture_error();
            if (!current.empty() && current != g_capture_error) {
                g_capture_error = current;
                log_once(g_last_connect_log, kLogWarning,
                         std::string("Stronghold capture pending: ") + current);
            }
        }
    }
    if (g_input_ready && capture_ready() && g_runtime_error.empty() && !g_connection_reported) {
        g_connection_reported = true;
        g_capture_error.clear();
        g_input_error.clear();
        g_last_connect_log.clear();
        g_host->log(g_host->owner, kLogInfo,
                    "Stronghold ACT connected to IL2CPP runtime and native input");
    }
    const bool failed = (!g_input_ready && !g_input_error.empty()) ||
                        (!capture_ready() && !g_capture_error.empty());
    if (failed) {
        g_next_connect_tick = (std::max)(g_next_connect_tick, now + 1000);
    }
}

const char* coverage_label(Coverage coverage) {
    switch (coverage) {
    case Coverage::FullRoom: return "全房间";
    case Coverage::SelfOnly: return "仅本机";
    case Coverage::Partial: return "部分采集";
    default: return "检测中";
    }
}

std::string player_badge(const PlayerAggregate& player) {
    std::string out = player_slot_label(player.index);
    if (player.self) out += " [自己]";
    if (player.status == PlayerStatus::Eliminated) {
        out += " [淘汰]";
    } else if (player.status == PlayerStatus::Disconnected) {
        out += " [离线]";
    } else if (player.status == PlayerStatus::Quit) {
        out += " [退出]";
    } else if (player.status == PlayerStatus::Unknown) {
        out += " [状态待确认]";
    }
    if (!player.name.empty()) {
        out += " " + player.name;
    }
    return out;
}

std::string format_damage(double value) {
    char buffer[64]{};
    if (value >= 100000000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2f亿", value / 100000000.0);
    } else if (value >= 10000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f万", value / 10000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    }
    return buffer;
}

std::string format_rate(double value) {
    char buffer[64]{};
    if (value >= 10000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f万/s", value / 10000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f/s", value);
    }
    return buffer;
}

const View& active_view() {
    return g_selection.view(g_model);
}

void flush_exports(bool force = false) {
    if (!g_host) return;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string status;
    const auto result = g_reports.flush(g_host->root_directory, now, force, status);
    if (result == ReportFlushResult::Written || result == ReportFlushResult::Failed) {
        g_export_status = status;
        if (result == ReportFlushResult::Failed)
            g_host->log(g_host->owner, kLogWarning, status.c_str());
    }
}

void pump_events() {
    Event buffer[512];
    for (int batch = 0; batch < 32; ++batch) {
        bool overrun = false;
        bool partial = false;
        const std::size_t count = event_queue().read(g_cursor, buffer, 512, &overrun, &partial);
        if (overrun) {
            g_queue_partial = true;
        }
        if (partial) {
            g_queue_partial = true;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const Event& event = buffer[i];
            if (event.kind == EventKind::SessionStart) {
                g_reports.capture(g_model, g_interrupted_report ? ReportCaptureReason::Clear :
                                   ReportCaptureReason::NewSession);
                g_expanded_players.clear();
                g_reset_pending = false;
                g_queue_partial = false;
                g_interrupted_report = false;
                g_timing_mask = 0;
            }
            g_model.apply(event);
            g_cursor = (std::max)(g_cursor, event.sequence);
            if (event.kind == EventKind::SessionStart || event.kind == EventKind::Coverage) {
                g_mode_active = true;
            }
            if (event.kind == EventKind::SessionEnd) {
                g_interrupted_report = event.partial != 0;
                g_reports.capture(g_model, g_interrupted_report ? ReportCaptureReason::Clear :
                                   ReportCaptureReason::SessionEnd);
            }
            if (event.kind == EventKind::Phase) {
                g_timing_mask = event.simulated_mask;
            }
            if (event.kind == EventKind::RoundStart && g_model.pending_round_export()) {
                g_reports.capture(g_model, ReportCaptureReason::RoundStart);
                // The archive has copied both phase views for every round.
                while (g_model.pending_round_export()) g_model.clear_pending_round_export();
            }
            if (event.kind == EventKind::SessionStart) g_selection.reset();
            if (event.kind == EventKind::ModeExit) {
                g_mode_active = false;
                if (g_model.session_id()) export_diagnostics("mode exit; retained in-mode snapshot");
            }
        }
        if (count < 512) {
            break;
        }
    }
}

void on_frame(void*, const ArkFrameV1*) {
    if (!g_host) {
        return;
    }
    retry_runtime_connection();
    pump_events();
    flush_exports();
    const std::string current_capture_error = capture_error();
    if (!current_capture_error.empty()) {
        g_capture_error = current_capture_error;
        log_once(g_last_connect_log, kLogWarning,
                 std::string("Stronghold capture warning: ") + current_capture_error);
    } else if (capture_ready()) {
        g_capture_error.clear();
    }
}

constexpr ImVec4 kWarning{1.f, .68f, .2f, 1.f};
constexpr ImVec4 kInfo{.55f, .8f, 1.f, 1.f};
constexpr ImVec4 kSelf{.55f, 1.f, .68f, 1.f};

void draw_player_row(const View& view, const PlayerAggregate& player, double duration) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID(player.index);
    ImGui::PushStyleColor(ImGuiCol_Text, player.self ? kSelf : ImGui::GetStyle().Colors[ImGuiCol_Text]);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (player.operators.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    const bool open = ImGui::TreeNodeEx("player", flags, "%s", player_badge(player).c_str());
    ImGui::PopStyleColor();
    const float damage_share = static_cast<float>(share(player.totals.actual, view.actual_total()));
    ImGui::ProgressBar(damage_share, ImVec2(0, 3), "");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(format_damage(player.totals.actual).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(format_rate(per_second(player.totals.actual, duration)).c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%.1f%%", share(player.totals.actual, view.actual_total()) * 100.0);
    ImGui::TableNextColumn();
    ImGui::TextDisabled("-");
    ImGui::TableNextColumn();
    ImGui::TextDisabled("-");
    ImGui::TableNextColumn();
    ImGui::TextDisabled("-");

    if (open && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
        std::vector<const OperatorAggregate*> actors;
        actors.reserve(player.operators.size());
        for (const auto& [id, actor] : player.operators) {
            if (actor.totals.actual > 0 || actor.totals.calculated > 0 ||
                actor.totals.heal > 0 || actor.totals.elemental > 0) {
                actors.push_back(&actor);
            }
        }
        std::sort(actors.begin(), actors.end(), [](const auto* a, const auto* b) {
            return a->totals.actual == b->totals.actual ? a->id < b->id
                                                        : a->totals.actual > b->totals.actual;
        });
        for (const OperatorAggregate* actor : actors) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string operator_label = "  " + actor->name;
            if (actor->instances.size() > 1) {
                operator_label += " ×" + std::to_string(actor->instances.size());
            }
            ImGui::TextDisabled("%s", operator_label.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_damage(actor->totals.actual).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_rate(per_second(actor->totals.actual, duration)).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", share(actor->totals.actual, view.actual_total()) * 100.0);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_damage(actor->totals.calculated).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_rate(per_second(actor->totals.heal, duration)).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_damage(actor->totals.elemental).c_str());
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

std::uint32_t selected_player_mask(const View& view) {
    std::uint32_t mask = 0;
    if (g_selection.live(g_model, g_mode_active) &&
        g_model.current_phase() == g_selection.phase) mask = g_timing_mask;
    for (const auto& [slot, player] : view.players) {
        if (include_player_in_export(player)) mask |= player_bit(slot);
    }
    return mask;
}

void draw_table() {
    const View& view = active_view();
    const std::uint32_t mask = selected_player_mask(view);
    const bool show_unknown = has_totals(view.unknown.totals);
    const auto order = view.sorted_players(false, mask, show_unknown);
    // All controls and notices are above the table. Use exactly the remaining
    // space, without a phantom footer or a minimum that overlaps other items.
    const float height = ImGui::GetContentRegionAvail().y;
    if (height < 1.f) return;
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("stronghold_players", 7, flags, {0, height})) {
        return;
    }
    ImGui::TableSetupColumn("玩家 / 干员", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("实际伤害", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableSetupColumn("DPS", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("占比", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableSetupColumn("计算伤害", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("HPS", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("元素损伤", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    if (order.empty()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", g_selection.phase == BattlePhase::Assist ? "该范围尚无协防数据" : "该范围尚无战斗数据");
    }
    for (int index : order) {
        const PlayerAggregate* player = view.find_player(index);
        if (!player) {
            continue;
        }
        const double duration = player->index < 0 ? view.battle_seconds : player->active_time;
        draw_player_row(view, *player, duration);
    }
    ImGui::EndTable();
}

void draw_round_selector() {
    const auto rounds = g_selection.rounds(g_model);
    const auto selected = g_selection.whole_session ? g_model.current_round() : g_selection.round(g_model);
    const auto current = std::lower_bound(rounds.begin(), rounds.end(), selected);
    const auto& style = ImGui::GetStyle();
    const float required = 2.f * (ImGui::CalcTextSize("上一轮").x + style.FramePadding.x * 2.f) +
                           ImGui::GetFontSize() * 12.f + style.ItemSpacing.x * 3.f;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const bool same_line = right - ImGui::GetItemRectMax().x >= required;
    if (same_line) ImGui::SameLine();
    ImGui::BeginDisabled(current == rounds.begin());
    if (ImGui::SmallButton("上一轮")) g_selection.previous(g_model);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const auto number = g_selection.round(g_model);
    const std::string label = g_selection.whole_session ? "选择轮次查看" :
        "第 " + std::to_string(number) + " 轮" +
        (g_selection.latest(g_model) ? (g_model.ended() ? "（末轮）" : "（本轮）") : "（历史）");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.f);
    if (ImGui::BeginCombo("##round_selector", label.c_str())) {
        for (auto it = rounds.rbegin(); it != rounds.rend(); ++it) {
            const bool latest = *it == g_model.current_round();
            const std::string item = "第 " + std::to_string(*it) + " 轮" +
                (latest ? (g_model.ended() ? "（末轮）" : "（跟随本轮）") : "");
            if (ImGui::Selectable(item.c_str(), *it == number)) g_selection.select(*it, g_model);
            if (*it == number) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const auto next_from = g_selection.whole_session ? g_model.current_round() : g_selection.round(g_model);
    ImGui::BeginDisabled(std::upper_bound(rounds.begin(), rounds.end(), next_from) == rounds.end());
    if (ImGui::SmallButton("下一轮")) g_selection.next(g_model);
    ImGui::EndDisabled();
}

void draw_notices() {
    const bool warning = !g_runtime_error.empty() || !g_capture_error.empty() ||
        !g_input_error.empty() || g_model.partial() || g_queue_partial || !g_model.warning().empty();
    if (!warning && g_export_status.empty() && g_model.coverage() != Coverage::SelfOnly) return;
    ImGui::SameLine();
    if (warning) ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    if (ImGui::SmallButton(warning ? "采集提示 / 导出结果" : "导出结果 / 说明"))
        ImGui::OpenPopup("统计提示");
    if (warning) ImGui::PopStyleColor();
    if (ImGui::BeginPopup("统计提示")) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 36.f);
        if (!g_runtime_error.empty()) ImGui::TextColored(kWarning, "采集警告：%s", g_runtime_error.c_str());
        else if (!g_capture_error.empty()) ImGui::TextColored(kWarning, "采集警告：%s", g_capture_error.c_str());
        if (!g_input_error.empty()) ImGui::TextColored(kWarning, "输入保护：%s", g_input_error.c_str());
        if (g_model.partial() || g_queue_partial) ImGui::TextColored(kWarning, "数据不完整：仅包含已捕获事件");
        if (!g_model.warning().empty()) ImGui::TextColored(kWarning, "%s", g_model.warning().c_str());
        if (g_model.coverage() == Coverage::SelfOnly)
            ImGui::TextUnformatted("当前仅确认采集自己；队友覆盖以本地实际模拟数据为准。");
        if (!g_export_status.empty()) ImGui::TextUnformatted(g_export_status.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}

void draw_header() {
    const auto number = g_selection.whole_session ? g_model.current_round() : g_selection.round(g_model);
    ImGui::Text("%s  |  第 %u 轮  |  %s", g_selection.caption().c_str(), number,
                coverage_label(g_model.coverage()));
    ImGui::SameLine();
    ImGui::TextDisabled("ACT %s", kPluginVersion);
    const std::string scope_button = std::string("范围：") + g_selection.scope_label() + "##scope";
    if (ImGui::SmallButton(scope_button.c_str())) g_selection.toggle_scope();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("点击切换整局 / 本轮");
    ImGui::SameLine();
    const std::string phase_button = std::string("阶段：") + g_selection.phase_label() + "##phase";
    if (ImGui::SmallButton(phase_button.c_str())) g_selection.toggle_phase();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("点击切换战斗 / 协防");
    draw_round_selector();

    if (!g_mode_active) {
        ImGui::TextColored(kInfo, "上一局数据（只读）；进入新对局后自动替换");
    } else if (g_model.ended()) {
        ImGui::TextColored(kInfo, "本局已结束，可查看整局和各轮数据");
    } else if (!g_selection.whole_session && !g_selection.latest(g_model)) {
        ImGui::TextDisabled("历史轮次快照（只读） | 当前对局第 %u 轮", g_model.current_round());
    } else {
        const auto* self = g_model.self();
        if (self && self->online && !self->eliminated && self->status == PlayerStatus::Online &&
            !(g_timing_mask & player_bit(self->index)) && g_model.current_phase() == BattlePhase::Main) {
            ImGui::TextDisabled("自己已完成，等待队友：自己的 DPS 计时已暂停");
        } else if (g_model.current_phase() == BattlePhase::Idle) {
            ImGui::TextDisabled("等待 / 准备中：DPS 计时已暂停");
        } else if (g_model.current_phase() == BattlePhase::Assist && !g_timing_mask) {
            ImGui::TextDisabled("协防等待 / 已完成：DPS 计时已暂停");
        } else if (g_model.current_phase() == BattlePhase::Assist) {
            ImGui::TextColored(kInfo, "协防进行中，伤害和时间单独统计");
        }
    }
    // Resolve again after controls: totals and table use the same selection.
    const View& selected_view = active_view();
    ImGui::TextDisabled("%s伤害 %s | 有效战斗 %.1fs", g_selection.phase_label(),
                       format_damage(selected_view.actual_total()).c_str(), selected_view.battle_seconds);

    ImGui::BeginDisabled(!g_mode_active || g_model.ended() || g_reset_pending);
    if (ImGui::SmallButton("清零")) ImGui::OpenPopup("确认清零");
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("确认清零", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("清空本场累计与各轮统计？");
        ImGui::BeginDisabled(!g_mode_active || g_model.ended() || g_reset_pending);
        if (ImGui::Button("确认")) {
            request_manual_reset();
            g_reset_pending = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("导出整局工作簿")) {
        g_reports.capture(g_model, g_interrupted_report ? ReportCaptureReason::Clear : ReportCaptureReason::Manual);
        if (!g_reports.pending_count()) g_export_status = "尚无可导出的战斗数据";
        flush_exports(true);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("导出诊断")) export_diagnostics("user request; includes retained in-mode snapshot");
    draw_notices();
    if (g_reset_pending) {
        ImGui::SameLine();
        ImGui::TextDisabled("等待游戏线程清零…");
    }
}

bool host_compatible(const ArkPluginApiV1* host) {
    return host && host->size >= sizeof(ArkPluginApiV1) && host->abi_version == ARK_ABI_VERSION &&
           host->imgui_version == IMGUI_VERSION_NUM && host->on_frame && host->on_draw &&
           host->set_input_adapter && host->root_directory && host->log;
}

void on_draw(void*, const ArkFrameV1* frame) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(frame->imgui_context));
    if (!g_host) {
        return;
    }
    const bool retained_data = g_model.session_id() != 0;
    if ((!g_input_ready || !capture_ready()) && !retained_data) {
        const ULONGLONG elapsed = GetTickCount64() - g_plugin_start_tick;
        if (elapsed < 30000) {
            return;
        }
        std::string detail = g_runtime_error;
        if (detail.empty()) {
            detail = g_capture_error;
        }
        if (!g_input_error.empty()) {
            if (!detail.empty()) detail += " | ";
            detail += g_input_error;
        }
        if (detail.empty()) {
            detail = "等待 GameAssembly.dll / IL2CPP 导出完成";
        }
        ImGui::SetNextWindowSize({620.f, 190.f}, ImGuiCond_FirstUseEver);
        constexpr ImGuiWindowFlags error_flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("卫戍协议伤害统计（采集状态）", nullptr, error_flags)) {
            const bool incompatible = capture_incompatible();
            ImGui::TextColored(kWarning, "%s | %s", kPluginVersion, incompatible ? "游戏版本结构不兼容，已停止采集" : "仍在等待游戏运行时");
            const auto fields = detail.find("; available:");
            const std::string short_detail = detail.substr(0, fields);
            ImGui::TextWrapped("%s", short_detail.c_str());
            if (incompatible) {
                ImGui::TextWrapped("类型结构诊断已写入：%s", capture_report_path().c_str());
                ImGui::TextWrapped("请提供该日志，用当前游戏结构完成适配。");
            } else {
                ImGui::TextWrapped("运行时就绪后会自动连接。");
            }
        }
        ImGui::End();
        return;
    }
    if (!g_runtime_error.empty() && !retained_data) {
        return;
    }
    if (!statistics_visible(g_model, g_mode_active)) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = (std::min)(900.f, (std::max)(620.f, display.x * 0.56f));
    const float height = (std::min)(460.f, (std::max)(300.f, display.y * 0.48f));
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({(std::max)(20.f, display.x - width - 28.f), 72.f},
                            ImGuiCond_FirstUseEver);
    const float min_height = (std::max)(260.f, ImGui::GetFrameHeightWithSpacing() * 8.f);
    ImGui::SetNextWindowSizeConstraints({(std::min)(620.f, display.x), (std::min)(min_height, display.y)},
                                        {(std::max)(620.f, display.x), (std::max)(min_height, display.y)});
    if (ImGui::Begin("卫戍协议伤害统计##StrongholdACT", nullptr,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        draw_header();
        ImGui::Separator();
        draw_table();
    }
    ImGui::End();
}

bool initialize(const ArkPluginApiV1* host) {
    if (!host_compatible(host) || g_host || g_input) {
        return false;
    }
    g_host = host;
    g_model = StatsModel{};
    g_reports = ReportArchive{};
    g_cursor = 0;
    g_mode_active = false;
    g_queue_partial = false;
    g_selection.reset();
    g_timing_mask = 0;
    g_interrupted_report = false;
    g_export_status.clear();
    g_runtime_error.clear();
    g_capture_error.clear();
    g_input_error.clear();
    g_last_connect_log.clear();
    g_input_ready = false;
    g_connection_reported = false;
    g_plugin_start_tick = GetTickCount64();
    g_next_connect_tick = 0;
    g_expanded_players.clear();

    g_input = std::make_unique<ark::UnityInput>(*host);
    if (!host->test_host) {
        ArkInputAdapterV1 adapter;
        adapter.user = g_input.get();
        adapter.set_capture = set_input_capture;
        adapter.route_message = route_input_message;
        adapter.protected_input = input_protected;
        if (!host->set_input_adapter(host->owner, &adapter)) {
            host->log(host->owner, kLogError, "Could not register Stronghold ACT input adapter");
            g_input.reset();
            g_host = nullptr;
            return false;
        }
    } else {
        g_input_ready = true;
    }

    if (!host->on_frame(host->owner, on_frame, nullptr) ||
        !host->on_draw(host->owner, on_draw, nullptr)) {
        if (!host->test_host) {
            host->set_input_adapter(host->owner, nullptr);
        }
        g_input.reset();
        g_host = nullptr;
        host->log(host->owner, kLogError, "Could not register Stronghold ACT callbacks");
        return false;
    }

    if (!host->create_native_hook || !host->enable_native_hook || !host->remove_native_hook) {
        g_runtime_error = "ArkFramework host does not provide native hook APIs";
        host->log(host->owner, kLogError, g_runtime_error.c_str());
    } else {
        host->log(host->owner, kLogInfo,
                  "Stronghold ACT initialized; waiting for IL2CPP runtime");
    }
    return true;
}

bool can_unload(char* reason, std::size_t capacity) {
    if (g_input && !g_input->can_unload()) {
        if (reason && capacity) {
            std::snprintf(reason, capacity, "%s", "请先松开被浮层捕获的鼠标或按键。");
        }
        return false;
    }
    return true;
}

bool shutdown() {
    if (g_input && !g_input->can_unload()) {
        return false;
    }
    if (g_input && !g_input->shutdown()) {
        return false;
    }
    if (!shutdown_capture()) {
        return false;
    }
    // Hooks are now drained; consume every copied event before saving the
    // unload snapshot. Failed writes keep this model/archive alive for retry.
    pump_events();
    g_reports.capture(g_model, g_interrupted_report ? ReportCaptureReason::Clear : ReportCaptureReason::Unload);
    const auto pending = g_reports.pending_count();
    for (std::size_t i = 0; i < pending; ++i) flush_exports(true);
    if (g_reports.pending_count()) {
        if (g_host) g_host->log(g_host->owner, kLogWarning, "Report export failed; pending snapshots retained, unload deferred");
        return false;
    }
    if (g_host && !g_host->test_host && g_input &&
        !g_host->set_input_adapter(g_host->owner, nullptr)) {
        return false;
    }
    g_input.reset();
    g_host = nullptr;
    g_mode_active = false;
    g_input_ready = false;
    g_connection_reported = false;
    g_next_connect_tick = 0;
    g_model = StatsModel{};
    return true;
}

} // namespace
} // namespace ark::stronghold

ARK_EXPORT const ArkPluginInfoV1* ark_plugin_query() {
    static const ArkPluginInfoV1 info{
        .size = sizeof(ArkPluginInfoV1),
        .abi_version = ARK_ABI_VERSION,
        .imgui_version = IMGUI_VERSION_NUM,
        .name = "Stronghold Protocol ACT",
        .version = ark::stronghold::kPluginVersion,
        .author = "ArkF-ACT",
        .flags = ARK_PLUGIN_DEFAULT,
        .dependencies = nullptr,
        .dependency_count = 0,
    };
    return &info;
}

ARK_EXPORT bool ark_plugin_initialize(const ArkPluginApiV1* host) {
    return ark::stronghold::initialize(host);
}

ARK_EXPORT bool ark_plugin_can_unload(char* reason, size_t capacity) {
    return ark::stronghold::can_unload(reason, capacity);
}

ARK_EXPORT bool ark_plugin_shutdown() {
    return ark::stronghold::shutdown();
}
