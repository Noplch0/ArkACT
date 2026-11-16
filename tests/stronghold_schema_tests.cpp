// Replay metadata from the user's client through the production schema binder.
// No game process or gameplay code is loaded. Closed singleton methods absent
// from the report are explicit supplements to the recorded metadata.
#include "../stronghold/capture.cpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ark::stronghold {
namespace schema {
struct Klass;
void native_stub() {}
struct Field { std::string name; Klass* type; int offset; int enum_value = -1; };
struct Method {
    void* code = reinterpret_cast<void*>(&native_stub);
    std::string name;
    Klass* owner;
    Klass* result;
    std::vector<Klass*> args;
    bool is_static;
};
struct Klass {
    std::string full, ns, name, parent_name;
    Klass* parent = nullptr;
    int bytes = 0;
    std::vector<Field> fields;
    std::vector<Method> methods;
    std::vector<Klass*> nested;
};
std::map<std::string, Klass> classes;
Klass* type(const std::string& name) {
    auto [it, added] = classes.try_emplace(name);
    auto& c = it->second;
    if (added) {
        c.full = name;
        const auto dot = name.rfind('.');
        c.ns = dot == std::string::npos ? "" : name.substr(0, dot);
        c.name = dot == std::string::npos ? name : name.substr(dot + 1);
        if (name == "System.Boolean" || name == "System.Byte") c.bytes = 1;
        if (name == "System.Int32" || name == "System.UInt32" || name == "System.Single") c.bytes = 4;
        // SideType was omitted by the 2.0.4 report; its int enum ABI comes from
        // the supplied dump, not from this runtime metadata recording.
        if (name == "Torappu.Battle.SideType") c.bytes = 4;
        if (name == "System.Int64" || name == "System.UInt64" || name == "Torappu.FP") c.bytes = 8;
    }
    return &c;
}
std::vector<std::string> parameters(const std::string& text) {
    std::vector<std::string> result;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] == '<') ++depth;
        if (i < text.size() && text[i] == '>') --depth;
        if (i == text.size() || (text[i] == ',' && depth == 0)) {
            auto part = text.substr(start, i - start);
            const auto first = part.find_first_not_of(' ');
            if (first != std::string::npos) result.push_back(part.substr(first));
            start = i + 1;
        }
    }
    return result;
}
void read_report(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open recorded client schema");
    Klass* current = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("CLASS ")) {
            const auto left = line.find('['), right = line.find(']');
            if (left == std::string::npos || right == std::string::npos) continue;
            current = type(line.substr(left + 1, right - left - 1));
            const auto display = line.substr(6, left - 7);
            const auto dot = display.rfind('.');
            current->ns = display.substr(0, dot);
            current->name = display.substr(dot + 1);
            if (right + 4 < line.size()) current->parent_name = line.substr(right + 4);
        } else if (line.starts_with("COLLECTION ")) {
            current = type(line.substr(11));
        } else if (line.starts_with("FIELD ") && line.find(" value=") != std::string::npos &&
                   line.find(" : ") == std::string::npos) {
            const auto valueAt = line.find(" value=");
            const auto qualified = line.substr(6, valueAt - 6);
            const auto dot = qualified.rfind('.');
            if (dot == std::string::npos) continue;
            const auto found = classes.find(qualified.substr(0, dot));
            if (found == classes.end()) continue;
            const auto fieldName = qualified.substr(dot + 1);
            for (auto& field : found->second.fields) {
                if (field.name == fieldName) {
                    field.enum_value = std::stoi(line.substr(valueAt + 7));
                    break;
                }
            }
        } else if (current && line.starts_with(" FIELD ")) {
            const auto colon = line.find(" : "), offset = line.find(" offset=");
            if (colon == std::string::npos || offset == std::string::npos) continue;
            auto* value = type(line.substr(colon + 3, offset - colon - 3));
            int enumValue = -1;
            const auto valueAt = line.find(" value=", offset);
            if (valueAt != std::string::npos) enumValue = std::stoi(line.substr(valueAt + 7));
            current->fields.push_back({line.substr(7, colon - 7), value,
                std::stoi(line.substr(offset + 8), nullptr, 16), enumValue});
        } else if (current && line.starts_with(" METHOD ")) {
            const bool stat = line.starts_with(" METHOD static ");
            const std::size_t start = stat ? 15 : 8;
            const auto space = line.find(' ', start), paren = line.find('(', space);
            if (paren == std::string::npos) continue;
            Method method;
            method.name = line.substr(space + 1, paren - space - 1);
            method.owner = current;
            method.result = type(line.substr(start, space - start));
            method.is_static = stat;
            for (const auto& p : parameters(line.substr(paren + 1, line.size() - paren - 2)))
                method.args.push_back(type(p));
            current->methods.push_back(std::move(method));
        }
    }
    for (auto& [name, c] : classes) {
        if (!c.parent_name.empty()) c.parent = type(c.parent_name);
        if (c.parent_name == "System.Enum") {
            c.bytes = 4;
            for (const auto& f : c.fields) if (f.name == "value__") c.bytes = f.type->bytes;
        }
        if (name == "Torappu.Battle.Modifier") c.bytes = 1024; // Only bounds-checking, not a claimed runtime size.
        const std::string outer = "Torappu.Battle.AutoChess.AutoChessPlayerDataModel";
        if (name == outer + ".ScenePlayerData") type(outer)->nested.push_back(&c);
    }
}
template <class T> void* next(std::vector<T>& items, void** iterator) {
    const auto i = reinterpret_cast<std::uintptr_t>(*iterator);
    if (i >= items.size()) return nullptr;
    *iterator = reinterpret_cast<void*>(i + 1);
    if constexpr (std::is_pointer_v<T>) return items[i];
    else return &items[i];
}
il2cpp::Runtime runtime() {
    il2cpp::Runtime r;
    r.domain_get = []() -> void* { return &classes; };
    r.domain_assemblies = [](void*, std::size_t* n) -> void** { static void* a = &classes; *n = 1; return &a; };
    r.assembly_image = [](void* a) { return a; };
    r.class_from_name = [](void*, const char* ns, const char* name) -> void* {
        const std::string full = std::string(ns) + "." + name;
        const auto it = classes.find(full);
        return it == classes.end() ? nullptr : &it->second;
    };
    r.class_parent = [](void* c) -> void* { return static_cast<Klass*>(c)->parent; };
    r.class_name = [](void* c) { return static_cast<Klass*>(c)->name.c_str(); };
    r.class_namespace = [](void* c) { return static_cast<Klass*>(c)->ns.c_str(); };
    r.class_type = [](void* c) { return c; };
    r.class_from_type = [](void* c) { return c; };
    r.class_is_value = [](void* c) { return static_cast<Klass*>(c)->bytes != 0; };
    r.class_value_size = [](void* c, std::uint32_t*) { return static_cast<Klass*>(c)->bytes; };
    r.class_nested = [](void* c, void** i) { return next(static_cast<Klass*>(c)->nested, i); };
    r.class_fields = [](void* c, void** i) { return next(static_cast<Klass*>(c)->fields, i); };
    r.class_field = [](void* c, const char* name) -> void* {
        for (auto& f : static_cast<Klass*>(c)->fields) if (f.name == name) return &f;
        return nullptr;
    };
    r.field_name = [](void* f) { return static_cast<Field*>(f)->name.c_str(); };
    r.field_type = [](void* f) -> void* { return static_cast<Field*>(f)->type; };
    r.field_static_get = [](void* f, void* out) {
        const auto& field = *static_cast<Field*>(f);
        const auto& name = field.name;
        if (field.enum_value >= 0) *static_cast<int*>(out) = field.enum_value;
        else if (name == "ONE") *static_cast<std::int64_t*>(out) = 4294967296LL;
        else if (name == "IS_CONTINUOUS_FLAG") *static_cast<int*>(out) = 1;
        else if (name == "IS_ENVIRONMENT_DAMAGE") *static_cast<int*>(out) = 32;
        else if (name == "ONLINE") *static_cast<int*>(out) = 1;
        else if (name == "MISS_CONN" || name == "PLAYING") *static_cast<int*>(out) = 2;
        else if (name == "QUIT" || name == "FINISHED") *static_cast<int*>(out) = 3;
        else throw std::runtime_error("Unmocked static field");
    };
    r.class_methods = [](void* c, void** i) { return next(static_cast<Klass*>(c)->methods, i); };
    r.method_name = [](void* m) { return static_cast<Method*>(m)->name.c_str(); };
    r.method_count = [](void* m) { return static_cast<std::uint32_t>(static_cast<Method*>(m)->args.size()); };
    r.method_param = [](void* m, std::uint32_t i) -> void* { return static_cast<Method*>(m)->args.at(i); };
    r.method_return = [](void* m) -> void* { return static_cast<Method*>(m)->result; };
    r.method_flags = [](void* m, std::uint32_t*) -> std::uint32_t { return static_cast<Method*>(m)->is_static ? 0x10u : 0u; };
    r.type_name = [](void* t) { return _strdup(static_cast<Klass*>(t)->full.c_str()); };
    r.free_memory = [](void* p) { std::free(p); };
    return r;
}
} // namespace schema

