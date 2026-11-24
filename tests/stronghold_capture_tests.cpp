// Exercise the production capture path against a small, typed IL2CPP mock.
// This catches ABI and attribution errors that aggregate-only tests cannot see.
#include <cstdlib>

#include "../stronghold/capture.cpp"
#include "model.h"
#include "shop_counter.h"

namespace ark::stronghold {
namespace mock {
struct Klass { const char* name; int bytes; };
Klass enemy_class{"Enemy", 0}, entity_class{"Entity", 0};
Klass character{"Character", 0}, projectile{"Projectile", 0}, dictionary{"Dictionary", 0};
Klass chess_class{"ChessInst", 0}, reference_dictionary{"ListDict<UInt32,ChessInst>", 0};
Klass boolean{"System.Boolean", 1}, uint32{"UInt32", 4}, int32{"System.Int32", 4};
Klass manager_class{"AutoChessGameManager", 0}, center_class{"AutoChessDataCenter", 0};
Klass scene_class{"ScenePlayerData", 0}, string_class{"String", 0};
Klass player_model_class{"AutoChessPlayerDataModel", 0}, player_card_class{"PlayerCard", 0};
Klass indexer_class{"AutoChessDataIndexer", 0}, settle_class{"AutoChessSettleDataModel", 0};
Klass controller_class{"BattleController", 0}, player_data_class{"BattlePlayerData", 0};
Klass sequence_class{"MockSequence", 0};
Klass shop_class{"AutoChessShopDataModel", 0}, shop_good_class{"ChessGoods", 0};
Klass game_status_class{"AutoChessGameStatus", 0}, battle_status_class{"AutoChessBattleStatusDataModel", 0};
Klass self_status_class{"Torappu.Battle.AutoChess.AutoChessBattleStatusDataModel.SelfBattleRoundStatus", 0};
Klass self_status_ref_class{"Torappu.Battle.AutoChess.AutoChessBattleStatusDataModel.SelfBattleRoundStatus&", 0};
Klass self_status_dictionary_class{"System.Collections.Generic.Dictionary<System.Int32,Torappu.Battle.AutoChess.AutoChessBattleStatusDataModel.SelfBattleRoundStatus>", 0};
Klass help_data_class{"HelpBattleData", 0};
Klass game_state_enum{"Torappu.AutoChessGameStateType", 4};
Klass player_game_state_enum{"Torappu.AutoChessPlayerGameStateType", 4};

struct Field {
    Klass* type;
    int index;
    const char* name = nullptr;
    int static_value = -1;
};
Field unique{&uint32, 0}, is_token{&boolean, 1}, host_uid{&uint32, 2};
Field chess_inst{&int32, 10}, chess_card{&uint32, 11}, chess_player{&int32, 12};
Field controller_state{&int32, 30};
Field scene_slot{&int32, 20}, scene_hp{&int32, 21}, scene_connect{&int32, 22}, scene_finished{&boolean, 23};
Field scene_uid{&string_class, 24}, scene_card{&player_card_class, 25};
Field card_nickname{&string_class, 40}, card_nicknumber{&string_class, 41};
Field manager_center{&center_class, 99};
Field center_player_model{&player_model_class, 100}, center_indexer{&indexer_class, 101};
Field center_settle{&settle_class, 102};
Field center_shop{&shop_class, 103}, shop_goods{&sequence_class, 104};
Field shop_chess_id{&string_class, 105}, shop_refresh_seq{&int32, 106};
Field shop_bad_price{&int32, 107};
Field indexer_inst_players{&dictionary, 110}, indexer_runtime_cards{&reference_dictionary, 111};
Field indexer_token_cards{&dictionary, 112};
Field battle_characters{&sequence_class, 50}, battle_tokens{&sequence_class, 51};
Field battle_side{&int32, 52};
Field manager_status{&game_status_class, 120, "gameStatus"};
Field center_battle_status{&battle_status_class, 121, "battleStatus"};
Field self_status_dict{&self_status_dictionary_class, 122, "m_selfBattleStatusInfo"};
Field help_players{&sequence_class, 123, "players"};
Field scene_game_state{&player_game_state_enum, 26, "gameState"};
Field state_none{&game_state_enum, 130, "NONE", 0};
Field state_loading{&game_state_enum, 131, "LOADING", 1};
Field state_sp_prepare{&game_state_enum, 132, "SP_PREPARE", 2};
Field state_prepare{&game_state_enum, 133, "PREPARE", 3};
Field state_battle{&game_state_enum, 134, "BATTLE", 4};
Field state_help{&game_state_enum, 135, "HELP_BATTLE", 5};
Field state_boss{&game_state_enum, 136, "BOSS_BATTLE", 6};
Field state_settle{&game_state_enum, 137, "SETTLE", 7};
Field state_end{&game_state_enum, 138, "END", 8};
Field state_pause{&game_state_enum, 139, "PAUSE", 9};
Field state_waiting{&game_state_enum, 140, "BATTLE_WAITING", 100};
Field state_prepare_restart{&game_state_enum, 141, "PREPARE_RESTART", 102};
Field state_sp_prepare_restart{&game_state_enum, 142, "SP_PREPARE_RESTART", 103};
Field state_boss_restart{&game_state_enum, 143, "BOSS_BATTLE_RESTART", 104};
Field player_battling{&player_game_state_enum, 150, "BATTLE_NOT_COMPLETE", 4};
Field player_battle_complete{&player_game_state_enum, 151, "BATTLE_COMPLETE", 5};

struct String { Klass* klass = &string_class; std::wstring value = L"private-test-UID"; } uid;
struct PlayerCard {
    Klass* klass = &player_card_class;
    String nickname;
    String nicknumber;
};
struct Scene {
    Klass* klass = &scene_class;
    int slot = 0, hp = 0, connection = 1;
    bool finished = false, dead = false;
    int game_state = 4;
    String* uid_string = nullptr;
    PlayerCard* card = nullptr;
} scene;
struct Manager { Klass* klass = &manager_class; void* center = nullptr; void* status = nullptr; } manager;
void* nullable_manager_result = nullptr;
struct Chess { Klass* klass = &chess_class; int inst = 0; std::uint32_t card = 0; int player = 0; };
struct ReferenceDictionary {
    Klass* klass = &reference_dictionary;
    std::map<std::uint32_t, Chess*> entries;
};
struct Dictionary { Klass* klass = &dictionary; std::map<std::uint32_t, std::uint32_t> entries; };
struct Data { std::uint32_t unique; bool token; std::uint32_t host; };
struct Entity {
    Klass* klass;
    std::uint32_t card = 0;
    int side = 1;
    Data* data = nullptr;
    Entity* source = nullptr;
    String* character_id = nullptr;
    int player_side = 0;
};
struct Box { Klass* klass = &boolean; bool value = false; } bool_box;
struct IntBox { Klass* klass = &int32; int value = 0; } int_box;

struct DataCenter {
    Klass* klass = &center_class;
    void* player_model = nullptr;
    void* indexer = nullptr;
    void* settle = nullptr;
    void* battle_status = nullptr;
    void* help_data = nullptr;
    void* battle_players = nullptr;
    int stage_seed = 9001;
    int round = 0;
    int self = 1;
    void* shop = nullptr;
} live_center;
struct PlayerModel {
    Klass* klass = &player_model_class;
    std::map<int, Scene*> by_slot;
} live_player_model;
struct Indexer {
    Klass* klass = &indexer_class;
    void* inst_players = nullptr;
    void* runtime_cards = nullptr;
    void* token_cards = nullptr;
} live_indexer;
struct Settle {
    Klass* klass = &settle_class;
    int ending = 0;
} live_settle;
struct Controller { Klass* klass = &controller_class; int state = 2; } live_controller;
struct Sequence { Klass* klass = &sequence_class; std::vector<void*> items; };
struct Shop { Klass* klass = &shop_class; Sequence goods; int refresh_seq = 0; };
struct ShopGood { Klass* klass = &shop_good_class; String id; };
struct PlayerData { Klass* klass = &player_data_class; Sequence characters; Sequence tokens; int side = 0; };
struct GameStatus { Klass* klass = &game_status_class; int state = 3; } live_game_status;
struct SelfRoundStatus { Klass* klass = &self_status_class; bool finished = false; };
struct SelfStatusDictionary {
    Klass* klass = &self_status_dictionary_class;
    std::map<int, SelfRoundStatus*> entries;
} live_self_status_dict;
struct BattleStatus {
    Klass* klass = &battle_status_class;
    void* self_statuses = nullptr;
} live_battle_status;
struct HelpData {
    Klass* klass = &help_data_class;
    Sequence players;
} live_help_data;
SelfRoundStatus live_self_statuses[4];
Sequence live_battle_players;
IntBox live_battle_boxes[4];
Sequence live_player_list;
PlayerData live_player_data[4];
Data live_character_data[4];
PlayerCard live_cards[4];
String live_uids[4], live_nicknames[4], live_nicknumbers[4];
Scene live_scenes[4];
bool use_live_roster = false;
double live_clock = 0;

void prepare_live_roster() {
    live_player_list.items.clear();
    live_player_model.by_slot.clear();
    live_battle_players.items.clear();
    live_self_status_dict.entries.clear();
    for (int i = 0; i < 4; ++i) {
        const int slot = i + 1;
        live_uids[i].value = L"uid-" + std::to_wstring(slot);
        live_nicknames[i].value = L"nick-" + std::to_wstring(slot);
        live_nicknumbers[i].value = L"00" + std::to_wstring(slot);
        live_cards[i].nickname = live_nicknames[i];
        live_cards[i].nicknumber = live_nicknumbers[i];
        live_scenes[i].slot = slot;
        live_scenes[i].hp = 100 + slot;
        live_scenes[i].connection = 1;
        live_scenes[i].game_state = 4;
        live_scenes[i].finished = false;
        live_scenes[i].dead = false;
        live_scenes[i].uid_string = &live_uids[i];
        live_scenes[i].card = &live_cards[i];
        live_player_model.by_slot[slot] = &live_scenes[i];
        live_character_data[i] = {static_cast<std::uint32_t>(100 + slot), false, 0};
        live_player_data[i].side = 1010 + slot; // Deliberately not a player index.
        live_player_data[i].characters.items = {&live_character_data[i]};
        live_player_data[i].tokens.items.clear();
        live_player_list.items.push_back(&live_player_data[i]);
        live_self_statuses[i] = {&self_status_class, false};
        live_self_status_dict.entries[slot] = &live_self_statuses[i];
        live_battle_boxes[i].value = slot;
        live_battle_players.items.push_back(&live_battle_boxes[i]);
    }
    live_center.player_model = &live_player_model;
    live_center.indexer = &live_indexer;
    live_center.settle = &live_settle;
    live_center.battle_status = &live_battle_status;
    live_center.help_data = &live_help_data;
    live_center.battle_players = &live_battle_players;
    live_center.stage_seed = 9001;
    live_center.round = 0;
    live_center.self = 1;
    live_indexer.inst_players = nullptr;
    live_indexer.runtime_cards = nullptr;
    live_indexer.token_cards = nullptr;
    live_settle.ending = 0;
    controller_state.type = &int32;
    live_game_status.state = 3;
    live_battle_status.self_statuses = &live_self_status_dict;
    live_help_data.players.items.clear();
    manager.center = &live_center;
    manager.status = &live_game_status;
    nullable_manager_result = &manager;
    use_live_roster = true;
}

int get_current_state_impl(void*, void*);
bool get_self_finished_impl(void*, void*);
struct Method {
    void* code = reinterpret_cast<void*>(1);
    unsigned arity = 2;
    bool is_static = false;
    const char* name = "MockMethod";
    Klass* owner = nullptr;
    Klass* result = nullptr;
    std::vector<Klass*> args;
};
Method try_get{reinterpret_cast<void*>(1), 2, false, "TryGetValue", &dictionary, &boolean, {&uint32, &int32}};
Method try_get_reference{reinterpret_cast<void*>(1), 2, false, "TryGetValue", &reference_dictionary, &boolean, {&uint32, &chess_class}};
Method get_count{reinterpret_cast<void*>(1), 0, false, "get_Count", &sequence_class, &int32};
Method get_item{reinterpret_cast<void*>(1), 1, false, "get_Item", &sequence_class, &int32, {&int32}};
Method nullable_manager{reinterpret_cast<void*>(1), 0, true, "get_instanceOrNull", &manager_class, &manager_class};
Method get_scene{reinterpret_cast<void*>(1), 1, false, "GetPlayerData", &player_model_class, &scene_class, {&int32}};
Method get_dead{reinterpret_cast<void*>(1), 0, false, "get_isDead", &scene_class, &boolean};
Method get_stage_seed{reinterpret_cast<void*>(1), 0, false, "get_stageRandomSeed", &center_class, &int32};
Method get_round{reinterpret_cast<void*>(1), 0, false, "get_currentRound", &center_class, &int32};
Method get_self{reinterpret_cast<void*>(1), 0, false, "get_selfPlayerIndex", &center_class, &int32};
Method get_battle_players{reinterpret_cast<void*>(1), 0, false, "get_battlePlayerIndex", &center_class, &sequence_class};
Method get_help_data{reinterpret_cast<void*>(1), 0, false, "get_helpBattleData", &center_class, &help_data_class};
Method get_current_state{reinterpret_cast<void*>(&get_current_state_impl), 0, false, "get_currentState", &game_status_class, &game_state_enum};
Method get_self_status{reinterpret_cast<void*>(1), 1, false, "GetSelfBattleStatus", &battle_status_class, &self_status_class, {&int32}};
Method get_self_finished{reinterpret_cast<void*>(&get_self_finished_impl), 0, false, "get_battleFinished", &self_status_class, &boolean};
Method self_status_lookup{reinterpret_cast<void*>(1), 2, false, "TryGetValue", &self_status_dictionary_class, &boolean, {&int32, &self_status_ref_class}};
Method get_ending{reinterpret_cast<void*>(1), 0, false, "get_endingStatus", &settle_class, &int32};
std::map<Klass*, std::vector<Field*>> field_registry;
std::map<Klass*, std::vector<Method*>> method_registry;
void register_timing_metadata() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    field_registry[&manager_class] = {&manager_status};
    field_registry[&center_class] = {&center_battle_status};
    field_registry[&battle_status_class] = {&self_status_dict};
    field_registry[&help_data_class] = {&help_players};
    field_registry[&scene_class] = {&scene_game_state};
    field_registry[&game_state_enum] = {&state_none, &state_loading, &state_sp_prepare, &state_prepare,
        &state_battle, &state_help, &state_boss, &state_settle, &state_end, &state_pause,
        &state_waiting, &state_prepare_restart, &state_sp_prepare_restart, &state_boss_restart};
    field_registry[&player_game_state_enum] = {&player_battling, &player_battle_complete};
    method_registry[&game_status_class] = {&get_current_state};
    method_registry[&battle_status_class] = {&get_self_status};
    method_registry[&self_status_class] = {&get_self_finished};
    method_registry[&self_status_dictionary_class] = {&self_status_lookup};
    method_registry[&center_class].push_back(&get_help_data);
}

struct ModifierData {
    std::int64_t real = 100LL << 32;
    int shared = 1;
    std::int64_t calculated = 150LL << 32;
    Entity* source = nullptr;
    int type = 1;
    Entity* original_source = nullptr;
    bool cancelled = false;
    Entity* target = nullptr;
    int player_side = 0;
};
int get_current_state_impl(void* object, void*) {
    return static_cast<GameStatus*>(object)->state;
}
bool get_self_finished_impl(void* object, void*) {
    return static_cast<SelfRoundStatus*>(object)->finished;
}
std::vector<void*> roots;
void* clock_argument = nullptr;

il2cpp::Runtime runtime() {
    register_timing_metadata();
    il2cpp::Runtime r;
    r.object_class = [](void* obj) { return *static_cast<void**>(obj); };
    r.class_assignable = [](void* a, void* b) {
        return a == b || (a == &entity_class && (b == &character || b == &enemy_class)) ||
            (a == &character && b == &enemy_class);
    };
    r.class_type = [](void* c) { return c; };
    r.class_from_type = [](void* t) { return t; };
    r.class_parent = [](void*) -> void* { return nullptr; };
    r.class_name = [](void* c) { return static_cast<Klass*>(c)->name; };
    r.class_namespace = [](void* c) { return c == &string_class ? "System" : "Mock"; };
    r.class_is_value = [](void* c) { return static_cast<Klass*>(c)->bytes != 0; };
    r.class_value_size = [](void* c, uint32_t*) { return static_cast<Klass*>(c)->bytes; };
    r.class_rank = [](void*) { return 0; };
    r.method_param = [](void* m, std::uint32_t index) -> void* {
        const auto& args = static_cast<Method*>(m)->args;
        return index < args.size() ? args[index] : nullptr;
    };
    r.method_return = [](void* m) -> void* { return static_cast<Method*>(m)->result; };
    r.type_name = [](void* t) {
        const char* name = static_cast<Klass*>(t)->name;
        const std::size_t size = std::strlen(name) + 1;
        auto* copy = static_cast<char*>(std::malloc(size));
        std::memcpy(copy, name, size);
        return copy;
    };
    r.free_memory = [](void* memory) { std::free(memory); };
    r.class_field = [](void* c, const char* name) -> void* {
        auto found = field_registry.find(static_cast<Klass*>(c));
        if (found == field_registry.end()) return nullptr;
        for (void* item : found->second) {
            Field* field = static_cast<Field*>(item);
            if (field->name && std::strcmp(field->name, name) == 0) return field;
        }
        return nullptr;
    };
    r.field_static_get = [](void* field, void* out) {
        Field* item = static_cast<Field*>(field);
        if (item->static_value >= 0) {
            *static_cast<int*>(out) = item->static_value;
            return;
        }
        if (item->name && std::strcmp(item->name, "ONE") == 0) {
            *static_cast<std::int64_t*>(out) = 4294967296LL;
            return;
        }
        throw std::runtime_error("Unmocked static field");
    };
    r.field_type = [](void* f) -> void* { return static_cast<Field*>(f)->type; };
    r.field_get = [](void* object, void* field, void* out) {
        const auto index = static_cast<Field*>(field)->index;
        if (index == 106) { *static_cast<int*>(out) = static_cast<Shop*>(object)->refresh_seq; return; }
        if (index == 107) throw std::runtime_error("Optional price unavailable");
        if (index == 52) { *static_cast<int*>(out) = static_cast<PlayerData*>(object)->side; return; }
        if (index == 30) {
            *static_cast<int*>(out) = static_cast<Controller*>(object)->state;
            return;
        }
        if (index >= 20 && index <= 23 || index == 26) {
            const auto& data = *static_cast<Scene*>(object);
            if (index == 20) *static_cast<int*>(out) = data.slot;
            if (index == 21) *static_cast<int*>(out) = data.hp;
            if (index == 22) *static_cast<int*>(out) = data.connection;
            if (index == 23) *static_cast<bool*>(out) = data.finished;
            if (index == 26) *static_cast<int*>(out) = data.game_state;
            return;
        }
        if (index >= 10 && index <= 12) {
            const auto& chess = *static_cast<Chess*>(object);
            if (index == 10) *static_cast<int*>(out) = chess.inst;
            if (index == 11) *static_cast<std::uint32_t*>(out) = chess.card;
            if (index == 12) *static_cast<int*>(out) = chess.player;
            return;
        }
        const auto& data = *static_cast<Data*>(object);
        switch (index) {
        case 0: *static_cast<std::uint32_t*>(out) = data.unique; break;
        case 1: *static_cast<bool*>(out) = data.token; break;
        case 2: *static_cast<std::uint32_t*>(out) = data.host; break;
        }
    };
    r.field_get_object = [](void* field, void* object) -> void* {
        const auto index = static_cast<Field*>(field)->index;

        if (index == 99) return static_cast<Manager*>(object)->center;
        if (index == 120) return static_cast<Manager*>(object)->status;
        if (index == 121) return static_cast<DataCenter*>(object)->battle_status;
        if (index == 122) return static_cast<BattleStatus*>(object)->self_statuses;
        if (index == 123) return &static_cast<HelpData*>(object)->players;
        if (index == 100) return static_cast<DataCenter*>(object)->player_model;
        if (index == 101) return static_cast<DataCenter*>(object)->indexer;
        if (index == 102) return static_cast<DataCenter*>(object)->settle;
        if (index == 103) return static_cast<DataCenter*>(object)->shop;
        if (index == 104) return &static_cast<Shop*>(object)->goods;
        if (index == 105) return &static_cast<ShopGood*>(object)->id;
        if (index == 110) return static_cast<Indexer*>(object)->inst_players;
        if (index == 111) return static_cast<Indexer*>(object)->runtime_cards;
        if (index == 112) return static_cast<Indexer*>(object)->token_cards;
        if (index == 50) return &static_cast<PlayerData*>(object)->characters;
        if (index == 51) return &static_cast<PlayerData*>(object)->tokens;
        if (index == 24) {
            auto* scene_data = static_cast<Scene*>(object);
            return scene_data->uid_string ? scene_data->uid_string : &uid;
        }
        if (index == 25) return static_cast<Scene*>(object)->card;
        if (index == 40) return &static_cast<PlayerCard*>(object)->nickname;
        if (index == 41) return &static_cast<PlayerCard*>(object)->nicknumber;
        return nullptr;
    };
    r.string_length = [](void* s) { return static_cast<int>(static_cast<String*>(s)->value.size()); };
    r.string_chars = [](void* s) { return static_cast<String*>(s)->value.c_str(); };
    r.thread_current = []() -> void* { return reinterpret_cast<void*>(1); };
    r.class_methods = [](void* c, void** it) -> void* {
        auto registered = method_registry.find(static_cast<Klass*>(c));
        if (registered != method_registry.end()) {
            const auto i = reinterpret_cast<std::uintptr_t>(*it);
            if (i >= registered->second.size()) return nullptr;
            *it = reinterpret_cast<void*>(i + 1);
            return registered->second[i];
        }
        if (c == &dictionary) {
            if (*it) return nullptr;
            *it = reinterpret_cast<void*>(1);
            return &try_get;
        }
        if (c == &sequence_class) {
            if (!*it) {
                *it = reinterpret_cast<void*>(1);
                return &get_count;
            }
            if (*it == reinterpret_cast<void*>(1)) {
                *it = reinterpret_cast<void*>(2);
                return &get_item;
            }
        }
        return nullptr;
    };
    r.method_name = [](void* m) { return static_cast<Method*>(m)->name; };
    r.method_count = [](void* m) -> std::uint32_t { return static_cast<Method*>(m)->arity; };
    r.method_class = [](void* m) -> void* { return static_cast<Method*>(m)->owner; };
    r.method_flags = [](void* m, std::uint32_t*) -> std::uint32_t {
        return static_cast<Method*>(m)->is_static ? 0x10u : 0;
    };
    r.invoke_method = [](void* method, void* receiver, void** args, void**) -> void* {

        if (method == &nullable_manager) return nullable_manager_result;
        if (method == &get_scene) {
            const int slot = *static_cast<int*>(args[0]);

            if (use_live_roster) {
                const auto found = live_player_model.by_slot.find(slot);
                return found == live_player_model.by_slot.end() ? nullptr : found->second;
            }
            return slot == scene.slot ? &scene : nullptr;
        }
        if (method == &get_dead) {
            bool_box.value = static_cast<Scene*>(receiver)->dead;
            return &bool_box;
        }
        if (method == &get_current_state) {
            int_box.value = static_cast<GameStatus*>(receiver)->state;
            return &int_box;
        }
        if (method == &get_help_data) return static_cast<DataCenter*>(receiver)->help_data;
        if (method == &get_self_finished) {
            bool_box.value = static_cast<SelfRoundStatus*>(receiver)->finished;
            return &bool_box;
        }
        if (method == &self_status_lookup) {
            const int slot = *static_cast<int*>(args[0]);
            auto* dictionary = static_cast<SelfStatusDictionary*>(receiver);
            const auto found = dictionary->entries.find(slot);
            bool_box.value = found != dictionary->entries.end();
            *static_cast<void**>(args[1]) = bool_box.value ? found->second : nullptr;
            return &bool_box;
        }
        if (method == &get_self_status) {
            const int slot = *static_cast<int*>(args[0]);
            auto* dictionary = static_cast<BattleStatus*>(receiver)->self_statuses;
            auto* rows = static_cast<SelfStatusDictionary*>(dictionary);
            const auto found = rows->entries.find(slot);
            return found == rows->entries.end() ? nullptr : found->second;
        }
        if (method == &get_stage_seed) { int_box.value = live_center.stage_seed; return &int_box; }
        if (method == &get_round) { int_box.value = live_center.round; return &int_box; }
        if (method == &get_self) { int_box.value = live_center.self; return &int_box; }
        if (method == &get_battle_players) return static_cast<DataCenter*>(receiver)->battle_players;
        if (method == &get_ending) { int_box.value = live_settle.ending; return &int_box; }
        if (method == &get_count) {
            int_box.value = static_cast<int>(static_cast<Sequence*>(receiver)->items.size());
            return &int_box;
        }
        if (method == &get_item) {
            return static_cast<Sequence*>(receiver)->items.at(*static_cast<int*>(args[0]));
        }
        if (method == &try_get_reference) {
            const auto& entries = static_cast<ReferenceDictionary*>(receiver)->entries;
            const auto found = entries.find(*static_cast<std::uint32_t*>(args[0]));
            bool_box.value = found != entries.end();
            // A real managed ref out argument writes a full pointer, not four bytes.
            *static_cast<void**>(args[1]) = bool_box.value ? found->second : nullptr;
            return &bool_box;
        }
        const auto& entries = static_cast<Dictionary*>(receiver)->entries;
        const auto found = entries.find(*static_cast<std::uint32_t*>(args[0]));
        bool_box.value = found != entries.end();
        if (bool_box.value) *static_cast<std::uint32_t*>(args[1]) = found->second;
        return &bool_box;
    };
    r.object_unbox = [](void* obj) -> void* {
        if (*static_cast<Klass**>(obj) == &int32) return &static_cast<IntBox*>(obj)->value;
        return &static_cast<Box*>(obj)->value;
    };
    r.gc_new = [](void* obj, bool) -> std::uint32_t {

        roots.push_back(obj);

        return static_cast<std::uint32_t>(roots.size());
    };
    r.gc_target = [](std::uint32_t handle) {

        return roots.at(handle - 1);
    };
    r.gc_free = [](std::uint32_t) {};
    return r;
}
} // namespace mock

