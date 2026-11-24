#include "capture.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ark/plugin.h>

#include "il2cpp_exports.h"
#include "il2cpp_runtime.h"
#include "metadata_report.h"
#include "capture_policy.h"

namespace ark::stronghold {
namespace {

constexpr std::uint32_t kSideAlly = 1;
constexpr std::uint32_t kModifierContinuous = 1;
constexpr std::uint32_t kModifierEnvironment = 32;

using LogModifierFn = void (*)(void*, void*, void*);
using AllStateReadFn = void (*)(void*, void*, void*);
using PreparationReadFn = void (*)(void*, void*, void*);
using InstanceMethodFn = void (*)(void*, void*);
using ReferenceGetter = void* (*)(void*, void*);
using BoolGetter = bool (*)(void*, void*);
using IntGetter = int (*)(void*, void*);
using UIntGetter = std::uint32_t (*)(void*, void*);
using FixedPointGetter = std::int64_t (*)(void*, void*);
using ClockGetter = std::int64_t (*)(void*);

EventQueue g_events;
std::atomic<bool> g_reset_requested{false};
std::atomic<bool> g_capture_ready{false};
std::atomic<bool> g_capture_incompatible{false};
std::string g_report_path;
std::mutex g_error_mutex;
std::string g_error;
std::mutex g_diagnostics_mutex;
std::string g_diagnostics = "Capture has not sampled the game thread yet.\n";
std::mutex g_match_intel_mutex;
MatchIntelSnapshot g_match_intel;
std::mutex g_hidden_sync_mutex;
HiddenSyncSnapshot g_hidden_sync;
std::uint64_t g_hidden_sync_sequence = 0;
std::mutex g_shop_observation_mutex;
std::deque<ShopObservation> g_shop_observations;
bool g_shop_observations_overrun = false;
std::uint64_t g_shop_observation_session_id = 0;
constexpr std::size_t kMaxShopObservations = 1024;
std::mutex g_lifecycle_mutex;
LogModifierFn original_log_modifier = nullptr;
LogModifierFn original_log_ep_modifier = nullptr;
InstanceMethodFn original_start_game = nullptr;
InstanceMethodFn original_clear_runtime_data = nullptr;
InstanceMethodFn original_on_destroy = nullptr;
InstanceMethodFn original_update = nullptr;
InstanceMethodFn original_global_update = nullptr;
LogModifierFn original_applied_modifier = nullptr;
AllStateReadFn original_all_state_read = nullptr;
PreparationReadFn original_preparation_read = nullptr;

void set_error(const std::string& error) {
    std::lock_guard lock(g_error_mutex);
    g_error = error;
}

std::string get_error() {
    std::lock_guard lock(g_error_mutex);
    return g_error;
}

double from_fixed_point(std::int64_t raw) {
    return static_cast<double>(raw) / 4294967296.0;
}

template <class T>
T read_value(void* object, void* field, il2cpp::Runtime& runtime) {
    T value{};
    if (object && field) {
        value = runtime.read_field<T>(object, field);
    }
    return value;
}

void* read_object(void* object, void* field, il2cpp::Runtime& runtime) {
    return runtime.read_reference(object, field);
}

template <class T>
T read_raw_value(void* object, std::size_t offset) noexcept {
    T value{};
    if (object) std::memcpy(&value, static_cast<unsigned char*>(object) + offset, sizeof(T));
    return value;
}

void* read_raw_reference(void* object, std::size_t offset) noexcept {
    return read_raw_value<void*>(object, offset);
}

std::size_t raw_field_offset(int (*field_offset)(void*), void* field) {
    if (!field_offset || !field) throw std::runtime_error("Missing value-type field offset");
    const int raw = field_offset(field);
    if (raw < 2 * static_cast<int>(sizeof(void*)))
        throw std::runtime_error("Value-type field offset lacks object header");
    return static_cast<std::size_t>(raw - 2 * sizeof(void*));
}

constexpr std::size_t kMaxIntelItems = 32;

void append_string_list(il2cpp::Runtime& runtime, void* sequence,
                        std::vector<std::string>& output, std::size_t limit = kMaxIntelItems) {
    if (!sequence) return;
    const int count = runtime.count(sequence);
    const int bounded = (std::min)(count, static_cast<int>(limit));
    output.reserve(output.size() + static_cast<std::size_t>((std::max)(0, bounded)));
    for (int i = 0; i < bounded; ++i) {
        try {
            const std::string value = runtime.string(runtime.at(sequence, i));
            if (!value.empty()) output.push_back(value);
        } catch (...) {
        }
    }
}

void clear_match_intel() {
    std::lock_guard lock(g_match_intel_mutex);
    g_match_intel = MatchIntelSnapshot{};
}

void publish_match_intel(MatchIntelSnapshot snapshot) {
    std::lock_guard lock(g_match_intel_mutex);
    g_match_intel = std::move(snapshot);
}

void clear_hidden_sync() {
    std::lock_guard lock(g_hidden_sync_mutex);
    g_hidden_sync = HiddenSyncSnapshot{};
    g_hidden_sync_sequence = 0;
}

void reset_hidden_sync_for_session(std::uint64_t session_id) {
    std::lock_guard lock(g_hidden_sync_mutex);
    g_hidden_sync = HiddenSyncSnapshot{};
    g_hidden_sync.session_id = session_id;
}

void publish_hidden_sync(HiddenSyncSnapshot snapshot) {
    std::lock_guard lock(g_hidden_sync_mutex);
    snapshot.sequence = ++g_hidden_sync_sequence;
    g_hidden_sync = std::move(snapshot);
}

void reset_shop_observations(std::uint64_t session_id) {
    std::lock_guard lock(g_shop_observation_mutex);
    g_shop_observations.clear();
    g_shop_observations_overrun = false;
    g_shop_observation_session_id = session_id;
}

void stop_shop_observations() {
    std::lock_guard lock(g_shop_observation_mutex);
    g_shop_observation_session_id = 0;
}

void publish_shop_observation(ShopObservation observation) {
    std::lock_guard lock(g_shop_observation_mutex);
    if (!observation.session_id || observation.session_id != g_shop_observation_session_id) return;
    if (g_shop_observations.size() == kMaxShopObservations) {
        g_shop_observations.pop_front();
        g_shop_observations_overrun = true;
    }
    g_shop_observations.push_back(std::move(observation));
}

void* find_field_in_hierarchy(il2cpp::Runtime& runtime, void* klass, const char* name) {
    for (void* owner = klass; owner; owner = runtime.class_parent(owner)) {
        if (void* field = runtime.class_field(owner, name)) {
            return field;
        }
    }
    return nullptr;
}

void* optional_field(il2cpp::Runtime& runtime, void* klass, const char* name) {
    try {
        return runtime.field(klass, name);
    } catch (...) {
        return nullptr;
    }
}

void* optional_method(il2cpp::Runtime& runtime, void* klass, const char* name, int count = 0) {
    try {
        return runtime.method(klass, name, count);
    } catch (...) {
        return nullptr;
    }
}

std::string read_string_field(il2cpp::Runtime& runtime, void* object, void* klass,
                              const char* name) {
    if (!object || !klass) {
        return {};
    }
    void* field = find_field_in_hierarchy(runtime, klass, name);
    return field ? runtime.string(read_object(object, field, runtime)) : std::string{};
}

void* find_class_in_torappu_battle(il2cpp::Runtime& runtime, const char* name) {
    try {
        using ClassCountFn = std::size_t (*)(void*);
        using ClassAtFn = void* (*)(void*, std::size_t);
        const ClassCountFn class_count =
            il2cpp::resolve_export<ClassCountFn>("il2cpp_image_get_class_count");
        const ClassAtFn class_at =
            il2cpp::resolve_export<ClassAtFn>("il2cpp_image_get_class");
        std::size_t assembly_count = 0;
        void** assemblies = runtime.domain_assemblies(runtime.domain_get(), &assembly_count);
        for (std::size_t i = 0; i < assembly_count; ++i) {
            void* image = runtime.assembly_image(assemblies[i]);
            if (!image) {
                continue;
            }
            const std::size_t count = class_count(image);
            for (std::size_t index = 0; index < count; ++index) {
                void* klass = class_at(image, index);
                if (!klass || std::strcmp(runtime.class_name(klass), name) != 0) {
                    continue;
                }
                const char* name_space = runtime.class_namespace(klass);
                if (name_space && std::strncmp(name_space, "Torappu.Battle", 14) == 0) {
                    return klass;
                }
            }
        }
    } catch (...) {
    }
    return nullptr;
}

void* find_class_with_fallback(il2cpp::Runtime& runtime, const char* name,
                               std::initializer_list<const char*> namespaces) {
    std::string last_error = std::string("Missing class: ") + name;
    for (const char* name_space : namespaces) {
        try {
            return runtime.find(name_space, name);
        } catch (const std::exception& error) {
            last_error = error.what();
        }
    }
    if (void* klass = find_class_in_torappu_battle(runtime, name)) {
        return klass;
    }
    throw std::runtime_error(std::string("Missing Torappu.Battle class: ") + name +
                             " (last lookup: " + last_error + ")");
}

template <class Getter>
void bind_getter(il2cpp::Runtime& runtime, Getter& getter, void*& info, void* klass,
                 const char* name) {
    info = runtime.method(klass, name, 0);
    std::uint32_t implementation = 0;
    const bool is_static = (runtime.method_flags(info, &implementation) & 0x10u) != 0;
    if (is_static != std::is_same_v<Getter, ClockGetter>) {
        throw std::runtime_error(std::string("Getter static/instance ABI mismatch: ") + name);
    }
    void* result_class = runtime.class_from_type(runtime.method_return(info));
    std::uint32_t alignment = 0;
    if constexpr (std::is_same_v<Getter, ReferenceGetter>) {
        if (!result_class || runtime.class_is_value(result_class))
            throw std::runtime_error(std::string("Expected reference getter: ") + name);
    } else {
        constexpr std::size_t expected = std::is_same_v<Getter, BoolGetter> ? 1 :
            (std::is_same_v<Getter, FixedPointGetter> || std::is_same_v<Getter, ClockGetter> ? 8 : 4);
        if (!result_class || !runtime.class_is_value(result_class) ||
            runtime.class_value_size(result_class, &alignment) != expected)
            throw std::runtime_error(std::string("Getter return ABI mismatch: ") + name);
    }
    void* target = *static_cast<void**>(info);
    if (!il2cpp::is_executable(target)) {
        throw std::runtime_error(std::string("Method is not executable: ") + name);
    }
    getter = reinterpret_cast<Getter>(target);
}

void* entry_point(void* method) {
    return *static_cast<void**>(method);
}

struct HostInfo {
    std::uint32_t unique_id = 0;
    std::uint32_t host_uid = 0;
    bool token = false;
    std::string id;
    std::string name;
};

struct Attribution {
    int player = kUnknownPlayer;
    std::uint32_t canonical_card = 0;
    HostInfo host;
};

struct PlayerInfo {
    int index = kUnknownPlayer;
    std::string uid;
    std::string name;
    std::string band_id;
    bool band_available = false;
    std::vector<IntelSquadSlot> squad_slots;
    bool squad_available = false;
    int shop_level = 0;
    bool shop_level_available = false;
    int shop_coin = 0;
    bool shop_coin_available = false;
    bool self = false;
    bool online = true;
    bool eliminated = false;
    PlayerStatus status = PlayerStatus::Online;
    int hp = 0;
    int connect_state = 0;
    int battle_state = -1;
    bool game_finished = false;
    bool dead = false;
};

} // namespace

class NativeCapture {
    friend struct CaptureTests;
public:
    explicit NativeCapture(const ArkPluginApiV1& host) : host_(host) {}
    ~NativeCapture() = default;
    NativeCapture(const NativeCapture&) = delete;
    NativeCapture& operator=(const NativeCapture&) = delete;

    bool initialize() {
        std::lock_guard lifecycle(lifecycle_mutex_);
        if (ready_) {
            return true;
        }
        bool binding_schema = false;
        try {
            runtime_.initialize();
            il2cpp::Runtime::Attachment attachment(runtime_);
            binding_schema = true;
            bind_classes();
            bind_fields();
            bind_getters();
            bind_hooks();
            binding_schema = false;
            ready_ = true;
            return true;
        } catch (const std::exception& error) {
            shutdown_locked();
            set_error(error.what());
            if (binding_schema && !hook_schema_validated_) {
                g_capture_incompatible.store(true);
                g_report_path = diagnostics::write_metadata_report(host_.root_directory,
                                                                    error.what());
                if (host_.log) {
                    const std::string message = "Stronghold game schema incompatible: " +
                        std::string(error.what()) + " | metadata report: " + g_report_path;
                    host_.log(host_.owner, 3, message.c_str());
                }
            }
            return false;
        }
    }

    bool shutdown() {
        std::lock_guard lifecycle(lifecycle_mutex_);
        return shutdown_locked();
    }

    bool has_live_hooks() const { return !hooks_.empty(); }

private:
    class CallbackScope {
    public:
        explicit CallbackScope(NativeCapture* capture) {
            if (!capture) {
                return;
            }
            std::lock_guard lock(capture->callback_mutex_);
            if (capture->callbacks_enabled_) {
                capture_ = capture;
                ++capture_->callbacks_in_flight_;
            }
        }
        ~CallbackScope() {
            if (!capture_) {
                return;
            }
            std::lock_guard lock(capture_->callback_mutex_);
            if (--capture_->callbacks_in_flight_ == 0) {
                capture_->callbacks_drained_.notify_all();
            }
        }
        CallbackScope(const CallbackScope&) = delete;
        CallbackScope& operator=(const CallbackScope&) = delete;
        NativeCapture* get() const { return capture_; }
        bool recording() const { return capture_ && capture_->host_active_.load(); }
    private:
        NativeCapture* capture_ = nullptr;
    };

    void bind_classes();
    void bind_fields();
    void bind_indexer();
    void bind_getters();
    void bind_timing();
    void bind_hooks();
    bool bind_hidden_sync_schema();
    bool bind_shop_counter_schema();
    void bind_chess_descriptors();
    bool install_hidden_sync_hook();
    bool install_preparation_hook();
    void capture_hidden_sync(void* all_state) noexcept;
    void capture_shop_observation(void* preparation) noexcept;
    int canonical_shop_player_slot(int raw_index) const noexcept;
    ShopObservation read_shop_observation(void* preparation) const;
    IntelChessRecord describe_chess(int identifier, int count) const;
    IntelChessRecord describe_chess_info(void* boxed, int identifier, int count) const;
    std::string resolve_operator_name(void* char_id, const std::string& id) const;
    void refresh_shop_catalog(void* data_center);
    IntelChessRecord shop_catalog_entry(int identifier, const std::string& chess_id = {}) const;
    std::string resolve_boss_name(void* data_center, void* boss_id_object,
                                  const std::string& boss_id) const;
    std::vector<IntelChessRecord> read_hidden_chess_list(void* sequence) const;
    std::vector<IntelChessRecord> read_hidden_gained_list(void* sequence) const;
    IntelHiddenStore read_hidden_store(void* store) const;
    std::vector<IntelShopGood> read_hidden_store_goods(void* sequence) const;
    bool shutdown_locked();
    void tick_runtime(bool force = false);
    void leave_mode();
    void snapshot_diagnostics() noexcept;
    void remember_failure(const std::string& context, const std::exception& error) const;
    bool inside_applied_modifier(void* modifier);
    void set_controller(void* controller);
    void begin_battle(void* controller, bool partial);
    void end_battle();
    void update_battle(void* controller);
    void record_damage(void* logger, void* modifier, bool elemental, bool applied = false,
                       std::optional<BattlePhase> phase_hint = std::nullopt, void* applied_target = nullptr);
    BattlePhase read_damage_phase() noexcept;
    void publish_clock(double delta);
    void sample_timing(bool advance_clock = true, bool allow_session_end = true);
    void reset_timing();
    bool player_in_combat(void* center, int slot);
    std::uint32_t list_player_mask(void* sequence);
    void publish_roster(const std::vector<PlayerInfo>& players);
    void publish_coverage(std::uint32_t simulated, std::uint32_t active);
    void publish_warning(const std::string& message);
    void publish_session_end(bool interrupted = false);
    void publish_session_start(bool partial);
    void publish_round_start();
    double game_clock() const;
    int read_battle_state(void* controller) const;
    int read_controller_player_count() const;
    void refresh_index_maps(void* data_center);
    void rebuild_host_cache();
    int player_for_card(std::uint32_t card_uid) const;
    int retained_player_for_card(std::uint32_t card_uid) const;
    bool enemy_damage_target(void* target) const;
    bool dictionary_int_int(void* dictionary, int key, int& value) const;
    bool dictionary_uint_int(void* dictionary, std::uint32_t key, int& value) const;
    bool dictionary_int_uint(void* dictionary, int key, std::uint32_t& value) const;
    HostInfo host_for(std::uint32_t card_uid) const;
    void store_host(std::uint32_t card_uid, const HostInfo& host);
    bool resolve_character(void* entity, HostInfo& host, int& player, std::uint32_t& card_uid);
    void read_players(void* player_model, int self_index, std::vector<PlayerInfo>& players,
                      int& self_out, std::string& self_uid) const;
    bool read_player(void* player_model, int index, PlayerInfo& info, int& hp,
                     int& connect_state, bool& game_finished) const;
    void refresh_match_intel(void* data_center, const std::vector<PlayerInfo>& players,
                             int stage_seed);
    std::vector<IntelBannedUnit> read_banned_units(void* sequence) const;
    bool read_squad_slots(void* sequence, std::vector<IntelSquadSlot>& result) const;
    std::vector<IntelShopGood> read_shop_goods(void* sequence, bool& slot_ids_available) const;

    static void on_log_modifier(void* self, void* modifier, void* method);
    static void on_log_ep_modifier(void* self, void* modifier, void* method);
    static void on_all_state_read(void* self, void* reader, void* method);
    static void on_preparation_read(void* self, void* reader, void* method);
    static void on_applied_modifier(void* self, void* modifier, void* method);
    static void on_start_game(void* self, void* method);
    static void on_clear_runtime_data(void* self, void* method);
    static void on_destroy(void* self, void* method);
    static void on_update(void* self, void* method);
    static void on_global_update(void* self, void* method);

    const ArkPluginApiV1& host_;
    mutable il2cpp::Runtime runtime_;
    bool ready_ = false;
    bool hook_schema_validated_ = false;
    std::atomic<bool> host_active_{false};
    std::uint64_t last_tick_ms_ = 0;

    void* character_class_ = nullptr;
    void* enemy_class_ = nullptr;
    void* entity_class_ = nullptr;
    void* projectile_class_ = nullptr;
    void* logger_class_ = nullptr;
    void* controller_class_ = nullptr;
    void* modifier_class_ = nullptr;
    void* battle_player_data_class_ = nullptr;
    void* battle_character_data_class_ = nullptr;
    void* auto_chess_center_class_ = nullptr;
    void* hidden_all_state_class_ = nullptr;
    void* hidden_static_player_class_ = nullptr;
    void* hidden_scene_class_ = nullptr;
    void* hidden_runtime_player_class_ = nullptr;
    void* hidden_prep_class_ = nullptr;
    void* hidden_store_class_ = nullptr;
    void* hidden_store_good_class_ = nullptr;
    void* hidden_analytics_class_ = nullptr;
    void* hidden_chess_count_class_ = nullptr;
    void* auto_chess_settle_class_ = nullptr;
    void* player_data_model_class_ = nullptr;
    void* indexer_class_ = nullptr;
    void* scene_player_data_class_ = nullptr;
    void* player_card_class_ = nullptr;
    void* fixed_point_class_ = nullptr;
    void* manager_class_ = nullptr;
    void* manager_instance_info_ = nullptr;
    void* manager_center_field_ = nullptr;
    void* manager_status_field_ = nullptr;
    void* center_battle_status_field_ = nullptr;
    void* center_help_data_info_ = nullptr;
    void* help_players_field_ = nullptr;
    void* self_status_info_ = nullptr;
    void* self_status_dict_field_ = nullptr;
    void* self_status_lookup_info_ = nullptr;
    IntGetter game_state_ = nullptr;
    void* game_state_info_ = nullptr;
    BoolGetter self_finished_ = nullptr;
    void* self_finished_info_ = nullptr;
    int phase_battle_ = -1, phase_help_ = -1, phase_boss_ = -1;
    int phase_waiting_ = -1, phase_end_ = -1, phase_pause_ = -1, phase_settle_ = -1;
    int player_battling_ = -1;
    void* scene_game_state_field_ = nullptr;
    std::uint32_t assist_member_mask_ = 0;
    bool assist_members_warning_sent_ = false;
    int diagnostic_game_state_ = -1;
    BattlePhase current_phase_ = BattlePhase::Idle;
    std::uint32_t timing_mask_ = 0;
    bool timing_warning_sent_ = false;
    bool phase_published_ = false;
    BattlePhase last_damage_phase_ = BattlePhase::Idle;
    double last_damage_clock_ = -1;
    bool session_end_published_ = false;
    il2cpp::Runtime::Root manager_root_;
    il2cpp::Runtime::Root center_root_;
    void* scene_dead_info_ = nullptr;
    int connect_online_ = 1;
    int connect_missing_ = 2;
    int connect_quit_ = 3;
    int state_running_ = 2;
    int state_finished_ = 3;

    ReferenceGetter modifier_source_ = nullptr;
    ReferenceGetter modifier_original_source_ = nullptr;
    ReferenceGetter modifier_target_ = nullptr;
    IntGetter modifier_player_side_ = nullptr;
    IntGetter entity_player_side_ = nullptr;
    FixedPointGetter modifier_final_delta_ = nullptr;
    IntGetter modifier_damage_type_ = nullptr;
    BoolGetter modifier_is_damage_ = nullptr;
    BoolGetter modifier_is_heal_ = nullptr;
    BoolGetter modifier_is_cancelled_ = nullptr;
    BoolGetter modifier_is_element_ = nullptr;
    ReferenceGetter character_id_ = nullptr;
    UIntGetter character_card_uid_ = nullptr;
    BoolGetter character_is_token_ = nullptr;
    ReferenceGetter character_data_ = nullptr;
    IntGetter character_side_ = nullptr;
    ReferenceGetter projectile_source_ = nullptr;
    ClockGetter controller_clock_ = nullptr;
    ReferenceGetter controller_player_list_ = nullptr;

    void* modifier_source_info_ = nullptr;
    void* modifier_original_source_info_ = nullptr;
    void* modifier_target_info_ = nullptr;
    void* modifier_player_side_info_ = nullptr;
    void* entity_player_side_info_ = nullptr;
    void* modifier_final_delta_info_ = nullptr;
    void* modifier_damage_type_info_ = nullptr;
    void* modifier_is_damage_info_ = nullptr;
    void* modifier_is_heal_info_ = nullptr;
    void* modifier_is_cancelled_info_ = nullptr;
    void* modifier_is_element_info_ = nullptr;
    void* character_id_info_ = nullptr;
    void* character_card_uid_info_ = nullptr;
    void* character_is_token_info_ = nullptr;
    void* character_data_info_ = nullptr;
    void* character_side_info_ = nullptr;
    void* projectile_source_info_ = nullptr;
    void* controller_clock_info_ = nullptr;
    void* controller_player_list_info_ = nullptr;

    void* modifier_real_delta_field_ = nullptr;
    void* modifier_shared_mask_field_ = nullptr;
    void* logger_controller_field_ = nullptr;
    void* controller_state_field_ = nullptr;
    void* data_center_player_model_field_ = nullptr;
    void* data_center_indexer_field_ = nullptr;
    void* data_center_settle_field_ = nullptr;
    void* card_nickname_field_ = nullptr;
    void* scene_index_field_ = nullptr;
    void* scene_uid_field_ = nullptr;
    void* scene_card_field_ = nullptr;
    void* scene_hp_field_ = nullptr;
    void* scene_finished_field_ = nullptr;
    void* scene_connect_field_ = nullptr;
    void* indexer_inst_player_field_ = nullptr;
    void* indexer_runtime_card_field_ = nullptr;
    void* indexer_token_card_field_ = nullptr;
    void* chess_class_ = nullptr;
    void* chess_inst_id_field_ = nullptr;
    void* chess_card_uid_field_ = nullptr;
    void* chess_player_field_ = nullptr;
    void* runtime_card_lookup_info_ = nullptr;
    bool runtime_cards_are_instances_ = false;
    void* battle_characters_field_ = nullptr;
    void* battle_tokens_field_ = nullptr;
    void* battle_player_side_field_ = nullptr;
    int player_side_invalid_ = -1;
    void* char_data_unique_field_ = nullptr;
    void* char_data_token_field_ = nullptr;
    void* char_data_host_uid_field_ = nullptr;