struct CaptureTests {
    static int run() {
        int failures = 0;
#define EXPECT(value) do { if (!(value)) { ++failures; std::fprintf(stderr, "FAIL schema %d: %s\n", __LINE__, #value); } } while (0)
        const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
        schema::read_report(root / "tests/fixtures/client-schema.txt");
        schema::read_report(root / "tests/fixtures/stronghold-status-schema.txt");
        schema::read_report(root / "tests/fixtures/stronghold-source-schema.txt");
        // The current report includes Entity and collection signatures. The
        // inherited closed generic singleton still needs this explicit
        // supplement, checked in the current client's metadata/native code.
        auto* entity = schema::type("Torappu.Battle.Entity");
        auto* bobject = schema::type("Torappu.Battle.BObject");
        auto* unit = schema::type("Torappu.Battle.Unit");
        auto* character = schema::type("Torappu.Battle.Character");
        auto* enemy = schema::type("Torappu.Battle.Enemy");
        auto* token = schema::type("Torappu.Battle.Token");
        auto* trap = schema::type("Torappu.Battle.Trap");
        auto* modifier = schema::type("Torappu.Battle.Modifier");
        auto* battlePlayerData = schema::type("Torappu.Battle.BattlePlayerData");
        const auto findMethod = [](schema::Klass* klass, const char* name) -> schema::Method* {
            for (auto& method : klass->methods) if (method.name == name) return &method;
            return nullptr;
        };
        const auto findField = [](schema::Klass* klass, const char* name) -> schema::Field* {
            for (auto& field : klass->fields) if (field.name == name) return &field;
            return nullptr;
        };
        auto* entityPlayerSide = findMethod(bobject, "get_playerSide");
        auto* modifierTarget = findMethod(modifier, "get_target");
        auto* modifierPlayerSide = findMethod(modifier, "get_playerSide");
        auto* battlePlayerSide = findField(battlePlayerData, "playerSide");
        auto* invalidSide = findField(schema::type("Torappu.PlayerSide"), "INVALID");
        EXPECT(entity->parent == bobject && unit->parent == entity && character->parent == unit);
        EXPECT(enemy->parent == unit && token->parent == character && trap->parent == token);
        EXPECT(entityPlayerSide && !entityPlayerSide->is_static && entityPlayerSide->args.empty() &&
               entityPlayerSide->result == schema::type("Torappu.PlayerSide"));
        EXPECT(modifierTarget && !modifierTarget->is_static && modifierTarget->args.empty() &&
               modifierTarget->result == entity);
        EXPECT(modifierPlayerSide && !modifierPlayerSide->is_static &&
               modifierPlayerSide->args.empty() &&
               modifierPlayerSide->result == schema::type("Torappu.PlayerSide"));
        EXPECT(battlePlayerSide && battlePlayerSide->offset == 0x10 &&
               battlePlayerSide->type == schema::type("Torappu.PlayerSide"));
        EXPECT(invalidSide && invalidSide->enum_value == 0);

        EXPECT(std::any_of(entity->methods.begin(), entity->methods.end(), [](const auto& method) {
            return method.name == "OnAppliedModifier" && !method.is_static && method.args.size() == 1 &&
                   method.args.front()->full == "Torappu.Battle.Modifier&";
        }));
        auto* manager = schema::type("Torappu.Battle.AutoChess.AutoChessGameManager");
        auto* singleton = schema::type("Mock.ClosedManagerSingleton");
        manager->parent = singleton;
        singleton->methods.push_back({reinterpret_cast<void*>(&schema::native_stub), "get_instanceOrNull", singleton,
            manager, {}, true});
        ArkPluginApiV1 api{};
        int hook_count = 0;
        api.owner = &hook_count;
        api.create_native_hook = [](void* owner, void*, void*, void** original) -> std::uint64_t {
            *original = reinterpret_cast<void*>(&schema::native_stub);
            return ++*static_cast<int*>(owner);
        };
        api.enable_native_hook = [](void*, std::uint64_t, bool) { return true; };
        api.remove_native_hook = [](void*, std::uint64_t) { return true; };
        NativeCapture capture(api);
        capture.runtime_ = schema::runtime();
        capture.field_offset_ = [](void* f) { return static_cast<schema::Field*>(f)->offset; };
        capture.bind_classes();
        capture.bind_fields();
        capture.bind_getters();
        EXPECT(capture.enemy_class_ == enemy);
        EXPECT(capture.modifier_target_info_ == modifierTarget);
        EXPECT(capture.modifier_player_side_info_ == modifierPlayerSide);
        EXPECT(capture.entity_player_side_info_ == entityPlayerSide);
        EXPECT(capture.battle_player_side_field_ == battlePlayerSide);
        EXPECT(capture.player_side_invalid_ == 0);

        auto* goodEntityParent = entity->parent;
        entity->parent = nullptr;
        bool inheritedGetterRejected = false;
        try { capture.bind_getters(); } catch (const std::exception&) { inheritedGetterRejected = true; }
        EXPECT(inheritedGetterRejected);
        entity->parent = goodEntityParent;
        capture.bind_getters();

        auto* goodTargetType = modifierTarget->result;
        modifierTarget->result = schema::type("System.Int32");
        bool targetTypeRejected = false;
        try { capture.bind_getters(); } catch (const std::exception&) { targetTypeRejected = true; }
        EXPECT(targetTypeRejected);
        modifierTarget->result = goodTargetType;
        capture.bind_getters();

        auto* goodModifierSideType = modifierPlayerSide->result;
        modifierPlayerSide->result = schema::type("System.Int32");
        bool modifierSideRejected = false;
        try { capture.bind_getters(); } catch (const std::exception&) { modifierSideRejected = true; }
        EXPECT(modifierSideRejected);
        modifierPlayerSide->result = goodModifierSideType;
        capture.bind_getters();

        auto* goodEntitySideType = entityPlayerSide->result;
        entityPlayerSide->result = schema::type("System.Int32");
        bool entitySideRejected = false;
        try { capture.bind_getters(); } catch (const std::exception&) { entitySideRejected = true; }
        EXPECT(entitySideRejected);
        entityPlayerSide->result = goodEntitySideType;
        capture.bind_getters();

        auto* goodPlayerSideType = battlePlayerSide->type;
        battlePlayerSide->type = schema::type("System.Int32");
        bool playerSideFieldRejected = false;
        try { capture.bind_fields(); } catch (const std::exception&) { playerSideFieldRejected = true; }
        EXPECT(playerSideFieldRejected);
        battlePlayerSide->type = goodPlayerSideType;
        capture.bind_fields();
        capture.bind_getters();
        capture.scene_player_data_class_ = schema::type("Torappu.Battle.AutoChess.AutoChessPlayerDataModel.ScenePlayerData");
        capture.bind_timing();
        EXPECT(capture.manager_status_field_ && capture.center_battle_status_field_ &&
               capture.self_status_info_ && capture.self_status_dict_field_ &&
               capture.self_status_lookup_info_ && capture.game_state_ && capture.self_finished_ &&
               capture.scene_game_state_field_ && capture.center_help_data_info_ && capture.help_players_field_);
        EXPECT(capture.phase_battle_ == 4 && capture.phase_help_ == 5 &&
               capture.phase_boss_ == 6 && capture.phase_waiting_ == 100 &&
               capture.phase_end_ == 8 && capture.phase_pause_ == 9 && capture.phase_settle_ == 7 && capture.player_battling_ == 4);

        auto* selfLookup = static_cast<schema::Method*>(capture.self_status_lookup_info_);
        auto* goodOut = selfLookup->args[1];
        selfLookup->args[1] = schema::type("System.Int32&");
        bool selfOutRejected = false;
        try { capture.bind_timing(); } catch (const std::exception&) { selfOutRejected = true; }
        EXPECT(selfOutRejected);
        selfLookup->args[1] = goodOut;

        auto* playerState = schema::type("Torappu.AutoChessPlayerGameStateType");
        std::string removedName;
        for (auto& field : playerState->fields) {
            if (field.name == "BATTLE_NOT_COMPLETE") {
                removedName = field.name;
                field.name = "RemovedBattleNotComplete";
                break;
            }
        }
        bool enumRejected = false;
        try { capture.bind_timing(); } catch (const std::exception&) { enumRejected = true; }
        EXPECT(enumRejected && !removedName.empty());
        for (auto& field : playerState->fields) if (field.name == "RemovedBattleNotComplete") field.name = removedName;

        auto* scene = schema::type("Torappu.Battle.AutoChess.AutoChessPlayerDataModel.ScenePlayerData");
        schema::Field* gameState = nullptr;
        for (auto& field : scene->fields) if (field.name == "gameState") gameState = &field;
        EXPECT(gameState != nullptr);
        auto* goodFieldType = gameState ? gameState->type : nullptr;
        if (gameState) gameState->type = schema::type("System.Int32");
        bool gameStateRejected = false;
        try { capture.bind_timing(); } catch (const std::exception&) { gameStateRejected = true; }
        EXPECT(gameStateRejected);
        if (gameState) gameState->type = goodFieldType;
        capture.bind_timing();
        EXPECT(capture.runtime_cards_are_instances_);
        EXPECT(!capture.indexer_token_card_field_);
        EXPECT(capture.real_delta_offset_ == 0x48 && capture.shared_mask_offset_ == 0x3c);
        capture.bind_hooks(); // Host mock records hooks but never patches or executes code.
        EXPECT(hook_count == 8);
        EXPECT(capture.manager_instance_info_ == &singleton->methods.front());
        EXPECT(capture.shutdown());

        auto* global = schema::type("Torappu.GlobalInitializerAndUpdater");
        for (auto& method : global->methods) if (method.name == "Update") method.name = "RemovedUpdate";
        bool hook_rejected = false;
        try { capture.bind_hooks(); } catch (const std::exception&) { hook_rejected = true; }
        EXPECT(hook_rejected && hook_count == 8 && !capture.hook_schema_validated_);

        // A field rename without the reference out ABI must be rejected.
        auto* lookup = static_cast<schema::Method*>(capture.runtime_card_lookup_info_);
        lookup->args[1] = schema::type("System.Int32&");
        bool rejected = false;
        try { capture.bind_indexer(); } catch (const std::exception&) { rejected = true; }
        EXPECT(rejected);
        std::printf("%s client schema plus explicit supplements: fields / getters / 8 hooks / inherited nullable singleton / reject wrong out ABI / reject missing hook\n", failures ? "FAIL" : "PASS");
        return failures;
    }
};
} // namespace ark::stronghold

int main() {
    try { return ark::stronghold::CaptureTests::run() ? 1 : 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL recorded schema: %s\n", error.what()); return 1; }
}