struct CaptureTests {
    static std::vector<Event> events_after(std::uint64_t after) {
        const std::uint64_t newest = g_events.newest_sequence();
        const std::size_t capacity = newest > after ? static_cast<std::size_t>(newest - after) : 0;
        std::vector<Event> events(capacity ? capacity : 1);
        bool overrun = false, partial = false;
        const std::size_t count = g_events.read(after, events.data(), events.size(), &overrun, &partial);
        events.resize(count);
        return events;
    }

    static int count_kind(const std::vector<Event>& events, EventKind kind) {
        int count = 0;
        for (const Event& event : events) if (event.kind == kind) ++count;
        return count;
    }

    static int match_intel_snapshot_contract() {
        int failures = 0;
        const auto expect = [&](bool condition, const char* expression, int line) {
            if (!condition) {
                ++failures;
                std::fprintf(stderr, "FAIL match intel %d: %s\n", line, expression);
            }
        };

        clear_match_intel();
        ArkPluginApiV1 api{};
        NativeCapture capture(api);
        capture.session_id_ = 41;
        capture.current_round_ = 3;

        std::vector<PlayerInfo> players(1);
        players[0].index = 2;
        players[0].uid = "private-test-UID";
        players[0].name = "private-test-nickname";
        players[0].band_id = "test-band";
        players[0].band_available = true;
        players[0].squad_available = true;
        players[0].shop_level = 4;
        players[0].shop_level_available = true;
        players[0].shop_coin = 12;
        players[0].shop_coin_available = true;
        IntelSquadSlot slot;
        slot.char_id = "test-char";
        slot.chess_id = "test-chess";
        slot.potential_rank = 3;
        slot.bond_ids = {"test-bond"};
        players[0].squad_slots.push_back(slot);

        capture.refresh_match_intel(nullptr, players, 123456);
        auto snapshot = capture_match_intel();
        expect(snapshot.session_id == 41 && snapshot.round == 3 && snapshot.stage_seed == 123456,
               "snapshot copies session/round/seed", __LINE__);
        expect(snapshot.players.size() == 1 && snapshot.players[0].index == 2,
               "snapshot copies player index", __LINE__);
        expect(snapshot.players[0].name == "private-test-nickname" &&
                   snapshot.players[0].band_id == "test-band",
               "snapshot copies display/static info", __LINE__);
        expect(snapshot.players[0].squad_slots.size() == 1 &&
                   snapshot.players[0].squad_slots[0].char_id == "test-char",
               "snapshot copies initial squad", __LINE__);
        expect(snapshot.players[0].shop_level == 4 && snapshot.players[0].shop_coin == 12,
               "snapshot copies per-player shop summary", __LINE__);

        auto copy = snapshot;
        copy.stage_id = "mutated-copy";
        auto fresh = capture_match_intel();
        expect(fresh.stage_id != "mutated-copy", "capture returns an independent copy", __LINE__);
        clear_match_intel();
        expect(!capture_match_intel().available, "clear resets published snapshot", __LINE__);
        return failures;
    }

