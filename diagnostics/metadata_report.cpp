#include "metadata_report.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <string>

#include "il2cpp/il2cpp_api.h"
#include "stronghold_version.h"

namespace ark::stronghold::diagnostics {
namespace {

constexpr const char* kSourceCommit = "ee2afbf528f73012c493277aff934f18440f21e6";

template <class F>
F extra(HMODULE module, const char* symbol) {
    return reinterpret_cast<F>(GetProcAddress(module, symbol));
}

bool wanted(const char* ns, const char* name) {
    if (!ns || !name) return false;
    if (std::strstr(ns, "AutoChess") || std::strstr(name, "AutoChess")) return true;
    const char* names[] = {"BObject", "Entity", "Unit", "Character", "Enemy", "Token", "Trap",
        "Projectile", "Modifier", "BattleLogger", "BattleController", "BattleCharacterData",
        "BattlePlayerData", "BattleEntityData", "ScenePlayerData", "PlayerCard", "ChessInst",
        "PlayerBattleData", "FP", "PlayerSide", "SideType", "EntityCategory", "DamageType",
        "State", "EndingStatus", "GlobalInitializerAndUpdater", "SelfBattleRoundStatus",
        "HelpBattleRoundStatus", "BossBattleRoundStatus", "IRoundStatus"};
    if (ns[0] && std::strncmp(ns, "Torappu", 7) != 0) return false;
    for (const char* candidate : names) {
        if (!std::strcmp(candidate, name)) return true;
    }
    return false;
}

std::string type_name(Il2CppApi& api, Il2CppType* type) {
    const char* raw = type && api.il2cpp_type_get_name ? api.il2cpp_type_get_name(type) : nullptr;
    if (!raw) return "?";
    std::string result(raw);
    if (api.il2cpp_free) api.il2cpp_free(const_cast<char*>(raw));
    return result;
}

struct ThreadAttachment {
    Il2CppApi& api;
    Il2CppThread* owned = nullptr;
    ~ThreadAttachment() { if (owned) api.il2cpp_thread_detach(owned); }
};

} // namespace

std::string write_metadata_report(const char* root, const char* reason, const std::string& runtime_snapshot) {
    try {
        if (!root || !*root) return "failed: framework root is missing";
        const std::string root_utf8(root);
        const auto folder = std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(root_utf8.data()), root_utf8.size())) / "diagnostics";
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error) return "failed: cannot create diagnostics directory: " + error.message();
        const auto file = folder / ("stronghold-schema-" + std::to_string(GetCurrentProcessId()) + ".log");
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) return "failed: cannot open metadata report";
        out << "\xEF\xBB\xBFStronghold metadata report (plugin " << kPluginVersion << ")\n"
            << "Backend: Mr-Courage/il2cpp-mod-template il2cpp_core @ " << kSourceCommit << '\n'
            << "Metadata enumeration and optional copied runtime snapshot; no raw player UID values.\n"
            << "Reason: " << (reason ? reason : "user request") << '\n';
        if (!runtime_snapshot.empty()) out << "\nRUNTIME SNAPSHOT BEGIN\n" << runtime_snapshot << "\nRUNTIME SNAPSHOT END\n";
        HMODULE module = GetModuleHandleW(L"GameAssembly.dll");
        if (!module) return "failed: GameAssembly.dll is not loaded";
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const char*>(module) + dos->e_lfanew);
        out << "GameAssembly timestamp=0x" << std::hex << pe->FileHeader.TimeDateStamp
            << " size=0x" << pe->OptionalHeader.SizeOfImage << std::dec << '\n';
        Il2CppApi api{};
        const int exports = il2cpp_api_resolve(&api, module);
        out << "Resolved exports: " << exports << "/" << IL2CPP_API_MEMBER_COUNT_2026_10_10 << '\n';
