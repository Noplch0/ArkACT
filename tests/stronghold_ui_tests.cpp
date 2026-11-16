// Run the actual plugin drawing code against a headless Dear ImGui context.
// All game/host entry points are stubs: no game modules or processes are used.
#include "../stronghold/plugin.cpp"
#include <imgui_internal.h>
#include <cstdarg>
#include <map>

namespace {
int failures = 0;
#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); ++failures; } } while (0)
struct Item { ImRect bounds; ImGuiItemStatusFlags flags = 0; };
std::map<ImGuiID, ImRect> item_bounds;
std::map<std::string, Item> items;
std::string frame_text;
bool ready = true;
bool reset_requested = false;
ark::stronghold::EventQueue queue;
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bounds, const ImGuiLastItemData*) {
    item_bounds[id] = bounds;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    items.try_emplace(label, Item{item_bounds[id], flags});
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

namespace ark {
UnityInput::UnityInput(const ArkPluginApiV1& host) : host_(host) {}
UnityInput::~UnityInput() = default;
bool UnityInput::initialize() { return true; }
void UnityInput::set_capture(bool, bool) {}
bool UnityInput::route_message(std::uint32_t, std::uintptr_t, std::intptr_t) { return false; }
bool UnityInput::protected_input() const { return false; }
bool UnityInput::can_unload() const { return true; }
bool UnityInput::shutdown() { return true; }
}
namespace ark::stronghold {
EventQueue& event_queue() { return queue; }
bool initialize_capture(const ArkPluginApiV1*) { return ready; }
bool shutdown_capture() { return true; }
bool capture_ready() { return ready; }
bool capture_incompatible() { return false; }
std::string capture_report_path() { return {}; }
std::string capture_error() { return {}; }
std::string capture_diagnostics() { return "headless test"; }
void request_manual_reset() { reset_requested = true; }
namespace diagnostics {
std::string write_metadata_report(const char*, const char*, const std::string&) { return "test report"; }
}
}

namespace {
using namespace ark::stronghold;
constexpr const char* window_name = "卫戍协议伤害统计##StrongholdACT";
ArkPluginApiV1 host{};

void log_message(void*, std::uint32_t, const char*) {}
Event make_event(EventKind kind, std::uint32_t round = 1, std::uint64_t session = 100) {
    Event e;
    e.kind = kind; e.round = round; e.session_id = session;
    return e;
}
void publish(Event e) { queue.publish(e); pump_events(); }
void hit(std::uint32_t round, BattlePhase phase, double amount, int player = 1,
         std::uint32_t card = 11, const char* name = "干员甲") {
    auto e = make_event(EventKind::Damage, round);
    e.phase = phase; e.actual_damage = amount; e.calculated_damage = amount;
    e.damage_type = 1; e.player_index = player; e.card_uid = card;
    copy_text(e.actor_id, sizeof(e.actor_id), "actor-" + std::to_string(card));
    copy_text(e.actor_name, sizeof(e.actor_name), name);
    publish(e);
}
void clock_event(std::uint32_t round, BattlePhase phase, double seconds) {
    auto e = make_event(EventKind::Clock, round);
    e.phase = phase; e.delta_time = seconds; e.simulated_mask = player_bit(1);
    publish(e);
}
void setup_model() {
    queue.clear(); g_cursor = 0; g_model = StatsModel{}; g_reports = ReportArchive{};
    g_selection.reset(); g_mode_active = false; g_reset_pending = false;
    g_runtime_error.clear(); g_capture_error.clear(); g_input_error.clear();
    g_export_status.clear(); g_queue_partial = false; g_interrupted_report = false;
    publish(make_event(EventKind::SessionStart));
    for (int slot = 1; slot <= 4; ++slot) {
        auto e = make_event(EventKind::Roster);
        e.player_index = slot; e.self = slot == 1; e.online = 1;
        copy_text(e.player_name, sizeof(e.player_name), "玩家" + std::to_string(slot));
        publish(e);
    }
    auto coverage = make_event(EventKind::Coverage);
    coverage.coverage = static_cast<std::uint32_t>(Coverage::FullRoom);
    coverage.simulated_mask = coverage.active_mask = player_bit(1) | player_bit(2) | player_bit(3) | player_bit(4);
    publish(coverage);
    hit(1, BattlePhase::Main, 100); clock_event(1, BattlePhase::Main, 10);
    hit(1, BattlePhase::Assist, 40); clock_event(1, BattlePhase::Assist, 2);
    publish(make_event(EventKind::RoundStart, 2));
    hit(2, BattlePhase::Main, 200); clock_event(2, BattlePhase::Main, 5);
    hit(2, BattlePhase::Assist, 60); clock_event(2, BattlePhase::Assist, 3);
    publish(make_event(EventKind::RoundStart, 8));
    hit(8, BattlePhase::Main, 800); clock_event(8, BattlePhase::Main, 8);
}

void frame(bool capture_text = false) {
    item_bounds.clear(); items.clear();
    ImGui::NewFrame();
    if (capture_text) ImGui::LogToBuffer();
    ArkFrameV1 f;
    f.imgui_context = ImGui::GetCurrentContext();
    on_draw(nullptr, &f);
    if (capture_text) {
        frame_text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
    }
    ImGui::Render();
}
void click(const char* label) {
    ImRect bounds;
    if (items.contains(label)) bounds = items.at(label).bounds;
    else {
        auto* window = ImGui::FindWindowByName(window_name);
        const auto id = window ? window->GetID(label) : 0;
        CHECK(item_bounds.contains(id));
        if (!item_bounds.contains(id)) {
            std::fprintf(stderr, "missing UI item: %s\n", label);
            return;
        }
        bounds = item_bounds.at(id);
    }
    const auto center = bounds.GetCenter();
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(center.x, center.y); frame();
    io.AddMouseButtonEvent(0, true); frame();
    io.AddMouseButtonEvent(0, false); frame();
    frame();
}
ImGuiTable* table() {
    ImGuiWindow* window = ImGui::FindWindowByName(window_name);
    return window ? ImGui::GetCurrentContext()->Tables.GetByKey(window->GetID("stronghold_players")) : nullptr;
}
void check_layout(float width, float height) {
    ImGui::SetWindowSize(window_name, {width, height});
    frame(); frame();
    const auto* parent = ImGui::FindWindowByName(window_name);
    const auto* grid = table();
    CHECK(parent != nullptr && grid != nullptr);
    if (!parent || !grid) return;
    const float expected_bottom = parent->InnerRect.Max.y - ImGui::GetStyle().WindowPadding.y;
    CHECK(std::abs(grid->OuterRect.Max.y - expected_bottom) <= 2.f);
    CHECK(grid->OuterRect.GetHeight() > ImGui::GetTextLineHeightWithSpacing());
    CHECK(grid->OuterRect.Min.y >= parent->InnerRect.Min.y);
    CHECK(grid->OuterRect.Max.y <= parent->InnerRect.Max.y);
    CHECK(parent->ScrollMax.y < 1.f);
    CHECK(grid->FreezeRowsRequest == 1);
}

void test_selection_and_lifecycle() {
    setup_model();
    CHECK(g_selection.caption() == "整局-战斗");
    CHECK(g_selection.rounds(g_model) == std::vector<std::uint32_t>({1, 2, 8}));
    CHECK(active_view().actual_total() == 1100);
    g_selection.toggle_phase();
    CHECK(g_selection.caption() == "整局-协防" && active_view().actual_total() == 100);
    g_selection.select(1, g_model);
    CHECK(g_selection.caption() == "本轮-协防" && active_view().actual_total() == 40);
    CHECK(!g_selection.live(g_model, true));
    g_selection.toggle_scope();
    CHECK(g_selection.previous(g_model) && g_selection.round(g_model) == 2);
    g_selection.select(1, g_model);
    publish(make_event(EventKind::RoundStart, 9));
    CHECK(g_selection.round(g_model) == 1 && g_selection.phase == BattlePhase::Assist);
    CHECK(active_view().actual_total() == 40);
    CHECK(!g_selection.previous(g_model));
    CHECK(g_selection.next(g_model) && g_selection.round(g_model) == 2);
    CHECK(g_selection.next(g_model) && g_selection.round(g_model) == 8);
    CHECK(g_selection.next(g_model) && g_selection.round(g_model) == 9);
    CHECK(g_selection.selected_round == 0 && !g_selection.next(g_model));
    g_selection.select(77, g_model);
    CHECK(g_selection.round(g_model) == 9);
    publish(make_event(EventKind::RoundStart, 10));
    CHECK(g_selection.round(g_model) == 10);
    auto end = make_event(EventKind::SessionEnd, 10); end.wall_time = 1794660000;
    publish(end); publish(make_event(EventKind::ModeExit, 10));
    CHECK(!g_mode_active && statistics_visible(g_model, false));
    CHECK(!g_selection.live(g_model, false));
    g_selection.select(1, g_model);
    CHECK(active_view().actual_total() == 40);
    publish(make_event(EventKind::SessionStart, 1, 200));
    CHECK(g_selection.caption() == "整局-战斗" && g_selection.selected_round == 0);
    CHECK(g_model.completed_rounds().empty() && active_view().actual_total() == 0);
    std::puts("PASS selection / pinned history / follow latest / only latest session");
}

void test_actual_buttons_and_layout() {
    setup_model();
    frame(); frame();
    click("上一轮");
    CHECK(!g_selection.whole_session && g_selection.round(g_model) == 2);
    CHECK(active_view().actual_total() == 200);
    click("范围：本轮##scope");
    CHECK(g_selection.whole_session && active_view().actual_total() == 1100);
    click("范围：整局##scope");
    CHECK(!g_selection.whole_session && active_view().actual_total() == 200);
    click("阶段：战斗##phase");
    CHECK(g_selection.phase == BattlePhase::Assist && active_view().actual_total() == 60);
    click("上一轮");
    CHECK(g_selection.round(g_model) == 1 && active_view().actual_total() == 40);
    click("下一轮");
    CHECK(g_selection.round(g_model) == 2 && active_view().actual_total() == 60);
    click("##round_selector");
    CHECK(items.contains("第 1 轮"));
    click("第 1 轮");
    CHECK(g_selection.round(g_model) == 1 && active_view().actual_total() == 40);
    click("下一轮");
    frame(true);
    const std::string text = frame_text;
    CHECK(text.find("本轮-协防") != std::string::npos && text.find("第 2 轮") != std::string::npos);
    CHECK(text.find("历史轮次快照") != std::string::npos);
    check_layout(900, 560); check_layout(620, 260);
    g_capture_error = std::string(2000, 'x'); g_export_status = std::string(2000, 'p');
    frame();
    const float before = table()->OuterRect.GetHeight();
    check_layout(620, 260);
    CHECK(std::abs(table()->OuterRect.GetHeight() - before) < 1.f);
    const ImRect closed = table()->OuterRect;
    click("采集提示 / 导出结果");
    frame(); frame();
    CHECK(std::abs(table()->OuterRect.Min.y - closed.Min.y) < 1.f);
    CHECK(std::abs(table()->OuterRect.Max.y - closed.Max.y) < 1.f);
    CHECK(ImGui::GetCurrentContext()->OpenPopupStack.Size > 0);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true); frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false); frame();
    // Exercise the narrow layout with enlarged fonts as used at high DPI.
    auto* parent = ImGui::FindWindowByName(window_name);
    parent->FontWindowScale = 1.5f;
    check_layout(620, 260);
    CHECK(items.at("上一轮").bounds.Min.y > items.at("阶段：协防##phase").bounds.Min.y);
    CHECK(items.at("下一轮").bounds.Max.x <= parent->InnerRect.Max.x);
    parent->FontWindowScale = 1.f;
    frame(); frame();
    std::puts("PASS actual scope / phase / previous-next buttons / labels / compact table geometry");
}