    static int hidden_sync_snapshot_contract() {
        int failures = 0;
        const auto expect = [&](bool condition, const char* expression, int line) {
            if (!condition) {
                ++failures;
                std::fprintf(stderr, "FAIL hidden sync %d: %s\n", line, expression);
            }
        };

        clear_hidden_sync();
        HiddenSyncSnapshot input;
        input.available = true;
        input.stage_seed = 123456;
        input.stage_seed_available = true;
        input.hidden_boss_id = "hidden-boss";
        input.hidden_boss_available = true;
        IntelHiddenPlayer player;
        player.index = 2;
        player.banned_units_available = true;
        player.banned_units.push_back({"hidden-bond", {7, 8}});
        player.store.available = true;
        player.store.level = 4;
        player.store.coin = 19;
        IntelShopGood good;
        good.slot_id = 3;
        good.price = 5;
        good.frozen = true;
        good.chess_identifier = 77;
        player.store.goods.push_back(good);
        input.players.push_back(player);
        publish_hidden_sync(input);

        const auto snapshot = capture_hidden_sync();
        expect(snapshot.available && snapshot.sequence == 1 && snapshot.stage_seed == 123456,
               "hidden snapshot copies availability/sequence/seed", __LINE__);
        expect(snapshot.players.size() == 1 && snapshot.players[0].index == 2,
               "hidden snapshot copies player index", __LINE__);
        expect(snapshot.players[0].banned_units.size() == 1 &&
                   snapshot.players[0].banned_units[0].bond_id == "hidden-bond",
               "hidden snapshot copies all-player banned units", __LINE__);
        expect(snapshot.players[0].store.goods.size() == 1 &&
                   snapshot.players[0].store.goods[0].chess_identifier == 77,
               "hidden snapshot copies full player store/identifier", __LINE__);
        auto copy = snapshot;
        copy.hidden_boss_id = "mutated";
        expect(capture_hidden_sync().hidden_boss_id == "hidden-boss",
               "hidden snapshot is an independent copy", __LINE__);
        clear_hidden_sync();
        expect(!capture_hidden_sync().available, "hidden snapshot clear resets state", __LINE__);
        return failures;
    }

    static bool local_shop_reaches_counter() {
        using namespace mock;
        ArkPluginApiV1 api{};
        NativeCapture capture(api);
        capture.runtime_ = runtime();
        capture.center_shop_model_field_ = &center_shop;
        capture.shop_goods_field_ = &shop_goods;
        capture.shop_goods_class_ = &shop_good_class;
        capture.shop_good_chess_id_field_ = &shop_chess_id;
        capture.shop_good_price_field_ = &shop_bad_price;
        capture.self_index_ = 1;
        capture.current_round_ = 3;
        capture.session_id_ = 42;
        capture.shop_session_id_.store(42);
        reset_shop_observations(42);

        Shop shop;
        ShopGood a, b;
        a.id.value = L"char-a";
        b.id.value = L"char-b";
        DataCenter center;
        center.shop = &shop;
        ShopCounter counter;
        const auto sample = [&](ShopGood& good) {
            shop.goods.items = {&good};
            capture.refresh_match_intel(&center, {}, 123456);
            for (const auto& observed : drain_shop_observations().observations)
                counter.apply(observed);
        };

        sample(a);
        sample(a);
        sample(b);
        auto rows = counter.frequencies(false, 1);
        const bool fallback = rows.size() == 2 && rows[0].total == 1 && rows[1].total == 1 &&
            counter.incomplete();

        capture.shop_refresh_seq_field_ = &shop_refresh_seq;
        counter.reset(42);
        sample(a);
        ++shop.refresh_seq;
        sample(a);
        rows = counter.frequencies(false, 1);
        reset_shop_observations(0);
        clear_match_intel();
        return fallback && rows.size() == 1 && rows[0].total == 2;
    }

    static bool shop_catalog_resolves_protocol_names() {
        using namespace mock;
        static Klass act_type{"Torappu.ActAutoChessData", 0};
        static Klass row_type{"Torappu.ActAutoChessData.ActAutoChessCharChessData", 0};
        static Klass table_type{"Torappu.ListDict<System.String,Torappu.ActAutoChessData.ActAutoChessCharChessData>", 0};
        static Klass pair_type{"System.Collections.Generic.KeyValuePair<System.String,Torappu.ActAutoChessData.ActAutoChessCharChessData>", 16};
        static Klass list_type{"System.Collections.Generic.List<System.String>", 0};
        static Klass bond_type{"Torappu.ActAutoChessData.ActAutoChessBondInfo", 0};
        static Klass bond_ref_type{"Torappu.ActAutoChessData.ActAutoChessBondInfo&", 0};
        static Klass bond_table_type{"Torappu.ListDict<System.String,Torappu.ActAutoChessData.ActAutoChessBondInfo>", 0};
        static Klass string_type{"System.String", 0};
        static Field table_field{&table_type, 0, "charChessDataDict"};
        static Field bond_table_field{&bond_table_type, 0, "bondInfoDict"};
        static Field bond_name_field{&string_type, 0, "name"};
        static Field value_field{&row_type, 1, "value"};
        static Field identifier_field{&int32, 2, "identifier"};
        static Field id_field{&string_type, 3, "chessId"};
        static Field bonds_field{&list_type, 4, "bondIds"};
        static Method get_act{reinterpret_cast<void*>(1), 0, false, "get_actData", &center_class, &act_type};
        static Method get_pair{reinterpret_cast<void*>(1), 1, false, "Get", &table_type, &pair_type, {&int32}};
        static Method get_name{reinterpret_cast<void*>(1), 1, true, "GetCharName", &act_type, &string_type, {&string_type}};
        static Method find_bond{reinterpret_cast<void*>(1), 2, false, "TryGetValue", &bond_table_type, &boolean, {&string_type, &bond_ref_type}};
        static String char_id{&string_class, L"char_catalog"};
        static String char_name{&string_class, L"示例干员"};
        static String bond_id{&string_class, L"bond_catalog"};
        static String bond_name{&string_class, L"示例盟约"};
        Sequence bond_list{&sequence_class, {&bond_id}};
        struct Reference { Klass* klass; void* value; };
        struct Activity { Klass* klass; void* chars; void* bonds; };
        static Reference bond{&bond_type, &bond_name};
        Reference row{&row_type, &bond_list};
        Reference pair{&pair_type, &row};
        Sequence table{&table_type, {&pair}};
        Reference bond_table{&bond_table_type, &bond};
        Activity act{&act_type, &table, &bond_table};
        Reference center{&center_class, &act};
        ArkPluginApiV1 api{};
        NativeCapture capture(api);
        auto& rt = capture.runtime_;
        rt = runtime();
        field_registry[&act_type] = {&table_field, &bond_table_field};
        field_registry[&bond_type] = {&bond_name_field};
        field_registry[&pair_type] = {&value_field};
        field_registry[&row_type] = {&identifier_field, &id_field, &bonds_field};
        method_registry[&table_type] = {&get_count, &get_pair};
        method_registry[&bond_table_type] = {&find_bond};
        rt.field_get_object = [](void* field, void* object) -> void* {
            if (field == &id_field) return &char_id;
            if (field == &table_field) return static_cast<Activity*>(object)->chars;
            if (field == &bond_table_field) return static_cast<Activity*>(object)->bonds;
            return static_cast<Reference*>(object)->value;
        };
        rt.field_get = [](void*, void*, void* out) { *static_cast<int*>(out) = 252; };
        const auto invoke = rt.invoke_method;
        // A real IL2CPP reference parameter is passed directly in params[0].
        rt.invoke_method = [](void* method, void* receiver, void** args, void**) -> void* {
            if (method == &get_act) return static_cast<Reference*>(receiver)->value;
            if (method == &get_name) return args[0] == &char_id ? &char_name : nullptr;
            if (method == &find_bond) {
                bool_box.value = args[0] == &bond_id;
                *static_cast<void**>(args[1]) = bool_box.value ? static_cast<Reference*>(receiver)->value : nullptr;
                return &bool_box;
            }
            if (method == &get_count) {
                int_box.value = static_cast<int>(static_cast<Sequence*>(receiver)->items.size());
                return &int_box;
            }
            return static_cast<Sequence*>(receiver)->items.at(*static_cast<int*>(args[0]));
        };
        capture.center_act_data_info_ = &get_act;
        capture.character_name_lookup_ = &get_name;
        capture.refresh_shop_catalog(&center);
        const auto protocol = capture.describe_chess(252, 3);
        const auto local = capture.shop_catalog_entry(0, "char_catalog");
        const auto unknown = capture.describe_chess(12, 1);
        rt.invoke_method = invoke;
        field_registry.erase(&act_type);
        field_registry.erase(&bond_type);
        field_registry.erase(&pair_type);
        field_registry.erase(&row_type);
        method_registry.erase(&table_type);
        method_registry.erase(&bond_table_type);
        return protocol.operator_name == "示例干员" && protocol.chess_id == "char_catalog" &&
            protocol.count == 3 && protocol.bond_ids == std::vector<std::string>{"bond_catalog"} &&
            protocol.bond_names == std::vector<std::string>{"示例盟约"} &&
            local.operator_name == protocol.operator_name && unknown.operator_name.empty();
    }

    static bool boss_name_uses_game_data_and_enemy_handbook() {
        using namespace mock;
        static Klass game_type{"Torappu.AutoChessData", 0};
        static Klass boss_type{"Torappu.AutoChessData.AutoChessBossInfoData", 0};
        static Klass boss_out{"Torappu.AutoChessData.AutoChessBossInfoData&", 0};
        static Klass boss_dict{"System.Collections.Generic.Dictionary<System.String,Torappu.AutoChessData.AutoChessBossInfoData>", 0};
        static Klass handbook_type{"Torappu.EnemyHandBookDataGroup", 0};
        static Klass enemy_type{"Torappu.EnemyHandBookData", 0};
        static Klass enemy_out{"Torappu.EnemyHandBookData&", 0};
        static Klass enemy_dict{"System.Collections.Generic.Dictionary<System.String,Torappu.EnemyHandBookData>", 0};
        static Klass keys_type{"BossKeys", 0}, enumerator_type{"BossKeysEnumerator", 0};
        static Klass string_type{"System.String", 0};
        static Field game_field{&game_type, 0, "gameData"};
        static Field boss_field{&boss_dict, 0, "bossInfoDict"};
        static Field entries_field{&enemy_dict, 0, "enemyData"};
        static Field enemy_id_field{&string_type, 0, "enemyId"};
        static Field handbook_id_field{&string_type, 1, "handbookEnemyId"};
        static Field name_field{&string_type, 0, "name"};
        static Method hidden_id{reinterpret_cast<void*>(1), 0, false, "get_hiddenBossId", &center_class, &string_type};
        static Method handbook_data{reinterpret_cast<void*>(1), 0, true, "get_data", &handbook_type, &handbook_type};
        static Method find_boss{reinterpret_cast<void*>(1), 2, false, "TryGetValue", &boss_dict, &boolean, {&string_type, &boss_out}};
        static Method find_enemy{reinterpret_cast<void*>(1), 2, false, "TryGetValue", &enemy_dict, &boolean, {&string_type, &enemy_out}};
        static Method get_keys{reinterpret_cast<void*>(1), 0, false, "get_Keys", &boss_dict, &keys_type};
        static Method get_enum{reinterpret_cast<void*>(1), 0, false, "GetEnumerator", &keys_type, &enumerator_type};
        static Method next_key{reinterpret_cast<void*>(1), 0, false, "MoveNext", &enumerator_type, &boolean};
        static Method current_key{reinterpret_cast<void*>(1), 0, false, "get_Current", &enumerator_type, &string_type};
        static String boss_id{&string_class, L"boss_16"};
        static String enemy_id{&string_class, L"enemy_boss_variant"};
        static String handbook_id{&string_class, L"enemy_boss_handbook"};
        static String name{&string_class, L"示例首领"};
        struct Reference { Klass* klass; void* first; void* second = nullptr; };
        static Reference enemy{&enemy_type, &name};
        static Reference enemies{&enemy_dict, &enemy};
        static Reference handbook{&handbook_type, &enemies};
        static Reference boss{&boss_type, &enemy_id, &handbook_id};
        static Reference enumerator{&enumerator_type, &boss_id};
        static Reference keys{&keys_type, &enumerator};
        Reference bosses{&boss_dict, &boss, &keys};
        Reference game{&game_type, &bosses};
        Reference center{&center_class, &game, &boss_id};
        ArkPluginApiV1 api{};
        NativeCapture capture(api);
        auto& rt = capture.runtime_;
        rt = runtime();
        field_registry[&game_type] = {&boss_field};
        field_registry[&handbook_type] = {&entries_field};
        field_registry[&boss_type] = {&enemy_id_field, &handbook_id_field};
        field_registry[&enemy_type] = {&name_field};
        method_registry[&boss_dict] = {&find_boss, &get_keys};
        method_registry[&enemy_dict] = {&find_enemy};
        method_registry[&keys_type] = {&get_enum};
        method_registry[&enumerator_type] = {&next_key, &current_key};
        rt.field_get_object = [](void* field, void* object) -> void* {
            return field == &handbook_id_field ? static_cast<Reference*>(object)->second
                                              : static_cast<Reference*>(object)->first;
        };
        rt.invoke_method = [](void* method, void* receiver, void** args, void**) -> void* {
            if (method == &handbook_data) return &handbook;
            auto* object = static_cast<Reference*>(receiver);
            if (method == &hidden_id || method == &get_keys) return object->second;
            if (method == &get_enum || method == &current_key) return object->first;
            if (method == &next_key) {
                bool_box.value = object->second == nullptr;
                object->second = object->first;
                return &bool_box;
            }
            // Enforce the IL2CPP string reference/in and managed reference/out ABI.
            bool_box.value = method == &find_boss ? args[0] == &boss_id : args[0] == &handbook_id;
            *static_cast<void**>(args[1]) = bool_box.value ? object->first : nullptr;
            return &bool_box;
        };
        capture.session_id_ = 55;
        capture.center_hidden_boss_id_info_ = &hidden_id;
        capture.center_game_data_field_ = &game_field;
        capture.enemy_handbook_data_info_ = &handbook_data;
        clear_hidden_sync();
        capture.refresh_match_intel(&center, {}, 0);
        const auto local = capture_match_intel();

        center.second = nullptr;
        HiddenSyncSnapshot sync;
        sync.available = true;
        sync.session_id = 55;
        sync.hidden_boss_available = true;
        sync.hidden_boss_id = "boss_16";
        publish_hidden_sync(sync);
        enumerator.second = nullptr;
        capture.refresh_match_intel(&center, {}, 0);
        const auto synced = capture_match_intel();

        sync.hidden_boss_id = "boss_missing";
        publish_hidden_sync(sync);
        enumerator.second = nullptr;
        capture.refresh_match_intel(&center, {}, 0);
        const auto missing = capture_match_intel();
        clear_hidden_sync();
        clear_match_intel();
        field_registry.erase(&game_type);
        field_registry.erase(&handbook_type);
        field_registry.erase(&boss_type);
        field_registry.erase(&enemy_type);
        method_registry.erase(&boss_dict);
        method_registry.erase(&enemy_dict);
        method_registry.erase(&keys_type);
        method_registry.erase(&enumerator_type);
        return local.hidden_boss_id == "boss_16" && local.hidden_boss_name_available &&
            local.hidden_boss_name == "示例首领" && synced.hidden_boss_id == "boss_16" &&
            synced.hidden_boss_name_available && synced.hidden_boss_name == local.hidden_boss_name &&
            missing.hidden_boss_id == "boss_missing" && !missing.hidden_boss_name_available;
    }