#define DO_API(r, n, p) if (!api.n) out << "MISSING EXPORT " #n "\n";
#define DO_API_NO_RETURN(r, n, p) DO_API(r,n,p)
#include "il2cpp/il2cpp_api_functions.h"
#undef DO_API
#undef DO_API_NO_RETURN
        if (!api.il2cpp_domain_get || !api.il2cpp_domain_get_assemblies ||
            !api.il2cpp_assembly_get_image || !api.il2cpp_image_get_class_count ||
            !api.il2cpp_image_get_class || !api.il2cpp_class_get_name ||
            !api.il2cpp_class_get_namespace || !api.il2cpp_class_get_fields ||
            !api.il2cpp_field_get_name || !api.il2cpp_class_get_methods ||
            !api.il2cpp_method_get_name || !api.il2cpp_class_get_type ||
            !api.il2cpp_method_get_return_type || !api.il2cpp_method_get_param_count ||
            !api.il2cpp_thread_current ||
            !api.il2cpp_thread_attach || !api.il2cpp_thread_detach) {
            out << "REPORT INCOMPLETE: required metadata exports missing\n";
        } else if (auto* domain = api.il2cpp_domain_get()) {
            ThreadAttachment attachment{api};
            if (!api.il2cpp_thread_current()) attachment.owned = api.il2cpp_thread_attach(domain);
            if (!api.il2cpp_thread_current()) return "failed: metadata thread attachment failed";
            auto method_param = extra<Il2CppType* (*)(MethodInfo*, uint32_t)>(module, "il2cpp_method_get_param");
            auto method_flags = extra<uint32_t (*)(MethodInfo*, uint32_t*)>(module, "il2cpp_method_get_flags");
            auto field_flags = extra<int (*)(FieldInfo*)>(module, "il2cpp_field_get_flags");
            auto properties = extra<PropertyInfo* (*)(Il2CppClass*, void**)>(module, "il2cpp_class_get_properties");
            size_t size = 0;
            Il2CppAssembly** assemblies = api.il2cpp_domain_get_assemblies(domain, &size);
            if (!assemblies || size > 4096) return "failed: invalid assembly metadata";
            size_t matched = 0;
            std::set<Il2CppClass*> index_collections;
            for (size_t a = 0; a < size; ++a) {
                auto* image = api.il2cpp_assembly_get_image(assemblies[a]);
                if (!image) continue;
                size_t count = api.il2cpp_image_get_class_count(image);
                out << "IMAGE " << (api.il2cpp_image_get_name ? api.il2cpp_image_get_name(image) : "?")
                    << " classes=" << count << '\n';
                if (count > 1000000) continue;
                for (size_t c = 0; c < count; ++c) {
                    auto* klass = api.il2cpp_image_get_class(image, c);
                    if (!klass) continue;
                    const char* ns = api.il2cpp_class_get_namespace(klass);
                    const char* name = api.il2cpp_class_get_name(klass);
                    if (!wanted(ns, name)) continue;
                    ++matched;
                    out << "\nCLASS " << ns << '.' << name;
                    if (api.il2cpp_class_get_type) out << " [" << type_name(api, api.il2cpp_class_get_type(klass)) << ']';
                    if (api.il2cpp_class_get_parent) {
                        if (auto* parent = api.il2cpp_class_get_parent(klass))
                            out << " : " << api.il2cpp_class_get_namespace(parent) << '.' << api.il2cpp_class_get_name(parent);
                    }
                    out << '\n';
                    void* it = nullptr;
                    while (auto* field = api.il2cpp_class_get_fields(klass, &it)) {
                        const char* field_name = api.il2cpp_field_get_name(field);
                        if ((!std::strcmp(name, "AutoChessDataIndexer") ||
                             !std::strcmp(name, "AutoChessBattleStatusDataModel")) && field_name &&
                            api.il2cpp_field_get_type && api.il2cpp_class_from_type &&
                            (!std::strcmp(field_name, "runtimeCard2Chess") ||
                             !std::strcmp(field_name, "instId2PlayerIndex") ||
                             !std::strcmp(field_name, "runtimeCard2ChessDict") ||
                             !std::strcmp(field_name, "m_selfBattleStatusInfo") ||
                             !std::strcmp(field_name, "runtimeToken2CardDict"))) {
                            if (auto* collection = api.il2cpp_class_from_type(api.il2cpp_field_get_type(field)))
                                index_collections.insert(collection);
                        }
                        out << " FIELD " << (field_name ? field_name : "?");
                        if (api.il2cpp_field_get_type) out << " : " << type_name(api, api.il2cpp_field_get_type(field));
                        if (api.il2cpp_field_get_offset) out << " offset=0x" << std::hex << api.il2cpp_field_get_offset(field) << std::dec;
                        if (field_flags) out << " flags=0x" << std::hex << field_flags(field) << std::dec;
                        out << '\n';
                    }
                    if (properties) {
                        it = nullptr;
                        while (auto* property = properties(klass, &it)) {
                            if (api.il2cpp_property_get_name) out << " PROPERTY " << api.il2cpp_property_get_name(property) << '\n';
                        }
                    }
                    it = nullptr;
                    while (auto* method = api.il2cpp_class_get_methods(klass, &it)) {
                        out << " METHOD ";
                        uint32_t impl = 0;
                        if (method_flags && (method_flags(method, &impl) & 0x10)) out << "static ";
                        if (api.il2cpp_method_get_return_type) out << type_name(api, api.il2cpp_method_get_return_type(method)) << ' ';
                        out << api.il2cpp_method_get_name(method) << '(';
                        const uint32_t arity = api.il2cpp_method_get_param_count ? api.il2cpp_method_get_param_count(method) : 0;
                        for (uint32_t p = 0; p < (std::min)(arity, 128u); ++p) {
                            if (p) out << ", ";
                            out << (method_param ? type_name(api, method_param(method, p)) : "?");
                        }
                        out << ")\n";
                    }
                }
            }
            // Closed generic collections are usually absent from image_get_class.
            // Include their actual inflated signatures through field metadata.
            for (auto* collection : index_collections) {
                out << "\nCOLLECTION " << type_name(api, api.il2cpp_class_get_type(collection)) << '\n';
                void* it = nullptr;
                while (auto* method = api.il2cpp_class_get_methods(collection, &it)) {
                    const char* method_name = api.il2cpp_method_get_name(method);
                    if (!method_name || std::strcmp(method_name, "TryGetValue")) continue;
                    out << " METHOD ";
                    uint32_t impl = 0;
                    if (method_flags && (method_flags(method, &impl) & 0x10)) out << "static ";
                    out << type_name(api, api.il2cpp_method_get_return_type(method)) << ' ' << method_name << '(';
                    const uint32_t arity = api.il2cpp_method_get_param_count(method);
                    for (uint32_t p = 0; p < (std::min)(arity, 128u); ++p) {
                        if (p) out << ", ";
                        out << (method_param ? type_name(api, method_param(method, p)) : "?");
                    }
                    out << ")\n";
                }
            }
            out << "\nREPORT COMPLETE; matching classes=" << matched << '\n';
        } else {
            out << "REPORT INCOMPLETE: domain not ready\n";
        }
        out.flush();
        if (!out) return "failed: writing metadata report";
        const auto utf8 = file.u8string();
        return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    } catch (const std::exception& error) {
        return std::string("failed: ") + error.what();
    }
}

} // namespace ark::stronghold::diagnostics