void test_phase_membership() {
    setup_model();
    hit(8, BattlePhase::Assist, 70, 2);
    CHECK(selected_player_mask(active_view()) == player_bit(1));
    g_selection.select(8, g_model);
    CHECK(selected_player_mask(active_view()) == player_bit(1));
    g_selection.toggle_phase();
    CHECK(selected_player_mask(active_view()) == player_bit(2));
    publish(make_event(EventKind::RoundStart, 9));
    g_selection.select(8, g_model);
    g_timing_mask = player_bit(3);
    CHECK(selected_player_mask(active_view()) == player_bit(2));
    g_selection.toggle_phase();
    CHECK(selected_player_mask(active_view()) == player_bit(1));
    std::puts("PASS selected phase membership / historical views ignore current timing mask");
}

void test_out_of_game_and_scrolling() {
    setup_model();
    g_selection.select(8, g_model);
    for (std::uint32_t i = 0; i < 40; ++i) hit(8, BattlePhase::Main, 1, 1, 100 + i, "多行干员");
    frame(); frame();
    std::string player_label;
    for (const auto& [label, item] : items) {
        if ((item.flags & ImGuiItemStatusFlags_Openable) && label.find("玩家1") != std::string::npos) {
            player_label = label;
            if (!(item.flags & ImGuiItemStatusFlags_Opened)) click(player_label.c_str());
            break;
        }
    }
    CHECK(!player_label.empty());
    frame();
    auto* grid = table();
    CHECK(grid && grid->InnerWindow->ScrollMax.y > 0);
    if (grid) {
        ImGui::SetScrollY(grid->InnerWindow, grid->InnerWindow->ScrollMax.y);
        frame(); frame();
        CHECK(table()->InnerWindow->Scroll.y > 0 && table()->FreezeRowsCount == 1);
    }
    const auto total = active_view().actual_total();
    auto end = make_event(EventKind::SessionEnd, 8); end.wall_time = 1794660000;
    publish(end); publish(make_event(EventKind::ModeExit, 8));
    ready = false; g_input_ready = false;
    frame(true);
    auto* window = ImGui::FindWindowByName(window_name);
    CHECK(window && window->Active);
    CHECK(active_view().actual_total() == total);
    CHECK(frame_text.find("上一局数据") != std::string::npos);
    click("清零");
    CHECK(!reset_requested && !g_reset_pending);
    click("上一轮");
    CHECK(g_selection.round(g_model) == 2 && active_view().actual_total() == 200);
    ready = true; g_input_ready = true;
    std::puts("PASS expanded rows scroll / frozen header / post-game review without live runtime / reset disabled");
}
}

int main() {
    host.root_directory = "."; host.log = log_message; host.test_host = 1;
    ark::stronghold::g_host = &host; ark::stronghold::g_input_ready = true;
    auto* ctx = ImGui::CreateContext();
    ctx->TestEngineHookItems = true;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {1280, 720}; io.DeltaTime = 1.f / 60.f;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\msyh.ttc") != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 16.f, nullptr,
                                   io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    unsigned char* pixels = nullptr; int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    test_selection_and_lifecycle();
    test_actual_buttons_and_layout();
    test_phase_membership();
    test_out_of_game_and_scrolling();
    ImGui::DestroyContext();
    ark::stronghold::g_host = nullptr;
    return failures ? 1 : 0;
}