    static int full_tick_runtime_contract() {
        int failures = 0;
        const auto expect = [&](bool condition, const char* expression, int line) {
            if (!condition) {
                ++failures;
                std::fprintf(stderr, "FAIL full tick %d: %s\n", line, expression);
            }
        };

        g_events.clear();
        mock::prepare_live_roster();

        mock::live_clock = 0;
        mock::live_controller.state = 2;
        mock::live_settle.ending = 0;

        mock::Chess chess_a[4]{};
        mock::ReferenceDictionary cards_a;
        mock::Dictionary owners_a;
        for (int i = 0; i < 4; ++i) {
            const int slot = i + 1;
            chess_a[i] = {&mock::chess_class, 1000 + slot, static_cast<std::uint32_t>(100 + slot), slot};
        }

        mock::Chess chess_b[5]{};
        mock::ReferenceDictionary cards_b;
        mock::Dictionary owners_b;
        for (int i = 0; i < 4; ++i) {
            const int slot = i + 1;
            chess_b[i] = {&mock::chess_class, 2000 + slot, static_cast<std::uint32_t>(100 + slot), slot};
            cards_b.entries[100 + slot] = &chess_b[i];
            owners_b.entries[2000 + slot] = slot;
        }
        chess_b[4] = {&mock::chess_class, 2005, 105, 4};
        cards_b.entries[105] = &chess_b[4];
        owners_b.entries[2005] = 4;

        mock::live_indexer.runtime_cards = &cards_a;
        mock::live_indexer.inst_players = &owners_a;

        ArkPluginApiV1 api{};
        NativeCapture live(api);
        live.runtime_ = mock::runtime();
        live.manager_instance_info_ = &mock::nullable_manager;
        live.manager_class_ = &mock::manager_class;
        live.auto_chess_center_class_ = &mock::center_class;
        live.manager_center_field_ = &mock::manager_center;
        live.data_center_player_model_field_ = &mock::center_player_model;
        live.data_center_indexer_field_ = &mock::center_indexer;
        live.data_center_settle_field_ = &mock::center_settle;
        live.center_stage_seed_info_ = &mock::get_stage_seed;
        live.center_round_info_ = &mock::get_round;
        live.center_self_info_ = &mock::get_self;
        live.center_battle_players_info_ = &mock::get_battle_players;
        live.settle_ending_info_ = &mock::get_ending;

        live.player_model_get_player_info_ = &mock::get_scene;
        live.scene_dead_info_ = &mock::get_dead;
        live.scene_index_field_ = &mock::scene_slot;
        live.scene_hp_field_ = &mock::scene_hp;
        live.scene_connect_field_ = &mock::scene_connect;
        live.scene_finished_field_ = &mock::scene_finished;
        live.scene_uid_field_ = &mock::scene_uid;
        live.scene_card_field_ = &mock::scene_card;
        live.card_nickname_field_ = &mock::card_nickname;

        live.indexer_inst_player_field_ = &mock::indexer_inst_players;
        live.indexer_runtime_card_field_ = &mock::indexer_runtime_cards;
        live.indexer_token_card_field_ = nullptr;
        live.runtime_cards_are_instances_ = true;
        live.runtime_card_lookup_info_ = &mock::try_get_reference;
        live.chess_class_ = &mock::chess_class;
        live.chess_inst_id_field_ = &mock::chess_inst;
        live.chess_card_uid_field_ = &mock::chess_card;
        live.chess_player_field_ = &mock::chess_player;

        live.character_class_ = &mock::character;
        live.enemy_class_ = &mock::enemy_class;
        live.entity_class_ = &mock::entity_class;
        live.entity_player_side_ = [](void* e, void*) { return static_cast<mock::Entity*>(e)->player_side; };
        live.modifier_target_ = [](void* m, void*) -> void* { return static_cast<mock::ModifierData*>(m)->target; };
        live.modifier_player_side_ = [](void* m, void*) { return static_cast<mock::ModifierData*>(m)->player_side; };
        live.projectile_class_ = &mock::projectile;
        live.battle_player_data_class_ = &mock::player_data_class;
        live.battle_characters_field_ = &mock::battle_characters;
        live.battle_tokens_field_ = &mock::battle_tokens;
        live.battle_player_side_field_ = &mock::battle_side;
        live.player_side_invalid_ = 0;
        live.battle_character_data_class_ = &mock::character;
        live.char_data_unique_field_ = &mock::unique;
        live.char_data_token_field_ = &mock::is_token;
        live.char_data_host_uid_field_ = &mock::host_uid;
        live.character_card_uid_ = [](void* e, void*) { return static_cast<mock::Entity*>(e)->card; };
        live.character_side_ = [](void* e, void*) { return static_cast<mock::Entity*>(e)->side; };
        live.character_data_ = [](void* e, void*) -> void* { return static_cast<mock::Entity*>(e)->data; };
        live.character_is_token_ = [](void* e, void*) {
            auto* data = static_cast<mock::Entity*>(e)->data;
            return data && data->token;
        };
        live.character_id_ = [](void* e, void*) -> void* {
            return static_cast<mock::Entity*>(e)->character_id;
        };
        live.projectile_source_ = [](void* e, void*) -> void* {
            return static_cast<mock::Entity*>(e)->source;
        };
        live.controller_class_ = &mock::controller_class;
        live.controller_state_field_ = &mock::controller_state;
        live.controller_player_list_ = [](void*, void*) -> void* { return &mock::live_player_list; };
        live.controller_clock_ = [](void*) -> std::int64_t {
            return static_cast<std::int64_t>(mock::live_clock * 4294967296.0);
        };

        live.modifier_source_ = [](void* m, void*) -> void* {
            return static_cast<mock::ModifierData*>(m)->source;
        };
        live.modifier_original_source_ = [](void* m, void*) -> void* {
            return static_cast<mock::ModifierData*>(m)->original_source;
        };
        live.modifier_is_cancelled_ = [](void* m, void*) {
            return static_cast<mock::ModifierData*>(m)->cancelled;
        };
        live.modifier_is_damage_ = [](void* m, void*) {
            const int type = static_cast<mock::ModifierData*>(m)->type;
            return type >= 1 && type <= 3;
        };
        live.modifier_is_heal_ = [](void* m, void*) {
            return static_cast<mock::ModifierData*>(m)->type == 4;
        };
        live.modifier_is_element_ = [](void* m, void*) {
            return static_cast<mock::ModifierData*>(m)->type == 5;
        };
        live.modifier_final_delta_ = [](void* m, void*) {
            return static_cast<mock::ModifierData*>(m)->calculated;
        };
        live.modifier_damage_type_ = [](void* m, void*) {
            return static_cast<mock::ModifierData*>(m)->type;
        };
        live.real_delta_offset_ = offsetof(mock::ModifierData, real);
        live.shared_mask_offset_ = offsetof(mock::ModifierData, shared);

        live.scene_player_data_class_ = &mock::scene_class;
        live.bind_timing();
        expect(live.manager_status_field_ && live.center_battle_status_field_ && live.self_status_info_ &&
                   live.self_status_dict_field_ && live.self_status_lookup_info_ && live.game_state_ &&
                   live.self_finished_ && live.scene_game_state_field_ && live.center_help_data_info_ &&
                   live.help_players_field_,
               "timing ABI binds every new field, getter and closed dictionary lookup", __LINE__);
        expect(live.phase_battle_ == 4 && live.phase_help_ == 5 && live.phase_boss_ == 6 &&
                   live.phase_waiting_ == 100 && live.phase_end_ == 8 && live.phase_pause_ == 9 &&
                   live.phase_settle_ == 7 && live.player_battling_ == 4,
               "timing enum values bind by name", __LINE__);

        // Preparation: the controller's local player list and character records
        // are ready before the index dictionaries are populated. The host cache
        // must still learn all four card UIDs, but cannot claim coverage yet.
        live.set_controller(&mock::live_controller);

        live.tick_runtime(true);

        expect(live.in_mode_ && live.mode_present_ && live.current_round_ == 1,
               "raw preparation round zero maps to round one", __LINE__);
        expect(live.last_players_.size() == 4, "preparation establishes slots 1..4", __LINE__);
        for (int slot = 1; slot <= 4; ++slot) {
            const auto found = live.last_players_.find(slot);
            expect(found != live.last_players_.end() && found->second.name == "nick-" + std::to_string(slot),
                   "preparation refreshes every nickname", __LINE__);
        }
        expect(live.hosts_.size() == 4, "local character records fill the host cache", __LINE__);
        expect(live.read_controller_player_count() == 4 && live.host_cache_player_count_ == 4,
               "host cache records the unchanged four-entry player list", __LINE__);
        expect(live.local_simulated_mask_ == 0 && live.observed_player_mask_ == 0 &&
                   live.simulated_mask_ == 0,
               "empty index dictionaries cannot fabricate coverage", __LINE__);
        expect(live.counters_.published == 0, "index recovery happens before the first damage", __LINE__);
        const std::uint64_t session = live.session_id_;
        const auto preparation_events = events_after(0);
        expect(count_kind(preparation_events, EventKind::SessionStart) == 1,
               "one preparation session starts", __LINE__);
        expect(count_kind(preparation_events, EventKind::Roster) == 4,
               "preparation publishes four roster rows", __LINE__);
        expect(count_kind(preparation_events, EventKind::SessionEnd) == 0,
               "preparation does not end session", __LINE__);

        // Fill the exact same dictionary objects. The list length is unchanged,
        // so a later tick must derive the local mask by rechecking cached card
        // UIDs rather than waiting for the first damage event.
        for (int i = 0; i < 4; ++i) {
            const int slot = i + 1;
            cards_a.entries[100 + slot] = &chess_a[i];
            owners_a.entries[1000 + slot] = slot;
        }
        mock::live_settle.ending = 1;
        expect(live.read_controller_player_count() == 4 && live.host_cache_player_count_ == 4,
               "player list length remains unchanged while indexes fill", __LINE__);
        live.tick_runtime(true);
        expect(live.local_simulated_mask_ == 30 && live.observed_player_mask_ == 0 &&
                   live.simulated_mask_ == 30,
               "same-length player list recovers local mask after index fill", __LINE__);
        expect(live.counters_.published == 0, "coverage recovery does not wait for a damage event", __LINE__);
        expect(live.active_mask_ == 30 && live.coverage_ == Coverage::FullRoom,
               "cached host UIDs establish full room before damage", __LINE__);
        expect(live.last_players_.size() == 4 && live.in_mode_,
               "index recovery does not reset the live roster", __LINE__);

        // Dynamic/nested summons do not change player-list length. Refresh
        // their copied host links every tick, and retain the operator identity
        // after despawn without using it as current simulation coverage.
        mock::String dynamic_host_id;
        dynamic_host_id.value = L"op-dynamic-host";
        mock::Entity dynamic_host{&mock::character, 101, 1, nullptr, nullptr, &dynamic_host_id};
        HostInfo dynamic_info;
        int dynamic_owner = kUnknownPlayer;
        std::uint32_t dynamic_card = 0;
        expect(live.resolve_character(&dynamic_host, dynamic_info, dynamic_owner, dynamic_card),
               "learn original operator before dynamic token spawn", __LINE__);
        mock::Data mid_data{8001, true, 101}, leaf_data{8002, true, 8001};
        mock::Entity leaf{&mock::character, 8002, 1, &leaf_data};
        mock::live_player_data[0].tokens.items = {&mid_data, &leaf_data};
        live.tick_runtime(true);
        expect(live.hosts_.count(8001) == 1 && live.hosts_.count(8002) == 1 &&
                   live.host_cache_player_count_ == 4,
               "new summons are sampled with unchanged player count", __LINE__);
        expect(live.resolve_character(&leaf, dynamic_info, dynamic_owner, dynamic_card) &&
                   dynamic_owner == 1 && dynamic_card == 101 && dynamic_info.id == "op-dynamic-host",
               "new nested summon resolves to the original operator", __LINE__);
        cards_a.entries.erase(101);
        owners_a.entries.erase(1001);
        mock::live_player_data[0].characters.items.clear();
        mock::live_player_data[0].tokens.items.clear();
        live.tick_runtime(true);
        expect(live.local_simulated_mask_ == 28 && !live.live_host_cards_.count(101),
               "despawned identities do not fabricate live coverage", __LINE__);
        expect(live.resolve_character(&leaf, dynamic_info, dynamic_owner, dynamic_card) &&
                   dynamic_owner == 1 && dynamic_card == 101,
               "late summon or DoT retains same-round verified owner after despawn", __LINE__);
        leaf_data.host = 8002;
        expect(live.resolve_character(&leaf, dynamic_info, dynamic_owner, dynamic_card) &&
                   dynamic_info.id == "unknown_host" && dynamic_card == 0,
               "changed token host invalidates old attribution and rejects a cycle", __LINE__);
        leaf_data.host = 8001;
        expect(live.resolve_character(&leaf, dynamic_info, dynamic_owner, dynamic_card) && dynamic_card == 101,
               "valid nested link recovers after a cyclic link is removed", __LINE__);
        cards_a.entries[101] = &chess_a[0];
        owners_a.entries[1001] = 1;
        mock::live_player_data[0].characters.items = {&mock::live_character_data[0]};
        mock::live_player_data[1].side = mock::live_player_data[0].side;
        live.tick_runtime(true);
        expect(live.side_players_.count(1011) == 0,
               "one side mapping to two players is rejected instead of guessing", __LINE__);
        mock::live_player_data[1].side = 1012;
        live.tick_runtime(true);
        expect(live.side_players_.at(1011) == 1 && live.side_players_.at(1012) == 2,
               "fresh nonconflicting side evidence recovers automatically", __LINE__);

        mock::live_clock = 0;
        mock::live_game_status.state = 4;
        for (int i = 0; i < 4; ++i) {
            mock::live_scenes[i].game_state = 4;
            mock::live_self_statuses[i].finished = false;
        }

        live.begin_battle(&mock::live_controller, false);

        expect(live.controller_ == &mock::live_controller && live.controller_root_.get() == &mock::live_controller,
               "controller is rooted for combat", __LINE__);
        expect(!live.hosts_.count(8001) && !live.host_players_.count(8002),
               "new battle/controller boundary clears stale summon identities", __LINE__);
        expect(live.battle_active_ && live.battle_ == 1 && live.last_clock_ == 0,
               "combat starts with a clean clock", __LINE__);

        mock::String actor_ids[4];
        mock::Entity actors[4]{};
        for (int i = 0; i < 4; ++i) {
            actor_ids[i].value = L"op-" + std::to_wstring(i + 1);
            actors[i].klass = &mock::character;
            actors[i].card = i == 3 ? 105 : static_cast<std::uint32_t>(101 + i);
            actors[i].side = 1;
            actors[i].character_id = &actor_ids[i];
        }

        mock::ModifierData modifier;
        modifier.shared = 1;
        modifier.type = 1;
        const double round_one_damage[4] = {100, 200, 300, 400};
        const int endings[4] = {1, 2, 3, 4};
        for (int i = 0; i < 4; ++i) {

            if (i == 0) {
                mock::live_cards[1].nickname.value = L"nick-2-refreshed";
            }
            if (i == 1) {
                // A tick must re-root the indexer collections; otherwise card
                // 105 cannot be attributed through the replacement maps.
                mock::live_indexer.runtime_cards = &cards_b;
                mock::live_indexer.inst_players = &owners_b;
            }
            mock::live_settle.ending = endings[i];
            live.tick_runtime(true);
            expect(live.diagnostic_settlement_status_ == endings[i],
                   "raw settlement status is retained for diagnostics", __LINE__);
            expect(live.in_mode_ && live.mode_present_ && mock::manager.center == &mock::live_center,
                   "nonzero settlement cannot end a live manager", __LINE__);
            expect(live.controller_ == &mock::live_controller &&
                       live.read_battle_state(&mock::live_controller) == live.state_running_,
                   "controller remains PLAYING", __LINE__);
            void* expected_cards = i < 1 ? static_cast<void*>(&cards_a) : static_cast<void*>(&cards_b);
            void* expected_owners = i < 1 ? static_cast<void*>(&owners_a) : static_cast<void*>(&owners_b);
            expect(live.last_players_.size() == 4 && live.runtime_card_root_.get() == expected_cards &&
                       live.inst_player_root_.get() == expected_owners,
                   "every tick refreshes roster and current index maps", __LINE__);
            expect(live.last_players_.at(2).name == "nick-2-refreshed",
                   "every tick refreshes updated roster values", __LINE__);
            expect(i == 0 ? live.player_for_card(101) == 1 : live.player_for_card(105) == 4,
                   "current indexer is usable without restarting the session", __LINE__);

            modifier.source = &actors[i];
            modifier.real = static_cast<std::int64_t>(round_one_damage[i]) << 32;
            modifier.calculated = static_cast<std::int64_t>(round_one_damage[i] * 1.5) << 32;
            const auto before = live.counters_.published;
            live.record_damage(nullptr, &modifier, false);
            expect(live.counters_.published == before + 1,
                   "damage publishes while settlement is nonzero", __LINE__);
            expect(live.in_mode_ && live.battle_active_ && live.session_id_ == session,
                   "damage does not clear the live session", __LINE__);
        }

        mock::live_settle.ending = 99;
        live.tick_runtime(true);

        expect(live.diagnostic_settlement_status_ == 99 && live.in_mode_,
               "unknown nonzero settlement remains diagnostic-only", __LINE__);
        expect(live.observed_player_mask_ == 30 && live.simulated_mask_ == 30 && live.active_mask_ == 30,
               "all four player slots become concrete coverage evidence", __LINE__);
        expect(live.coverage_ == Coverage::FullRoom, "four observed players form full-room coverage", __LINE__);

        // 10s of normal four-player combat.
        mock::live_clock = 10;
        live.update_battle(&mock::live_controller);
        expect(live.last_clock_ == 10 && live.battle_active_,
               "round one accumulates main combat time", __LINE__);
        expect(live.current_phase_ == BattlePhase::Main && live.timing_mask_ == 30,
               "all four players share the initial main clock", __LINE__);

        // The local player finishes first. BATTLE_WAITING freezes that player,
        // while still playing players remain on the main clock.
        mock::live_self_statuses[0].finished = true;
        mock::live_game_status.state = 100;
        mock::live_self_status_dict.entries.erase(2);
        live.tick_runtime(true);
        expect(live.in_mode_ && live.diagnostic_game_state_ == 100,
               "BATTLE_WAITING does not stop the session", __LINE__);
        expect(live.current_phase_ == BattlePhase::Main && live.timing_mask_ == (2u | 4u | 8u | 16u) - 2u,
               "waiting clears only the finished local player", __LINE__);
        expect(live.timing_warning_sent_,
               "missing self row falls back to trusted scene gameState timing", __LINE__);
        mock::live_self_status_dict.entries[2] = &mock::live_self_statuses[1];
        expect(count_kind(events_after(0), EventKind::SessionEnd) == 0,
               "nonzero settlement plus waiting never emits SessionEnd", __LINE__);

        // A teammate still deals damage during the wait; event capture and its
        // main clock must survive even though the global self battle state is false.
        mock::live_clock = 20;
        modifier.source = &actors[1];
        modifier.real = 25LL << 32;
        modifier.calculated = 25LL << 32;
        live.record_damage(nullptr, &modifier, false);
        expect(live.current_phase_ == BattlePhase::Main && live.timing_mask_ == (4u | 8u | 16u),
               "waiting damage keeps other players' main mask", __LINE__);

        mock::live_clock = 40;
        live.tick_runtime(true);
        expect(live.current_phase_ == BattlePhase::Main && live.timing_mask_ == (4u | 8u | 16u),
               "other players accumulate the full 30s wait window", __LINE__);

        for (auto& status : mock::live_self_statuses) status.finished = true;
        live.tick_runtime(true);
        expect(live.current_phase_ == BattlePhase::Idle && live.timing_mask_ == 0,
               "all finished players stop the main clock during waiting", __LINE__);

        // Two helper players enter a separate assist phase. Their clock and
        // Damage are independent from the main view.
        mock::live_game_status.state = 5;
        for (int i = 0; i < 4; ++i) mock::live_scenes[i].game_state = i < 2 ? 4 : 5;
        mock::live_battle_boxes[0].value = 1;
        mock::live_battle_boxes[1].value = 2;
        mock::live_battle_boxes[2].value = 3;
        mock::live_battle_players.items = {&mock::live_battle_boxes[0], &mock::live_battle_boxes[1],
                                          &mock::live_battle_boxes[2]};
        mock::live_help_data.players.items = {&mock::live_battle_boxes[0], &mock::live_battle_boxes[1]};
        live.tick_runtime(true);
        expect(live.current_phase_ == BattlePhase::Assist && live.timing_mask_ == (2u | 4u),
               "two explicit assist players start an independent clock", __LINE__);
        expect(live.assist_member_mask_ == (2u | 4u) && live.assist_members_warning_sent_,
               "stale primary >2 falls back to valid help players with one warning", __LINE__);

        mock::live_clock = 45;
        modifier.source = &actors[0];
        modifier.real = 50LL << 32;
        modifier.calculated = 60LL << 32;
        live.record_damage(nullptr, &modifier, false);
        modifier.source = &actors[1];
        live.record_damage(nullptr, &modifier, false);
        modifier.source = &actors[2];
        modifier.real = 7LL << 32;
        modifier.calculated = 7LL << 32;
        const std::uint64_t before_outside_assist = g_events.newest_sequence();
        live.record_damage(nullptr, &modifier, false);
        const auto outside_assist_events = events_after(before_outside_assist);
        bool outside_assist_kept = false;
        for (const Event& event : outside_assist_events) {
            if (event.kind == EventKind::Damage && event.actual_damage == 7) {
                outside_assist_kept = event.phase == BattlePhase::Assist && event.player_index == 3;
            }
        }
        expect(outside_assist_kept && live.timing_mask_ == (2u | 4u),
               "out-of-list assist damage is retained without inventing its clock", __LINE__);
        expect(live.current_phase_ == BattlePhase::Assist,
               "assist damage is labelled Assist", __LINE__);

        // A helper's killing hit may switch HELP_BATTLE -> SETTLE inside the
        // original callback. Keep that hit in Assist even after the phase moves.
        g_callback_capture = &live;
        live.callbacks_enabled_ = true;
        live.host_active_.store(true);
        original_applied_modifier = [](void*, void* raw, void*) {
            auto* killing = static_cast<mock::ModifierData*>(raw);
            killing->real = 12LL << 32;
            mock::live_game_status.state = 7;
        };
        mock::ModifierData help_last_hit;
        help_last_hit.source = &actors[0];
        help_last_hit.type = 1;
        help_last_hit.shared = 1;
        const std::uint64_t before_help_last_hit = g_events.newest_sequence();
        NativeCapture::on_applied_modifier(nullptr, &help_last_hit, nullptr);
        const auto help_last_events = events_after(before_help_last_hit);
        bool help_last_seen = false;
        for (const Event& event : help_last_events) {
            if (event.kind == EventKind::Damage && event.actual_damage == 12) {
                help_last_seen = event.phase == BattlePhase::Assist;
            }
            expect(event.kind != EventKind::SessionEnd,
                   "HELP -> SETTLE last hit cannot publish SessionEnd", __LINE__);
        }
        expect(help_last_seen, "HELP -> SETTLE last hit retains Assist phase", __LINE__);
        mock::live_game_status.state = 5;
        original_applied_modifier = nullptr;
        live.callbacks_enabled_ = false;
        live.host_active_.store(false);
        g_callback_capture = nullptr;
        live.tick_runtime(true); // Restore the Assist mask after the SETTLE transition.

        mock::live_clock = 50;
        live.tick_runtime(true);
        expect(live.current_phase_ == BattlePhase::Assist && live.timing_mask_ == (2u | 4u),
               "both helpers accumulate the first 10 assist seconds", __LINE__);

        mock::live_scenes[0].game_state = 5;
        live.tick_runtime(true);
        expect(live.current_phase_ == BattlePhase::Assist && live.timing_mask_ == 4u,
               "one helper gameState COMPLETE freezes only that helper", __LINE__);

        mock::live_clock = 60;
        live.tick_runtime(true);
        expect(live.current_phase_ == BattlePhase::Assist && live.timing_mask_ == 4u,
               "remaining helper continues independently", __LINE__);

        mock::live_help_data.players.items.clear();
        mock::live_battle_players.items = {&mock::live_battle_boxes[0], &mock::live_battle_boxes[1],
                                          &mock::live_battle_boxes[2]};
        live.tick_runtime(true);
        expect(live.current_phase_ == BattlePhase::Assist && live.timing_mask_ == 0,
               "HELP remains Assist even when the membership mask is empty", __LINE__);

        // Preparation is idle. Raw round zero already mapped to round one,
        // so observing raw round one again must not create another round.
        mock::live_game_status.state = 3;
        live.tick_runtime(true);
        expect(live.current_phase_ == BattlePhase::Idle && live.timing_mask_ == 0,
               "prepare phase contributes no combat time", __LINE__);

        const std::uint64_t before_same_raw_round = g_events.newest_sequence();
        const std::uint64_t round_one_battle = live.battle_;
        mock::live_center.round = 1;
        mock::live_game_status.state = 4;
        mock::live_help_data.players.items.clear();
        for (int i = 0; i < 4; ++i) {
            mock::live_scenes[i].game_state = 4;
            mock::live_self_statuses[i].finished = false;
            mock::live_battle_boxes[i].value = i + 1;
        }
        mock::live_battle_players.items = {&mock::live_battle_boxes[0], &mock::live_battle_boxes[1],
                                          &mock::live_battle_boxes[2], &mock::live_battle_boxes[3]};
        live.tick_runtime(true);
        expect(live.in_mode_ && live.current_round_ == 1 && live.battle_ == round_one_battle,
               "raw round one remains round one without restarting battle", __LINE__);
        expect(count_kind(events_after(before_same_raw_round), EventKind::RoundStart) == 0,
               "raw round one does not publish a duplicate RoundStart", __LINE__);

        mock::live_center.round = 2;
        live.tick_runtime(true);
        expect(live.in_mode_ && live.current_round_ == 2 && live.session_id_ == session,
               "raw round two preserves session and cumulative state", __LINE__);
        live.update_battle(&mock::live_controller);
        expect(live.battle_active_ && live.battle_ == 2 && live.last_clock_ == 60,
               "round two starts a separate main clock", __LINE__);

        mock::live_settle.ending = 99;
        live.tick_runtime(true);
        const double round_two_damage[4] = {10, 20, 30, 40};
        for (int i = 0; i < 4; ++i) {
            modifier.source = &actors[i];
            modifier.real = static_cast<std::int64_t>(round_two_damage[i]) << 32;
            modifier.calculated = static_cast<std::int64_t>(round_two_damage[i] * 1.5) << 32;
            live.record_damage(nullptr, &modifier, false);
        }
        mock::live_clock = 70;
        live.tick_runtime(true);
        expect(live.diagnostic_settlement_status_ == 99 && live.in_mode_,
               "nonzero settlement remains diagnostic-only through round two", __LINE__);
        expect(count_kind(events_after(0), EventKind::SessionEnd) == 0,
               "endingStatus>0 never stops capture before END", __LINE__);

        const auto captured = events_after(0);
        StatsModel model;
        for (const Event& event : captured) model.apply(event);
        expect(model.session_id() == session && !model.ended(), "model retains the live session", __LINE__);
        expect(model.current_round() == 2, "model reaches round two", __LINE__);
        expect(model.effective_mask() == 30, "model displays all four confirmed players", __LINE__);
        expect(model.roster().size() == 4, "model retains four roster players", __LINE__);

        const double expected_round_two[4] = {10, 20, 30, 40};
        const double expected_main_session[4] = {110, 245, 330, 440};
        const double expected_main_time[4] = {20, 50, 50, 50};
        for (int i = 0; i < 4; ++i) {
            const int slot = i + 1;
            expect(model.session(BattlePhase::Main).players.at(slot).totals.actual ==
                       expected_main_session[i],
                   "main session damage keeps waiting damage and excludes assist", __LINE__);
            expect(model.round_view(BattlePhase::Main).players.at(slot).totals.actual ==
                       expected_round_two[i],
                   "round two main damage is isolated", __LINE__);
            expect(model.session(BattlePhase::Main).players.at(slot).active_time ==
                       expected_main_time[i],
                   "waiting freezes the finished player but not active teammates", __LINE__);
            expect(model.round_view(BattlePhase::Main).players.at(slot).active_time == 10,
                   "round two starts with ten main seconds", __LINE__);
        }
        expect(model.session(BattlePhase::Main).actual_total() == 1125,
               "main session total combines both rounds and waiting damage", __LINE__);
        expect(model.round_view(BattlePhase::Main).actual_total() == 100,
               "round two main total contains only round two", __LINE__);
        expect(model.session(BattlePhase::Assist).players.at(1).active_time == 10 &&
                   model.session(BattlePhase::Assist).players.at(1).totals.actual == 62,
               "first helper has independent assist time and damage", __LINE__);
        expect(model.session(BattlePhase::Assist).players.at(2).active_time == 20 &&
                   model.session(BattlePhase::Assist).players.at(2).totals.actual == 50,
               "second helper freezes only after its own gameState completes", __LINE__);
        expect(model.session(BattlePhase::Assist).players.at(3).totals.actual == 7,
               "out-of-list assist damage remains visible", __LINE__);
        expect(model.session(BattlePhase::Assist).actual_total() == 119,
               "assist damage is independent from main", __LINE__);
        const View* first_round = model.pending_round_export(BattlePhase::Main);
        const View* first_assist = model.pending_round_export(BattlePhase::Assist);
        expect(first_round && model.pending_round_number() == 1 && first_round->actual_total() == 1025,
               "round one main snapshot retains waiting-phase damage", __LINE__);
        expect(first_assist && first_assist->actual_total() == 119,
               "round one assist snapshot is separate", __LINE__);
        expect(model.completed_rounds().at(1).main.actual_total() == 1025,
               "completed round one snapshot retains its own totals", __LINE__);
        model.clear_pending_round_export();
        expect(!model.pending_round_export(), "round one export drains independently", __LINE__);

        const std::uint64_t before_raw_eight = g_events.newest_sequence();
        mock::live_center.round = 8;
        live.tick_runtime(true);
        expect(live.in_mode_ && live.current_round_ == 8,
               "raw round eight remains round eight", __LINE__);
        const auto raw_eight_events = events_after(before_raw_eight);
        bool raw_eight_round_start = false;
        for (const Event& event : raw_eight_events) {
            if (event.kind == EventKind::RoundStart) raw_eight_round_start = event.round == 8;
        }
        expect(count_kind(raw_eight_events, EventKind::RoundStart) == 1 && raw_eight_round_start,
               "round eight RoundStart is not offset to round nine", __LINE__);
        for (const Event& event : raw_eight_events) model.apply(event);
        expect(model.current_round() == 8, "model reaches raw round eight unchanged", __LINE__);
        expect(model.pending_round_number() == 2,
               "round two becomes the next completed export", __LINE__);
        const View* second_round = model.pending_round_export(BattlePhase::Main);
        const View* second_assist = model.pending_round_export(BattlePhase::Assist);
        expect(second_round && second_round->actual_total() == 100 &&
                   second_round->players.at(1).totals.actual == 10 &&
                   second_round->players.at(1).active_time == 10,
               "round two snapshot contains only round two damage and clock", __LINE__);
        expect(second_assist && second_assist->actual_total() == 0,
               "round two assist snapshot does not inherit round one", __LINE__);
        expect(model.round_view(BattlePhase::Main).actual_total() == 0 &&
                   model.round_view(BattlePhase::Assist).actual_total() == 0,
               "round eight starts with empty current-round views", __LINE__);
        expect(model.session(BattlePhase::Main).actual_total() == 1125 &&
                   model.session(BattlePhase::Assist).actual_total() == 119,
               "round snapshot rotation leaves session totals intact", __LINE__);

        // The killing hit changes BATTLE -> END inside the original callback.
        // Its damage keeps the pre-callback Main phase and must precede SessionEnd.
        g_callback_capture = &live;
        live.callbacks_enabled_ = true;
        live.host_active_.store(true);
        original_applied_modifier = [](void*, void* raw, void*) {
            auto* killing = static_cast<mock::ModifierData*>(raw);
            killing->real = 15LL << 32;
            mock::live_game_status.state = 8;
        };
        mock::ModifierData last_hit;
        last_hit.source = &actors[1];
        last_hit.type = 1;
        last_hit.shared = 1;
        const std::uint64_t before_last_hit = g_events.newest_sequence();
        NativeCapture::on_applied_modifier(nullptr, &last_hit, nullptr);
        const auto last_hit_events = events_after(before_last_hit);
        expect(count_kind(last_hit_events, EventKind::SessionEnd) == 0,
               "last-hit callback cannot publish SessionEnd before its damage", __LINE__);
        bool last_hit_seen = false;
        bool last_hit_is_last = false;
        for (const Event& event : last_hit_events) {
            if (event.kind == EventKind::Damage && event.actual_damage == 15) {
                last_hit_seen = true;
                last_hit_is_last = event.phase == BattlePhase::Main;
            }
            if (event.kind == EventKind::SessionEnd) last_hit_is_last = false;
        }
        expect(last_hit_seen && last_hit_is_last,
               "last-hit damage retains the pre-original Main phase", __LINE__);

        // Additional same-tick modifiers can observe END or SETTLE after the
        // first one switched the state. They must keep the recorded battle phase.
        mock::live_game_status.state = 7;
        expect(live.read_damage_phase() == BattlePhase::Main,
               "same-clock SETTLE reuses the last battle damage phase", __LINE__);
        mock::ModifierData settle_follow;
        settle_follow.source = &actors[2];
        settle_follow.type = 1;
        settle_follow.shared = 1;
        settle_follow.real = 9LL << 32;
        settle_follow.calculated = 9LL << 32;
        const std::uint64_t before_settle_follow = g_events.newest_sequence();
        live.record_damage(nullptr, &settle_follow, false);
        const auto settle_follow_events = events_after(before_settle_follow);
        bool settle_follow_seen = false;
        for (const Event& event : settle_follow_events) {
            if (event.kind == EventKind::Damage && event.actual_damage == 9)
                settle_follow_seen = event.phase == BattlePhase::Main;
            expect(event.kind != EventKind::SessionEnd,
                   "same-tick SETTLE modifier cannot publish SessionEnd", __LINE__);
        }
        expect(settle_follow_seen,
               "second END/SETTLE modifier retains the last battle phase", __LINE__);

        // Normal updater observes SETTLE with allow_session_end=true and clears
        // the same-tick phase cache. Later damage must not reuse an old phase.
        live.tick_runtime(true);
        expect(live.last_damage_phase_ == BattlePhase::Idle && live.last_damage_clock_ < 0,
               "SETTLE updater clears the last damage phase cache", __LINE__);
        const auto published_before_late = live.counters_.published;
        const std::uint64_t late_before = g_events.newest_sequence();
        live.record_damage(nullptr, &settle_follow, false);
        expect(live.counters_.published == published_before_late &&
                   count_kind(events_after(late_before), EventKind::Damage) == 0,
               "damage after SETTLE updater is rejected", __LINE__);

        mock::live_game_status.state = 8;
        // The next runtime tick observes END and publishes SessionEnd once.
        const std::uint64_t before_end = g_events.newest_sequence();
        live.tick_runtime(true);
        const auto end_events = events_after(before_end);
        expect(count_kind(end_events, EventKind::SessionEnd) == 1,
               "END emits exactly one SessionEnd after the last hit", __LINE__);
        mock::live_clock = 80;
        live.tick_runtime(true);
        expect(count_kind(events_after(before_end), EventKind::SessionEnd) == 1,
               "repeated END is idempotent", __LINE__);
        const auto outside_before = live.counters_.outside_mode;
        live.record_damage(nullptr, &settle_follow, false);
        expect(live.counters_.outside_mode == outside_before + 1,
               "damage after SessionEnd is rejected", __LINE__);
        original_applied_modifier = nullptr;
        live.callbacks_enabled_ = false;
        live.host_active_.store(false);
        g_callback_capture = nullptr;

        const std::uint64_t before_exit = g_events.newest_sequence();
        mock::manager.center = nullptr;
        live.tick_runtime(true);
        const auto exit_events = events_after(before_exit);
        expect(count_kind(exit_events, EventKind::SessionEnd) == 0,
               "manager teardown does not republish SessionEnd after END", __LINE__);
        expect(count_kind(exit_events, EventKind::ModeExit) == 1,
               "manager teardown still emits one ModeExit", __LINE__);
        expect(count_kind(events_after(0), EventKind::SessionEnd) == 1,
               "the whole timeline has exactly one SessionEnd", __LINE__);
        expect(!live.in_mode_ && !live.mode_present_ && live.controller_ == nullptr,
               "normal exit clears runtime state", __LINE__);
        const std::uint64_t after_exit = g_events.newest_sequence();
        live.tick_runtime(true);
        expect(events_after(after_exit).empty(), "repeated absent center is idempotent", __LINE__);

        // A manual reset request survives failed/absent-center ticks and is
        // consumed only after a successful replacement SessionStart.
        g_reset_requested.store(true);
        live.tick_runtime(true);
        expect(g_reset_requested.load(), "failed reset tick preserves the request", __LINE__);
        mock::manager.center = &mock::live_center;
        mock::nullable_manager_result = &mock::manager;
        mock::live_game_status.state = 3;
        live.tick_runtime(true);
        expect(!g_reset_requested.load() && live.in_mode_,
               "successful new SessionStart consumes the reset request", __LINE__);

        mock::manager.center = nullptr;
        mock::nullable_manager_result = nullptr;
        mock::use_live_roster = false;
        mock::live_center.player_model = nullptr;
        mock::live_center.indexer = nullptr;
        mock::live_center.settle = nullptr;
        mock::live_indexer.runtime_cards = nullptr;
        mock::live_indexer.inst_players = nullptr;
        mock::live_indexer.token_cards = nullptr;
        mock::live_settle.ending = 0;
        mock::live_player_list.items.clear();
        mock::live_player_model.by_slot.clear();
        g_events.clear();
        return failures;
    }

