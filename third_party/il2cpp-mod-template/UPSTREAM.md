Source: https://github.com/Mr-Courage/il2cpp-mod-template

Commit: ee2afbf528f73012c493277aff934f18440f21e6

The IL2CPP API table, resolver, opaque types and Windows symbol adapter are copied
from il2cpp_core/src, with their MIT license. They are compiled into the existing
ArkFramework plugin to independently inspect current game metadata. No extra DLL
or injection route is installed. The original tutorial ZIP has the same commit.

Local changes for 2.1.0: the API table is reduced to 65 read/metadata/thread/GC
entries. Unused field setters, object/string/array creation, initialization,
boxing, assembly loading and reflection type construction are removed; the
table size assertion is updated with the resolver. Runtime invocation remains
for read-only getters and collection queries used by the capture code.