    void* center_stage_seed_info_ = nullptr;
    void* center_stage_id_info_ = nullptr;
    void* center_mode_id_info_ = nullptr;
    void* center_boss_id_info_ = nullptr;
    void* center_hidden_boss_id_info_ = nullptr;
    void* center_enemy_types_info_ = nullptr;
    void* center_banned_bonds_info_ = nullptr;
    void* center_self_banned_units_info_ = nullptr;
    void* center_shop_model_field_ = nullptr;
    void* center_round_info_ = nullptr;
    void* center_self_info_ = nullptr;
    void* center_battle_players_info_ = nullptr;
    void* settle_ending_info_ = nullptr;
    void* player_model_get_player_info_ = nullptr;
    void* scene_band_field_ = nullptr;
    void* scene_squad_slots_field_ = nullptr;
    void* scene_shop_level_field_ = nullptr;
    void* scene_shop_coin_field_ = nullptr;
    void* squad_slot_class_ = nullptr;
    void* squad_char_id_field_ = nullptr;
    void* squad_chess_id_field_ = nullptr;
    void* squad_template_id_field_ = nullptr;
    void* squad_cultivate_effect_id_field_ = nullptr;
    void* squad_equip_id_field_ = nullptr;
    void* squad_skin_id_field_ = nullptr;
    void* squad_type_field_ = nullptr;
    void* squad_potential_rank_field_ = nullptr;
    void* squad_skill_index_field_ = nullptr;
    void* squad_bond_ids_field_ = nullptr;
    void* squad_banned_field_ = nullptr;
    void* banned_unit_class_ = nullptr;
    void* banned_unit_bond_id_field_ = nullptr;
    void* banned_unit_chess_ids_field_ = nullptr;
    void* shop_class_ = nullptr;
    void* shop_level_field_ = nullptr;
    void* shop_coin_field_ = nullptr;
    void* shop_upgrade_field_ = nullptr;
    void* shop_refresh_field_ = nullptr;
    void* shop_goods_type_field_ = nullptr;
    void* shop_free_refresh_field_ = nullptr;
    void* shop_special_refresh_field_ = nullptr;
    void* shop_goods_field_ = nullptr;
    void* shop_refresh_seq_field_ = nullptr;
    void* shop_goods_class_ = nullptr;
    void* shop_good_chess_id_field_ = nullptr;
    void* shop_good_slot_id_field_ = nullptr;
    void* shop_good_price_field_ = nullptr;
    void* shop_good_frozen_field_ = nullptr;

    void* hidden_boss_id_field_ = nullptr;
    void* hidden_stage_seed_field_ = nullptr;
    void* hidden_static_players_field_ = nullptr;
    void* hidden_scene_detail_field_ = nullptr;
    void* hidden_static_index_field_ = nullptr;
    void* hidden_static_band_field_ = nullptr;
    void* hidden_static_banned_units_field_ = nullptr;
    void* hidden_scene_state_field_ = nullptr;
    void* hidden_scene_round_field_ = nullptr;
    void* hidden_scene_runtime_players_field_ = nullptr;
    void* hidden_scene_prep_status_field_ = nullptr;
    void* hidden_runtime_uid_field_ = nullptr;
    void* hidden_runtime_shop_lv_field_ = nullptr;
    void* hidden_runtime_hp_field_ = nullptr;
    void* hidden_runtime_state_field_ = nullptr;
    void* hidden_prep_uid_field_ = nullptr;
    void* hidden_prep_round_field_ = nullptr;
    void* hidden_prep_store_field_ = nullptr;
    void* hidden_prep_char_chess_field_ = nullptr;
    void* hidden_prep_round_analytics_field_ = nullptr;
    void* hidden_analytics_chess_gained_field_ = nullptr;
    void* hidden_store_level_field_ = nullptr;
    void* hidden_store_coin_field_ = nullptr;
    void* hidden_store_upgrade_price_field_ = nullptr;
    void* hidden_store_refresh_price_field_ = nullptr;
    void* hidden_store_goods_type_field_ = nullptr;
    void* hidden_store_free_refresh_field_ = nullptr;
    void* hidden_store_special_refresh_field_ = nullptr;
    void* hidden_store_goods_field_ = nullptr;
    void* hidden_store_refresh_seq_field_ = nullptr;
    void* hidden_good_identifier_field_ = nullptr;
    void* hidden_good_slot_field_ = nullptr;
    void* hidden_good_price_field_ = nullptr;
    void* hidden_good_frozen_field_ = nullptr;
    void* hidden_owned_identifier_field_ = nullptr;
    void* hidden_chess_identifier_field_ = nullptr;
    void* hidden_chess_count_field_ = nullptr;
    void* battle_util_class_ = nullptr;
    void* chess_basic_info_class_ = nullptr;
    void* chess_basic_info_by_id_method_ = nullptr;
    void* character_name_lookup_ = nullptr;
    void* center_act_data_info_ = nullptr;
    void* center_game_data_field_ = nullptr;
    void* enemy_handbook_data_info_ = nullptr;
    void* bond_info_method_ = nullptr;
    void* bond_info_class_ = nullptr;
    void* basic_info_char_id_field_ = nullptr;
    void* basic_info_item_type_field_ = nullptr;
    void* basic_info_bonds_field_ = nullptr;
    void* bond_info_name_field_ = nullptr;
    mutable std::mutex shop_catalog_mutex_;
    std::map<int, IntelChessRecord> shop_catalog_;
    std::map<std::string, int> shop_catalog_ids_;
    std::uint64_t shop_catalog_next_sample_ = 0;
    void* boss_info_class_ = nullptr;
    int item_type_char_ = -1;
    std::size_t hidden_boss_id_offset_ = 0;
    std::size_t hidden_stage_seed_offset_ = 0;
    std::size_t hidden_static_players_offset_ = 0;
    std::size_t hidden_scene_detail_offset_ = 0;

    int (*field_offset_)(void*) = nullptr;
    std::size_t real_delta_offset_ = 0;
    std::size_t shared_mask_offset_ = 0;

    il2cpp::Runtime::Root runtime_card_root_;
    il2cpp::Runtime::Root inst_player_root_;
    il2cpp::Runtime::Root token_card_root_;
    il2cpp::Runtime::Root controller_root_;
    std::unordered_map<std::uint32_t, HostInfo> hosts_;
    // Copied identities survive a despawn only within this round/controller.
    // Coverage, unlike attribution, must use the freshly sampled cards.
    std::set<std::uint32_t> live_host_cards_;
    std::unordered_map<std::uint32_t, int> host_players_;
    std::map<int, int> side_players_;
    std::map<int, PlayerInfo> last_players_;
    std::map<int, int> missing_player_ticks_;
    std::unordered_map<std::uint32_t, Attribution> attribution_cache_;
    std::unordered_map<std::uint32_t, int> token_players_;
    bool token_index_warning_sent_ = false;

    void* controller_ = nullptr;
    bool battle_active_ = false;
    bool battle_ended_ = false;
    bool in_mode_ = false;
    bool mode_present_ = false;
    Coverage logged_coverage_ = Coverage::Unknown;
    int logged_roster_players_ = -1;
    int logged_local_players_ = -1;
    int logged_battle_players_ = -1;
    int host_cache_player_count_ = -1;
    bool explicit_partial_ = false;
    std::uint64_t session_id_ = 0;
    std::atomic<std::uint64_t> shop_session_id_{0};
    std::atomic<int> shop_round_{1};
    std::atomic<int> shop_slot_mode_{0}; // 0: established 1-based, 1: observed zero-based
    std::atomic<std::uint64_t> shop_observations_{0};
    std::atomic<std::uint64_t> shop_local_observations_{0};
    std::atomic<std::uint64_t> shop_hidden_observations_{0};
    std::atomic<std::uint64_t> shop_rejected_observations_{0};
    std::uint64_t session_counter_ = 0;
    SessionTracker session_tracker_;
    std::uint32_t local_simulated_mask_ = 0;
    std::uint32_t observed_player_mask_ = 0;
    std::string self_uid_;
    std::uint32_t simulated_mask_ = 0;
    std::uint32_t active_mask_ = 0;
    Coverage coverage_ = Coverage::Unknown;
    int self_index_ = kUnknownPlayer;
    int current_round_ = 1;
    std::uint64_t battle_ = 0;
    double last_clock_ = -1;
    ULONGLONG last_damage_error_tick_ = 0;

    std::vector<std::uint64_t> hooks_;
    std::mutex lifecycle_mutex_;
    std::mutex callback_mutex_;
    std::condition_variable callbacks_drained_;
    std::size_t callbacks_in_flight_ = 0;
    bool callbacks_enabled_ = false;
    bool coverage_warning_sent_ = false;
    bool attribution_warning_sent_ = false;