    static int run() {

        int failures = 0;
#define EXPECT(value) do { if (!(value)) { ++failures; std::fprintf(stderr, "FAIL capture %d: %s\n", __LINE__, #value); } } while (0)
        failures += match_intel_snapshot_contract();
        failures += hidden_sync_snapshot_contract();
        EXPECT(local_shop_reaches_counter());
        EXPECT(shop_catalog_resolves_protocol_names());
        EXPECT(boss_name_uses_game_data_and_enemy_handbook());
        ArkPluginApiV1 api{};
        NativeCapture capture(api);
        capture.runtime_ = mock::runtime();
        capture.character_class_ = &mock::character;
        capture.enemy_class_ = &mock::enemy_class;
        capture.entity_class_ = &mock::entity_class;
        capture.entity_player_side_ = [](void* e, void*) { return static_cast<mock::Entity*>(e)->player_side; };
        capture.modifier_target_ = [](void* m, void*) -> void* { return static_cast<mock::ModifierData*>(m)->target; };
        capture.modifier_player_side_ = [](void* m, void*) { return static_cast<mock::ModifierData*>(m)->player_side; };
        capture.projectile_class_ = &mock::projectile;
        capture.battle_character_data_class_ = &mock::character;
        capture.char_data_unique_field_ = &mock::unique;
        capture.char_data_token_field_ = &mock::is_token;
        capture.char_data_host_uid_field_ = &mock::host_uid;
        capture.character_card_uid_ = [](void* e, void*) { return static_cast<mock::Entity*>(e)->card; };
        capture.character_side_ = [](void* e, void*) { return static_cast<mock::Entity*>(e)->side; };
        capture.character_data_ = [](void* e, void*) -> void* { return static_cast<mock::Entity*>(e)->data; };
        capture.character_is_token_ = [](void* e, void*) {
            auto* data = static_cast<mock::Entity*>(e)->data;
            return data && data->token;
        };
        capture.character_id_ = [](void*, void*) -> void* { return nullptr; };
        capture.projectile_source_ = [](void* e, void*) -> void* { return static_cast<mock::Entity*>(e)->source; };
        mock::Dictionary cards, owners, tokens;
        cards.entries = {{7, 100}, {8, 101}, {9, 102}, {17, 103}};
        owners.entries = {{100, 2}, {101, 2}, {102, 2}, {103, 1}};
        for (int slot = 1; slot <= 4; ++slot) {
            cards.entries[100 + slot] = 1000 + slot;
            owners.entries[1000 + slot] = slot;
        }
        capture.runtime_card_root_ = il2cpp::Runtime::Root(capture.runtime_, &cards);
        capture.inst_player_root_ = il2cpp::Runtime::Root(capture.runtime_, &owners);
        capture.token_card_root_ = il2cpp::Runtime::Root(capture.runtime_, &tokens);
        capture.indexer_inst_player_field_ = &mock::indexer_inst_players;
        capture.indexer_runtime_card_field_ = &mock::indexer_runtime_cards;
        capture.runtime_cards_are_instances_ = false;
        capture.runtime_card_lookup_info_ = &mock::try_get;
        HostInfo host;
        host.unique_id = 7;
        host.id = "char_same";
        host.name = "干员甲";
        capture.hosts_[7] = host;
        host.unique_id = 17;
        capture.hosts_[17] = host;
        mock::Data token_data{8, true, 7}, nested_data{9, true, 8};
        mock::String actor_id, teammate_id;
        actor_id.value = L"char_same";
        teammate_id.value = L"char_same";
        mock::Entity actor{&mock::character, 7, 1, nullptr, nullptr, &actor_id};
        mock::Entity token{&mock::character, 8, 1, &token_data};
        mock::Entity nested{&mock::character, 9, 1, &nested_data};
        mock::Entity teammate{&mock::character, 17, 2, nullptr, nullptr, &teammate_id};
        mock::Entity enemy{&mock::character, 777, 2};
        mock::Entity projectiles[6]{};
        for (int i = 0; i < 6; ++i) {
            projectiles[i].klass = &mock::projectile;
            projectiles[i].source = i == 5 ? &token : &projectiles[i + 1];
        }
        int player = -1;
        std::uint32_t card = 0;
        EXPECT(capture.resolve_character(&actor, host, player, card));
        EXPECT(player == 2 && card == 7 && host.id == "char_same");
        EXPECT(capture.resolve_character(&token, host, player, card));
        EXPECT(player == 2 && card == 7 && host.name == "干员甲");
        EXPECT(capture.resolve_character(&projectiles[0], host, player, card));
        EXPECT(player == 2 && card == 7);
        HostInfo intermediate;
        intermediate.token = true;
        intermediate.host_uid = 7;
        capture.hosts_[8] = intermediate;
        EXPECT(capture.resolve_character(&nested, host, player, card));
        EXPECT(player == 2 && card == 7 && !host.token);
        EXPECT(capture.resolve_character(&teammate, host, player, card));
        EXPECT(player == 1 && card == 17);
        mock::Entity indexed_token{&mock::character, 88, 2, &token_data};
        capture.token_players_[88] = 2;
        EXPECT(capture.resolve_character(&indexed_token, host, player, card));
        EXPECT(player == 2 && card == 7); // token's own card UID is not its host UID
        EXPECT(!capture.resolve_character(&enemy, host, player, card));
        projectiles[5].source = &projectiles[0];
        EXPECT(!capture.resolve_character(&projectiles[0], host, player, card));
        projectiles[5].source = &token;

        mock::prepare_live_roster();
        mock::live_game_status.state = 4;
        mock::live_center.self = 1;
        mock::live_center.round = 0;
        mock::live_indexer.inst_players = &owners;
        mock::live_indexer.runtime_cards = &cards;
        capture.manager_instance_info_ = &mock::nullable_manager;
        capture.manager_class_ = &mock::manager_class;
        capture.auto_chess_center_class_ = &mock::center_class;
        capture.manager_center_field_ = &mock::manager_center;
        capture.data_center_player_model_field_ = &mock::center_player_model;
        capture.data_center_indexer_field_ = &mock::center_indexer;
        capture.data_center_settle_field_ = &mock::center_settle;
        capture.center_stage_seed_info_ = &mock::get_stage_seed;
        capture.center_round_info_ = &mock::get_round;
        capture.center_self_info_ = &mock::get_self;
        capture.center_battle_players_info_ = &mock::get_battle_players;
        capture.settle_ending_info_ = &mock::get_ending;
        capture.player_model_get_player_info_ = &mock::get_scene;
        capture.scene_dead_info_ = &mock::get_dead;
        capture.scene_index_field_ = &mock::scene_slot;
        capture.scene_hp_field_ = &mock::scene_hp;
        capture.scene_connect_field_ = &mock::scene_connect;
        capture.scene_finished_field_ = &mock::scene_finished;
        capture.scene_uid_field_ = &mock::scene_uid;
        capture.scene_card_field_ = &mock::scene_card;
        capture.card_nickname_field_ = &mock::card_nickname;
        capture.scene_player_data_class_ = &mock::scene_class;
        capture.indexer_inst_player_field_ = &mock::indexer_inst_players;
        capture.indexer_runtime_card_field_ = &mock::indexer_runtime_cards;
        capture.indexer_token_card_field_ = nullptr;
        capture.runtime_cards_are_instances_ = false;
        capture.runtime_card_lookup_info_ = &mock::try_get;
        capture.chess_class_ = &mock::chess_class;
        capture.chess_inst_id_field_ = &mock::chess_inst;
        capture.chess_card_uid_field_ = &mock::chess_card;
        capture.chess_player_field_ = &mock::chess_player;
        capture.battle_player_data_class_ = &mock::player_data_class;
        capture.battle_characters_field_ = &mock::battle_characters;
        capture.battle_tokens_field_ = &mock::battle_tokens;
        capture.battle_player_side_field_ = &mock::battle_side;
        capture.player_side_invalid_ = 0;
        capture.battle_character_data_class_ = &mock::character;
        capture.char_data_unique_field_ = &mock::unique;
        capture.char_data_token_field_ = &mock::is_token;
        capture.char_data_host_uid_field_ = &mock::host_uid;
        capture.controller_class_ = &mock::controller_class;
        capture.controller_state_field_ = &mock::controller_state;
        capture.controller_player_list_ = [](void*, void*) -> void* { return &mock::live_player_list; };
        capture.controller_clock_ = [](void*) -> std::int64_t {
            return static_cast<std::int64_t>(mock::live_clock * 4294967296.0);
        };
        capture.bind_timing();
        mock::live_controller.state = 2;
        capture.set_controller(&mock::live_controller);
        mock::live_clock = 0;
        capture.tick_runtime(true);
        EXPECT(capture.in_mode_ && capture.manager_root_.get() == &mock::manager &&
                   capture.center_root_.get() == &mock::live_center);
        EXPECT(capture.diagnostic_game_state_ == 4 && capture.timing_mask_ == 30);
        capture.session_id_ = capture.session_id_ ? capture.session_id_ : 1;
        capture.current_round_ = 1;
        capture.last_clock_ = 0;
        HostInfo cached_host;
        cached_host.unique_id = 7;
        cached_host.id = "char_same";
        cached_host.name = "干员甲";
        capture.hosts_[7] = cached_host;
        cached_host.unique_id = 17;
        capture.hosts_[17] = cached_host;
        capture.set_controller(&mock::live_controller);
        capture.controller_clock_info_ = &tokens;
        capture.controller_clock_ = [](void* info) -> std::int64_t { mock::clock_argument = info; return 10LL << 32; };
        capture.modifier_source_ = [](void* m, void*) -> void* { return static_cast<mock::ModifierData*>(m)->source; };
        capture.modifier_original_source_ = [](void* m, void*) -> void* { return static_cast<mock::ModifierData*>(m)->original_source; };
        capture.modifier_is_cancelled_ = [](void* m, void*) { return static_cast<mock::ModifierData*>(m)->cancelled; };
        capture.modifier_is_damage_ = [](void* m, void*) { return static_cast<mock::ModifierData*>(m)->type >= 1 && static_cast<mock::ModifierData*>(m)->type <= 3; };
        capture.modifier_is_heal_ = [](void* m, void*) { return static_cast<mock::ModifierData*>(m)->type == 4; };
        capture.modifier_is_element_ = [](void* m, void*) { return static_cast<mock::ModifierData*>(m)->type == 5; };
        capture.modifier_final_delta_ = [](void* m, void*) { return static_cast<mock::ModifierData*>(m)->calculated; };
        capture.modifier_damage_type_ = [](void* m, void*) { return static_cast<mock::ModifierData*>(m)->type; };
        capture.real_delta_offset_ = offsetof(mock::ModifierData, real);
        capture.shared_mask_offset_ = offsetof(mock::ModifierData, shared);
        const auto verify_damage = [&] {
        mock::ModifierData modifier;
        modifier.source = &projectiles[0];
        g_events.clear();
        // Identical stack address/value on successive DoT ticks are distinct hits.
        capture.record_damage(nullptr, &modifier, false);
        capture.record_damage(nullptr, &modifier, false);
        modifier.source = &teammate;
        capture.record_damage(nullptr, &modifier, false);
        modifier.source = &enemy;
        capture.record_damage(nullptr, &modifier, false);
        modifier.source = &token;
        modifier.shared = 32;
        capture.record_damage(nullptr, &modifier, false);
        EXPECT(mock::clock_argument == &tokens);
        Event events[16]{};
        bool overrun = false, partial = false;
        const auto count = g_events.read(0, events, 16, &overrun, &partial);
        std::vector<Event> queued(events, events + count);
        std::vector<Event> damage_events;
        for (const Event& event : queued) {
            if (event.kind == EventKind::Damage || event.kind == EventKind::ElementalBuildup)
                damage_events.push_back(event);
        }
        EXPECT(damage_events.size() == 3);
        StatsModel model;
        Event start;
        start.kind = EventKind::SessionStart;
        start.session_id = capture.session_id_;
        model.apply(start);
        for (const Event& event : queued) {
            model.apply(event);
            model.apply(event); // replaying the same queued event is idempotent
        }
        for (const Event& event : damage_events) EXPECT(event.continuous == 1);
        EXPECT(model.session().actual_total() == 300);
        EXPECT(model.session().players.at(2).operators.at("char_same").totals.actual == 200);
        EXPECT(model.session().players.at(2).operators.at("char_same").instances.size() == 1);
        EXPECT(model.session().players.at(1).operators.at("char_same").totals.actual == 100);
        };

        verify_damage();

        // The current client maps card UID -> reference ChessInst -> instance owner.
        mock::Chess chess[] = {
            {&mock::chess_class, 100, 7, 2}, {&mock::chess_class, 101, 8, 2},
            {&mock::chess_class, 102, 9, 2}, {&mock::chess_class, 103, 17, 1},
            {&mock::chess_class, 104, 18, 0}, {&mock::chess_class, 105, 19, 3}};
        mock::ReferenceDictionary current_cards;
        for (auto& item : chess) current_cards.entries[item.card] = &item;
        owners.entries[104] = 0;
        owners.entries[105] = 3;
        capture.runtime_cards_are_instances_ = true;
        capture.runtime_card_lookup_info_ = &mock::try_get_reference;
        capture.chess_class_ = &mock::chess_class;
        capture.chess_inst_id_field_ = &mock::chess_inst;
        capture.chess_card_uid_field_ = &mock::chess_card;
        capture.chess_player_field_ = &mock::chess_player;
        capture.runtime_card_root_ = il2cpp::Runtime::Root(capture.runtime_, &current_cards);
        capture.token_card_root_ = il2cpp::Runtime::Root{};
        capture.token_players_.clear();
        capture.attribution_cache_.clear();
        EXPECT(capture.player_for_card(7) == 2);
        EXPECT(capture.player_for_card(17) == 1);
        EXPECT(capture.player_for_card(18) == 0);
        EXPECT(capture.player_for_card(19) == 3);
        EXPECT(capture.player_for_card(999) == kUnknownPlayer);
        current_cards.entries[99] = nullptr;
        EXPECT(capture.player_for_card(99) == kUnknownPlayer);
        chess[0].card = 99;
        EXPECT(capture.player_for_card(7) == kUnknownPlayer);
        chess[0].card = 7;
        chess[0].player = 0;
        EXPECT(capture.player_for_card(7) == kUnknownPlayer);
        chess[0].player = 2;
        owners.entries.erase(100);
        EXPECT(capture.player_for_card(7) == kUnknownPlayer);
        owners.entries[100] = 2;

        verify_damage();

        // Exercise actual wrappers: nested logger paths must not double-count,
        // while a different modifier logged during completion is still captured.
        g_callback_capture = &capture;
        capture.callbacks_enabled_ = true;
        capture.host_active_.store(true);
        original_applied_modifier = [](void*, void* m, void*) {
            auto* modifier = static_cast<mock::ModifierData*>(m);
            modifier->real = 75LL << 32; // Completion must read after original.
            NativeCapture::on_log_modifier(nullptr, m, nullptr);
            NativeCapture::on_log_ep_modifier(nullptr, m, nullptr);
            auto secondary = *modifier;
            secondary.real = 25LL << 32;
            NativeCapture::on_log_modifier(nullptr, &secondary, nullptr);
        };
        mock::ModifierData completion;
        completion.source = &actor;
        g_events.clear();
        NativeCapture::on_applied_modifier(&enemy, &completion, nullptr);
        NativeCapture::on_applied_modifier(&enemy, &completion, nullptr);
        Event captured[32]{};
        bool overrun = false, partial = false;
        auto count = g_events.read(0, captured, 32, &overrun, &partial);
        EXPECT(count == 4 && capture.counters_.suppressed == 4);
        EXPECT(captured[0].actual_damage == 25 && captured[1].actual_damage == 75);
        EXPECT(captured[2].actual_damage == 25 && captured[3].actual_damage == 75);
        EXPECT(capture.applied_modifiers_.empty() && capture.callbacks_in_flight_ == 0);

        // A completion implementation that never calls the logger (e.g. XLua)
        // still yields one hit. A failed original does not publish a fake hit.
        original_applied_modifier = [](void*, void*, void*) {};
        g_events.clear();
        NativeCapture::on_applied_modifier(&enemy, &completion, nullptr);
        EXPECT(g_events.read(0, captured, 32, &overrun, &partial) == 1);
        original_applied_modifier = [](void*, void*, void*) { throw std::runtime_error("mock original failure"); };
        bool threw = false;
        try { NativeCapture::on_applied_modifier(&enemy, &completion, nullptr); }
        catch (const std::exception&) { threw = true; }
        EXPECT(threw && capture.applied_modifiers_.empty() && capture.callbacks_in_flight_ == 0);
        EXPECT(g_events.read(0, captured, 32, &overrun, &partial) == 1);
        original_applied_modifier = nullptr;

        // Elemental/healing channels and diagnostic filters use the same
        // production path as damage. A missing immediate source can use origin.
        g_events.clear();
        completion.type = 5;
        capture.record_damage(nullptr, &completion, false, true);
        completion.type = 4;
        capture.record_damage(nullptr, &completion, false, true);
        completion.type = 1;
        completion.source = nullptr;
        completion.original_source = &actor;
        capture.record_damage(nullptr, &completion, false, true);
        completion.cancelled = true;
        capture.record_damage(nullptr, &completion, false, true);
        completion.cancelled = false;
        completion.type = 0;
        capture.record_damage(nullptr, &completion, false, true);
        completion.type = 1;
        completion.real = completion.calculated = 0;
        capture.record_damage(nullptr, &completion, false, true);
        count = g_events.read(0, captured, 32, &overrun, &partial);
        EXPECT(count == 3 && captured[0].kind == EventKind::ElementalBuildup && captured[1].damage_type == 4);
        EXPECT(capture.counters_.original_source > 0 && capture.counters_.cancelled == 1);
        EXPECT(capture.counters_.wrong_type == 1 && capture.counters_.zero == 1);

        // Additional effects may retain a non-null projectile whose source has
        // expired. Its verified original source must still receive the hit.
        mock::Entity orphan_projectile{&mock::projectile};
        completion.real = 75LL << 32;
        completion.calculated = 150LL << 32;
        completion.source = &orphan_projectile;
        completion.original_source = &actor;
        g_events.clear();
        capture.record_damage(nullptr, &completion, false, true);
        auto recovered = events_after(0);
        EXPECT(count_kind(recovered, EventKind::Damage) == 1);
        if (count_kind(recovered, EventKind::Damage) == 1) {
            const auto hit = std::find_if(recovered.begin(), recovered.end(), [](const Event& event) {
                return event.kind == EventKind::Damage;
            });
            EXPECT(hit->player_index == 2 && hit->card_uid == 7);
            EXPECT(hit->actual_damage == 75 && text(hit->actor_id) == "char_same");
        }

        // Recovered origin is secondary evidence, not permission to steal a
        // complete hit from another player or from a known direct operator.
        completion.source = &actor;
        completion.original_source = &teammate;
        g_events.clear();
        capture.record_damage(nullptr, &completion, false, true);
        auto hits = [&] {
            auto events = events_after(0);
            events.erase(std::remove_if(events.begin(), events.end(), [](const Event& e) {
                return e.kind != EventKind::Damage;
            }), events.end());
            return events;
        };
        EXPECT(hits().size() == 1 && hits()[0].player_index == 2 && hits()[0].card_uid == 7);

        // A getter failure for one candidate must not prevent the safe origin
        // from being tried. No setter, game command or object creation is used.
        capture.projectile_source_ = [](void* object, void*) -> void* {
            if (!static_cast<mock::Entity*>(object)->source)
                throw std::runtime_error("expired projectile getter");
            return static_cast<mock::Entity*>(object)->source;
        };
        completion.source = &orphan_projectile;
        completion.original_source = &actor;
        g_events.clear();
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().size() == 1 && hits()[0].player_index == 2 && capture.counters_.source_errors > 0);
        capture.projectile_source_ = [](void* e, void*) -> void* { return static_cast<mock::Entity*>(e)->source; };

