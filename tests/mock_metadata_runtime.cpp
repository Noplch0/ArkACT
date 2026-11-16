// Test-only metadata provider; built below .build/verify, never distributed.
#include <cstdlib>
#include <cstring>
#include "il2cpp/il2cpp_types.h"

struct Il2CppType { const char* text; };
struct FieldInfo { const char* name; Il2CppType* type; };
struct MethodInfo { const char* name; Il2CppType* result; };
struct PropertyInfo { const char* name; };
struct Il2CppClass { const char* name; const char* ns; Il2CppType* type; };
struct Il2CppImage {};
struct Il2CppAssembly {};
struct Il2CppDomain {};
struct Il2CppThread {};

namespace {
Il2CppDomain domain;
Il2CppThread thread;
Il2CppAssembly assembly;
Il2CppAssembly* assemblies[] = {&assembly};
Il2CppImage image;
Il2CppType center_type{"Torappu.Battle.DataCenter.AutoChessDataCenter"};
Il2CppType indexer_type{"Torappu.Battle.DataCenter.AutoChessDataIndexer"};
Il2CppType dictionary_type{"Torappu.ListDict<System.UInt32,Torappu.Battle.AutoChess.ChessInst>"};
Il2CppType void_type{"System.Void"}, uint_type{"System.UInt32"}, byref_int{"System.Int32&"};
Il2CppType bool_type{"System.Boolean"}, byref_chess{"Torappu.Battle.AutoChess.ChessInst&"};
Il2CppClass center{"AutoChessDataCenter", "Torappu.Battle.DataCenter", &center_type};
Il2CppClass indexer{"AutoChessDataIndexer", "Torappu.Battle.DataCenter", &indexer_type};
Il2CppClass ignored{"Unrelated", "Other.Namespace", &void_type};
Il2CppClass closed_map{"ListDict`2", "Torappu", &dictionary_type};
Il2CppClass* classes[] = {&center, &indexer, &ignored};
FieldInfo renamed{"renamedRuntimeCardMap", &dictionary_type};
FieldInfo current_map{"runtimeCard2Chess", &dictionary_type};
MethodInfo method{"ChangedMethod", &void_type};
MethodInfo try_get{"TryGetValue", &bool_type};
PropertyInfo property{"ActualProperty"};
bool attached = false;
int attach_count = 0, detach_count = 0;
}

#define EXPORT extern "C" __declspec(dllexport)
EXPORT Il2CppDomain* il2cpp_domain_get() { return &domain; }
EXPORT Il2CppThread* il2cpp_thread_current() { return attached ? &thread : nullptr; }
EXPORT Il2CppThread* il2cpp_thread_attach(Il2CppDomain*) { attached = true; ++attach_count; return &thread; }
EXPORT void il2cpp_thread_detach(Il2CppThread*) { attached = false; ++detach_count; }
EXPORT Il2CppAssembly** il2cpp_domain_get_assemblies(Il2CppDomain*, size_t* count) { *count = 1; return assemblies; }
EXPORT Il2CppImage* il2cpp_assembly_get_image(Il2CppAssembly*) { return &image; }
EXPORT const char* il2cpp_image_get_name(Il2CppImage*) { return "Assembly-CSharp.dll"; }
EXPORT size_t il2cpp_image_get_class_count(Il2CppImage*) { return 3; }
EXPORT Il2CppClass* il2cpp_image_get_class(Il2CppImage*, size_t i) { return classes[i]; }
EXPORT const char* il2cpp_class_get_name(Il2CppClass* c) { return c->name; }
EXPORT const char* il2cpp_class_get_namespace(Il2CppClass* c) { return c->ns; }
EXPORT Il2CppType* il2cpp_class_get_type(Il2CppClass* c) { return c->type; }
EXPORT Il2CppClass* il2cpp_class_from_type(Il2CppType* t) { return t == &dictionary_type ? &closed_map : nullptr; }
EXPORT Il2CppClass* il2cpp_class_get_parent(Il2CppClass*) { return nullptr; }
EXPORT FieldInfo* il2cpp_class_get_fields(Il2CppClass* c, void** it) {
    if (c != &indexer) return nullptr;
    const auto n = reinterpret_cast<size_t>(*it);
    if (n >= 2) return nullptr;
    *it = reinterpret_cast<void*>(n + 1);
    return n ? &current_map : &renamed;
}
EXPORT const char* il2cpp_field_get_name(FieldInfo* f) { return f->name; }
EXPORT Il2CppType* il2cpp_field_get_type(FieldInfo* f) { return f->type; }
EXPORT size_t il2cpp_field_get_offset(FieldInfo*) { return 0x28; }
EXPORT int il2cpp_field_get_flags(FieldInfo*) { return 6; }
EXPORT MethodInfo* il2cpp_class_get_methods(Il2CppClass* c, void** it) {
    if ((c != &indexer && c != &closed_map) || *it) return nullptr;
    *it = reinterpret_cast<void*>(1);
    return c == &closed_map ? &try_get : &method;
}
EXPORT const char* il2cpp_method_get_name(MethodInfo* m) { return m->name; }
EXPORT Il2CppType* il2cpp_method_get_return_type(MethodInfo* m) { return m->result; }
EXPORT uint32_t il2cpp_method_get_param_count(MethodInfo*) { return 2; }
EXPORT Il2CppType* il2cpp_method_get_param(MethodInfo* m, uint32_t i) { return i ? (m == &try_get ? &byref_chess : &byref_int) : &uint_type; }
EXPORT uint32_t il2cpp_method_get_flags(MethodInfo* m, uint32_t*) { return m == &try_get ? 0 : 0x10; }
EXPORT PropertyInfo* il2cpp_class_get_properties(Il2CppClass* c, void** it) {
    if (c != &indexer || *it) return nullptr;
    *it = reinterpret_cast<void*>(1);
    return &property;
}
EXPORT const char* il2cpp_property_get_name(PropertyInfo* p) { return p->name; }
EXPORT const char* il2cpp_type_get_name(Il2CppType* t) {
    const auto size = std::strlen(t->text) + 1;
    auto* copy = static_cast<char*>(std::malloc(size));
    if (copy) std::memcpy(copy, t->text, size);
    return copy;
}
EXPORT void il2cpp_free(void* memory) { std::free(memory); }
EXPORT int MockAttachCount() { return attach_count; }
EXPORT int MockDetachCount() { return detach_count; }