    struct Counters {
        std::uint64_t logger_hp = 0, logger_ep = 0, applied = 0, suppressed = 0;
        std::uint64_t outside_mode = 0, cancelled = 0, wrong_type = 0;
        std::uint64_t unresolved_source = 0, environment = 0, zero = 0;
        std::uint64_t published = 0, unknown_player = 0, published_frozen = 0;
        std::uint64_t errors = 0, original_source = 0, roster_errors = 0;
        std::uint64_t source_errors = 0, side_recovered = 0, environment_recovered = 0;
        std::uint64_t target_rejected = 0, missing_host = 0, side_conflicts = 0;
    };
    mutable Counters counters_;
    mutable std::string last_read_failure_;
    std::string last_diagnostic_state_;
    std::deque<std::string> diagnostic_history_;
    std::string last_in_mode_diagnostics_;
    std::string last_combat_diagnostics_;
    std::string last_modifier_sample_;
    std::string last_unresolved_sample_;
    bool source_warning_sent_ = false;
    bool unattributed_effect_warning_sent_ = false;
    int diagnostic_battle_state_ = -1;
    int diagnostic_settlement_status_ = -1;
    int diagnostic_runtime_cards_ = -1;
    int diagnostic_inst_players_ = -1;
    std::vector<std::string> hook_diagnostics_;
    std::mutex applied_mutex_;
    std::vector<std::pair<DWORD, void*>> applied_modifiers_;
};

NativeCapture* g_active_capture = nullptr;
std::unique_ptr<NativeCapture> g_pending_capture;
NativeCapture* g_callback_capture = nullptr;

void NativeCapture::bind_classes() {
    entity_class_ = runtime_.find("Torappu.Battle", "Entity");
    character_class_ = runtime_.find("Torappu.Battle", "Character");
    enemy_class_ = runtime_.find("Torappu.Battle", "Enemy");
    projectile_class_ = runtime_.find("Torappu.Battle", "Projectile");
    logger_class_ = runtime_.find("Torappu.Battle", "BattleLogger");
    controller_class_ = runtime_.find("Torappu.Battle", "BattleController");
    modifier_class_ = runtime_.find("Torappu.Battle", "Modifier");
    battle_player_data_class_ = runtime_.find("Torappu.Battle", "BattlePlayerData");
    battle_character_data_class_ = runtime_.find("Torappu.Battle", "BattleCharacterData");
    auto_chess_center_class_ = find_class_with_fallback(
        runtime_, "AutoChessDataCenter", {"Torappu.Battle.DataCenter", "Torappu.Battle.AutoChess"});
    try {
        auto_chess_settle_class_ = find_class_with_fallback(
            runtime_, "AutoChessSettleDataModel",
            {"Torappu.Battle.AutoChess", "Torappu.Battle.DataCenter"});
    } catch (...) {
        auto_chess_settle_class_ = nullptr;
    }
    player_data_model_class_ = find_class_with_fallback(
        runtime_, "AutoChessPlayerDataModel",
        {"Torappu.Battle.AutoChess", "Torappu.Battle.DataCenter"});
    indexer_class_ = find_class_with_fallback(
        runtime_, "AutoChessDataIndexer",
        {"Torappu.Battle.DataCenter", "Torappu.Battle.AutoChess"});
    scene_player_data_class_ = runtime_.nested(player_data_model_class_, "ScenePlayerData");
    player_card_class_ = find_class_with_fallback(
        runtime_, "PlayerCard", {"Torappu.Battle.AutoChess", "Torappu.Battle.DataCenter"});
    try {
        squad_slot_class_ = find_class_with_fallback(
            runtime_, "SquadSlot", {"Torappu.Battle.AutoChess"});
    } catch (...) {
        squad_slot_class_ = nullptr;
    }
    try {
        banned_unit_class_ = find_class_with_fallback(
            runtime_, "BannedUnit", {"Torappu.UI.AutoChess.Server"});
    } catch (...) {
        banned_unit_class_ = nullptr;
    }
    try {
        hidden_all_state_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattleAllStateSyncData", {"Torappu.UI.AutoChess.Server"});
        hidden_static_player_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattlePlayerStaticInfo", {"Torappu.UI.AutoChess.Server"});
        hidden_scene_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattleSceneStatus", {"Torappu.UI.AutoChess.Server"});
        hidden_runtime_player_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattlePlayerRuntimeInfo", {"Torappu.UI.AutoChess.Server"});
    } catch (...) {
        hidden_all_state_class_ = nullptr;
        hidden_static_player_class_ = nullptr;
        hidden_scene_class_ = nullptr;
        hidden_runtime_player_class_ = nullptr;
    }
    try {
        hidden_prep_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattlePreparationStatus", {"Torappu.UI.AutoChess.Server"});
        hidden_store_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattleStoreInfo", {"Torappu.UI.AutoChess.Server"});
        hidden_store_good_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattleStoreGoodInfo", {"Torappu.UI.AutoChess.Server"});
        hidden_analytics_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattlePreparationRoundAnalytics", {"Torappu.UI.AutoChess.Server"});
        hidden_chess_count_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattleChessCountInfo", {"Torappu.UI.AutoChess.Server"});
    } catch (...) {
        hidden_prep_class_ = nullptr;
        hidden_store_class_ = nullptr;
        hidden_store_good_class_ = nullptr;
        hidden_analytics_class_ = nullptr;
        hidden_chess_count_class_ = nullptr;
    }
    try {
        battle_util_class_ = find_class_with_fallback(
            runtime_, "AutoChessBattleUtil", {"Torappu.Battle.AutoChess"});
        chess_basic_info_class_ = find_class_with_fallback(
            runtime_, "ChessBasicInfo", {"Torappu.Battle.AutoChess"});
    } catch (...) {
        battle_util_class_ = nullptr;
        chess_basic_info_class_ = nullptr;
    }
    fixed_point_class_ = runtime_.find("Torappu", "FP");
    manager_class_ = find_class_with_fallback(runtime_, "AutoChessGameManager", {"Torappu.Battle.AutoChess"});
    if (host_.log) {
        const std::string resolved =
            "Resolved AutoChess classes: " + runtime_.klass_name(auto_chess_center_class_) + ", " +
            runtime_.klass_name(indexer_class_) + ", " +
            runtime_.klass_name(player_data_model_class_);
        host_.log(host_.owner, 1, resolved.c_str());
    }
}

void NativeCapture::bind_fields() {
    const std::int64_t fixed_point_one =
        runtime_.static_value<std::int64_t>(fixed_point_class_, "ONE");
    if (fixed_point_one != 4294967296LL) {
        throw std::runtime_error("Unsupported fixed-point scale");
    }
    if (runtime_.static_value<int>(modifier_class_, "IS_CONTINUOUS_FLAG") != kModifierContinuous ||
        runtime_.static_value<int>(modifier_class_, "IS_ENVIRONMENT_DAMAGE") != kModifierEnvironment)
        throw std::runtime_error("Unsupported Modifier flag constants");
    modifier_real_delta_field_ = runtime_.field(modifier_class_, "realDelta");
    modifier_shared_mask_field_ = runtime_.field(modifier_class_, "sharedFlagMask");
    logger_controller_field_ = runtime_.field(logger_class_, "m_controller");
    controller_state_field_ = runtime_.field(controller_class_, "m_state");
    data_center_player_model_field_ = runtime_.field(auto_chess_center_class_, "playerDataModel");
    data_center_indexer_field_ = runtime_.field(auto_chess_center_class_, "indexer");
    try {
        data_center_settle_field_ = runtime_.field(auto_chess_center_class_, "settleDataModel");
    } catch (...) {
        data_center_settle_field_ = nullptr;
    }
    center_shop_model_field_ = optional_field(runtime_, auto_chess_center_class_, "shopDataModel");
    if (center_shop_model_field_) {
        try {
            shop_class_ = runtime_.class_from_type(runtime_.field_type(center_shop_model_field_));
            if (shop_class_ && runtime_.class_is_value(shop_class_)) shop_class_ = nullptr;
        } catch (...) {}
    }
    center_stage_id_info_ = optional_method(runtime_, auto_chess_center_class_, "get_stageId");
    center_mode_id_info_ = optional_method(runtime_, auto_chess_center_class_, "get_modeId");
    center_boss_id_info_ = optional_method(runtime_, auto_chess_center_class_, "get_bossId");
    center_hidden_boss_id_info_ =
        optional_method(runtime_, auto_chess_center_class_, "get_hiddenBossId");
    center_act_data_info_ = optional_method(runtime_, auto_chess_center_class_, "get_actData");
    center_game_data_field_ = optional_field(runtime_, auto_chess_center_class_, "gameData");
    try {
        void* handbook = runtime_.find("Torappu", "EnemyHandBookDB");
        void* getter = runtime_.method(handbook, "get_data", 0);
        std::uint32_t impl = 0;
        if ((runtime_.method_flags(getter, &impl) & 0x10u) &&
            runtime_.type(runtime_.method_return(getter)) == "Torappu.EnemyHandBookDataGroup")
            enemy_handbook_data_info_ = getter;
    } catch (...) {}
    center_enemy_types_info_ = optional_method(runtime_, auto_chess_center_class_, "get_enemyTypes");
    center_banned_bonds_info_ = optional_method(runtime_, auto_chess_center_class_, "get_bannedBonds");
    center_self_banned_units_info_ = optional_method(
        runtime_, auto_chess_center_class_, "get_selfBannedUnits");
    card_nickname_field_ = runtime_.field(player_card_class_, "nickName");
    scene_index_field_ = runtime_.field(scene_player_data_class_, "index");
    scene_uid_field_ = runtime_.field(scene_player_data_class_, "uid");
    scene_card_field_ = runtime_.field(scene_player_data_class_, "playerCard");
    scene_hp_field_ = runtime_.field(scene_player_data_class_, "hp");
    scene_finished_field_ = runtime_.field(scene_player_data_class_, "gameFinished");
    scene_connect_field_ = runtime_.field(scene_player_data_class_, "connectState");
    scene_band_field_ = optional_field(runtime_, scene_player_data_class_, "bandId");
    scene_squad_slots_field_ = optional_field(runtime_, scene_player_data_class_, "squadSlots");
    scene_shop_level_field_ = optional_field(runtime_, scene_player_data_class_, "shopLevel");
    scene_shop_coin_field_ = optional_field(runtime_, scene_player_data_class_, "shopCoin");
    if (squad_slot_class_) {
        squad_char_id_field_ = optional_field(runtime_, squad_slot_class_, "charId");
        squad_chess_id_field_ = optional_field(runtime_, squad_slot_class_, "chessId");
        squad_template_id_field_ = optional_field(runtime_, squad_slot_class_, "tmplId");
        squad_cultivate_effect_id_field_ =
            optional_field(runtime_, squad_slot_class_, "cultivateEffectId");
        squad_equip_id_field_ = optional_field(runtime_, squad_slot_class_, "currentEquip");
        squad_skin_id_field_ = optional_field(runtime_, squad_slot_class_, "skinId");
        squad_type_field_ = optional_field(runtime_, squad_slot_class_, "type");
        squad_potential_rank_field_ = optional_field(runtime_, squad_slot_class_, "potentialRank");
        squad_skill_index_field_ = optional_field(runtime_, squad_slot_class_, "skillIndex");
        squad_bond_ids_field_ = optional_field(runtime_, squad_slot_class_, "bondList");
        squad_banned_field_ = optional_field(runtime_, squad_slot_class_, "isBanned");
    }
    if (banned_unit_class_) {
        banned_unit_bond_id_field_ = optional_field(runtime_, banned_unit_class_, "bondId");
        banned_unit_chess_ids_field_ = optional_field(runtime_, banned_unit_class_, "chessIdentifier");
    }
    if (shop_class_) {
        shop_level_field_ = optional_field(runtime_, shop_class_, "level");
        shop_coin_field_ = optional_field(runtime_, shop_class_, "currentCoin");
        shop_upgrade_field_ = optional_field(runtime_, shop_class_, "upgradePrice");
        shop_refresh_field_ = optional_field(runtime_, shop_class_, "refreshPrice");
        shop_goods_type_field_ = optional_field(runtime_, shop_class_, "goodsType");
        shop_free_refresh_field_ = optional_field(runtime_, shop_class_, "freeRefreshCnt");
        shop_special_refresh_field_ = optional_field(runtime_, shop_class_, "specialRefresh");
        shop_goods_field_ = optional_field(runtime_, shop_class_, "chessGoods");
        shop_refresh_seq_field_ = optional_field(runtime_, shop_class_, "refreshSeq");
        if (shop_refresh_seq_field_ &&
            runtime_.type(runtime_.field_type(shop_refresh_seq_field_)) != "System.Int32")
            shop_refresh_seq_field_ = nullptr;
        if (shop_goods_field_) {
            try {
                void* list_class = runtime_.class_from_type(runtime_.field_type(shop_goods_field_));
                void* indexer = runtime_.method(list_class, "get_Item", 1, {"System.Int32"});
                shop_goods_class_ = runtime_.class_from_type(runtime_.method_return(indexer));
                if (shop_goods_class_ && runtime_.type(runtime_.class_type(shop_goods_class_)) !=
                        "Torappu.Battle.AutoChess.ChessGoods")
                    shop_goods_class_ = nullptr;
            } catch (...) {}
            if (!shop_goods_class_ &&
                runtime_.type(runtime_.field_type(shop_goods_field_)) ==
                    "System.Collections.Generic.List<Torappu.Battle.AutoChess.ChessGoods>") {
                try { shop_goods_class_ = runtime_.find("Torappu.Battle.AutoChess", "ChessGoods"); }
                catch (...) {}
            }
        }
    }
    if (shop_goods_class_) {
        shop_good_chess_id_field_ = optional_field(runtime_, shop_goods_class_, "chessId");
        shop_good_slot_id_field_ = optional_field(runtime_, shop_goods_class_, "slotId");
        shop_good_price_field_ = optional_field(runtime_, shop_goods_class_, "price");
        shop_good_frozen_field_ = optional_field(runtime_, shop_goods_class_, "isFrozen");
        if (shop_good_chess_id_field_ &&
            runtime_.type(runtime_.field_type(shop_good_chess_id_field_)) != "System.String")
            shop_good_chess_id_field_ = nullptr;
        if (shop_good_slot_id_field_ &&
            runtime_.type(runtime_.field_type(shop_good_slot_id_field_)) != "System.Int32")
            shop_good_slot_id_field_ = nullptr;
        if (shop_good_price_field_ &&
            runtime_.type(runtime_.field_type(shop_good_price_field_)) != "System.Int32")
            shop_good_price_field_ = nullptr;
        if (shop_good_frozen_field_ &&
            runtime_.type(runtime_.field_type(shop_good_frozen_field_)) != "System.Boolean")
            shop_good_frozen_field_ = nullptr;
    }
    scene_dead_info_ = runtime_.method(scene_player_data_class_, "get_isDead", 0);
    void* connect_class = runtime_.class_from_type(runtime_.field_type(scene_connect_field_));
    connect_online_ = runtime_.enum_value(connect_class, "ONLINE");
    connect_missing_ = runtime_.enum_value(connect_class, "MISS_CONN");
    connect_quit_ = runtime_.enum_value(connect_class, "QUIT");
    void* state_class = runtime_.class_from_type(runtime_.field_type(controller_state_field_));
    state_running_ = runtime_.enum_value(state_class, "PLAYING");
    state_finished_ = runtime_.enum_value(state_class, "FINISHED");
    // AutoChessDataCenter.instanceOrNull dereferences the manager without a null
    // check in this client. Query its genuinely nullable parent property first.
    manager_instance_info_ = runtime_.method(manager_class_, "get_instanceOrNull", 0);
    manager_center_field_ = runtime_.field(manager_class_, "dataCenter");
    bind_timing();
    bind_indexer();
    battle_characters_field_ = runtime_.field(battle_player_data_class_, "characters");
    battle_tokens_field_ = runtime_.field(battle_player_data_class_, "tokens");
    battle_player_side_field_ = runtime_.field(battle_player_data_class_, "playerSide");
    if (runtime_.type(runtime_.field_type(battle_player_side_field_)) != "Torappu.PlayerSide")
        throw std::runtime_error("BattlePlayerData.playerSide type mismatch");
    char_data_unique_field_ = runtime_.field(battle_character_data_class_, "uniqueId");
    char_data_token_field_ = runtime_.field(battle_character_data_class_, "isToken");
    char_data_host_uid_field_ = runtime_.field(battle_character_data_class_, "tokenOrHostUniqueId");

    if (!field_offset_)
        field_offset_ = il2cpp::resolve_export<int (*)(void*)>("il2cpp_field_get_offset");
    const int raw_real = field_offset_(modifier_real_delta_field_);
    const int raw_shared = field_offset_(modifier_shared_mask_field_);
    if (!runtime_.class_is_value(modifier_class_) || raw_real < 2 * static_cast<int>(sizeof(void*)) ||
        raw_shared < 2 * static_cast<int>(sizeof(void*))) {
        throw std::runtime_error("Unsupported Modifier field offsets");
    }
    real_delta_offset_ = static_cast<std::size_t>(raw_real - 2 * sizeof(void*));
    shared_mask_offset_ = static_cast<std::size_t>(raw_shared - 2 * sizeof(void*));
    std::uint32_t alignment = 0;
    const int modifier_size = runtime_.class_value_size(modifier_class_, &alignment);
    if (real_delta_offset_ + sizeof(std::int64_t) > static_cast<std::size_t>(modifier_size) ||
        shared_mask_offset_ + sizeof(std::int32_t) > static_cast<std::size_t>(modifier_size)) {
        throw std::runtime_error("Unsupported Modifier layout");
    }

    center_stage_seed_info_ = runtime_.method(auto_chess_center_class_, "get_stageRandomSeed", 0);
    center_round_info_ = runtime_.method(auto_chess_center_class_, "get_currentRound", 0);
    center_self_info_ = runtime_.method(auto_chess_center_class_, "get_selfPlayerIndex", 0);
    center_battle_players_info_ =
        runtime_.method(auto_chess_center_class_, "get_battlePlayerIndex", 0);
    if (auto_chess_settle_class_) {
        try {
            settle_ending_info_ = runtime_.method(auto_chess_settle_class_, "get_endingStatus", 0);
        } catch (...) {
            settle_ending_info_ = nullptr;
        }
    }
    player_model_get_player_info_ = runtime_.method(player_data_model_class_, "GetPlayerData", 1,
                                                   {"System.Int32"});
    std::uint32_t impl = 0;
    if (!(runtime_.method_flags(manager_instance_info_, &impl) & 0x10u) ||
        runtime_.type(runtime_.method_return(scene_dead_info_)) != "System.Boolean" ||
        (runtime_.method_flags(scene_dead_info_, &impl) & 0x10u))
        throw std::runtime_error("AutoChess nullable manager / player death getter ABI mismatch");
}

void NativeCapture::bind_timing() {
    manager_status_field_ = runtime_.field(manager_class_, "gameStatus");
    void* status_class = runtime_.class_from_type(runtime_.field_type(manager_status_field_));
    bind_getter(runtime_, game_state_, game_state_info_, status_class, "get_currentState");
    void* state_class = runtime_.class_from_type(runtime_.method_return(game_state_info_));
    phase_battle_ = runtime_.enum_value(state_class, "BATTLE");
    phase_help_ = runtime_.enum_value(state_class, "HELP_BATTLE");
    phase_boss_ = runtime_.enum_value(state_class, "BOSS_BATTLE");
    phase_waiting_ = runtime_.enum_value(state_class, "BATTLE_WAITING");
    phase_end_ = runtime_.enum_value(state_class, "END");
    phase_pause_ = runtime_.enum_value(state_class, "PAUSE");
    phase_settle_ = runtime_.enum_value(state_class, "SETTLE");
    center_battle_status_field_ = runtime_.field(auto_chess_center_class_, "battleStatus");
    void* battle_class = runtime_.class_from_type(runtime_.field_type(center_battle_status_field_));
    self_status_info_ = runtime_.method(battle_class, "GetSelfBattleStatus", 1, {"System.Int32"});
    std::uint32_t impl = 0;
    void* self_class = runtime_.class_from_type(runtime_.method_return(self_status_info_));
    if ((runtime_.method_flags(self_status_info_, &impl) & 0x10u) || !self_class ||
        runtime_.class_is_value(self_class))
        throw std::runtime_error("GetSelfBattleStatus must be an instance reference lookup");
    bind_getter(runtime_, self_finished_, self_finished_info_, self_class, "get_battleFinished");
    self_status_dict_field_ = runtime_.field(battle_class, "m_selfBattleStatusInfo");
    void* dict_class = runtime_.class_from_type(runtime_.field_type(self_status_dict_field_));
    self_status_lookup_info_ = runtime_.method(dict_class, "TryGetValue", 2,
        {"System.Int32", runtime_.type(runtime_.class_type(self_class)) + "&"});
    if ((runtime_.method_flags(self_status_lookup_info_, &impl) & 0x10u) ||
        runtime_.type(runtime_.method_return(self_status_lookup_info_)) != "System.Boolean")
        throw std::runtime_error("Self battle status lookup ABI mismatch");
    // Help/Boss get_battleFinished are constant false in the current native
    // client. Their UI uses player gameState and actual phase membership.
    scene_game_state_field_ = runtime_.field(scene_player_data_class_, "gameState");
    player_battling_ = runtime_.enum_value(
        runtime_.class_from_type(runtime_.field_type(scene_game_state_field_)), "BATTLE_NOT_COMPLETE");
    center_help_data_info_ = runtime_.method(auto_chess_center_class_, "get_helpBattleData", 0);
    void* help_data_class = runtime_.class_from_type(runtime_.method_return(center_help_data_info_));
    help_players_field_ = runtime_.field(help_data_class, "players");
}

void NativeCapture::bind_indexer() {
    // Validate closed collection signatures: both Dictionary and Torappu.ListDict
    // exist in supported clients, and reference out values need pointer-sized storage.
    const auto lookup = [&](void* field, const char* key, const std::string& value) {
        void* klass = runtime_.class_from_type(runtime_.field_type(field));
        if (!klass || runtime_.class_is_value(klass))
            throw std::runtime_error("Expected reference collection in AutoChessDataIndexer");
        void* method = runtime_.method(klass, "TryGetValue", 2, {key, value});
        std::uint32_t impl = 0;
        if ((runtime_.method_flags(method, &impl) & 0x10u) ||
            runtime_.type(runtime_.method_return(method)) != "System.Boolean")
            throw std::runtime_error("AutoChess index TryGetValue ABI mismatch");
        return method;
    };
    indexer_inst_player_field_ = runtime_.field(indexer_class_, "instId2PlayerIndex");
    lookup(indexer_inst_player_field_, "System.Int32", "System.Int32&");
    runtime_cards_are_instances_ =
        find_field_in_hierarchy(runtime_, indexer_class_, "runtimeCard2Chess") != nullptr;
    indexer_token_card_field_ = nullptr;
    if (runtime_cards_are_instances_) {
        indexer_runtime_card_field_ = runtime_.field(indexer_class_, "runtimeCard2Chess");
        chess_class_ = find_class_with_fallback(runtime_, "ChessInst", {"Torappu.Battle.AutoChess"});
        chess_inst_id_field_ = runtime_.field(chess_class_, "instId");
        chess_card_uid_field_ = runtime_.field(chess_class_, "cardUid");
        chess_player_field_ = runtime_.field(chess_class_, "playerIndex");
        for (void* field : {chess_inst_id_field_, chess_player_field_}) {
            if (runtime_.type(runtime_.field_type(field)) != "System.Int32")
                throw std::runtime_error("ChessInst owner/instance field must be System.Int32");
        }
        if (runtime_.type(runtime_.field_type(chess_card_uid_field_)) != "System.UInt32")
            throw std::runtime_error("ChessInst.cardUid must be System.UInt32");
        runtime_card_lookup_info_ = lookup(indexer_runtime_card_field_, "System.UInt32",
            runtime_.type(runtime_.class_type(chess_class_)) + "&");
    } else {
        indexer_runtime_card_field_ = runtime_.field(indexer_class_, "runtimeCard2ChessDict");
        runtime_card_lookup_info_ = lookup(indexer_runtime_card_field_, "System.UInt32", "System.Int32&");
        // Only legacy clients have this reverse token index. It is not a host map.
        indexer_token_card_field_ = runtime_.field(indexer_class_, "runtimeToken2CardDict");
        lookup(indexer_token_card_field_, "System.Int32", "System.UInt32&");
    }
    if (host_.log) host_.log(host_.owner, 1, runtime_cards_are_instances_
        ? "AutoChess attribution: runtimeCard2Chess -> ChessInst.instId -> instId2PlayerIndex"
        : "AutoChess attribution: legacy runtimeCard2ChessDict -> instId2PlayerIndex");
}

void NativeCapture::bind_getters() {
    bind_getter(runtime_, modifier_source_, modifier_source_info_, modifier_class_, "get_source");
    bind_getter(runtime_, modifier_original_source_, modifier_original_source_info_, modifier_class_, "get_originalSource");
    bind_getter(runtime_, modifier_target_, modifier_target_info_, modifier_class_, "get_target");
    bind_getter(runtime_, modifier_player_side_, modifier_player_side_info_, modifier_class_, "get_playerSide");
    if (runtime_.type(runtime_.method_return(modifier_target_info_)) != "Torappu.Battle.Entity" ||
        runtime_.type(runtime_.method_return(modifier_player_side_info_)) != "Torappu.PlayerSide")
        throw std::runtime_error("Modifier target/playerSide type mismatch");
    player_side_invalid_ = runtime_.enum_value(
        runtime_.class_from_type(runtime_.method_return(modifier_player_side_info_)), "INVALID");
    bind_getter(runtime_, modifier_final_delta_, modifier_final_delta_info_, modifier_class_,
                "get_finalDelta");
    bind_getter(runtime_, modifier_damage_type_, modifier_damage_type_info_, modifier_class_,
                "get_damageType");
    bind_getter(runtime_, modifier_is_damage_, modifier_is_damage_info_, modifier_class_,
                "get_isDamage");
    bind_getter(runtime_, modifier_is_heal_, modifier_is_heal_info_, modifier_class_, "get_isHeal");
    bind_getter(runtime_, modifier_is_cancelled_, modifier_is_cancelled_info_, modifier_class_,
                "get_isCancelled");
    bind_getter(runtime_, modifier_is_element_, modifier_is_element_info_, modifier_class_,
                "get_isElementDamage");
    bind_getter(runtime_, character_id_, character_id_info_, character_class_, "get_characterId");
    bind_getter(runtime_, character_card_uid_, character_card_uid_info_, character_class_,
                "get_cardUid");
    bind_getter(runtime_, character_is_token_, character_is_token_info_, character_class_,
                "get_isToken");
    bind_getter(runtime_, character_data_, character_data_info_, character_class_, "get_data");
    bind_getter(runtime_, character_side_, character_side_info_, character_class_,
                "get_initSideType");
    void* side_class = runtime_.class_from_type(runtime_.method_return(character_side_info_));
    if (runtime_.type(runtime_.method_return(character_side_info_)) != "Torappu.Battle.SideType" ||
        runtime_.enum_value(side_class, "ALLY") != kSideAlly)
        throw std::runtime_error("Character side type mismatch");
    bind_getter(runtime_, entity_player_side_, entity_player_side_info_, entity_class_, "get_playerSide");
    if (runtime_.type(runtime_.method_return(entity_player_side_info_)) != "Torappu.PlayerSide")
        throw std::runtime_error("Entity.playerSide type mismatch");
    bind_getter(runtime_, projectile_source_, projectile_source_info_, projectile_class_,
                "get_source");
    bind_getter(runtime_, controller_clock_, controller_clock_info_, controller_class_,
                "get_fixedPlayTime");
    bind_getter(runtime_, controller_player_list_, controller_player_list_info_, controller_class_,
                "get_playerDataList");
}

void NativeCapture::bind_hooks() {
    hook_schema_validated_ = false;
    bind_hidden_sync_schema();
    struct PendingHook { void* target; void* detour; void** original; const char* name; };
    std::vector<PendingHook> pending;
    const auto hook = [&](void* klass, const char* name, int params, auto detour,
                          auto& original) {
        void* method = runtime_.method(klass, name, params);
        std::uint32_t implementation = 0;
        if ((runtime_.method_flags(method, &implementation) & 0x10u) ||
            runtime_.type(runtime_.method_return(method)) != "System.Void")
            throw std::runtime_error(std::string("Hook ABI mismatch: ") + name);
        if (params == 1 && runtime_.type(runtime_.method_param(method, 0)) != "Torappu.Battle.Modifier&")
            throw std::runtime_error(std::string("Expected ref Modifier hook parameter: ") + name);
        void* target = entry_point(method);
        if (!il2cpp::is_executable(target)) {
            throw std::runtime_error(std::string("Hook target is not executable: ") + name);
        }
        pending.push_back({target, reinterpret_cast<void*>(detour),
                           reinterpret_cast<void**>(&original), name});
    };

    hook(logger_class_, "LogModifier", 1, NativeCapture::on_log_modifier, original_log_modifier);
    hook(logger_class_, "LogEpModifier", 1, NativeCapture::on_log_ep_modifier,
         original_log_ep_modifier);
    // OnAppliedModifier is reached after realDelta is assigned. The native
    // logger calls live inside its optional XLua branch, so logger hooks alone
    // cannot cover every mode/hotfix. Nested logger calls are suppressed below.
    hook(entity_class_, "OnAppliedModifier", 1, NativeCapture::on_applied_modifier,
         original_applied_modifier);
    hook(controller_class_, "StartGame", 0, NativeCapture::on_start_game, original_start_game);
    hook(controller_class_, "ClearRunTimeData", 0, NativeCapture::on_clear_runtime_data,
         original_clear_runtime_data);
    hook(controller_class_, "OnDestroy", 0, NativeCapture::on_destroy, original_on_destroy);
    hook(controller_class_, "Update", 0, NativeCapture::on_update, original_update);
    hook(runtime_.find("Torappu", "GlobalInitializerAndUpdater"), "Update", 0,
         NativeCapture::on_global_update, original_global_update);

    bind_chess_descriptors();
    const bool shop_protocol_schema = bind_shop_counter_schema();

    // Resolve every signature before creating the first hook. Metadata failures
    // stay distinguishable from a temporarily unavailable host hook service.
    hook_schema_validated_ = true;
    hook_diagnostics_.clear();
    hook_diagnostics_.push_back(center_game_data_field_ && enemy_handbook_data_info_
        ? "bossName bound" : "bossName unavailable: schema");
    hook_diagnostics_.push_back(shop_goods_field_ && shop_good_chess_id_field_
        ? "localShopShelf bound" : "localShopShelf unavailable: schema");
    hook_diagnostics_.push_back(chess_basic_info_by_id_method_
        ? "localShopDescriptors bound" : "localShopDescriptors unavailable: schema");
    hook_diagnostics_.push_back(character_name_lookup_
        ? "shopOperatorName bound" : "shopOperatorName unavailable: schema");
    hook_diagnostics_.push_back(bond_info_method_
        ? "shopBondName bound" : "shopBondName unavailable: schema");
    for (const auto& item : pending) {
        std::ostringstream description;
        description << item.name << " RVA=0x" << std::hex <<
            (reinterpret_cast<std::uintptr_t>(item.target) -
             reinterpret_cast<std::uintptr_t>(il2cpp::game_assembly()));
        hook_diagnostics_.push_back(description.str());
        const std::uint64_t id = host_.create_native_hook(host_.owner, item.target,
                                                        item.detour, item.original);
        if (!id) throw std::runtime_error(std::string("Could not create hook: ") + item.name);
        hooks_.push_back(id);
    }
    g_callback_capture = this;
    host_active_.store(true);
    {
        std::lock_guard lock(callback_mutex_);
        callbacks_enabled_ = true;
    }
    for (const std::uint64_t id : hooks_) {
        if (!host_.enable_native_hook(host_.owner, id, true)) {
            throw std::runtime_error("Could not enable IL2CPP hook");
        }
    }
    install_hidden_sync_hook();
    if (shop_protocol_schema) install_preparation_hook();
    else hook_diagnostics_.push_back("preparationStatusRead unavailable: schema");
}

bool NativeCapture::bind_hidden_sync_schema() {
    if (!hidden_all_state_class_ || !hidden_static_player_class_ || !hidden_scene_class_ ||
        !hidden_runtime_player_class_) {
        return false;
    }
    if (!field_offset_ || !runtime_.class_is_value(hidden_all_state_class_)) return false;

    hidden_boss_id_field_ = optional_field(runtime_, hidden_all_state_class_, "hiddenBossId");
    hidden_stage_seed_field_ = optional_field(runtime_, hidden_all_state_class_, "stageSeed");
    hidden_static_players_field_ =
        optional_field(runtime_, hidden_all_state_class_, "staticPlayers");
    hidden_scene_detail_field_ =
        optional_field(runtime_, hidden_all_state_class_, "sceneDetail");
    if (!hidden_boss_id_field_ || !hidden_stage_seed_field_ || !hidden_static_players_field_ ||
        !hidden_scene_detail_field_) {
        return false;
    }

    hidden_static_index_field_ =
        optional_field(runtime_, hidden_static_player_class_, "index");
    hidden_static_band_field_ =
        optional_field(runtime_, hidden_static_player_class_, "bandId");
    hidden_static_banned_units_field_ =
        optional_field(runtime_, hidden_static_player_class_, "bannedUnits");
    hidden_scene_state_field_ = optional_field(runtime_, hidden_scene_class_, "state");
    hidden_scene_round_field_ = optional_field(runtime_, hidden_scene_class_, "round");
    hidden_scene_runtime_players_field_ =
        optional_field(runtime_, hidden_scene_class_, "runtimePlayers");
    hidden_scene_prep_status_field_ =
        optional_field(runtime_, hidden_scene_class_, "preparationStatus");
    hidden_runtime_uid_field_ =
        optional_field(runtime_, hidden_runtime_player_class_, "uidIndex");
    hidden_runtime_shop_lv_field_ =
        optional_field(runtime_, hidden_runtime_player_class_, "shopLv");
    hidden_runtime_hp_field_ = optional_field(runtime_, hidden_runtime_player_class_, "hp");
    hidden_runtime_state_field_ = optional_field(runtime_, hidden_runtime_player_class_, "state");

    try {
        hidden_boss_id_offset_ = raw_field_offset(field_offset_, hidden_boss_id_field_);
        hidden_stage_seed_offset_ = raw_field_offset(field_offset_, hidden_stage_seed_field_);
        hidden_static_players_offset_ =
            raw_field_offset(field_offset_, hidden_static_players_field_);
        hidden_scene_detail_offset_ = raw_field_offset(field_offset_, hidden_scene_detail_field_);
        std::uint32_t alignment = 0;
        const int size = runtime_.class_value_size(hidden_all_state_class_, &alignment);
        if (hidden_boss_id_offset_ + sizeof(void*) > static_cast<std::size_t>(size) ||
            hidden_stage_seed_offset_ + sizeof(int) > static_cast<std::size_t>(size) ||
            hidden_static_players_offset_ + sizeof(void*) > static_cast<std::size_t>(size) ||
            hidden_scene_detail_offset_ + sizeof(void*) > static_cast<std::size_t>(size)) {
            return false;
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool NativeCapture::bind_shop_counter_schema() {
    if (!hidden_prep_class_ || !hidden_store_class_ || !hidden_store_good_class_) return false;
    try {
        hidden_prep_uid_field_ = runtime_.field(hidden_prep_class_, "uidIndex");
        hidden_prep_round_field_ = runtime_.field(hidden_prep_class_, "round");
        hidden_prep_store_field_ = runtime_.field(hidden_prep_class_, "store");
        hidden_prep_char_chess_field_ = optional_field(runtime_, hidden_prep_class_, "charChess");
        hidden_prep_round_analytics_field_ = optional_field(runtime_, hidden_prep_class_, "roundAnalytics");
        hidden_store_goods_field_ = runtime_.field(hidden_store_class_, "goods");
        hidden_store_level_field_ = optional_field(runtime_, hidden_store_class_, "level");
        hidden_store_coin_field_ = optional_field(runtime_, hidden_store_class_, "coin");
        hidden_store_upgrade_price_field_ = optional_field(runtime_, hidden_store_class_, "upgradePrice");
        hidden_store_refresh_price_field_ = optional_field(runtime_, hidden_store_class_, "refreshPrice");
        hidden_store_goods_type_field_ = optional_field(runtime_, hidden_store_class_, "isSpecialGoods");
        hidden_store_free_refresh_field_ = optional_field(runtime_, hidden_store_class_, "freeRefreshCnt");
        hidden_store_special_refresh_field_ = optional_field(runtime_, hidden_store_class_, "specialRefresh");
        hidden_store_refresh_seq_field_ = optional_field(runtime_, hidden_store_class_, "refreshSeq");
        hidden_good_identifier_field_ = runtime_.field(hidden_store_good_class_, "chessIdentifier");
        hidden_good_slot_field_ = runtime_.field(hidden_store_good_class_, "slotId");
        hidden_good_price_field_ = optional_field(runtime_, hidden_store_good_class_, "price");
        hidden_good_frozen_field_ = optional_field(runtime_, hidden_store_good_class_, "isFrozen");
        hidden_chess_identifier_field_ = optional_field(runtime_, hidden_chess_count_class_, "chessIdentifier");
        hidden_chess_count_field_ = optional_field(runtime_, hidden_chess_count_class_, "count");
        hidden_analytics_chess_gained_field_ = optional_field(runtime_, hidden_analytics_class_, "chessGained");
        if (hidden_prep_char_chess_field_) {
            try {
                void* owned_class = runtime_.find(
                    "Torappu.UI.AutoChess.Server", "AutoChessBattleCharChess");
                if (runtime_.type(runtime_.field_type(hidden_prep_char_chess_field_)) ==
                    "System.Collections.Generic.List<Torappu.UI.AutoChess.Server.AutoChessBattleCharChess>")
                    hidden_owned_identifier_field_ = optional_field(runtime_, owned_class, "chessIdentifier");
                if (hidden_owned_identifier_field_ &&
                    runtime_.type(runtime_.field_type(hidden_owned_identifier_field_)) != "System.Int32")
                    hidden_owned_identifier_field_ = nullptr;
            } catch (...) {}
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string NativeCapture::resolve_operator_name(void* char_id, const std::string& id) const {
    if (!character_name_lookup_ || !char_id || id.empty()) return {};
    try {
        auto name = runtime_.string(runtime_.invoke(character_name_lookup_, nullptr, {char_id}));
        if (name != id) return name;
    } catch (...) {}
    return {};
}

IntelChessRecord NativeCapture::shop_catalog_entry(int identifier, const std::string& chess_id) const {
    std::lock_guard lock(shop_catalog_mutex_);
    if (!chess_id.empty()) {
        const auto it = shop_catalog_ids_.find(chess_id);
        if (it == shop_catalog_ids_.end()) return {};
        identifier = it->second;
    }
    const auto entry = shop_catalog_.find(identifier);
    if (entry != shop_catalog_.end()) return entry->second;
    IntelChessRecord unresolved;
    unresolved.chess_identifier = identifier;
    return unresolved;
}

void NativeCapture::refresh_shop_catalog(void* data_center) {
    if (!data_center || !center_act_data_info_ || GetTickCount64() < shop_catalog_next_sample_) return;
    shop_catalog_next_sample_ = GetTickCount64() + 1000;
    try {
        if (runtime_.type(runtime_.method_return(center_act_data_info_)) != "Torappu.ActAutoChessData")
            return;
        il2cpp::Runtime::Root act(runtime_, runtime_.invoke(center_act_data_info_, data_center));
        if (!act.get()) return;
        void* act_class = runtime_.object_class(act.get());
        const auto field = [&](void* owner, const char* name, const std::string& type) -> void* {
            void* result = optional_field(runtime_, owner, name);
            return result && runtime_.type(runtime_.field_type(result)) == type ? result : nullptr;
        };
        const auto table = [&](const char* name, const char* value_type) -> void* {
            const auto type = std::string("Torappu.ListDict<System.String,") + value_type + ">";
            return read_object(act.get(), field(act_class, name, type), runtime_);
        };
        constexpr auto chars = "Torappu.ActAutoChessData.ActAutoChessCharChessData";
        constexpr auto shop_chars = "Torappu.ActAutoChessData.ActAutoChessCharShopChessData";
        constexpr auto bonds = "Torappu.ActAutoChessData.ActAutoChessBondInfo";
        constexpr auto traps = "Torappu.ActAutoChessData.ActAutoChessTrapChessData";
        il2cpp::Runtime::Root shop_table(runtime_, table("charShopChessDatas", shop_chars));
        il2cpp::Runtime::Root bond_table(runtime_, table("bondInfoDict", bonds));

        const auto lookup = [&](void* dictionary, const char* type) -> void* {
            if (!dictionary) return nullptr;
            void* result = runtime_.method(runtime_.object_class(dictionary), "TryGetValue", 2,
                                           {"System.String", std::string(type) + "&"});
            std::uint32_t impl = 0;
            return !(runtime_.method_flags(result, &impl) & 0x10u) &&
                    runtime_.type(runtime_.method_return(result)) == "System.Boolean" ? result : nullptr;
        };
        void* shop_lookup = nullptr;
        void* bond_lookup = nullptr;
        try { shop_lookup = lookup(shop_table.get(), shop_chars); } catch (...) {}
        try { bond_lookup = lookup(bond_table.get(), bonds); } catch (...) {}
        const auto find = [&](void* method, void* dictionary, void* id) -> void* {
            void* value = nullptr;
            if (method && runtime_.unbox<bool>(runtime_.invoke(method, dictionary, {id, &value})))
                return value;
            return nullptr;
        };
        const auto string_field = [&](void* object, const char* name) -> void* {
            return object ? read_object(object, field(runtime_.object_class(object), name, "System.String"),
                                        runtime_) : nullptr;
        };

        std::map<int, IntelChessRecord> entries;
        std::map<std::string, int> by_id;
        const auto read_table = [&](const char* table_name, const char* row_type, bool is_character) {
            il2cpp::Runtime::Root rows(runtime_, table(table_name, row_type));
            if (!rows.get()) return;
            // ListDict.Get(Int32) reads its existing KeyValuePair list by position.
            void* get = runtime_.method(runtime_.object_class(rows.get()), "Get", 1, {"System.Int32"});
            void* pair_type = runtime_.method_return(get);
            const auto expected_pair = std::string("System.Collections.Generic.KeyValuePair<System.String,") +
                                       row_type + ">";
            std::uint32_t impl = 0;
            if ((runtime_.method_flags(get, &impl) & 0x10u) || runtime_.type(pair_type) != expected_pair)
                return;
            void* value_field = field(runtime_.class_from_type(pair_type), "value", row_type);
            if (!value_field) return;
            void* row_class = runtime_.class_from_type(runtime_.field_type(value_field));
            void* identifier_field = field(row_class, "identifier", "System.Int32");
            void* id_field = field(row_class, "chessId", "System.String");
            if (!identifier_field || !id_field) return;
            void* bond_ids_field = is_character ? field(row_class, "bondIds",
                "System.Collections.Generic.List<System.String>") : nullptr;
            const int count = runtime_.count(rows.get());
            if (count <= 0 || count > 4096) return;
            for (int i = 0; i < count; ++i) {
                try {
                    il2cpp::Runtime::Root pair(runtime_, runtime_.invoke(get, rows.get(), {&i}));
                    il2cpp::Runtime::Root row(runtime_, read_object(pair.get(), value_field, runtime_));
                    if (!row.get()) continue;
                    const int identifier = read_value<int>(row.get(), identifier_field, runtime_);
                    void* chess_id_object = read_object(row.get(), id_field, runtime_);
                    const auto chess_id = runtime_.string(chess_id_object);
                    if (identifier < 0 || chess_id.empty()) continue;
                    IntelChessRecord descriptor;
                    descriptor.chess_identifier = identifier;
                    descriptor.chess_id = chess_id;
                    descriptor.operator_available = is_character;
                    if (is_character) {
                        try {
                            if (void* shop_row = find(shop_lookup, shop_table.get(), chess_id_object)) {
                                void* char_id_object = string_field(shop_row, "charId");
                                auto char_id = runtime_.string(char_id_object);
                                if (!char_id.empty()) {
                                    descriptor.chess_id = std::move(char_id);
                                    descriptor.operator_name = resolve_operator_name(char_id_object, descriptor.chess_id);
                                }
                            }
                        } catch (...) {}
                        if (descriptor.operator_name.empty())
                            descriptor.operator_name = resolve_operator_name(chess_id_object, chess_id);
                    }
                    void* bond_ids = read_object(row.get(), bond_ids_field, runtime_);
                    if (bond_ids) {
                        const int bond_count = (std::min)(runtime_.count(bond_ids), static_cast<int>(kMaxIntelItems));
                        for (int index = 0; index < bond_count; ++index) {
                            void* bond_id = runtime_.at(bond_ids, index);
                            auto id = runtime_.string(bond_id);
                            if (id.empty()) continue;
                            std::string name;
                            try {
                                name = runtime_.string(string_field(
                                    find(bond_lookup, bond_table.get(), bond_id), "name"));
                            } catch (...) {}
                            descriptor.bond_ids.push_back(std::move(id));
                            descriptor.bond_names.push_back(std::move(name));
                        }
                    }
                    if (entries.try_emplace(identifier, std::move(descriptor)).second)
                        by_id.try_emplace(chess_id, identifier);
                } catch (...) {}
            }
        };
        read_table("charChessDataDict", chars, true);
        if (!entries.empty()) {
            try { read_table("trapChessDataDict", traps, false); } catch (...) {}
            std::lock_guard lock(shop_catalog_mutex_);
            shop_catalog_ = std::move(entries);
            shop_catalog_ids_ = std::move(by_id);
            shop_catalog_next_sample_ = UINT64_MAX;
        }
    } catch (...) {}
}

void NativeCapture::bind_chess_descriptors() {
    try {
        void* util = runtime_.find("Torappu", "CharacterUtil");
        void* method = runtime_.method(util, "GetCharName", 1, {"System.String"});
        std::uint32_t impl = 0;
        if ((runtime_.method_flags(method, &impl) & 0x10u) &&
            runtime_.type(runtime_.method_return(method)) == "System.String")
            character_name_lookup_ = method;
    } catch (...) {}
    if (!battle_util_class_ || !chess_basic_info_class_) return;
    try {
        void* method = runtime_.method(battle_util_class_, "GetBondInfo", 1, {"System.String"});
        std::uint32_t impl = 0;
        void* result_type = runtime_.method_return(method);
        if ((runtime_.method_flags(method, &impl) & 0x10u) &&
            runtime_.type(result_type) == "Torappu.ActAutoChessData.ActAutoChessBondInfo") {
            bond_info_class_ = runtime_.class_from_type(result_type);
            bond_info_name_field_ = runtime_.field(bond_info_class_, "name");
            if (!runtime_.class_is_value(bond_info_class_) &&
                runtime_.type(runtime_.field_type(bond_info_name_field_)) == "System.String")
                bond_info_method_ = method;
        }
    } catch (...) {}
    try {
        basic_info_char_id_field_ = runtime_.field(chess_basic_info_class_, "charId");
        basic_info_item_type_field_ = runtime_.field(chess_basic_info_class_, "itemType");
        basic_info_bonds_field_ = runtime_.field(chess_basic_info_class_, "basicBondIds");
        if (!runtime_.class_is_value(chess_basic_info_class_) ||
            runtime_.type(runtime_.field_type(basic_info_char_id_field_)) != "System.String" ||
            runtime_.type(runtime_.field_type(basic_info_bonds_field_)) !=
                "System.Collections.Generic.List<System.String>") return;
        void* item_type_class = runtime_.class_from_type(runtime_.field_type(basic_info_item_type_field_));
        item_type_char_ = runtime_.enum_value(item_type_class, "CHAR");
        std::uint32_t impl = 0;
        const auto basic_lookup = [&](const char* parameter) -> void* {
            try {
                void* method = runtime_.method(battle_util_class_, "GetChessBasicInfo", 1, {parameter});
                if ((runtime_.method_flags(method, &impl) & 0x10u) &&
                    runtime_.type(runtime_.method_return(method)) ==
                        runtime_.type(runtime_.class_type(chess_basic_info_class_)))
                    return method;
            } catch (...) {}
            return nullptr;
        };
        chess_basic_info_by_id_method_ = basic_lookup("System.String");
    } catch (...) {
    }
}

bool NativeCapture::install_hidden_sync_hook() {
    if (!hidden_all_state_class_ || !bind_hidden_sync_schema()) {
        hook_diagnostics_.push_back("hiddenAllStateRead unavailable: schema");
        return false;
    }
    try {
        void* method = runtime_.method(hidden_all_state_class_, "Read", 1,
                                       {"Torappu.DataStream.IStreamReader"});
        std::uint32_t implementation = 0;
        if ((runtime_.method_flags(method, &implementation) & 0x10u) ||
            runtime_.type(runtime_.method_return(method)) != "System.Void" ||
            runtime_.type(runtime_.method_param(method, 0)) != "Torappu.DataStream.IStreamReader") {
            hook_diagnostics_.push_back("hiddenAllStateRead unavailable: ABI");
            return false;
        }
        void* target = entry_point(method);
        if (!il2cpp::is_executable(target)) {
            hook_diagnostics_.push_back("hiddenAllStateRead unavailable: target");
            return false;
        }
        const std::uint64_t id = host_.create_native_hook(
            host_.owner, target, reinterpret_cast<void*>(NativeCapture::on_all_state_read),
            reinterpret_cast<void**>(&original_all_state_read));
        if (!id) return false;
        hooks_.push_back(id);
        if (!host_.enable_native_hook(host_.owner, id, true)) {
            if (host_.remove_native_hook(host_.owner, id)) hooks_.pop_back();
            original_all_state_read = nullptr;
            hook_diagnostics_.push_back("hiddenAllStateRead unavailable: host hook");
            return false;
        }
        std::ostringstream description;
        description << "hiddenAllStateRead RVA=0x" << std::hex
                    << (reinterpret_cast<std::uintptr_t>(target) -
                        reinterpret_cast<std::uintptr_t>(il2cpp::game_assembly()));
        hook_diagnostics_.push_back(description.str());
        return true;
    } catch (const std::exception& error) {
        if (host_.log) {
            host_.log(host_.owner, 1, (std::string("Hidden AllStateSync hook unavailable: ") +
                                       error.what()).c_str());
        }
        original_all_state_read = nullptr;
        return false;
    }
}

bool NativeCapture::install_preparation_hook() {
    try {
        void* method = runtime_.method(hidden_prep_class_, "Read", 1,
                                       {"Torappu.DataStream.IStreamReader"});
        std::uint32_t implementation = 0;
        if ((runtime_.method_flags(method, &implementation) & 0x10u) ||
            runtime_.type(runtime_.method_return(method)) != "System.Void" ||
            runtime_.type(runtime_.method_param(method, 0)) != "Torappu.DataStream.IStreamReader") {
            hook_diagnostics_.push_back("preparationStatusRead unavailable: ABI");
            return false;
        }
        void* target = entry_point(method);
        if (!il2cpp::is_executable(target)) {
            hook_diagnostics_.push_back("preparationStatusRead unavailable: target");
            return false;
        }
        const std::uint64_t id = host_.create_native_hook(
            host_.owner, target, reinterpret_cast<void*>(NativeCapture::on_preparation_read),
            reinterpret_cast<void**>(&original_preparation_read));
        if (!id) return false;
        hooks_.push_back(id);
        if (!host_.enable_native_hook(host_.owner, id, true)) {
            if (host_.remove_native_hook(host_.owner, id)) hooks_.pop_back();
            original_preparation_read = nullptr;
            hook_diagnostics_.push_back("preparationStatusRead unavailable: host hook");
            return false;
        }
        hook_diagnostics_.push_back("preparationStatusRead installed");
        return true;
    } catch (const std::exception& error) {
        if (host_.log) {
            host_.log(host_.owner, 1, (std::string("Shop observation hook unavailable: ") +
                                       error.what()).c_str());
        }
        original_preparation_read = nullptr;
        return false;
    }
}

IntelHiddenStore NativeCapture::read_hidden_store(void* store) const {
    IntelHiddenStore result;
    if (!store) return result;
    result.available = true;
    try { result.level = read_value<int>(store, hidden_store_level_field_, runtime_); } catch (...) {}
    try { result.coin = read_value<int>(store, hidden_store_coin_field_, runtime_); } catch (...) {}
    try {
        result.upgrade_price = read_value<int>(store, hidden_store_upgrade_price_field_, runtime_);
    } catch (...) {}
    try {
        result.refresh_price = read_value<int>(store, hidden_store_refresh_price_field_, runtime_);
    } catch (...) {}
    try { result.goods_type = read_value<int>(store, hidden_store_goods_type_field_, runtime_); } catch (...) {}
    try {
        result.free_refresh_count = read_value<int>(store, hidden_store_free_refresh_field_, runtime_);
    } catch (...) {}
    try {
        result.special_refresh = read_value<bool>(store, hidden_store_special_refresh_field_, runtime_);
    } catch (...) {}
    try {
        result.refresh_seq = read_value<int>(store, hidden_store_refresh_seq_field_, runtime_);
        result.refresh_seq_available = true;
    } catch (...) {}
    try {
        void* goods = read_object(store, hidden_store_goods_field_, runtime_);
        result.goods_available = goods != nullptr;
        result.goods = read_hidden_store_goods(goods);
    } catch (...) {}
    return result;
}

std::vector<IntelShopGood> NativeCapture::read_hidden_store_goods(void* sequence) const {
    std::vector<IntelShopGood> result;
    if (!sequence || !hidden_store_good_class_) return result;
    const int count = runtime_.count(sequence);
    const int bounded = (std::min)(count, static_cast<int>(kMaxIntelItems));
    for (int i = 0; i < bounded; ++i) {
        try {
            void* good = runtime_.at(sequence, i);
            if (!good) continue;
            IntelShopGood entry;
            if (hidden_good_identifier_field_)
                entry.chess_identifier = read_value<int>(good, hidden_good_identifier_field_, runtime_);
            if (hidden_good_slot_field_) entry.slot_id = read_value<int>(good, hidden_good_slot_field_, runtime_);
            if (hidden_good_price_field_) entry.price = read_value<int>(good, hidden_good_price_field_, runtime_);
            if (hidden_good_frozen_field_)
                entry.frozen = read_value<bool>(good, hidden_good_frozen_field_, runtime_);
            const auto descriptor = describe_chess(entry.chess_identifier, 1);
            if (descriptor.count == 0) continue;
            entry.chess_id = descriptor.chess_id;
            entry.operator_name = descriptor.operator_name;
            entry.bond_ids = descriptor.bond_ids;
            entry.bond_names = descriptor.bond_names;
            entry.operator_available = descriptor.operator_available;
            result.push_back(std::move(entry));
        } catch (...) {
        }
    }
    return result;
}

IntelChessRecord NativeCapture::describe_chess(int identifier, int count) const {
    // The Int32 GetChessBasicInfo overload takes a deployed instId, not a
    // protocol chessIdentifier. The activity table provides the latter.
    auto result = shop_catalog_entry(identifier);
    if (result.operator_available || result.chess_id.empty()) result.count = count;
    return result;
}

IntelChessRecord NativeCapture::describe_chess_info(void* boxed, int identifier, int count) const {
    IntelChessRecord result;
    result.chess_identifier = identifier;
    result.count = count;
    try {
        if (!boxed || !runtime_.class_assignable(
                chess_basic_info_class_, runtime_.object_class(boxed))) return result;
        il2cpp::Runtime::Root boxed_root(runtime_, boxed);
        const int item_type = read_value<int>(boxed_root.get(), basic_info_item_type_field_, runtime_);
        if (item_type != item_type_char_) {
            result.count = 0; // The descriptor identifies a non-character good.
            return result;
        }
        void* char_id_object = read_object(boxed_root.get(), basic_info_char_id_field_, runtime_);
        result.chess_id = runtime_.string(char_id_object);
        if (result.chess_id.empty()) return result;
        result.operator_available = true;
        result.operator_name = resolve_operator_name(char_id_object, result.chess_id);
        void* bonds = read_object(boxed_root.get(), basic_info_bonds_field_, runtime_);
        if (bonds) {
            const int bond_count = (std::min)(runtime_.count(bonds),
                                              static_cast<int>(kMaxIntelItems));
            result.bond_ids.reserve(static_cast<std::size_t>(bond_count));
            result.bond_names.reserve(static_cast<std::size_t>(bond_count));
            for (int i = 0; i < bond_count; ++i) {
                try {
                    void* bond_object = runtime_.at(bonds, i);
                    const std::string bond_id = runtime_.string(bond_object);
                    if (bond_id.empty()) continue;
                    std::string bond_name;
                    try {
                        if (bond_info_method_) {
                            void* bond_info = runtime_.invoke(bond_info_method_, nullptr, {bond_object});
                            if (bond_info)
                                bond_name = runtime_.string(read_object(
                                    bond_info, bond_info_name_field_, runtime_));
                        }
                    } catch (...) {}
                    result.bond_ids.push_back(bond_id);
                    result.bond_names.push_back(std::move(bond_name));
                } catch (...) {
                }
            }
        }
    } catch (...) {
    }
    return result;
}

std::string NativeCapture::resolve_boss_name(void* data_center, void* boss_id_object,
                                            const std::string& boss_id) const {
    if (!data_center || boss_id.empty() || !center_game_data_field_ || !enemy_handbook_data_info_)
        return {};
    try {
        constexpr auto boss_type = "Torappu.AutoChessData.AutoChessBossInfoData";
        constexpr auto enemy_type = "Torappu.EnemyHandBookData";
        constexpr auto dictionary_prefix = "System.Collections.Generic.Dictionary<System.String,";
        const auto reference = [&](void* object, const char* field_name, const std::string& type) {
            if (!object) return static_cast<void*>(nullptr);
            void* field = runtime_.field(runtime_.object_class(object), field_name);
            return runtime_.type(runtime_.field_type(field)) == type
                ? read_object(object, field, runtime_) : nullptr;
        };
        const auto instance_method = [&](void* object, const char* name, int arity,
                                         const char* result_type,
                                         const std::vector<std::string>& params = {}) {
            void* method = runtime_.method(runtime_.checked_class(object), name, arity, params);
            std::uint32_t impl = 0;
            if ((runtime_.method_flags(method, &impl) & 0x10u) ||
                (result_type && runtime_.type(runtime_.method_return(method)) != result_type))
                throw std::runtime_error("Unexpected Boss name lookup signature");
            return method;
        };
        const auto find = [&](void* dictionary, void* key, const char* value_type) -> void* {
            if (!dictionary || !key) return nullptr;
            void* lookup = instance_method(dictionary, "TryGetValue", 2, "System.Boolean",
                                           {"System.String", std::string(value_type) + "&"});
            void* value = nullptr;
            return runtime_.unbox<bool>(runtime_.invoke(lookup, dictionary, {key, &value})) ? value : nullptr;
        };

        if (runtime_.type(runtime_.field_type(center_game_data_field_)) != "Torappu.AutoChessData")
            return {};
        il2cpp::Runtime::Root game_data(runtime_, read_object(data_center, center_game_data_field_, runtime_));
        il2cpp::Runtime::Root bosses(runtime_, reference(
            game_data.get(), "bossInfoDict", std::string(dictionary_prefix) + boss_type + ">"));
        if (!bosses.get()) return {};

        il2cpp::Runtime::Root key(runtime_, boss_id_object);
        if (!key.get()) {
            // A synchronized ID may precede the DataCenter getter. Reuse the
            // matching managed dictionary key without constructing a string.
            void* get_keys = instance_method(bosses.get(), "get_Keys", 0, nullptr);
            il2cpp::Runtime::Root keys(runtime_, runtime_.invoke(get_keys, bosses.get()));
            void* get_enumerator = instance_method(keys.get(), "GetEnumerator", 0, nullptr);
            il2cpp::Runtime::Root enumerator(runtime_, runtime_.invoke(get_enumerator, keys.get()));
            void* next = instance_method(enumerator.get(), "MoveNext", 0, "System.Boolean");
            void* current = instance_method(enumerator.get(), "get_Current", 0, "System.String");
            for (int i = 0; i < 256 && runtime_.unbox<bool>(runtime_.invoke(next, enumerator.get())); ++i) {
                void* candidate = runtime_.invoke(current, enumerator.get());
                if (runtime_.string(candidate) == boss_id) {
                    key = il2cpp::Runtime::Root(runtime_, candidate);
                    break;
                }
            }
        }
        il2cpp::Runtime::Root boss(runtime_, find(bosses.get(), key.get(), boss_type));
        if (!boss.get()) return {};
        il2cpp::Runtime::Root handbook(runtime_, runtime_.invoke(enemy_handbook_data_info_, nullptr));
        il2cpp::Runtime::Root enemies(runtime_, reference(
            handbook.get(), "enemyData", std::string(dictionary_prefix) + enemy_type + ">"));
        for (const char* field : {"enemyId", "handbookEnemyId"}) {
            il2cpp::Runtime::Root enemy_id(runtime_, reference(boss.get(), field, "System.String"));
            const std::string id = runtime_.string(enemy_id.get());
            if (id.empty()) continue;
            il2cpp::Runtime::Root enemy(runtime_, find(enemies.get(), enemy_id.get(), enemy_type));
            std::string name = runtime_.string(reference(enemy.get(), "name", "System.String"));
            if (!name.empty() && name != id && name != boss_id) return name;
        }
    } catch (...) {
    }
    return {};
}

std::vector<IntelChessRecord> NativeCapture::read_hidden_chess_list(void* sequence) const {
    std::vector<IntelChessRecord> result;
    if (!sequence || !hidden_owned_identifier_field_) return result;
    const int bounded = (std::min)(runtime_.count(sequence), static_cast<int>(kMaxIntelItems));
    result.reserve(static_cast<std::size_t>(bounded));
    for (int i = 0; i < bounded; ++i) {
        try {
            void* chess = runtime_.at(sequence, i);
            if (!chess) continue;
            const int identifier = read_value<int>(chess, hidden_owned_identifier_field_, runtime_);
            result.push_back(describe_chess(identifier, 1));
        } catch (...) {
        }
    }
    return result;
}

std::vector<IntelChessRecord> NativeCapture::read_hidden_gained_list(void* sequence) const {
    std::vector<IntelChessRecord> result;
    if (!sequence) return result;
    const int bounded = (std::min)(runtime_.count(sequence), static_cast<int>(kMaxIntelItems));
    result.reserve(static_cast<std::size_t>(bounded));
    for (int i = 0; i < bounded; ++i) {
        try {
            void* gained = runtime_.at(sequence, i);
            if (!gained) continue;
            const int identifier = read_value<int>(gained, hidden_chess_identifier_field_, runtime_);
            const int count = read_value<int>(gained, hidden_chess_count_field_, runtime_);
            result.push_back(describe_chess(identifier, count));
        } catch (...) {
        }
    }
    return result;
}

ShopObservation NativeCapture::read_shop_observation(void* preparation) const {
    ShopObservation result;
    if (!preparation) return result;
    result.session_id = shop_session_id_.load(std::memory_order_acquire);
    const int fallback_round = shop_round_.load(std::memory_order_acquire);
    try { result.player_index = read_value<int>(preparation, hidden_prep_uid_field_, runtime_); }
    catch (...) { return result; }
    try { result.round = read_value<int>(preparation, hidden_prep_round_field_, runtime_); }
    catch (...) { result.round = fallback_round; }
    if (result.round <= 0) result.round = fallback_round;

    try {
        void* store = read_object(preparation, hidden_prep_store_field_, runtime_);
        if (store) {
            try {
                result.refresh_seq = read_value<int>(store, hidden_store_refresh_seq_field_, runtime_);
                result.refresh_seq_available = true;
            } catch (...) {}
            void* goods = read_object(store, hidden_store_goods_field_, runtime_);
            result.goods_available = goods != nullptr;
            if (goods) result.goods = read_hidden_store_goods(goods);
        }
    } catch (...) {}

    try {
        void* owned = read_object(preparation, hidden_prep_char_chess_field_, runtime_);
        if (owned && hidden_owned_identifier_field_) {
            result.owned = read_hidden_chess_list(owned);
            result.owned_available = true;
        }
    } catch (...) {}

    try {
        void* analytics = read_object(preparation, hidden_prep_round_analytics_field_, runtime_);
        if (analytics) {
            void* gained = read_object(analytics, hidden_analytics_chess_gained_field_, runtime_);
            if (gained && hidden_chess_identifier_field_ && hidden_chess_count_field_) {
                result.gained = read_hidden_gained_list(gained);
                result.gained_available = true;
            }
        }
    } catch (...) {}
    return result;
}

int NativeCapture::canonical_shop_player_slot(int raw_index) const noexcept {
    const bool zero_based = shop_slot_mode_.load(std::memory_order_acquire) == 1;
    if (zero_based)
        return stronghold::canonical_shop_player_slot(raw_index, true);
    return stronghold::canonical_shop_player_slot(raw_index, false);
}

void NativeCapture::capture_shop_observation(void* preparation) noexcept {
    if (!preparation || !shop_session_id_.load(std::memory_order_acquire)) return;
    try {
        ShopObservation observation = read_shop_observation(preparation);
        observation.player_index = canonical_shop_player_slot(observation.player_index);
        if (is_shop_player_slot(observation.player_index) && observation.round > 0 &&
            (observation.goods_available || observation.owned_available || observation.gained_available)) {
            shop_observations_.fetch_add(1, std::memory_order_relaxed);
            shop_hidden_observations_.fetch_add(1, std::memory_order_relaxed);
            publish_shop_observation(std::move(observation));
        } else shop_rejected_observations_.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        shop_rejected_observations_.fetch_add(1, std::memory_order_relaxed);
    }
}

void NativeCapture::on_preparation_read(void* self, void* reader, void* method) {
    CallbackScope scope(g_callback_capture);
    if (original_preparation_read) original_preparation_read(self, reader, method);
    if (!scope.recording()) return;
    try {
        il2cpp::Runtime::Attachment attachment(scope.get()->runtime_);
        scope.get()->capture_shop_observation(self);
    } catch (...) {
    }
}

void NativeCapture::capture_hidden_sync(void* all_state) noexcept {
    HiddenSyncSnapshot snapshot;
    if (!all_state) return;
    snapshot.available = true;
    snapshot.session_id = shop_session_id_.load(std::memory_order_acquire);
    snapshot.captured_tick = GetTickCount64();
    try {
        snapshot.hidden_boss_id =
            runtime_.string(read_raw_reference(all_state, hidden_boss_id_offset_));
        snapshot.hidden_boss_available = true;
        snapshot.stage_seed = read_raw_value<int>(all_state, hidden_stage_seed_offset_);
        snapshot.stage_seed_available = true;

        std::map<int, IntelHiddenPlayer> players;
        void* static_players = read_raw_reference(all_state, hidden_static_players_offset_);
        if (static_players && hidden_static_player_class_) {
            const int count = runtime_.count(static_players);
            const int bounded = (std::min)(count, static_cast<int>(kMaxPlayers));
            for (int i = 0; i < bounded; ++i) {
                try {
                    void* player = runtime_.at(static_players, i);
                    if (!player) continue;
                    int index = -1;
                    if (hidden_static_index_field_)
                        index = read_value<int>(player, hidden_static_index_field_, runtime_);
                    if (index < 0 || index >= static_cast<int>(kMaxPlayers)) continue;
                    auto& entry = players[index];
                    entry.index = index;
                    if (hidden_static_band_field_)
                        entry.band_id = runtime_.string(
                            read_object(player, hidden_static_band_field_, runtime_));
                    if (hidden_static_banned_units_field_) {
                        entry.banned_units = read_banned_units(
                            read_object(player, hidden_static_banned_units_field_, runtime_));
                        entry.banned_units_available = true;
                    }
                } catch (...) {
                }
            }
        }

        void* scene = read_raw_reference(all_state, hidden_scene_detail_offset_);
        if (scene) {
            if (hidden_scene_state_field_) {
                try {
                    snapshot.scene_state =
                        read_value<int>(scene, hidden_scene_state_field_, runtime_);
                    snapshot.scene_state_available = true;
                } catch (...) {}
            }
            if (hidden_scene_round_field_) {
                try {
                    snapshot.round = read_value<int>(scene, hidden_scene_round_field_, runtime_);
                    snapshot.round_available = true;
                } catch (...) {}
            }
            if (hidden_scene_runtime_players_field_) {
                try {
                    void* runtime_players =
                        read_object(scene, hidden_scene_runtime_players_field_, runtime_);
                    const int count = runtime_.count(runtime_players);
                    const int bounded = (std::min)(count, static_cast<int>(kMaxPlayers));
                    for (int i = 0; i < bounded; ++i) {
                        try {
                            void* runtime_player = runtime_.at(runtime_players, i);
                            if (!runtime_player) continue;
                            int index = -1;
                            if (hidden_runtime_uid_field_)
                                index = read_value<int>(runtime_player, hidden_runtime_uid_field_, runtime_);
                            if (index < 0 || index >= static_cast<int>(kMaxPlayers)) continue;
                            auto& entry = players[index];
                            entry.index = index;
                            if (hidden_runtime_shop_lv_field_) {
                                entry.shop_level = read_value<int>(
                                    runtime_player, hidden_runtime_shop_lv_field_, runtime_);
                                entry.shop_level_available = true;
                            }
                            if (hidden_runtime_hp_field_) {
                                entry.hp = read_value<int>(
                                    runtime_player, hidden_runtime_hp_field_, runtime_);
                                entry.hp_available = true;
                            }
                            if (hidden_runtime_state_field_) {
                                entry.game_state = read_value<int>(
                                    runtime_player, hidden_runtime_state_field_, runtime_);
                                entry.game_state_available = true;
                            }
                        } catch (...) {
                        }
                    }
                } catch (...) {
                }
            }
            if (hidden_scene_prep_status_field_) {
                try {
                    void* preparation =
                        read_object(scene, hidden_scene_prep_status_field_, runtime_);
                    const int count = runtime_.count(preparation);
                    const int bounded = (std::min)(count, static_cast<int>(kMaxPlayers));
                    for (int i = 0; i < bounded; ++i) {
                        try {
                            void* prep = runtime_.at(preparation, i);
                            if (!prep) continue;
                            int index = -1;
                            if (hidden_prep_uid_field_)
                                index = read_value<int>(prep, hidden_prep_uid_field_, runtime_);
                            if (index < 0 || index >= static_cast<int>(kMaxPlayers)) continue;
                            auto& entry = players[index];
                            entry.index = index;
                            if (hidden_prep_store_field_) {
                                entry.store = read_hidden_store(
                                    read_object(prep, hidden_prep_store_field_, runtime_));
                            }
                            if (hidden_prep_char_chess_field_) {
                                void* owned = read_object(prep, hidden_prep_char_chess_field_, runtime_);
                                if (owned && hidden_owned_identifier_field_) {
                                    entry.owned = read_hidden_chess_list(owned);
                                    entry.owned_available = true;
                                }
                            }
                            if (hidden_prep_round_analytics_field_) {
                                void* analytics = read_object(
                                    prep, hidden_prep_round_analytics_field_, runtime_);
                                if (analytics) {
                                    void* gained = read_object(
                                        analytics, hidden_analytics_chess_gained_field_, runtime_);
                                    if (gained && hidden_chess_identifier_field_ && hidden_chess_count_field_) {
                                        entry.gained = read_hidden_gained_list(gained);
                                        entry.gained_available = true;
                                    }
                                }
                            }
                            if (entry.store.available) {
                                entry.shop_level = entry.store.level;
                                entry.shop_level_available = true;
                            }
                            ShopObservation observation;
                            observation.session_id = snapshot.session_id;
                            observation.player_index = canonical_shop_player_slot(index);
                            observation.round = snapshot.round_available && snapshot.round > 0
                                ? snapshot.round : shop_round_.load(std::memory_order_acquire);
                            try {
                                const int prep_round = read_value<int>(
                                    prep, hidden_prep_round_field_, runtime_);
                                if (prep_round > 0) observation.round = prep_round;
                            } catch (...) {}
                            observation.refresh_seq = entry.store.refresh_seq;
                            observation.refresh_seq_available = entry.store.refresh_seq_available;
                            observation.goods_available = entry.store.goods_available;
                            observation.goods = entry.store.goods;
                            observation.owned_available = entry.owned_available;
                            observation.owned = entry.owned;
                            observation.gained_available = entry.gained_available;
                            observation.gained = entry.gained;
                            if (observation.session_id &&
                                is_shop_player_slot(observation.player_index) &&
                                observation.round > 0 &&
                                (observation.goods_available || observation.owned_available ||
                                 observation.gained_available)) {
                                shop_observations_.fetch_add(1, std::memory_order_relaxed);
                                shop_hidden_observations_.fetch_add(1, std::memory_order_relaxed);
                                publish_shop_observation(std::move(observation));
                            } else {
                                shop_rejected_observations_.fetch_add(1, std::memory_order_relaxed);
                            }
                        } catch (...) {
                        }
                    }
                } catch (...) {
                }
            }
        }
        snapshot.players.reserve(players.size());
        for (auto& [index, player] : players) {
            (void)index;
            snapshot.players.push_back(std::move(player));
        }
    } catch (...) {
        snapshot.available = true;
    }
    publish_hidden_sync(std::move(snapshot));
}

bool NativeCapture::shutdown_locked() {
    host_active_.store(false);
    {
        std::lock_guard lock(callback_mutex_);
        callbacks_enabled_ = false;
        ready_ = false;
    }
    bool removed = true;
    for (std::uint64_t& id : hooks_) {
        if (!id) {
            continue;
        }
        if (host_.remove_native_hook(host_.owner, id)) {
            id = 0;
        } else {
            removed = false;
        }
    }
    if (!removed) {
        return false;
    }
    {
        std::unique_lock lock(callback_mutex_);
        callbacks_drained_.wait(lock, [&] { return callbacks_in_flight_ == 0; });
    }
    hooks_.clear();
    g_callback_capture = nullptr;
    original_log_modifier = nullptr;
    original_log_ep_modifier = nullptr;
    original_start_game = nullptr;
    original_clear_runtime_data = nullptr;
    original_on_destroy = nullptr;
    original_update = nullptr;
    original_global_update = nullptr;
    original_applied_modifier = nullptr;
    original_all_state_read = nullptr;
    original_preparation_read = nullptr;
    runtime_card_root_ = il2cpp::Runtime::Root{};
    inst_player_root_ = il2cpp::Runtime::Root{};
    token_card_root_ = il2cpp::Runtime::Root{};
    manager_root_ = il2cpp::Runtime::Root{};
    center_root_ = il2cpp::Runtime::Root{};
    reset_timing();
    hosts_.clear();
    live_host_cards_.clear();
    host_players_.clear();
    side_players_.clear();
    host_cache_player_count_ = -1;
    set_controller(nullptr);
    battle_active_ = false;
    battle_ended_ = false;
    in_mode_ = false;
    return true;
}

bool NativeCapture::dictionary_int_int(void* dictionary, int key, int& value) const {
    if (!dictionary) {
        return false;
    }
    return runtime_.unbox<bool>(runtime_.call(dictionary, "TryGetValue", {&key, &value}));
}

bool NativeCapture::dictionary_uint_int(void* dictionary, std::uint32_t key, int& value) const {
    if (!dictionary) {
        return false;
    }
    return runtime_.unbox<bool>(runtime_.call(dictionary, "TryGetValue", {&key, &value}));
}

bool NativeCapture::dictionary_int_uint(void* dictionary, int key,
                                        std::uint32_t& value) const {
    if (!dictionary) {
        return false;
    }
    return runtime_.unbox<bool>(runtime_.call(dictionary, "TryGetValue", {&key, &value}));
}

int NativeCapture::player_for_card(std::uint32_t card_uid) const {
    if (!card_uid || !runtime_card_root_.get() || !inst_player_root_.get()) {
        return kUnknownPlayer;
    }
    try {
        int inst_id = 0;
        bool found = false;
        int chess_player = kUnknownPlayer;
        if (runtime_cards_are_instances_) {
            void* chess = nullptr;
            found = runtime_.unbox<bool>(runtime_.invoke(runtime_card_lookup_info_,
                runtime_card_root_.get(), {&card_uid, &chess}));
            if (found) {
                if (!chess || !runtime_.class_assignable(chess_class_, runtime_.object_class(chess)))
                    return kUnknownPlayer;
                il2cpp::Runtime::Root root(runtime_, chess);
                if (read_value<std::uint32_t>(chess, chess_card_uid_field_, runtime_) != card_uid)
                    return kUnknownPlayer;
                inst_id = read_value<int>(chess, chess_inst_id_field_, runtime_);
                chess_player = read_value<int>(chess, chess_player_field_, runtime_);
            }
        } else {
            found = dictionary_uint_int(runtime_card_root_.get(), card_uid, inst_id);
        }
        if (!found) {
            const auto token = token_players_.find(card_uid);
            return token == token_players_.end() ? kUnknownPlayer : token->second;
        }
        int player = kUnknownPlayer;
        if (!dictionary_int_int(inst_player_root_.get(), inst_id, player) ||
            player < 0 || player >= static_cast<int>(kMaxPlayers)) return kUnknownPlayer;
        // Do not attribute a stale card to another player's reused instance.
        if (runtime_cards_are_instances_ && chess_player != player) return kUnknownPlayer;
        return player;
    } catch (const std::exception& error) {
        remember_failure("card owner lookup", error);
        return kUnknownPlayer;
    }
}

HostInfo NativeCapture::host_for(std::uint32_t card_uid) const {
    const auto found = hosts_.find(card_uid);
    return found == hosts_.end() ? HostInfo{} : found->second;
}

int NativeCapture::retained_player_for_card(std::uint32_t card_uid) const {
    const int current = player_for_card(card_uid);
    if (current >= 0) return current;
    const auto found = host_players_.find(card_uid);
    return found == host_players_.end() ? kUnknownPlayer : found->second;
}

bool NativeCapture::enemy_damage_target(void* target) const {
    if (!target || !enemy_class_) return false;
    try {
        // ENEMY visual side is not sufficient: right-hand teammates can use it.
        return runtime_.class_assignable(enemy_class_, runtime_.object_class(target));
    } catch (const std::exception& error) {
        remember_failure("damage target", error);
        return false;
    }
}

void NativeCapture::store_host(std::uint32_t card_uid, const HostInfo& host) {
    if (!card_uid) {
        return;
    }
    const auto found = hosts_.find(card_uid);
    HostInfo merged = host;
    if (found != hosts_.end() && found->second.token == host.token &&
        found->second.host_uid == host.host_uid) {
        if (merged.id.empty()) merged.id = found->second.id;
        if (merged.name.empty()) merged.name = found->second.name;
    }
    const bool changed = found == hosts_.end() || found->second.id != merged.id ||
                         found->second.name != merged.name || found->second.token != merged.token ||
                         found->second.host_uid != merged.host_uid;
    if (found != hosts_.end() && (found->second.token != merged.token ||
        found->second.host_uid != merged.host_uid)) attribution_cache_.clear();
    hosts_[card_uid] = merged;
    if (!changed) {
        return;
    }
    for (auto& [source_card, attribution] : attribution_cache_) {
        if (attribution.canonical_card == card_uid) {
            attribution.host = merged;
        }
    }
}

void NativeCapture::refresh_index_maps(void* data_center) {
    void* indexer = data_center ? read_object(data_center, data_center_indexer_field_, runtime_)
                                : nullptr;
    if (!indexer) {
        runtime_card_root_ = il2cpp::Runtime::Root{};
        inst_player_root_ = il2cpp::Runtime::Root{};
        token_card_root_ = il2cpp::Runtime::Root{};
        token_players_.clear();
        diagnostic_runtime_cards_ = diagnostic_inst_players_ = 0;
        return;
    }
    runtime_card_root_ =
        il2cpp::Runtime::Root(runtime_, read_object(indexer, indexer_runtime_card_field_, runtime_));
    inst_player_root_ =
        il2cpp::Runtime::Root(runtime_, read_object(indexer, indexer_inst_player_field_, runtime_));
    token_card_root_ =
        il2cpp::Runtime::Root(runtime_, read_object(indexer, indexer_token_card_field_, runtime_));
    token_players_.clear();
    try {
        diagnostic_runtime_cards_ = runtime_card_root_.get() ? runtime_.count(runtime_card_root_.get()) : 0;
        diagnostic_inst_players_ = inst_player_root_.get() ? runtime_.count(inst_player_root_.get()) : 0;
    } catch (const std::exception& error) {
        diagnostic_runtime_cards_ = diagnostic_inst_players_ = -1;
        remember_failure("diagnostic index counts", error);
    }
    // In the supplied native binary this dictionary maps a token chess instId
    // to the token's OWN cardUid. It does not map a token to its host operator.
    // Reverse it only to find the token's player; use tokenOrHostUniqueId for hosts.
    if (token_card_root_.get() && runtime_.count(token_card_root_.get()) > 0) {
        try {
            il2cpp::Runtime::Root keys(runtime_, runtime_.call(token_card_root_.get(), "get_Keys"));
            il2cpp::Runtime::Root enumerator(runtime_, runtime_.call(keys.get(), "GetEnumerator"));
            for (int n = 0; n < 4096 && runtime_.unbox<bool>(runtime_.call(enumerator.get(), "MoveNext")); ++n) {
                const int inst_id = runtime_.unbox<int>(runtime_.call(enumerator.get(), "get_Current"));
                std::uint32_t token_card = 0;
                int player = kUnknownPlayer;
                if (dictionary_int_uint(token_card_root_.get(), inst_id, token_card) &&
                    dictionary_int_int(inst_player_root_.get(), inst_id, player))
                    token_players_[token_card] = player;
            }
        } catch (const std::exception& error) {
            if (!token_index_warning_sent_) {
                token_index_warning_sent_ = true;
                const std::string message = std::string("Token player index unavailable; using verified host links: ") + error.what();
                if (host_.log) host_.log(host_.owner, 2, message.c_str());
            }
        }
    }
}

void NativeCapture::rebuild_host_cache() {
    live_host_cards_.clear();
    side_players_.clear();
    local_simulated_mask_ = 0;
    if (!controller_) {
        return;
    }
    try {
        void* list = controller_player_list_(controller_, controller_player_list_info_);
        const int count = runtime_.count(list);
        if (count < 0 || count > static_cast<int>(kMaxPlayers))
            throw std::runtime_error("Invalid local BattlePlayerData list size");
        std::set<int> conflicted_sides;
        for (int i = 0; i < count; ++i) {
            void* player_data = runtime_.at(list, i);
            if (!player_data) continue;
            const int side = battle_player_side_field_ ?
                read_value<int>(player_data, battle_player_side_field_, runtime_) : player_side_invalid_;
            int verified_player = kUnknownPlayer;
            bool conflict = false;
            for (void* field : {battle_characters_field_, battle_tokens_field_}) {
                void* array = read_object(player_data, field, runtime_);
                const int size = runtime_.count(array);
                for (int j = 0; j < size; ++j) {
                    void* data = runtime_.at(array, j);
                    if (!data) {
                        continue;
                    }
                    try {
                        HostInfo info;
                        info.unique_id =
                            read_value<std::uint32_t>(data, char_data_unique_field_, runtime_);
                        info.token = read_value<bool>(data, char_data_token_field_, runtime_);
                        info.host_uid =
                            read_value<std::uint32_t>(data, char_data_host_uid_field_, runtime_);
                        info.id =
                            read_string_field(runtime_, data, battle_character_data_class_, "id");
                        if (info.id.empty()) {
                            info.id = read_string_field(runtime_, data, battle_character_data_class_,
                                                        "tmplId");
                        }
                        info.name = read_string_field(runtime_, data, battle_character_data_class_,
                                                      "nameCn");
                        if (info.name.empty()) {
                            info.name = read_string_field(runtime_, data,
                                                          battle_character_data_class_, "nameEn");
                        }
                        if (info.name.empty()) {
                            info.name = info.id;
                        }
                        if (info.unique_id) {
                            const int owner = player_for_card(info.unique_id);
                            local_simulated_mask_ |= player_bit(owner);
                            live_host_cards_.insert(info.unique_id);
                            if (owner >= 0) {
                                if (verified_player >= 0 && verified_player != owner) conflict = true;
                                verified_player = owner;
                                const auto old = host_players_.find(info.unique_id);
                                if (old != host_players_.end() && old->second != owner)
                                    attribution_cache_.clear();
                                host_players_[info.unique_id] = owner;
                            }
                            store_host(info.unique_id, info);
                        }
                    } catch (const std::exception& error) {
                        remember_failure("battle character data", error);
                        continue;
                    }
                }
            }
            if (side != player_side_invalid_ && verified_player >= 0) {
                const auto previous = side_players_.find(side);
                if (conflict || (previous != side_players_.end() && previous->second != verified_player)) {
                    conflicted_sides.insert(side);
                    side_players_.erase(side);
                    ++counters_.side_conflicts;
                } else if (!conflicted_sides.count(side)) side_players_[side] = verified_player;
            }
        }
        // One player appearing under multiple sides is also ambiguous.
        std::map<int, int> player_sides;
        for (const auto& [side, owner] : side_players_) {
            const auto found = player_sides.find(owner);
            if (found != player_sides.end() && found->second != side) {
                conflicted_sides.insert(side);
                conflicted_sides.insert(found->second);
                ++counters_.side_conflicts;
            } else player_sides[owner] = side;
        }
        for (int side : conflicted_sides) side_players_.erase(side);
    } catch (const std::exception& error) {
        remember_failure("battle player data list", error);
        // Keep copied hosts for late projectiles, not stale coverage evidence.
        live_host_cards_.clear();
        side_players_.clear();
        local_simulated_mask_ = 0;
    }
}

bool NativeCapture::resolve_character(void* entity, HostInfo& host, int& player,
                                      std::uint32_t& card_uid) {
    host = {};
    player = kUnknownPlayer;
    card_uid = 0;
    void* visited[16]{};
    for (int depth = 0; entity && depth < 16; ++depth) {
        for (int prior = 0; prior < depth; ++prior) if (visited[prior] == entity) return false;
        visited[depth] = entity;
        void* klass = runtime_.object_class(entity);
        if (enemy_class_ && runtime_.class_assignable(enemy_class_, klass)) return false;
        if (runtime_.class_assignable(projectile_class_, klass)) {
            entity = projectile_source_(entity, projectile_source_info_);
            continue;
        }
        if (!runtime_.class_assignable(character_class_, klass)) return false;

        const int side = character_side_(entity, character_side_info_);
        void* data = character_data_(entity, character_data_info_);
        std::uint32_t direct_card = character_card_uid_(entity, character_card_uid_info_);
        if (!direct_card && data) direct_card = read_value<std::uint32_t>(data, char_data_unique_field_, runtime_);
        const bool token = character_is_token_(entity, character_is_token_info_) ||
            (data && read_value<bool>(data, char_data_token_field_, runtime_));
        const std::uint32_t host_uid = data ?
            read_value<std::uint32_t>(data, char_data_host_uid_field_, runtime_) : 0;
        player = retained_player_for_card(direct_card);
        if (player < 0 && host_uid) player = retained_player_for_card(host_uid);
        // A verified AutoChess player mapping also permits the right-hand map.
        if (side != static_cast<int>(kSideAlly) && player < 0) return false;

        const HostInfo previous_host = host_for(direct_card);
        if (token && host_uid && previous_host.token && previous_host.host_uid != host_uid)
            attribution_cache_.clear();
        const auto cached = attribution_cache_.find(direct_card);
        if (direct_card && cached != attribution_cache_.end() &&
            (player < 0 || cached->second.player == player)) {
            host = cached->second.host;
            player = cached->second.player;
            card_uid = cached->second.canonical_card;
            return true;
        }

        if (direct_card && player >= 0) host_players_[direct_card] = player;
        if (token) {
            // A token can spawn without changing the controller player count.
            HostInfo token_info;
            token_info.unique_id = direct_card;
            token_info.host_uid = host_uid;
            token_info.token = true;
            if (direct_card && host_uid) store_host(direct_card, token_info);
            std::uint32_t canonical = host_uid;
            if (!canonical) {
                const HostInfo cached_token = host_for(direct_card);
                if (cached_token.token) canonical = cached_token.host_uid;
            }
            // Tokens may themselves be owned by another token. Never turn a
            // token's own ID into an operator row when the owner is unknown.
            std::uint32_t seen[16]{};
            for (int host_depth = 0; canonical && host_depth < 16; ++host_depth) {
                bool cycle = false;
                for (int i = 0; i < host_depth; ++i) cycle = cycle || seen[i] == canonical;
                if (cycle) break;
                seen[host_depth] = canonical;
                HostInfo candidate = host_for(canonical);
                const int owner = retained_player_for_card(canonical);
                if (owner >= 0 && player >= 0 && owner != player) break;
                const auto parent = attribution_cache_.find(canonical);
                if (candidate.id.empty() && parent != attribution_cache_.end() &&
                    (player < 0 || parent->second.player == player)) {
                    candidate = parent->second.host;
                    canonical = parent->second.canonical_card;
                    if (player < 0) player = parent->second.player;
                }
                if (!candidate.token && !candidate.id.empty() && candidate.id != "unknown") {
                    host = candidate;
                    card_uid = canonical;
                    const int canonical_owner = retained_player_for_card(canonical);
                    if (canonical_owner >= 0) player = canonical_owner;
                    if (direct_card && player >= 0)
                        attribution_cache_[direct_card] = Attribution{player, card_uid, host};
                    return true;
                }
                canonical = candidate.token ? candidate.host_uid : 0;
            }
            host.id = "unknown_host";
            host.name = "未归属宿主";
            host.token = true;
            return true;
        }

        host = host_for(direct_card);
        const std::string character_id = runtime_.string(character_id_(entity, character_id_info_));
        if (!character_id.empty()) host.id = character_id;
        if (host.id.empty() && data) {
            host.id = read_string_field(runtime_, data, battle_character_data_class_, "id");
            if (host.id.empty()) host.id = read_string_field(runtime_, data, battle_character_data_class_, "tmplId");
        }
        if (host.name.empty() && data)
            host.name = read_string_field(runtime_, data, battle_character_data_class_, "nameCn");
        if (host.id.empty()) host.id = "unknown";
        if (host.name.empty()) host.name = host.id;
        host.unique_id = direct_card;
        host.host_uid = host_uid;
        host.token = false;
        card_uid = direct_card;
        if (direct_card) store_host(direct_card, host);
        if (direct_card && player >= 0 && host.id != "unknown")
            attribution_cache_[direct_card] = Attribution{player, card_uid, host};
        return true;
    }
    return false;
}

bool NativeCapture::read_player(void* player_model, int index, PlayerInfo& info, int& hp,
                                int& connect_state, bool& game_finished) const {
    void* player = runtime_.invoke(player_model_get_player_info_, player_model, {&index});
    if (!player) {
        return false;
    }
    info.uid = runtime_.string(read_object(player, scene_uid_field_, runtime_));
    void* card = read_object(player, scene_card_field_, runtime_);
    info.name = card ? runtime_.string(read_object(card, card_nickname_field_, runtime_)) : "";
    info.band_id.clear();
    info.band_available = false;
    if (scene_band_field_) {
        try {
            info.band_id = runtime_.string(read_object(player, scene_band_field_, runtime_));
            info.band_available = true;
        } catch (...) {
        }
    }
    info.squad_slots.clear();
    info.squad_available = false;
    if (scene_squad_slots_field_) {
        try {
            info.squad_available =
                read_squad_slots(read_object(player, scene_squad_slots_field_, runtime_),
                                 info.squad_slots);
        } catch (...) {
        }
    }
    info.shop_level = 0;
    info.shop_level_available = false;
    if (scene_shop_level_field_) {
        try {
            info.shop_level = read_value<int>(player, scene_shop_level_field_, runtime_);
            info.shop_level_available = true;
        } catch (...) {
        }
    }
    info.shop_coin = 0;
    info.shop_coin_available = false;
    if (scene_shop_coin_field_) {
        try {
            info.shop_coin = read_value<int>(player, scene_shop_coin_field_, runtime_);
            info.shop_coin_available = true;
        } catch (...) {
        }
    }
    int scene_index = read_value<int>(player, scene_index_field_, runtime_);
    if (scene_index != index || scene_index < 0 || scene_index >= static_cast<int>(kMaxPlayers))
        throw std::runtime_error("GetPlayerData returned a different/invalid player slot");
    info.index = scene_index;
    hp = read_value<int>(player, scene_hp_field_, runtime_);
    connect_state = read_value<int>(player, scene_connect_field_, runtime_);
    game_finished = read_value<bool>(player, scene_finished_field_, runtime_);
    info.hp = hp;
    info.connect_state = connect_state;
    info.battle_state = read_value<int>(player, scene_game_state_field_, runtime_);
    info.game_finished = game_finished;
    info.dead = runtime_.unbox<bool>(runtime_.invoke(scene_dead_info_, player));
    return !info.uid.empty() || !info.name.empty();
}

std::vector<IntelBannedUnit> NativeCapture::read_banned_units(void* sequence) const {
    std::vector<IntelBannedUnit> result;
    if (!sequence || !banned_unit_class_) return result;
    const int count = runtime_.count(sequence);
    const int bounded = (std::min)(count, static_cast<int>(kMaxIntelItems));
    for (int i = 0; i < bounded; ++i) {
        try {
            void* unit = runtime_.at(sequence, i);
            if (!unit) continue;
            IntelBannedUnit entry;
            if (banned_unit_bond_id_field_) {
                try {
                    entry.bond_id = runtime_.string(
                        read_object(unit, banned_unit_bond_id_field_, runtime_));
                } catch (...) {
                }
            }
            if (banned_unit_chess_ids_field_) {
                try {
                    void* identifiers = read_object(unit, banned_unit_chess_ids_field_, runtime_);
                    if (identifiers) {
                        const int id_count = runtime_.count(identifiers);
                        const int id_bounded =
                            (std::min)(id_count, static_cast<int>(kMaxIntelItems));
                        for (int j = 0; j < id_bounded; ++j) {
                            try {
                                entry.chess_identifiers.push_back(
                                    runtime_.unbox<int>(runtime_.at(identifiers, j)));
                            } catch (...) {
                            }
                        }
                    }
                } catch (...) {
                }
            }
            if (!entry.bond_id.empty() || !entry.chess_identifiers.empty())
                result.push_back(std::move(entry));
        } catch (...) {
        }
    }
    return result;
}

bool NativeCapture::read_squad_slots(void* sequence,
                                      std::vector<IntelSquadSlot>& result) const {
    result.clear();
    if (!squad_slot_class_) return false;
    if (!sequence) return true;
    const int count = runtime_.count(sequence);
    const int bounded = (std::min)(count, 16);
    for (int i = 0; i < bounded; ++i) {
        try {
            void* slot = runtime_.at(sequence, i);
            if (!slot) continue;
            IntelSquadSlot entry;
            const auto read_text = [&](void* field) -> std::string {
                if (!field) return {};
                try {
                    return runtime_.string(read_object(slot, field, runtime_));
                } catch (...) {
                    return {};
                }
            };
            entry.char_id = read_text(squad_char_id_field_);
            entry.chess_id = read_text(squad_chess_id_field_);
            entry.template_id = read_text(squad_template_id_field_);
            entry.cultivate_effect_id = read_text(squad_cultivate_effect_id_field_);
            entry.equip_id = read_text(squad_equip_id_field_);
            entry.skin_id = read_text(squad_skin_id_field_);
            if (squad_type_field_) {
                try {
                    entry.type = read_value<int>(slot, squad_type_field_, runtime_);
                } catch (...) {
                }
            }
            if (squad_potential_rank_field_) {
                try {
                    entry.potential_rank = read_value<int>(slot, squad_potential_rank_field_, runtime_);
                } catch (...) {
                }
            }
            if (squad_skill_index_field_) {
                try {
                    entry.skill_index = read_value<int>(slot, squad_skill_index_field_, runtime_);
                } catch (...) {
                }
            }
            if (squad_bond_ids_field_) {
                try {
                    void* bonds = read_object(slot, squad_bond_ids_field_, runtime_);
                    if (bonds) {
                        const int bond_count = runtime_.count(bonds);
                        const int bond_bounded =
                            (std::min)(bond_count, static_cast<int>(kMaxIntelItems));
                        for (int j = 0; j < bond_bounded; ++j) {
                            try {
                                std::string bond = runtime_.string(runtime_.at(bonds, j));
                                if (!bond.empty()) entry.bond_ids.push_back(std::move(bond));
                            } catch (...) {
                            }
                        }
                    }
                } catch (...) {
                }
            }
            if (squad_banned_field_) {
                try {
                    entry.banned = read_value<bool>(slot, squad_banned_field_, runtime_);
                    entry.banned_available = true;
                } catch (...) {
                }
            }
            if (!entry.char_id.empty() || !entry.chess_id.empty() ||
                !entry.template_id.empty() || !entry.cultivate_effect_id.empty() ||
                !entry.equip_id.empty() || !entry.skin_id.empty() || !entry.bond_ids.empty() ||
                entry.type >= 0 || entry.potential_rank >= 0 || entry.skill_index >= 0 ||
                entry.banned_available) {
                result.push_back(std::move(entry));
            }
        } catch (...) {
        }
    }
    return true;
}

std::vector<IntelShopGood> NativeCapture::read_shop_goods(void* sequence, bool& slot_ids_available) const {
    std::vector<IntelShopGood> result;
    if (!sequence || !shop_goods_class_ || !shop_good_chess_id_field_) return result;
    slot_ids_available = shop_good_slot_id_field_ != nullptr;
    const int count = runtime_.count(sequence);
    const int bounded = (std::min)(count, static_cast<int>(kMaxIntelItems));
    std::set<int> used_slots;
    for (int i = 0; i < bounded; ++i) {
        try {
            void* good = runtime_.at(sequence, i);
            if (!good) continue;
            IntelShopGood entry;
            entry.slot_id = i;
            if (shop_good_slot_id_field_) {
                try { entry.slot_id = read_value<int>(good, shop_good_slot_id_field_, runtime_); }
                catch (...) { slot_ids_available = false; }
            }
            if (entry.slot_id < 0 || !used_slots.insert(entry.slot_id).second)
                slot_ids_available = false;
            if (shop_good_price_field_) {
                try { entry.price = read_value<int>(good, shop_good_price_field_, runtime_); }
                catch (...) {}
            }
            if (shop_good_frozen_field_) {
                try { entry.frozen = read_value<bool>(good, shop_good_frozen_field_, runtime_); }
                catch (...) {}
            }
            void* chess_id_object = read_object(good, shop_good_chess_id_field_, runtime_);
            entry.chess_id = runtime_.string(chess_id_object);
            if (entry.chess_id.empty()) continue;
            // The local ShopData path exposes the stable chessId string rather
            // than the protocol's chessIdentifier. It is still sufficient to
            // count an observed operator even when the optional descriptor
            // lookup is unavailable.
            auto descriptor = shop_catalog_entry(0, entry.chess_id);
            if (!descriptor.operator_available && !descriptor.chess_id.empty()) continue;
            if (chess_basic_info_by_id_method_) {
                try {
                    auto live = describe_chess_info(runtime_.invoke(
                        chess_basic_info_by_id_method_, nullptr, {chess_id_object}), 0, 1);
                    if (live.operator_available) {
                        if (live.chess_id == descriptor.chess_id) {
                            if (live.operator_name.empty()) live.operator_name = descriptor.operator_name;
                            if (live.bond_ids.empty()) {
                                live.bond_ids = std::move(descriptor.bond_ids);
                                live.bond_names = std::move(descriptor.bond_names);
                            } else {
                                for (std::size_t bond = 0; bond < live.bond_ids.size(); ++bond) {
                                    if (!live.bond_names[bond].empty()) continue;
                                    const auto configured_id = std::find(descriptor.bond_ids.begin(),
                                        descriptor.bond_ids.end(), live.bond_ids[bond]);
                                    if (configured_id != descriptor.bond_ids.end())
                                        live.bond_names[bond] = descriptor.bond_names[
                                            configured_id - descriptor.bond_ids.begin()];
                                }
                            }
                        }
                        descriptor = std::move(live);
                    }
                    else if (live.count == 0 && !descriptor.operator_available) continue;
                } catch (...) {}
            }
            if (descriptor.operator_available) {
                entry.chess_id = std::move(descriptor.chess_id);
                entry.operator_name = std::move(descriptor.operator_name);
                entry.bond_ids = std::move(descriptor.bond_ids);
                entry.bond_names = std::move(descriptor.bond_names);
                entry.operator_available = true;
            }
            if (entry.operator_name.empty()) {
                entry.operator_name = resolve_operator_name(chess_id_object, entry.chess_id);
            }
            result.push_back(std::move(entry));
        } catch (...) {
        }
    }
    return result;
}

void NativeCapture::refresh_match_intel(void* data_center,
                                        const std::vector<PlayerInfo>& players,
                                        int stage_seed) {
    MatchIntelSnapshot snapshot;
    snapshot.available = data_center != nullptr;
    snapshot.session_id = session_id_;
    snapshot.round = static_cast<std::uint32_t>(current_round_);
    snapshot.stage_seed = stage_seed;
    snapshot.stage_seed_available = data_center != nullptr;
    bool shop_refresh_seq_available = false;
    bool shop_slot_ids_available = false;
    if (data_center) {
        const auto read_text = [&](void* method, bool& available) -> std::string {
            if (!method) return {};
            try {
                std::string value = runtime_.string(runtime_.invoke(method, data_center));
                available = true;
                return value;
            } catch (...) {
                return {};
            }
        };
        const auto read_list = [&](void* method, std::vector<std::string>& target,
                                   bool& available) {
            if (!method) return;
            try {
                append_string_list(runtime_, runtime_.invoke(method, data_center), target);
                available = true;
            } catch (...) {
            }
        };
        snapshot.stage_id = read_text(center_stage_id_info_, snapshot.stage_available);
        snapshot.mode_id = read_text(center_mode_id_info_, snapshot.mode_available);
        snapshot.boss_id = read_text(center_boss_id_info_, snapshot.boss_available);
        il2cpp::Runtime::Root local_boss_key;
        try {
            if (center_hidden_boss_id_info_)
                local_boss_key = il2cpp::Runtime::Root(
                    runtime_, runtime_.invoke(center_hidden_boss_id_info_, data_center));
            snapshot.hidden_boss_id = runtime_.string(local_boss_key.get());
        } catch (...) {}
        void* matching_boss_key = local_boss_key.get();
        {
            std::lock_guard lock(g_hidden_sync_mutex);
            if (g_hidden_sync.available && g_hidden_sync.session_id == snapshot.session_id &&
                g_hidden_sync.hidden_boss_available && !g_hidden_sync.hidden_boss_id.empty() &&
                g_hidden_sync.hidden_boss_id != snapshot.hidden_boss_id) {
                snapshot.hidden_boss_id = g_hidden_sync.hidden_boss_id;
                matching_boss_key = nullptr;
            }
        }
        snapshot.hidden_boss_available = !snapshot.hidden_boss_id.empty();
        snapshot.hidden_boss_name = resolve_boss_name(data_center, matching_boss_key, snapshot.hidden_boss_id);
        snapshot.hidden_boss_name_available = !snapshot.hidden_boss_name.empty();
        read_list(center_enemy_types_info_, snapshot.enemy_types,
                  snapshot.enemy_types_available);
        read_list(center_banned_bonds_info_, snapshot.banned_bonds,
                  snapshot.banned_bonds_available);
        if (center_self_banned_units_info_) {
            try {
                snapshot.self_banned_units =
                    read_banned_units(runtime_.invoke(center_self_banned_units_info_, data_center));
                snapshot.self_banned_units_available = true;
            } catch (...) {
            }
        }
        if (center_shop_model_field_) {
            try {
                void* shop = read_object(data_center, center_shop_model_field_, runtime_);
                if (shop) {
                    snapshot.shop_available = true;
                    snapshot.shop_core_available = shop_goods_field_ && shop_goods_class_ &&
                        shop_good_chess_id_field_;
                    try { if (shop_level_field_)
                        snapshot.shop_level = read_value<int>(shop, shop_level_field_, runtime_);
                    } catch (...) {}
                    try { if (shop_coin_field_)
                        snapshot.shop_coin = read_value<int>(shop, shop_coin_field_, runtime_);
                    } catch (...) {}
                    try { if (shop_upgrade_field_)
                        snapshot.shop_upgrade_price = read_value<int>(shop, shop_upgrade_field_, runtime_);
                    } catch (...) {}
                    try { if (shop_refresh_field_)
                        snapshot.shop_refresh_price = read_value<int>(shop, shop_refresh_field_, runtime_);
                    } catch (...) {}
                    try { if (shop_goods_type_field_)
                        snapshot.shop_goods_type = read_value<int>(shop, shop_goods_type_field_, runtime_);
                    } catch (...) {}
                    try { if (shop_free_refresh_field_)
                        snapshot.shop_free_refresh_count = read_value<int>(shop, shop_free_refresh_field_, runtime_);
                    } catch (...) {}
                    try { if (shop_special_refresh_field_)
                        snapshot.shop_special_refresh = read_value<bool>(shop, shop_special_refresh_field_, runtime_);
                    } catch (...) {}
                    try { if (shop_refresh_seq_field_) {
                        snapshot.shop_refresh_seq = read_value<int>(shop, shop_refresh_seq_field_, runtime_);
                        shop_refresh_seq_available = snapshot.shop_refresh_seq >= 0;
                    }} catch (...) {}
                    try {
                        void* goods = shop_goods_field_
                            ? read_object(shop, shop_goods_field_, runtime_) : nullptr;
                        if (goods && snapshot.shop_core_available) {
                            snapshot.shop_goods = read_shop_goods(goods, shop_slot_ids_available);
                            snapshot.shop_goods_available = true;
                        }
                    } catch (...) { snapshot.shop_goods_available = false; }
                }
            } catch (...) {
                snapshot.shop_core_available = false;
                snapshot.shop_goods_available = false;
            }
        }
    }
    if (snapshot.shop_available && snapshot.shop_goods_available && snapshot.shop_core_available) {
        ShopObservation observation;
        observation.session_id = shop_session_id_.load(std::memory_order_acquire);
        observation.round = current_round_ > 0 ? current_round_ : 1;
        observation.player_index = canonical_shop_player_slot(self_index_);
        observation.refresh_seq = snapshot.shop_refresh_seq;
        observation.refresh_seq_available = shop_refresh_seq_available;
        observation.slot_ids_available = shop_slot_ids_available;
        observation.goods_available = true;
        observation.goods = snapshot.shop_goods;
        if (observation.session_id && is_shop_player_slot(observation.player_index)) {
            shop_observations_.fetch_add(1, std::memory_order_relaxed);
            shop_local_observations_.fetch_add(1, std::memory_order_relaxed);
            publish_shop_observation(std::move(observation));
        } else {
            shop_rejected_observations_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    snapshot.players.reserve(players.size());
    for (const PlayerInfo& player : players) {
        IntelPlayer entry;
        entry.index = player.index;
        entry.self = player.self;
        entry.name = player.name;
        entry.band_id = player.band_id;
        entry.band_available = player.band_available;
        entry.squad_slots = player.squad_slots;
        entry.squad_available = player.squad_available;
        entry.shop_level = player.shop_level;
        entry.shop_level_available = player.shop_level_available;
        entry.shop_coin = player.shop_coin;
        entry.shop_coin_available = player.shop_coin_available;
        snapshot.players.push_back(std::move(entry));
    }
    publish_match_intel(std::move(snapshot));
}

void NativeCapture::read_players(void* player_model, int self_index,
                                 std::vector<PlayerInfo>& players, int& self_out,
                                 std::string& self_uid) const {
    players.clear();
    self_out = self_index;
    self_uid.clear();
    if (!player_model) {
        return;
    }
    for (int index = 0; index < static_cast<int>(kMaxPlayers); ++index) {
        PlayerInfo info;
        int hp = 0;
        int connect_state = 0;
        bool game_finished = false;
        try {
            if (!read_player(player_model, index, info, hp, connect_state, game_finished)) {
                continue;
            }
        } catch (const std::exception& error) {
            ++counters_.roster_errors;
            remember_failure("player slot " + std::to_string(index), error);
            continue;
        }
        info.self = info.index == self_index;
        const auto state = player_state(connect_state, connect_online_, connect_missing_, connect_quit_, info.dead);
        info.online = state.online;
        info.eliminated = state.eliminated;
        info.status = state.status;
        if (info.self) {
            self_out = info.index;
            self_uid = info.uid;
        }
        players.push_back(std::move(info));
    }
    std::sort(players.begin(), players.end(), [](const PlayerInfo& a, const PlayerInfo& b) {
        return a.index < b.index;
    });
}

double NativeCapture::game_clock() const {
    return controller_ && controller_clock_ ? from_fixed_point(controller_clock_(controller_clock_info_)) : 0.0;
}

int NativeCapture::read_battle_state(void* controller) const {
    return controller ? read_value<int>(controller, controller_state_field_, runtime_) : 0;
}

int NativeCapture::read_controller_player_count() const {
    if (!controller_ || !controller_player_list_) {
        return 0;
    }
    try {
        return runtime_.count(controller_player_list_(controller_, controller_player_list_info_));
    } catch (const std::exception& error) {
        remember_failure("battle player count", error);
        return 0;
    }
}

void NativeCapture::remember_failure(const std::string& context, const std::exception& error) const {
    last_read_failure_ = context + ": " + error.what();
    if (last_read_failure_.size() > 2048) last_read_failure_.resize(2048);
}

void NativeCapture::set_controller(void* controller) {
    if (controller_ == controller) return;
    controller_root_ = controller ? il2cpp::Runtime::Root(runtime_, controller) : il2cpp::Runtime::Root{};
    controller_ = controller;
    hosts_.clear();
    live_host_cards_.clear();
    host_players_.clear();
    side_players_.clear();
    attribution_cache_.clear();
    host_cache_player_count_ = -1;
}

bool NativeCapture::inside_applied_modifier(void* modifier) {
    std::lock_guard lock(applied_mutex_);
    const auto key = std::make_pair(GetCurrentThreadId(), modifier);
    return std::find(applied_modifiers_.begin(), applied_modifiers_.end(), key) != applied_modifiers_.end();
}

void NativeCapture::snapshot_diagnostics() noexcept {
    try {
    // Only this function reads managed state; callers run on the game thread.
    // Exporters consume the copied string, even after the manager is destroyed.
    if (controller_) {
        try { diagnostic_battle_state_ = read_battle_state(controller_); }
        catch (const std::exception& error) { remember_failure("diagnostic battle state", error); }
    } else diagnostic_battle_state_ = -1;
    std::ostringstream counters;
    counters << "hooks logger_hp=" << counters_.logger_hp << " logger_ep=" << counters_.logger_ep
        << " applied=" << counters_.applied << " nested_logger_suppressed=" << counters_.suppressed << '\n'
        << "events published=" << counters_.published << " unknown_player=" << counters_.unknown_player
        << " published_to_frozen_player=" << counters_.published_frozen
        << " original_source_fallback=" << counters_.original_source
        << " source_side_recovered=" << counters_.side_recovered
        << " environment_recovered=" << counters_.environment_recovered << '\n'
        << "attribution source_errors=" << counters_.source_errors
        << " target_rejected=" << counters_.target_rejected
        << " missing_host=" << counters_.missing_host << " side_conflicts=" << counters_.side_conflicts << '\n'
        << "filters outside_mode=" << counters_.outside_mode << " cancelled=" << counters_.cancelled
        << " wrong_type=" << counters_.wrong_type << " unresolved_source=" << counters_.unresolved_source
        << " environment=" << counters_.environment << " zero=" << counters_.zero
        << " exceptions=" << counters_.errors << " roster_errors=" << counters_.roster_errors << '\n'
        << "shop observations=" << shop_observations_.load(std::memory_order_relaxed)
        << " local=" << shop_local_observations_.load(std::memory_order_relaxed)
        << " hidden=" << shop_hidden_observations_.load(std::memory_order_relaxed)
        << " rejected=" << shop_rejected_observations_.load(std::memory_order_relaxed) << '\n';
    std::ostringstream state;
    state << "mode_present=" << mode_present_ << " capture_active=" << in_mode_
        << " session=" << session_id_ << " round=" << current_round_ << " self_slot=" << self_index_ << '\n'
        << "controller_present=" << (controller_ != nullptr) << " battle_active=" << battle_active_
        << " battle_state=" << diagnostic_battle_state_ << " clock=" << game_clock()
        << " settlement_status=" << diagnostic_settlement_status_
        << " game_state=" << diagnostic_game_state_ << " stats_phase=" << static_cast<unsigned>(current_phase_)
        << " timing_mask=0x" << std::hex << timing_mask_ << " assist_members=0x" << assist_member_mask_ << std::dec << '\n'
        << "coverage=" << static_cast<unsigned>(coverage_) << " active_mask=0x" << std::hex << active_mask_
        << " simulated_mask=0x" << simulated_mask_ << " local_mask=0x" << local_simulated_mask_
        << " observed_mask=0x" << observed_player_mask_ << std::dec << '\n'
        << "runtime_cards=" << diagnostic_runtime_cards_ << " instance_owners=" << diagnostic_inst_players_
        << " host_cache=" << hosts_.size() << " live_cards=" << live_host_cards_.size()
        << " verified_source_sides=" << side_players_.size() << " roster=" << last_players_.size() << '\n';
    for (const auto& [slot, player] : last_players_) {
        state << "player slot=" << slot << " nickname_present=" << !player.name.empty()
            << " connection=" << player.connect_state << " hp=" << player.hp
            << " gameState=" << player.battle_state
            << " gameFinished=" << player.game_finished << " isDead=" << player.dead
            << " online=" << player.online << " eliminated=" << player.eliminated
            << " status=" << static_cast<unsigned>(player.status) << '\n';
    }
    const std::string current = state.str();
    // Retain phase changes instead of evicting them every 250 ms merely
    // because the clock advanced. CURRENT still contains the newest values.
    std::ostringstream phase;
    phase << mode_present_ << ':' << in_mode_ << ':' << session_id_ << ':' << current_round_
        << ':' << (controller_ != nullptr) << ':' << battle_active_ << ':' << diagnostic_battle_state_
        << ':' << diagnostic_settlement_status_ << ':' << active_mask_ << ':' << simulated_mask_
        << ':' << diagnostic_game_state_ << ':' << timing_mask_ << ':' << assist_member_mask_
        << ':' << (counters_.published != 0) << ':' << (counters_.outside_mode != 0);
    for (const auto& [slot, player] : last_players_)
        phase << ':' << slot << ',' << player.connect_state << ',' << player.dead << ',' << player.eliminated;
    const auto phase_key = phase.str();
    if (phase_key != last_diagnostic_state_) {
        diagnostic_history_.push_back("tick=" + std::to_string(GetTickCount64()) + "\n" + current + counters.str());
        while (diagnostic_history_.size() > 16) diagnostic_history_.pop_front();
        last_diagnostic_state_ = phase_key;
    }
    if (in_mode_) {
        last_in_mode_diagnostics_ = "tick=" + std::to_string(GetTickCount64()) + "\n" + current +
            counters.str() + "last_modifier=" + last_modifier_sample_ + "\nlast_read_failure=" + last_read_failure_ + '\n';
        if (battle_active_) last_combat_diagnostics_ = last_in_mode_diagnostics_;
    }
    std::ostringstream report;
    report << "Snapshot tick=" << GetTickCount64() << " (not the export time)\n"
        << "Counters are cumulative since plugin load; history and last in-mode snapshot survive mode exit.\n"
        << "Enums ONLINE=" << connect_online_ << " MISS_CONN=" << connect_missing_ << " QUIT=" << connect_quit_
        << " PLAYING=" << state_running_ << " FINISHED=" << state_finished_ << '\n';
    for (const auto& hook : hook_diagnostics_) report << "hook " << hook << '\n';
    report << "CURRENT\n" << current << counters.str()
        << "last_modifier=" << last_modifier_sample_ << "\nlast_unresolved_modifier=" << last_unresolved_sample_
        << "\nlast_read_failure=" << last_read_failure_
        << "\nLAST IN-MODE SNAPSHOT\n" << last_in_mode_diagnostics_
        << "\nLAST COMBAT SNAPSHOT\n" << last_combat_diagnostics_ << "\nSTATE HISTORY\n";
    for (const auto& item : diagnostic_history_) report << item << '\n';
    std::lock_guard lock(g_diagnostics_mutex);
    g_diagnostics = report.str();
    } catch (...) {
        // Diagnostics must never change the outcome of a game callback.
    }
}

void NativeCapture::leave_mode() {
    const bool was_present = mode_present_;
    if (in_mode_) {
        end_battle();
        snapshot_diagnostics();
        publish_session_end();
    }
    shop_session_id_.store(0, std::memory_order_release);
    stop_shop_observations();
    mode_present_ = false;
    in_mode_ = false;
    logged_coverage_ = Coverage::Unknown;
    session_tracker_.leave();
    local_simulated_mask_ = 0;
    observed_player_mask_ = 0;
    set_controller(nullptr);
    battle_active_ = false;
    battle_ended_ = false;
    last_clock_ = -1;
    last_players_.clear();
    missing_player_ticks_.clear();
    hosts_.clear();
    live_host_cards_.clear();
    host_players_.clear();
    side_players_.clear();
    host_cache_player_count_ = -1;
    attribution_cache_.clear();
    token_players_.clear();
    runtime_card_root_ = il2cpp::Runtime::Root{};
    inst_player_root_ = il2cpp::Runtime::Root{};
    token_card_root_ = il2cpp::Runtime::Root{};
    manager_root_ = il2cpp::Runtime::Root{};
    center_root_ = il2cpp::Runtime::Root{};
    reset_timing();
    diagnostic_runtime_cards_ = diagnostic_inst_players_ = -1;
    diagnostic_settlement_status_ = -1;
    simulated_mask_ = active_mask_ = 0;
    coverage_ = Coverage::Unknown;
    set_error("");
    snapshot_diagnostics();
    // Publish only after the copied snapshot reflects exit: the render thread
    // can immediately consume this event and generate the automatic report.
    if (was_present) {
        Event exit;
        exit.kind = EventKind::ModeExit;
        exit.session_id = session_id_;
        g_events.publish(exit);
    }
}

void NativeCapture::publish_session_start(bool partial) {
    shop_session_id_.store(0, std::memory_order_release);
    session_id_ = (GetTickCount64() << 16) ^ ++session_counter_;
    reset_shop_observations(session_id_);
    shop_round_.store(current_round_, std::memory_order_release);
    reset_hidden_sync_for_session(session_id_);
    coverage_warning_sent_ = false;
    session_end_published_ = false;
    reset_timing();
    Event event;
    event.kind = EventKind::SessionStart;
    event.session_id = session_id_;
    event.round = static_cast<std::uint32_t>(current_round_);
    event.partial = partial ? 1u : 0u;
    g_events.publish(event);
    shop_session_id_.store(session_id_, std::memory_order_release);
}

void NativeCapture::publish_round_start() {
    shop_round_.store(current_round_, std::memory_order_release);
    phase_published_ = false;
    last_damage_phase_ = BattlePhase::Idle;
    last_damage_clock_ = -1;
    Event event;
    event.kind = EventKind::RoundStart;
    event.session_id = session_id_;
    event.round = static_cast<std::uint32_t>(current_round_);
    g_events.publish(event);
}

void NativeCapture::publish_session_end(bool interrupted) {
    if (!in_mode_ || session_end_published_) {
        return;
    }
    session_end_published_ = true;
    Event event;
    event.kind = EventKind::SessionEnd;
    event.session_id = session_id_;
    event.round = static_cast<std::uint32_t>(current_round_);
    event.wall_time = interrupted ? 0 : static_cast<std::int64_t>(std::time(nullptr));
    event.partial = interrupted ? 1u : 0u;
    g_events.publish(event);
}

void NativeCapture::publish_warning(const std::string& message) {
    Event event;
    event.kind = EventKind::Warning;
    event.session_id = session_id_;
    event.round = static_cast<std::uint32_t>(current_round_);
    copy_text(event.message, sizeof(event.message), message);
    g_events.publish(event);
}

void NativeCapture::publish_roster(const std::vector<PlayerInfo>& players) {
    for (const PlayerInfo& player : players) {
        Event event;
        event.kind = EventKind::Roster;
        event.session_id = session_id_;
        event.round = static_cast<std::uint32_t>(current_round_);
        event.player_index = player.index;
        copy_text(event.player_uid, sizeof(event.player_uid), player.uid);
        copy_text(event.player_name, sizeof(event.player_name), player.name);
        event.self = player.self ? 1u : 0u;
        event.online = player.online ? 1u : 0u;
        event.eliminated = player.eliminated ? 1u : 0u;
        event.status = static_cast<std::uint32_t>(player.status);
        g_events.publish(event);
    }
}

void NativeCapture::publish_coverage(std::uint32_t simulated, std::uint32_t active) {
    Event event;
    event.kind = EventKind::Coverage;
    event.session_id = session_id_;
    event.round = static_cast<std::uint32_t>(current_round_);
    event.coverage = static_cast<std::uint32_t>(coverage_);
    event.simulated_mask = simulated;
    event.active_mask = active;
    event.partial = (explicit_partial_ || coverage_ == Coverage::Partial) ? 1u : 0u;
    g_events.publish(event);
    if (coverage_ == Coverage::Partial && !coverage_warning_sent_) {
        coverage_warning_sent_ = true;
        publish_warning("当前仅能采集部分玩家伤害");
    } else if (coverage_ != Coverage::Partial) {
        coverage_warning_sent_ = false;
    }
}

void NativeCapture::publish_clock(double delta) {
    if (delta <= 0 || !in_mode_ || session_end_published_ || current_phase_ == BattlePhase::Idle) {
        return;
    }
    const std::uint32_t mask = timing_mask_ & simulated_mask_ & active_mask_;
    if (!mask) return;
    Event event;
    event.kind = EventKind::Clock;
    event.session_id = session_id_;
    event.round = static_cast<std::uint32_t>(current_round_);
    event.phase = current_phase_;
    event.battle = battle_;
    event.game_time = game_clock();
    event.delta_time = delta;
    event.simulated_mask = mask;
    g_events.publish(event);
}

void NativeCapture::reset_timing() {
    current_phase_ = BattlePhase::Idle;
    timing_mask_ = 0;
    diagnostic_game_state_ = -1;
    timing_warning_sent_ = false;
    phase_published_ = false;
    assist_member_mask_ = 0;
    assist_members_warning_sent_ = false;
    last_damage_phase_ = BattlePhase::Idle;
    last_damage_clock_ = -1;
}

bool NativeCapture::player_in_combat(void* center, int slot) {
    void* model = read_object(center, data_center_player_model_field_, runtime_);
    void* player = model ? runtime_.invoke(player_model_get_player_info_, model, {&slot}) : nullptr;
    if (!player) return false;
    const int connect = read_value<int>(player, scene_connect_field_, runtime_);
    const bool dead = runtime_.unbox<bool>(runtime_.invoke(scene_dead_info_, player));
    return connect == connect_online_ && !dead &&
        read_value<int>(player, scene_game_state_field_, runtime_) == player_battling_;
}

std::uint32_t NativeCapture::list_player_mask(void* sequence) {
    const int count = sequence ? runtime_.count(sequence) : 0;
    if (count < 0 || count > static_cast<int>(kMaxPlayers))
        throw std::runtime_error("Invalid AutoChess phase player list size");
    std::uint32_t mask = 0;
    for (int i = 0; i < count; ++i) {
        const int slot = runtime_.unbox<int>(runtime_.at(sequence, i));
        if (!player_bit(slot)) throw std::runtime_error("Invalid AutoChess phase player slot");
        mask |= player_bit(slot);
    }
    return mask;
}

BattlePhase NativeCapture::read_damage_phase() noexcept {
    try {
        if (!in_mode_ || session_end_published_) return BattlePhase::Idle;
        void* manager = manager_root_.get();
        void* status = manager ? read_object(manager, manager_status_field_, runtime_) : nullptr;
        if (!status || !game_state_) return BattlePhase::Idle;
        const int state = game_state_(status, game_state_info_);
        BattlePhase phase = BattlePhase::Idle;
        if (state == phase_help_) phase = BattlePhase::Assist;
        else if (state == phase_battle_ || state == phase_boss_ || state == phase_waiting_) phase = BattlePhase::Main;
        const double now = game_clock();
        if (phase != BattlePhase::Idle) {
            last_damage_phase_ = phase;
            last_damage_clock_ = now;
            return phase;
        }
        // An area hit can apply several modifiers in the same simulation tick.
        // Keep that tick's phase until the normal updater publishes SessionEnd.
        if ((state == phase_end_ || state == phase_settle_) && now == last_damage_clock_)
            return last_damage_phase_;
    } catch (...) {}
    return BattlePhase::Idle;
}

void NativeCapture::sample_timing(bool advance_clock, bool allow_session_end) {
    const double now = game_clock();
    // Settle the preceding frame with its preceding mask. In particular, the
    // interval before an idle -> assist switch must never become assist time.
    if (advance_clock && battle_active_ && last_clock_ >= 0 && now >= last_clock_)
        publish_clock(now - last_clock_);
    last_clock_ = now;
    const auto previous_phase = current_phase_;
    const auto previous_mask = timing_mask_;
    current_phase_ = BattlePhase::Idle;
    timing_mask_ = 0;
    assist_member_mask_ = 0;
    diagnostic_game_state_ = -1;
    if (!in_mode_ || session_end_published_) return;
    try {
        void* manager = manager_root_.get();
        void* center = center_root_.get();
        void* status = manager ? read_object(manager, manager_status_field_, runtime_) : nullptr;
        if (!status || !center || !game_state_)
            throw std::runtime_error("AutoChess game phase is unavailable");
        diagnostic_game_state_ = game_state_(status, game_state_info_);
        if (allow_session_end && (diagnostic_game_state_ == phase_end_ ||
                                   diagnostic_game_state_ == phase_settle_)) {
            last_damage_phase_ = BattlePhase::Idle;
            last_damage_clock_ = -1;
        }
        if (diagnostic_game_state_ == phase_end_) {
            if (allow_session_end) publish_session_end();
        } else {
            void* battle_status = read_object(center, center_battle_status_field_, runtime_);
            const std::uint32_t candidates = simulated_mask_ & active_mask_;
            if (battle_status && (diagnostic_game_state_ == phase_battle_ ||
                                  diagnostic_game_state_ == phase_waiting_)) {
                for (int slot = 0; slot < static_cast<int>(kMaxPlayers); ++slot) {
                    const auto bit = player_bit(slot);
                    if (!(candidates & bit)) continue;
                    // WAITING describes the local player's end, while other
                    // locally simulated players can still be fighting.
                    if (slot == self_index_ && diagnostic_game_state_ == phase_waiting_) continue;
                    if (!player_in_combat(center, slot)) continue;
                    // GetSelfBattleStatus logs from the game on every missing
                    // key. Read the same dictionary without producing log spam.
                    void* dictionary = read_object(battle_status, self_status_dict_field_, runtime_);
                    void* row = nullptr;
                    if (dictionary) runtime_.invoke(self_status_lookup_info_, dictionary, {&slot, &row});
                    if (!row || !self_finished_(row, self_finished_info_)) timing_mask_ |= bit;
                    if (!row && !timing_warning_sent_) {
                        timing_warning_sent_ = true;
                        publish_warning("部分主战状态缺失，已按玩家战斗完成状态计时，请导出诊断");
                    }
                }
                if (timing_mask_) current_phase_ = BattlePhase::Main;
            } else if (battle_status && diagnostic_game_state_ == phase_help_) {
                void* data = runtime_.invoke(center_help_data_info_, center);
                void* players = data ? read_object(data, help_players_field_, runtime_) : nullptr;
                const auto help_mask = list_player_mask(players);
                const auto primary_mask = list_player_mask(runtime_.invoke(center_battle_players_info_, center));
                const auto too_many = [](std::uint32_t bits) {
                    bits &= bits - (bits != 0);
                    return bits && (bits & (bits - 1));
                };
                const bool valid_primary = primary_mask && !too_many(primary_mask);
                const bool valid_help = help_mask && !too_many(help_mask);
                assist_member_mask_ = valid_primary ? primary_mask : valid_help ? help_mask : 0;
                current_phase_ = BattlePhase::Assist;
                if ((!assist_member_mask_ || too_many(assist_member_mask_) ||
                     too_many(primary_mask) || too_many(help_mask) ||
                     (primary_mask && help_mask && primary_mask != help_mask)) &&
                    !assist_members_warning_sent_) {
                    assist_members_warning_sent_ = true;
                    publish_warning("协防名单缺失或冲突，已保留伤害；不确定玩家的 DPS 计时不完整");
                }
                if (assist_member_mask_ && !too_many(assist_member_mask_)) {
                    for (int slot = 0; slot < static_cast<int>(kMaxPlayers); ++slot) {
                        const auto bit = player_bit(slot);
                        if ((assist_member_mask_ & candidates & bit) && player_in_combat(center, slot))
                            timing_mask_ |= bit;
                    }
                }
            } else if (battle_status && diagnostic_game_state_ == phase_boss_) {
                for (int slot = 0; slot < static_cast<int>(kMaxPlayers); ++slot) {
                    const auto bit = player_bit(slot);
                    if ((candidates & bit) && player_in_combat(center, slot)) timing_mask_ |= bit;
                }
                if (timing_mask_) current_phase_ = BattlePhase::Main;
            }
        }
        if (!controller_ || read_battle_state(controller_) != state_running_) {
            current_phase_ = BattlePhase::Idle;
            timing_mask_ = 0;
        }
    } catch (const std::exception& error) {
        remember_failure("AutoChess combat timing", error);
        if (!timing_warning_sent_) {
            timing_warning_sent_ = true;
            publish_warning("战斗阶段读取失败，DPS 计时已暂停，请导出诊断");
        }
    }
    if (!phase_published_ || previous_phase != current_phase_ || previous_mask != timing_mask_) {
        phase_published_ = true;
        Event event;
        event.kind = EventKind::Phase;
        event.session_id = session_id_;
        event.round = static_cast<std::uint32_t>(current_round_);
        event.phase = current_phase_;
        event.simulated_mask = timing_mask_;
        g_events.publish(event);
    }
}

void NativeCapture::tick_runtime(bool force) {
    const std::uint64_t now_ms = GetTickCount64();
    const bool manual_reset = g_reset_requested.load();
    if (!force && !manual_reset && now_ms - last_tick_ms_ < 250) {
        return;
    }
    last_tick_ms_ = now_ms;
    force = force || manual_reset;
    struct SnapshotOnReturn {
        NativeCapture& capture;
        ~SnapshotOnReturn() { capture.snapshot_diagnostics(); }
    } snapshot{*this};

    void* data_center = nullptr;
    try {
        void* manager = runtime_.invoke(manager_instance_info_, nullptr);
        if (manager) {
            if (!runtime_.class_assignable(manager_class_, runtime_.checked_class(manager)))
                throw std::runtime_error("Nullable manager returned an unexpected type");
            manager_root_ = il2cpp::Runtime::Root(runtime_, manager);
            data_center = read_object(manager, manager_center_field_, runtime_);
            if (data_center && !runtime_.class_assignable(auto_chess_center_class_, runtime_.checked_class(data_center)))
                throw std::runtime_error("Manager dataCenter has an unexpected type");
        }
    } catch (const std::exception& error) {
        remember_failure("AutoChess manager lookup", error);
        set_error(last_read_failure_);
        active_mask_ = 0;
        return;
    }
    if (!data_center) {
        leave_mode();
        return;
    }
    mode_present_ = true;
    center_root_ = il2cpp::Runtime::Root(runtime_, data_center);
    il2cpp::Runtime::Root center_root(runtime_, data_center);
    int stage_seed = 0;
    int round_value = 0;
    int self_index = kUnknownPlayer;
    try {
        stage_seed = runtime_.unbox<int>(runtime_.invoke(center_stage_seed_info_, data_center));
        round_value = runtime_.unbox<int>(runtime_.invoke(center_round_info_, data_center));
        self_index = runtime_.unbox<int>(runtime_.invoke(center_self_info_, data_center));
    } catch (const std::exception& error) {
        remember_failure("AutoChess session properties", error);
        set_error(last_read_failure_);
        active_mask_ = 0;
        return;
    }
    const int round = round_value > 0 ? round_value : 1;
    diagnostic_settlement_status_ = -1;
    try {
        void* settle = data_center_settle_field_
                           ? read_object(data_center, data_center_settle_field_, runtime_)
                           : nullptr;
        if (settle && settle_ending_info_)
            diagnostic_settlement_status_ = runtime_.unbox<int>(runtime_.invoke(settle_ending_info_, settle));
        // Settlement describes an outcome, not the lifetime of a simulation.
        // It may be nonzero while the manager/controller are still playing.
        // Never stop roster/index refresh or discard damage based on this value.
        // The manager/dataCenter lifetime and session identity end the session;
        // BattleController state independently controls combat timing.
    } catch (const std::exception& error) {
        remember_failure("AutoChess settlement", error);
    }

    void* player_model = read_object(data_center, data_center_player_model_field_, runtime_);
    std::vector<PlayerInfo> players;
    int resolved_self = self_index;
    std::string self_uid;
    read_players(player_model, self_index, players, resolved_self, self_uid);
    bool roster_has_zero = false;
    bool roster_has_four = false;
    for (const PlayerInfo& player : players) {
        roster_has_zero = roster_has_zero || player.index == 0;
        roster_has_four = roster_has_four || player.index == 4;
    }
    shop_slot_mode_.store(roster_has_zero && !roster_has_four ? 1 : 0,
                          std::memory_order_release);
    if (players.empty()) {
        set_error("AutoChess player roster is unavailable; capture paused");
        active_mask_ = 0;
        return;
    }
    std::vector<std::pair<int, std::string>> identity_roster;
    for (const auto& player : players) identity_roster.emplace_back(player.index, player.uid);
    const bool identity_changed = session_tracker_.update(stage_seed,
        reinterpret_cast<std::uintptr_t>(data_center), identity_roster, manual_reset);
    const bool new_session = identity_changed || !in_mode_;
    if (round != current_round_) observed_player_mask_ = 0;
    if (new_session) {
        if (manual_reset && in_mode_ && battle_active_) end_battle();
        // Do this before merging missing players, or a new game inherits old slots.
        last_players_.clear();
        missing_player_ticks_.clear();
        observed_player_mask_ = 0;
        local_simulated_mask_ = 0;
        explicit_partial_ = false;
        attribution_warning_sent_ = false;
        battle_active_ = false;
        battle_ended_ = false;
        last_clock_ = -1;
    }
    std::set<int> current_indices;
    for (const PlayerInfo& player : players) {
        current_indices.insert(player.index);
    }
    if (in_mode_) {
        for (const auto& [index, previous] : last_players_) {
            if (current_indices.count(index)) {
                missing_player_ticks_.erase(index);
                continue;
            }
            int& missing = missing_player_ticks_[index];
            ++missing;
            PlayerInfo disappeared = previous;
            if (missing >= 3) {
                disappeared.online = false;
                disappeared.status = previous.eliminated ? PlayerStatus::Eliminated
                                                         : PlayerStatus::Quit;
            }
            players.push_back(disappeared);
        }
    }
    last_players_.clear();
    for (const PlayerInfo& player : players) {
        last_players_[player.index] = player;
    }

    std::uint32_t active_mask = 0;
    for (const PlayerInfo& player : players) {
        if (player.online && !player.eliminated) {
            active_mask |= stronghold::player_bit(player.index);
        }
    }

    std::uint32_t battle_mask = 0;
    int battle_count = 0;
    try {
        void* battle_players = runtime_.invoke(center_battle_players_info_, data_center);
        const int count = runtime_.count(battle_players);
        for (int i = 0; i < count && i < static_cast<int>(kMaxPlayers); ++i) {
            const int index = runtime_.unbox<int>(runtime_.at(battle_players, i));
            if (index >= 0 && index < static_cast<int>(kMaxPlayers)) {
                battle_mask |= stronghold::player_bit(index);
            }
        }
    } catch (const std::exception& error) {
        remember_failure("AutoChess battle player indices", error);
    }
    for (int i = 0; i < static_cast<int>(kMaxPlayers); ++i) {
        if (battle_mask & stronghold::player_bit(i)) {
            ++battle_count;
        }
    }
    const int local_player_count = read_controller_player_count();
    std::set<int> roster_indices;
    for (const PlayerInfo& player : players) {
        roster_indices.insert(player.index);
    }
    const int roster_players = static_cast<int>(roster_indices.size());
    refresh_index_maps(data_center);
    if (new_session || round != current_round_) {
        hosts_.clear();
        live_host_cards_.clear();
        host_players_.clear();
        side_players_.clear();
        attribution_cache_.clear();
        source_warning_sent_ = false;
        unattributed_effect_warning_sent_ = false;
    }
    // Tick is throttled to 250 ms. Tokens can appear/disappear with an unchanged
    // player count, so refreshing only at round boundaries misses their hosts.
    rebuild_host_cache();
    host_cache_player_count_ = local_player_count;
    // Character data can arrive before card-owner indices, with no change in
    // player-list length when those indices become ready. Revalidate cached
    // local characters against the current maps instead of keeping a stale
    // zero mask until the next round or the first damage event.
    local_simulated_mask_ = 0;
    if (controller_) {
        for (const auto card_uid : live_host_cards_)
            local_simulated_mask_ |= player_bit(player_for_card(card_uid));
    }
    const std::uint32_t confirmed_mask = local_simulated_mask_ | observed_player_mask_;
    coverage_ = coverage_for_masks(active_mask, confirmed_mask, resolved_self);
    if (coverage_ != logged_coverage_ || roster_players != logged_roster_players_ ||
        local_player_count != logged_local_players_ || battle_count != logged_battle_players_) {
        char message[256]{};
        std::snprintf(message, sizeof(message),
                      "AutoChess coverage=%u roster=%d local=%d battle=%d active=0x%X confirmed=0x%X",
                      static_cast<unsigned>(coverage_), roster_players, local_player_count,
                      battle_count, active_mask, confirmed_mask);
        if (host_.log) {
            host_.log(host_.owner, 1, message);
        }
        logged_coverage_ = coverage_;
        logged_roster_players_ = roster_players;
        logged_local_players_ = local_player_count;
        logged_battle_players_ = battle_count;
    }

    const std::uint32_t effective_mask = confirmed_mask;
    if (new_session) {
        if (in_mode_) {
            publish_session_end(manual_reset);
        }
        attribution_cache_.clear();
        self_index_ = resolved_self;
        if (!self_uid.empty()) {
            self_uid_ = self_uid;
        }
        current_round_ = round;
        shop_session_id_.store(0, std::memory_order_release);
        {
            std::lock_guard lock(shop_catalog_mutex_);
            shop_catalog_.clear();
            shop_catalog_ids_.clear();
        }
        shop_catalog_next_sample_ = 0;
        refresh_shop_catalog(data_center);
        publish_session_start(false);
        if (manual_reset) g_reset_requested.store(false);
        publish_round_start();
    } else if (round != current_round_) {
        // Finish the old round's clock before publishing its export snapshot.
        end_battle();
        current_round_ = round;
        battle_ended_ = false;
        observed_player_mask_ = 0;
        attribution_cache_.clear();
        publish_round_start();
    }

    self_index_ = resolved_self;
    if (!self_uid.empty()) {
        self_uid_ = self_uid;
    }
    current_round_ = round;
    simulated_mask_ = effective_mask;
    active_mask_ = active_mask;
    in_mode_ = true;
    refresh_shop_catalog(data_center);
    refresh_match_intel(data_center, players, stage_seed);
    publish_roster(players);
    publish_coverage(effective_mask, active_mask);
    sample_timing();
    set_error("");
}

void NativeCapture::begin_battle(void* controller, bool partial) {
    if (!controller) {
        return;
    }
    set_controller(controller);
    hosts_.clear();
    live_host_cards_.clear();
    host_players_.clear();
    side_players_.clear();
    host_cache_player_count_ = -1;
    attribution_cache_.clear();
    tick_runtime(true);
    if (!in_mode_) return;
    battle_active_ = true;
    battle_ended_ = false;
    ++battle_;
    last_clock_ = game_clock();
    sample_timing(false);
    if (partial || last_clock_ > 0.25) {
        explicit_partial_ = true;
        publish_warning("战斗中接入，只能从当前时刻开始累计");
    }
}

void NativeCapture::end_battle() {
    if (!battle_active_) {
        return;
    }
    sample_timing();
    const double now = game_clock();
    snapshot_diagnostics();
    battle_active_ = false;
    battle_ended_ = true;
    Event event;
    event.kind = EventKind::BattleEnd;
    event.session_id = session_id_;
    event.round = static_cast<std::uint32_t>(current_round_);
    event.battle = battle_;
    event.game_time = now;
    g_events.publish(event);
    current_phase_ = BattlePhase::Idle;
    timing_mask_ = 0;
    Event phase = event;
    phase.kind = EventKind::Phase;
    phase.phase = BattlePhase::Idle;
    phase.simulated_mask = 0;
    g_events.publish(phase);
}

void NativeCapture::update_battle(void* controller) {
    if (!controller || !in_mode_ || session_end_published_) {
        return;
    }
    if (controller_ != controller) {
        set_controller(controller);
        hosts_.clear();
    live_host_cards_.clear();
    host_players_.clear();
    side_players_.clear();
        host_cache_player_count_ = -1;
        attribution_cache_.clear();
        battle_active_ = false;
        battle_ended_ = false;
    }
    const int state = read_battle_state(controller);
    if (state == state_running_ && !battle_active_ && !battle_ended_) {
        begin_battle(controller, true);
    }
    if (state == state_finished_) {
        end_battle();
        return;
    }
    if (state != state_running_) {
        last_clock_ = game_clock();
        sample_timing(false);
        return;
    }
    if (!battle_active_) {
        return;
    }
    sample_timing();
}

void NativeCapture::record_damage(void* logger, void* modifier, bool elemental, bool applied,
                                  std::optional<BattlePhase> phase_hint, void* applied_target) {
    if (!in_mode_ || session_end_published_ || !modifier) {
        ++counters_.outside_mode;
        return;
    }
    try {
        if (!controller_ && logger && logger_controller_field_) {
            set_controller(read_object(logger, logger_controller_field_, runtime_));
        }
        // A killing hit can change the game phase inside the original callback.
        // Publish that hit before an END event observed by the next runtime tick.
        sample_timing(true, false);
        if (session_end_published_) return;
        // A final applied hit may already have set battleFinished. Attribute
        // its damage by the explicit game phase, independently of its clock mask.
        const BattlePhase damage_phase = phase_hint ? *phase_hint : read_damage_phase();
        if (damage_phase == BattlePhase::Idle) return;
        last_modifier_sample_ = "channel=" + std::string(applied ? "applied" : elemental ? "logger_ep" : "logger_hp");
        if (modifier_is_cancelled_(modifier, modifier_is_cancelled_info_)) {
            last_modifier_sample_ += " filtered=cancelled";
            ++counters_.cancelled;
            return;
        }
        const bool damage = modifier_is_damage_(modifier, modifier_is_damage_info_);
        const bool heal = modifier_is_heal_(modifier, modifier_is_heal_info_);
        const bool element_damage =
            modifier_is_element_(modifier, modifier_is_element_info_);
        if (applied) elemental = element_damage;
        if ((elemental && !element_damage) || (!elemental && !damage && !heal)) {
            last_modifier_sample_ += " filtered=wrong_type damage=" + std::to_string(damage) +
                " heal=" + std::to_string(heal) + " elemental=" + std::to_string(element_damage);
            ++counters_.wrong_type;
            return;
        }
        int shared_mask = 0;
        std::memcpy(&shared_mask, static_cast<char*>(modifier) + shared_mask_offset_, sizeof(shared_mask));
        const bool environment = (shared_mask & static_cast<int>(kModifierEnvironment)) != 0;
        std::int64_t real = 0;
        std::memcpy(&real, static_cast<char*>(modifier) + real_delta_offset_, sizeof(real));
        const double real_value = std::abs(from_fixed_point(real));
        const double calculated = std::abs(
            from_fixed_point(modifier_final_delta_(modifier, modifier_final_delta_info_)));
        last_modifier_sample_ += " actual=" + std::to_string(real_value) +
            " calculated=" + std::to_string(calculated) + " flags=" + std::to_string(shared_mask);
        void* target = applied_target;
        if (!target && modifier_target_) {
            try { target = modifier_target_(modifier, modifier_target_info_); }
            catch (const std::exception& error) { remember_failure("modifier target", error); }
        }
        const bool enemy_target = enemy_damage_target(target);
        void* entity = nullptr;
        const auto read_source = [&](ReferenceGetter getter, void* info) -> void* {
            try { return getter ? getter(modifier, info) : nullptr; }
            catch (const std::exception& error) {
                ++counters_.source_errors;
                remember_failure("modifier source", error);
                return nullptr;
            }
        };
        const auto resolve = [&](void* source, HostInfo& out, int& owner, std::uint32_t& card) {
            try { return resolve_character(source, out, owner, card); }
            catch (const std::exception& error) {
                ++counters_.source_errors;
                remember_failure("source attribution", error);
                out = {}; owner = kUnknownPlayer; card = 0;
                return false;
            }
        };
        entity = read_source(modifier_source_, modifier_source_info_);
        // originalSource is secondary provenance, not permission to convert
        // an explicitly hostile attacker into a friendly damage event. Check
        // the untouched direct chain before any fallback can replace it.
        {
            void* source = entity;
            void* visited[16]{};
            for (int depth = 0; source && depth < 16; ++depth) {
                bool cycle = false;
                for (int n = 0; n < depth; ++n) cycle |= visited[n] == source;
                if (cycle) break;
                visited[depth] = source;
                if (enemy_damage_target(source)) {
                    ++counters_.unresolved_source;
                    ++counters_.target_rejected;
                    if (environment) ++counters_.environment;
                    last_modifier_sample_ += " filtered=hostile_direct_source";
                    last_unresolved_sample_ = last_modifier_sample_;
                    return;
                }
                try {
                    if (!runtime_.class_assignable(projectile_class_, runtime_.object_class(source))) break;
                    source = projectile_source_(source, projectile_source_info_);
                } catch (const std::exception& error) {
                    ++counters_.source_errors;
                    remember_failure("direct source classification", error);
                    break;
                }
            }
        }
        void* original = nullptr;
        HostInfo host;
        int player = kUnknownPlayer;
        std::uint32_t card_uid = 0;
        bool resolved = resolve(entity, host, player, card_uid);
        // Effects can retain a non-null projectile/proxy after its source has
        // disappeared, or a token whose host link is incomplete. A null-only
        // fallback silently lost these modifiers even with a valid origin.
        if (modifier_original_source_ &&
            (!resolved || player < 0 || host.token || host.id == "unknown")) {
            original = read_source(modifier_original_source_, modifier_original_source_info_);
            if (original && original != entity) {
                HostInfo origin_host;
                int origin_player = kUnknownPlayer;
                std::uint32_t origin_card = 0;
                if (resolve(original, origin_host, origin_player, origin_card)) {
                    const bool better_player = player < 0 && origin_player >= 0;
                    const bool better_host = (host.token || host.id == "unknown") &&
                        !origin_host.token && !origin_host.id.empty() && origin_host.id != "unknown" &&
                        (player < 0 || origin_player == player);
                    if (!resolved || better_player || better_host) {
                        host = std::move(origin_host);
                        player = origin_player;
                        card_uid = origin_card;
                        entity = original;
                        resolved = true;
                        ++counters_.original_source;
                    }
                }
            }
        }
        // Only a real source object's PlayerSide is ownership evidence.
        // Modifier.playerSide is caller-supplied and is diagnostic-only: it can
        // describe a target/context, so never cast or map it to a player slot.
        bool hostile_source = false;
        if (!resolved || player < 0) {
            for (void* source : {entity, original}) {
                void* visited[16]{};
                for (int depth = 0; source && depth < 16; ++depth) {
                    bool cycle = false;
                    for (int n = 0; n < depth; ++n) cycle |= visited[n] == source;
                    if (cycle) break;
                    visited[depth] = source;
                    try {
                        void* klass = runtime_.object_class(source);
                        if (runtime_.class_assignable(projectile_class_, klass)) {
                            source = projectile_source_(source, projectile_source_info_);
                            continue;
                        }
                        if (enemy_damage_target(source)) { hostile_source = true; break; }
                        if (entity_player_side_ && entity_class_ &&
                            runtime_.class_assignable(entity_class_, klass)) {
                            const int side = entity_player_side_(source, entity_player_side_info_);
                            const auto owner = side_players_.find(side);
                            if (owner != side_players_.end() &&
                                (resolved || (enemy_target && damage && !heal && !elemental))) {
                                player = owner->second;
                                if (!resolved) {
                                    host.id = "unknown_effect";
                                    host.name = "未归属附加伤害";
                                    card_uid = 0;
                                }
                                resolved = true;
                                ++counters_.side_recovered;
                            }
                        }
                    } catch (const std::exception& error) {
                        ++counters_.source_errors;
                        remember_failure("effect source side", error);
                    }
                    break;
                }
                if (resolved && player >= 0) break;
            }
        }
        const auto class_label = [&](void* object) {
            if (!object) return std::string("null");
            try { return runtime_.klass_name(runtime_.checked_class(object)); }
            catch (...) { return std::string("unavailable"); }
        };
        if (!resolved || host.token || player < 0 || environment || host.id == "unknown_effect") {
            last_modifier_sample_ += " source_class=" + class_label(entity) +
                " original_class=" + class_label(original) + " target_class=" + class_label(target);
            if (modifier_player_side_) {
                try { last_modifier_sample_ += " modifier_side=" + std::to_string(
                    modifier_player_side_(modifier, modifier_player_side_info_)); }
                catch (const std::exception& error) { remember_failure("modifier side diagnostic", error); }
            }
        }
        if (!resolved) {
            ++counters_.unresolved_source;
            // An explicit Enemy attacking is not player output (including self
            // damage). Truly source-less enemy HP loss is preserved separately
            // without inventing a player/operator or attributing it by recency.
            if (hostile_source || !enemy_target || !damage || heal || elemental || real_value == 0) {
                if (environment) ++counters_.environment;
                ++counters_.target_rejected;
                last_modifier_sample_ += " filtered=unresolved_source target_enemy=" +
                    std::to_string(enemy_target) + " hostile_source=" + std::to_string(hostile_source);
                last_unresolved_sample_ = last_modifier_sample_;
                if (!hostile_source && !source_warning_sent_) {
                    source_warning_sent_ = true;
                    publish_warning("存在无法确认来源或敌方目标的效果，未计入排名；请导出诊断");
                }
                return;
            }
            host.id = "unknown_effect";
            host.name = "无来源附加伤害";
            player = kUnknownPlayer;
            card_uid = 0;
            if (!unattributed_effect_warning_sent_) {
                unattributed_effect_warning_sent_ = true;
                publish_warning("无来源敌方承伤已单列未归属（可能含环境伤害），不推定玩家或干员");
            }
        }
        if (environment && (!enemy_target || !damage || heal || elemental)) {
            ++counters_.environment;
            last_modifier_sample_ += " filtered=environment_target";
            last_unresolved_sample_ = last_modifier_sample_;
            return;
        }
        if (environment) ++counters_.environment_recovered;
        if (host.token) ++counters_.missing_host;
        if (host.id == "unknown_effect" || host.token || player < 0)
            last_unresolved_sample_ = last_modifier_sample_ + " retained=unattributed";
        std::ostringstream sample;
        sample << "channel=" << (applied ? "applied" : elemental ? "logger_ep" : "logger_hp")
            << " slot=" << player << " damage=" << damage << " heal=" << heal
            << " elemental=" << element_damage << " flags=" << shared_mask
            << " actual=" << real_value << " calculated=" << calculated;
        last_modifier_sample_ = sample.str();
        if (real_value == 0 && calculated == 0) {
            ++counters_.zero;
            return;
        }
        if (player < 0) ++counters_.unknown_player;
        if (damage_phase == BattlePhase::Assist &&
            !(assist_member_mask_ & player_bit(player)) && !assist_members_warning_sent_) {
            assist_members_warning_sent_ = true;
            publish_warning("协防伤害来源不在当前名单，已保留伤害；该玩家 DPS 计时不完整");
        }
        const auto roster = last_players_.find(player);
        if (roster != last_players_.end() && player_frozen(roster->second.online,
            roster->second.eliminated, roster->second.status)) ++counters_.published_frozen;

        if ((player < 0 || host.token || host.id == "unknown" || host.id == "unknown_effect") &&
            !(player < 0 && host.id == "unknown_effect") && !attribution_warning_sent_) {
            attribution_warning_sent_ = true;
            publish_warning("部分伤害无法确认玩家或宿主，已保留在未归属明细");
        }
        Event event;
        event.kind = elemental ? EventKind::ElementalBuildup : EventKind::Damage;
        event.phase = damage_phase;
        event.session_id = session_id_;
        event.round = static_cast<std::uint32_t>(current_round_);
        event.battle = battle_;
        event.game_time = game_clock();
        event.actual_damage = real_value;
        event.calculated_damage = calculated;
        event.damage_type = modifier_damage_type_(modifier, modifier_damage_type_info_);
        event.player_index = player;
        observed_player_mask_ |= player_bit(player);
        event.card_uid = card_uid;
        event.continuous = (shared_mask & static_cast<int>(kModifierContinuous)) ? 1u : 0u;
        copy_text(event.actor_id, sizeof(event.actor_id), host.id);
        copy_text(event.actor_name, sizeof(event.actor_name), host.name);
        g_events.publish(event);
        ++counters_.published;
    } catch (const std::exception& error) {
        ++counters_.errors;
        remember_failure("damage", error);
        const ULONGLONG now = GetTickCount64();
        if (now - last_damage_error_tick_ >= 5000) {
            last_damage_error_tick_ = now;
            const std::string message = std::string("Damage capture failed: ") + error.what();
            set_error(message);
            if (host_.log) {
                host_.log(host_.owner, 2, message.c_str());
            }
        }
    } catch (...) {
        ++counters_.errors;
        const ULONGLONG now = GetTickCount64();
        if (now - last_damage_error_tick_ >= 5000) {
            last_damage_error_tick_ = now;
            set_error("Damage capture failed with an unknown error");
            if (host_.log) {
                host_.log(host_.owner, 2, "Damage capture failed with an unknown error");
            }
        }
    }
}

void NativeCapture::on_all_state_read(void* self, void* reader, void* method) {
    CallbackScope scope(g_callback_capture);
    if (original_all_state_read) {
        original_all_state_read(self, reader, method);
    }
    if (scope.recording()) {
        try {
            il2cpp::Runtime::Attachment attachment(scope.get()->runtime_);
            scope.get()->capture_hidden_sync(self);
        } catch (...) {
        }
    }
}

void NativeCapture::on_log_modifier(void* self, void* modifier, void* method) {
    CallbackScope scope(g_callback_capture);
    const auto phase = scope.recording() ? scope.get()->read_damage_phase() : BattlePhase::Idle;
    if (original_log_modifier) {
        original_log_modifier(self, modifier, method);
    }
    if (scope.recording()) {
        ++scope.get()->counters_.logger_hp;
        if (scope.get()->inside_applied_modifier(modifier)) ++scope.get()->counters_.suppressed;
        else scope.get()->record_damage(self, modifier, false, false, phase);
    }
}

void NativeCapture::on_log_ep_modifier(void* self, void* modifier, void* method) {
    CallbackScope scope(g_callback_capture);
    const auto phase = scope.recording() ? scope.get()->read_damage_phase() : BattlePhase::Idle;
    if (original_log_ep_modifier) {
        original_log_ep_modifier(self, modifier, method);
    }
    if (scope.recording()) {
        ++scope.get()->counters_.logger_ep;
        if (scope.get()->inside_applied_modifier(modifier)) ++scope.get()->counters_.suppressed;
        else scope.get()->record_damage(self, modifier, true, false, phase);
    }
}

void NativeCapture::on_applied_modifier(void* self, void* modifier, void* method) {
    CallbackScope scope(g_callback_capture);
    const auto phase = scope.recording() ? scope.get()->read_damage_phase() : BattlePhase::Idle;
    struct AppliedScope {
        NativeCapture* capture = nullptr;
        std::pair<DWORD, void*> key;
        explicit AppliedScope(NativeCapture* owner, void* modifier) : key(GetCurrentThreadId(), modifier) {
            if (!owner) return;
            try {
                std::lock_guard lock(owner->applied_mutex_);
                owner->applied_modifiers_.push_back(key);
                capture = owner;
            } catch (...) {}
        }
        ~AppliedScope() {
            if (!capture) return;
            std::lock_guard lock(capture->applied_mutex_);
            const auto found = std::find(capture->applied_modifiers_.rbegin(), capture->applied_modifiers_.rend(), key);
            if (found != capture->applied_modifiers_.rend()) capture->applied_modifiers_.erase(std::next(found).base());
        }
    } applied(scope.recording() ? scope.get() : nullptr, modifier);
    if (original_applied_modifier) original_applied_modifier(self, modifier, method);
    if (applied.capture) {
        ++applied.capture->counters_.applied;
        applied.capture->record_damage(nullptr, modifier, false, true, phase, self);
    }
}

void NativeCapture::on_start_game(void* self, void* method) {
    CallbackScope scope(g_callback_capture);
    if (original_start_game) {
        original_start_game(self, method);
    }
    if (scope.recording()) {
        try {
            scope.get()->begin_battle(self, false);
        } catch (const std::exception& error) {
            set_error(std::string("Battle start capture failed: ") + error.what());
        }
    }
}

void NativeCapture::on_clear_runtime_data(void* self, void* method) {
    CallbackScope scope(g_callback_capture);
    if (scope.recording() && scope.get()->controller_ == self) {
        try {
            scope.get()->end_battle();
            scope.get()->snapshot_diagnostics();
        } catch (const std::exception& error) {
            set_error(std::string("Battle end capture failed: ") + error.what());
        }
        scope.get()->set_controller(nullptr);
    }
    if (original_clear_runtime_data) {
        original_clear_runtime_data(self, method);
    }
}

void NativeCapture::on_destroy(void* self, void* method) {
    CallbackScope scope(g_callback_capture);
    if (scope.recording() && scope.get()->controller_ == self) {
        try {
            scope.get()->end_battle();
            scope.get()->snapshot_diagnostics();
        } catch (const std::exception& error) {
            set_error(std::string("Battle destroy capture failed: ") + error.what());
        }
        scope.get()->set_controller(nullptr);
    }
    if (original_on_destroy) {
        original_on_destroy(self, method);
    }
}

void NativeCapture::on_update(void* self, void* method) {
    CallbackScope scope(g_callback_capture);
    if (original_update) {
        original_update(self, method);
    }
    if (scope.recording()) {
        try {
            scope.get()->update_battle(self);
        } catch (const std::exception& error) {
            set_error(std::string("Battle update capture failed: ") + error.what());
        }
    }
}

void NativeCapture::on_global_update(void* self, void* method) {
    CallbackScope scope(g_callback_capture);
    if (original_global_update) {
        original_global_update(self, method);
    }
    if (!scope.recording()) {
        return;
    }
    try {
        scope.get()->tick_runtime(false);
    } catch (const std::exception& error) {
        scope.get()->remember_failure("AutoChess runtime update", error);
        set_error(std::string("AutoChess runtime update failed: ") + error.what());
        scope.get()->snapshot_diagnostics();
    }
}

EventQueue& event_queue() {
    return g_events;
}

std::string capture_diagnostics() {
    std::lock_guard lock(g_diagnostics_mutex);
    return g_diagnostics;
}

MatchIntelSnapshot capture_match_intel() {
    std::lock_guard lock(g_match_intel_mutex);
    return g_match_intel;
}

HiddenSyncSnapshot capture_hidden_sync() {
    std::lock_guard lock(g_hidden_sync_mutex);
    return g_hidden_sync;
}

ShopObservationBatch drain_shop_observations() {
    ShopObservationBatch batch;
    std::lock_guard lock(g_shop_observation_mutex);
    batch.incomplete = g_shop_observations_overrun;
    batch.observations.reserve(g_shop_observations.size());
    while (!g_shop_observations.empty()) {
        batch.observations.push_back(std::move(g_shop_observations.front()));
        g_shop_observations.pop_front();
    }
    g_shop_observations_overrun = false;
    return batch;
}

bool initialize_capture(const ArkPluginApiV1* host) {
    if (!host) {
        return false;
    }
    std::lock_guard lock(g_lifecycle_mutex);
    if (g_capture_incompatible.load()) return false;
    if (g_active_capture) {
        return true;
    }
    clear_match_intel();
    clear_hidden_sync();
    reset_shop_observations(0);
    if (g_pending_capture) {
        if (!g_pending_capture->shutdown()) {
            return false;
        }
        g_pending_capture.reset();
    }
    auto capture = std::make_unique<NativeCapture>(*host);
    if (!capture->initialize()) {
        if (capture->has_live_hooks()) {
            g_pending_capture = std::move(capture);
        }
        return false;
    }
    g_active_capture = capture.release();
    {
        std::lock_guard error_lock(g_error_mutex);
        g_error.clear();
    }
    g_capture_ready.store(true);
    return true;
}

bool shutdown_capture() {
    std::lock_guard lock(g_lifecycle_mutex);
    if (g_pending_capture) {
        if (!g_pending_capture->shutdown()) {
            return false;
        }
        g_pending_capture.reset();
    }
    if (!g_active_capture) {
        g_capture_ready.store(false);
        return true;
    }
    if (!g_active_capture->shutdown()) {
        return false;
    }
    delete g_active_capture;
    g_active_capture = nullptr;
    g_capture_ready.store(false);
    return true;
}

bool capture_ready() {
    return g_capture_ready.load();
}

bool capture_incompatible() {
    return g_capture_incompatible.load();
}

std::string capture_report_path() {
    std::lock_guard lock(g_lifecycle_mutex);
    return g_report_path;
}

std::string capture_error() {
    return get_error();
}

void request_manual_reset() {
    g_reset_requested.store(true);
}

} // namespace ark::stronghold