        mock::Entity actual_enemy{&mock::enemy_class, 777, 2};
        mock::Entity anonymous_effect{&mock::entity_class};
        anonymous_effect.player_side = 1011; // Verified BattlePlayerData side -> P1.
        EXPECT(capture.side_players_.at(1011) == 1 && capture.side_players_.at(1012) == 2);
        completion = {};
        completion.real = 75LL << 32;
        completion.target = &actual_enemy;
        completion.player_side = 1012; // Must NOT turn a null source into P2.
        g_events.clear();
        capture.record_damage(nullptr, &completion, false, true);
        capture.record_damage(nullptr, &completion, false, true); // Same-value next DoT tick.
        EXPECT(hits().size() == 2);
        for (const auto& e : hits()) {
            EXPECT(e.player_index == kUnknownPlayer && e.card_uid == 0);
            EXPECT(text(e.actor_id) == "unknown_effect" && e.actual_damage == 75 && e.continuous == 1);
        }
        // The explicit Enemy type is essential: a right-hand friendly operator
        // has side==ENEMY too and must not be treated as an enemy target.
        completion.target = &teammate;
        g_events.clear();
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().empty());
        completion.target = nullptr;
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().empty());
        completion.target = &actual_enemy;
        completion.source = &actual_enemy;
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().empty()); // Explicit hostile/self damage is not player output.
        completion.original_source = &actor;
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().empty()); // A friendly origin cannot re-label an Enemy attacker.
        mock::Entity hostile_projectile{&mock::projectile};
        hostile_projectile.source = &actual_enemy;
        completion.source = &hostile_projectile;
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().empty()); // Preserve the same rule through a projectile chain.
        completion.original_source = nullptr;

        // A real effect Entity's own side, unlike Modifier.playerSide, can
        // identify a player through the sampled and conflict-checked card map.
        completion.source = &anonymous_effect;
        g_events.clear();
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().size() == 1 && hits()[0].player_index == 1);
        EXPECT(text(hits()[0].actor_id) == "unknown_effect" && hits()[0].card_uid == 0);
        anonymous_effect.player_side = 4999;
        g_events.clear();
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().size() == 1 && hits()[0].player_index == kUnknownPlayer);

        // Environment flag alone no longer drops a proven enemy HP loss. A
        // null-source flagged hit stays unknown, never assigned to self/P1.
        completion.source = &actor;
        completion.shared = 32;
        g_events.clear();
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().size() == 1 && hits()[0].card_uid == 7 && hits()[0].player_index == 2);
        completion.source = nullptr;
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().size() == 2 && hits()[1].player_index == kUnknownPlayer);
        completion.target = &teammate;
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().size() == 2);
        completion.target = &actual_enemy;
        completion.type = 4;
        capture.record_damage(nullptr, &completion, false, true);
        completion.type = 5;
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().size() == 2);

        // Applied self is the authoritative target, not a potentially stale
        // Modifier target getter. A nested logger still publishes only once.
        completion.type = 1;
        completion.shared = 1;
        completion.target = &teammate;
        original_applied_modifier = [](void*, void* m, void*) { NativeCapture::on_log_modifier(nullptr, m, nullptr); };
        g_events.clear();
        NativeCapture::on_applied_modifier(&actual_enemy, &completion, nullptr);
        EXPECT(hits().size() == 1 && hits()[0].player_index == kUnknownPlayer);
        EXPECT(capture.applied_modifiers_.empty());
        original_applied_modifier = nullptr;

        // A known player's incomplete token host cannot be replaced by the
        // different player's originalSource. Preserve an explicit missing host.
        mock::Data missing_token_data{19, true, 6000};
        mock::Entity missing_token{&mock::character, 19, 1, &missing_token_data};
        completion = {};
        completion.source = &missing_token;
        completion.original_source = &teammate;
        g_events.clear();
        capture.record_damage(nullptr, &completion, false, true);
        EXPECT(hits().size() == 1 && hits()[0].player_index == 3 && hits()[0].card_uid == 0);
        EXPECT(text(hits()[0].actor_id) == "unknown_host");

        // Diagnostics retain only type names, flags and slots, never account
        // UIDs/nickname suffixes, and keep an unresolved sample across good hits.
        capture.snapshot_diagnostics();
        EXPECT(g_diagnostics.find("environment_recovered=") != std::string::npos);
        EXPECT(g_diagnostics.find("last_unresolved_modifier=") != std::string::npos);
        EXPECT(g_diagnostics.find("private-test-UID") == std::string::npos);

        // Read the game's death getter, never infer death from hp/gameFinished.
        mock::use_live_roster = false;
        mock::scene.slot = 0;
        mock::scene.connection = 1;
        mock::scene.dead = false;
        mock::scene.finished = false;
        capture.scene_index_field_ = &mock::scene_slot;
        capture.scene_hp_field_ = &mock::scene_hp;
        capture.scene_connect_field_ = &mock::scene_connect;
        capture.scene_finished_field_ = &mock::scene_finished;
        capture.scene_uid_field_ = &mock::scene_uid;
        capture.scene_card_field_ = &mock::scene_card;
        capture.scene_dead_info_ = &mock::get_dead;
        capture.player_model_get_player_info_ = &mock::get_scene;
        PlayerInfo info;
        int hp = 0, connection = 0;
        bool finished = false;
        EXPECT(capture.read_player(&actor, 0, info, hp, connection, finished));
        EXPECT(hp == 0 && !info.dead && !info.game_finished);
        mock::scene.finished = true;
        mock::scene.hp = 100;
        EXPECT(capture.read_player(&actor, 0, info, hp, connection, finished) && !info.dead);
        mock::scene.dead = true;
        EXPECT(capture.read_player(&actor, 0, info, hp, connection, finished) && info.dead);

        // Exporting outside a match must retain its latest useful sample, and
        // a genuinely absent manager/dataCenter is a normal, idempotent exit.
        capture.manager_instance_info_ = &mock::nullable_manager;
        capture.manager_class_ = &mock::manager_class;
        capture.auto_chess_center_class_ = &mock::center_class;
        capture.manager_center_field_ = &mock::manager_center;
        capture.self_index_ = 2;
        capture.mode_present_ = true;
        capture.active_mask_ = capture.simulated_mask_ = 15;
        info.index = 2;
        info.uid = "private-test-UID";
        info.name = "private-test-nickname";
        capture.last_players_.clear();
        capture.missing_player_ticks_.clear();
        capture.last_players_[2] = info;
        capture.snapshot_diagnostics();
        const auto previous = capture.last_in_mode_diagnostics_;
        EXPECT(previous.find("roster=1") != std::string::npos);
        capture.battle_active_ = true;
        capture.last_clock_ = 10;
        capture.end_battle();
        const auto combat = capture.last_combat_diagnostics_;
        EXPECT(combat.find("battle_active=1") != std::string::npos);
        g_events.clear();
        mock::manager.center = nullptr;
        mock::nullable_manager_result = &mock::manager;
        capture.tick_runtime(true);
        EXPECT(!capture.in_mode_ && !capture.mode_present_ && capture.controller_ == nullptr);
        EXPECT(capture.controller_root_.get() == nullptr && get_error().empty());
        const auto report = capture_diagnostics();
        const auto retained = capture.last_in_mode_diagnostics_;
        EXPECT(report.find("CURRENT\nmode_present=0 capture_active=0") != std::string::npos);
        EXPECT(report.find("LAST IN-MODE SNAPSHOT") != std::string::npos);
        EXPECT(report.find("roster=1") != std::string::npos);
        EXPECT(report.find("LAST COMBAT SNAPSHOT") != std::string::npos);
        EXPECT(capture.last_combat_diagnostics_ == combat);
        EXPECT(report.find("private-test-UID") == std::string::npos);
        EXPECT(report.find("private-test-nickname") == std::string::npos);
        const auto exit_count = g_events.read(0, captured, 32, &overrun, &partial);
        EXPECT(exit_count == 2 && captured[1].kind == EventKind::ModeExit);
        mock::nullable_manager_result = &mock::manager; // Manager exists; center still absent.
        capture.tick_runtime(true);
        EXPECT(g_events.read(0, captured, 32, &overrun, &partial) == exit_count);
        EXPECT(capture.last_in_mode_diagnostics_ == retained && get_error().empty());
        mock::nullable_manager_result = nullptr;
        g_callback_capture = nullptr;
        capture.host_active_.store(false);
        capture.callbacks_enabled_ = false;
        capture.in_mode_ = true;

        capture.attribution_cache_.erase(8);
        token_data.host = 999;
        capture.token_players_[8] = 2;
        EXPECT(capture.resolve_character(&token, host, player, card));
        EXPECT(host.id == "unknown_host" && card == 0);
        capture.set_controller(nullptr);
        // No real hooks were installed; roots remain valid until capture destruction.
        g_events.clear();

        failures += full_tick_runtime_contract();

        std::printf("%s capture: legacy/current maps, full four-player tick lifecycle, settlement tolerance, raw round mapping, isolated round snapshots, idempotent exit, tokens, DoT, filters, clock ABI\n",
            failures ? "FAIL" : "PASS");
        return failures;
    }
};
} // namespace ark::stronghold

int main() {
    try { return ark::stronghold::CaptureTests::run() ? 1 : 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL capture exception: %s\n", error.what()); return 1; }
}
