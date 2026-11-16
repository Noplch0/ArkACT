#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "il2cpp_runtime.h"
#include "il2cpp/il2cpp_api.h"

namespace {
int failures = 0;
#define CHECK(value) do { if (!(value)) { ++failures; std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #value); } } while (0)

struct Klass;
struct Field { const char* name; Klass* type; };
struct Klass { const char* name; int size = 0; Klass* parent = nullptr; std::vector<Field*> fields; };
Klass reference{"Reference"}, number{"Int32", 4}, child{"Child"}, parent{"Parent"};
Field ref{"value", &reference}, integer{"integer", &number}, backing{"<cards>k__BackingField", &reference};
#define DECLARE_API_MEMBER_PROBE(member)                                                   \
    template <class T, class = void>                                                       \
    struct has_##member : std::false_type {};                                              \
    template <class T>                                                                     \
    struct has_##member<T, std::void_t<decltype(&T::member)>> : std::true_type {};

DECLARE_API_MEMBER_PROBE(il2cpp_init)
DECLARE_API_MEMBER_PROBE(il2cpp_shutdown)
DECLARE_API_MEMBER_PROBE(il2cpp_alloc)
DECLARE_API_MEMBER_PROBE(il2cpp_array_new)
DECLARE_API_MEMBER_PROBE(il2cpp_type_get_object)
DECLARE_API_MEMBER_PROBE(il2cpp_class_get_static_field_data)
DECLARE_API_MEMBER_PROBE(il2cpp_domain_assembly_open)
DECLARE_API_MEMBER_PROBE(il2cpp_field_set_value)
DECLARE_API_MEMBER_PROBE(il2cpp_field_static_set_value)
DECLARE_API_MEMBER_PROBE(il2cpp_field_set_value_object)
DECLARE_API_MEMBER_PROBE(il2cpp_property_get_set_method)
DECLARE_API_MEMBER_PROBE(il2cpp_object_new)
DECLARE_API_MEMBER_PROBE(il2cpp_value_box)
DECLARE_API_MEMBER_PROBE(il2cpp_runtime_invoke_convert_args)
DECLARE_API_MEMBER_PROBE(il2cpp_runtime_class_init)
DECLARE_API_MEMBER_PROBE(il2cpp_runtime_object_init)
DECLARE_API_MEMBER_PROBE(il2cpp_string_new)
DECLARE_API_MEMBER_PROBE(il2cpp_string_new_utf16)
DECLARE_API_MEMBER_PROBE(il2cpp_runtime_invoke)
DECLARE_API_MEMBER_PROBE(il2cpp_field_get_value)
DECLARE_API_MEMBER_PROBE(il2cpp_field_get_value_object)

#undef DECLARE_API_MEMBER_PROBE

static_assert(!has_il2cpp_init<Il2CppApi>::value);
static_assert(!has_il2cpp_shutdown<Il2CppApi>::value);
static_assert(!has_il2cpp_alloc<Il2CppApi>::value);
static_assert(!has_il2cpp_array_new<Il2CppApi>::value);
static_assert(!has_il2cpp_type_get_object<Il2CppApi>::value);
static_assert(!has_il2cpp_class_get_static_field_data<Il2CppApi>::value);
static_assert(!has_il2cpp_domain_assembly_open<Il2CppApi>::value);
static_assert(!has_il2cpp_field_set_value<Il2CppApi>::value);
static_assert(!has_il2cpp_field_static_set_value<Il2CppApi>::value);
static_assert(!has_il2cpp_field_set_value_object<Il2CppApi>::value);
static_assert(!has_il2cpp_property_get_set_method<Il2CppApi>::value);
static_assert(!has_il2cpp_object_new<Il2CppApi>::value);
static_assert(!has_il2cpp_value_box<Il2CppApi>::value);
static_assert(!has_il2cpp_runtime_invoke_convert_args<Il2CppApi>::value);
static_assert(!has_il2cpp_runtime_class_init<Il2CppApi>::value);
static_assert(!has_il2cpp_runtime_object_init<Il2CppApi>::value);
static_assert(!has_il2cpp_string_new<Il2CppApi>::value);
static_assert(!has_il2cpp_string_new_utf16<Il2CppApi>::value);
static_assert(has_il2cpp_runtime_invoke<Il2CppApi>::value);
static_assert(has_il2cpp_field_get_value<Il2CppApi>::value);
static_assert(has_il2cpp_field_get_value_object<Il2CppApi>::value);

void* last_field = nullptr;
void* last_object = nullptr;
int receiver = 123;
int returned_object = 456;

ark::il2cpp::Runtime make_runtime() {
    ark::il2cpp::Runtime r;
    r.class_parent = [](void* k) -> void* { return static_cast<Klass*>(k)->parent; };
    r.class_name = [](void* k) { return static_cast<Klass*>(k)->name; };
    r.class_namespace = [](void*) { return "Mock"; };
    r.class_from_type = [](void* t) { return t; };
    r.class_is_value = [](void* k) { return static_cast<Klass*>(k)->size != 0; };
    r.class_value_size = [](void* k, uint32_t*) { return static_cast<Klass*>(k)->size; };
    r.field_type = [](void* f) -> void* { return static_cast<Field*>(f)->type; };
    r.field_name = [](void* f) { return static_cast<Field*>(f)->name; };
    r.class_field = [](void* k, const char* n) -> void* {
        for (auto* f : static_cast<Klass*>(k)->fields) if (!std::strcmp(f->name, n)) return f;
        return nullptr;
    };
    r.class_fields = [](void* k, void** it) -> void* {
        auto& fields = static_cast<Klass*>(k)->fields;
        const auto i = reinterpret_cast<std::uintptr_t>(*it);
        if (i >= fields.size()) return nullptr;
        *it = reinterpret_cast<void*>(i + 1);
        return fields[i];
    };
    r.field_get_object = [](void* f, void* o) -> void* {
        last_field = f;
        last_object = o;
        return &returned_object;
    };
    r.field_get = [](void*, void*, void* out) { *static_cast<int*>(out) = 789; };
    return r;
}
}

int main() {
    auto r = make_runtime();
    CHECK(r.read_reference(&receiver, &ref) == &returned_object);
    CHECK(last_field == &ref && last_object == &receiver);
    CHECK(!r.read_reference(nullptr, &ref));
    CHECK(!r.read_reference(&receiver, nullptr));
    bool rejected = false;
    try { r.read_reference(&receiver, &integer); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    CHECK(r.read_field<int>(&receiver, &integer) == 789);
    rejected = false;
    try { r.read_field<std::int64_t>(&receiver, &integer); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    parent.fields = {&ref};
    child.parent = &parent;
    child.fields = {&backing};
    CHECK(r.field(&child, "value") == &ref);
    CHECK(r.field(&child, "cards") == &backing);
    r.class_field = [](void*, const char*) -> void* { return nullptr; };
    CHECK(r.field(&child, "value") == &ref); // Enumeration fallback uses the same exact name.
    CHECK(r.field(&child, "cards") == &backing);
    rejected = false;
    try { r.field(&child, "not_present"); }
    catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("available: <cards>k__BackingField") != std::string::npos;
    }
    CHECK(rejected);
    static_assert(std::is_same_v<decltype(Il2CppApi::il2cpp_field_get_value_object),
        Il2CppObject* (*)(FieldInfo*, Il2CppObject*)>);
    std::printf("%s IL2CPP read-only field order / hierarchy / backing fields / storage checks\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
