"""Check the deployed x64 plugin with only Python's standard library."""
from pathlib import Path
import ctypes
import os
import struct
import sys


def inspect(path: Path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    assert data[pe:pe + 4] == b'PE\0\0', 'not a PE file'
    machine, sections = struct.unpack_from('<HH', data, pe + 4)
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    assert machine == 0x8664 and struct.unpack_from('<H', data, optional)[0] == 0x20B, 'not x64 PE32+'
    table = optional + optional_size

    def offset(rva):
        for index in range(sections):
            size, address, raw_size, raw = struct.unpack_from('<IIII', data, table + 40 * index + 8)
            if address <= rva < address + max(size, raw_size):
                return raw + rva - address
        raise AssertionError(f'bad RVA {rva:x}')

    def cstring(at):
        return data[at:data.index(b'\0', at)].decode('ascii')

    export_rva, _ = struct.unpack_from('<II', data, optional + 112)
    export_directory = struct.unpack_from('<IIHHIIIIIII', data, offset(export_rva))
    name_count, name_rvas = export_directory[7], export_directory[9]
    exports = {cstring(offset(struct.unpack_from('<I', data, offset(name_rvas) + 4 * i)[0]))
               for i in range(name_count)}
    expected = {'ark_plugin_query', 'ark_plugin_initialize', 'ark_plugin_shutdown', 'ark_plugin_can_unload'}
    assert expected <= exports, f'missing exports {expected - exports}'
    assert not any('rdps' in item.lower() for item in exports), 'obsolete RDPS export'
    import_rva, _ = struct.unpack_from('<II', data, optional + 120)
    imports = []
    descriptor = offset(import_rva)
    while any(struct.unpack_from('<IIIII', data, descriptor)):
        imports.append(cstring(offset(struct.unpack_from('<I', data, descriptor + 12)[0])))
        descriptor += 20
    allowed = {'arkimgui.dll', 'kernel32.dll', 'user32.dll', 'ntdll.dll', 'msvcrt.dll',
               'advapi32.dll', 'shell32.dll', 'ole32.dll', 'bcrypt.dll'}
    assert all(name.lower() in allowed or name.lower().startswith('api-ms-win-') for name in imports), imports
    assert b'damage_meter.dll' not in data.lower() and b'rdps' not in data.lower(), 'obsolete binary content'
    forbidden = (b'il2cpp_field_set_value', b'il2cpp_field_static_set_value',
                 b'il2cpp_field_set_value_object', b'il2cpp_object_new',
                 b'il2cpp_string_new', b'il2cpp_array_new', b'il2cpp_runtime_object_init',
                 b'il2cpp_runtime_class_init', b'MakeGenericType', b'_EnsureSelfBattleStatus')
    assert not any(symbol in data for symbol in forbidden), 'unexpected managed write/creation capability'
    print('PASS x64 exports / imports / no damage_meter or RDPS')
    print('PASS no unused managed setter / object creation / state initialization APIs')
    print('Imports:', ', '.join(imports))

    if os.name == 'nt':
        class PluginInfo(ctypes.Structure):
            _fields_ = [('size', ctypes.c_uint32), ('abi', ctypes.c_uint32),
                        ('imgui', ctypes.c_uint32), ('name', ctypes.c_char_p),
                        ('version', ctypes.c_char_p), ('author', ctypes.c_char_p),
                        ('flags', ctypes.c_uint32), ('dependencies', ctypes.c_void_p),
                        ('dependency_count', ctypes.c_size_t)]

        with os.add_dll_directory(str(path.parent.parent.resolve())):
            module = ctypes.WinDLL(str(path.resolve()))
            module.ark_plugin_query.restype = ctypes.POINTER(PluginInfo)
            info = module.ark_plugin_query().contents
            assert info.abi == 1 and info.size == ctypes.sizeof(PluginInfo)
            assert info.dependency_count == 0 and not info.dependencies
            import re
            version_header = Path(__file__).resolve().parents[1] / 'common/stronghold_version.h'
            expected = re.search(r'kPluginVersion\s*=\s*"([^"]+)"', version_header.read_text()).group(1).encode()
            assert info.version == expected
            print(f'PASS actual DLL load / plugin query / version {info.version.decode()} / zero plugin dependencies')


if __name__ == '__main__':
    inspect(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 'dist/plugins/stronghold_stats.dll')
