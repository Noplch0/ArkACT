#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "metadata_report.h"
#include "csv_export.h"

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    HMODULE mock = LoadLibraryW(ark::stronghold::utf8_path(argv[1]).c_str());
    if (!mock) return 2;
    const auto root = std::filesystem::absolute("metadata-report-test-output");
    const auto path = ark::stronghold::diagnostics::write_metadata_report(
        ark::stronghold::path_utf8(root).c_str(), "测试：游戏字段发生变化",
        "CURRENT\nmode_present=0\nLAST IN-MODE SNAPSHOT\n已退出对局；published=123\n");
    std::ifstream file(ark::stronghold::utf8_path(path), std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();
    using Count = int (*)();
    auto attaches = reinterpret_cast<Count>(GetProcAddress(mock, "MockAttachCount"));
    auto detaches = reinterpret_cast<Count>(GetProcAddress(mock, "MockDetachCount"));
    const bool ok = text.starts_with("\xEF\xBB\xBF") &&
        text.find("renamedRuntimeCardMap") != std::string::npos &&
        text.find("offset=0x28") != std::string::npos &&
        text.find("PROPERTY ActualProperty") != std::string::npos &&
        text.find("static System.Void ChangedMethod(System.UInt32, System.Int32&)") != std::string::npos &&
        text.find("COLLECTION Torappu.ListDict<System.UInt32,Torappu.Battle.AutoChess.ChessInst>") != std::string::npos &&
        text.find("System.Boolean TryGetValue(System.UInt32, Torappu.Battle.AutoChess.ChessInst&)") != std::string::npos &&
        text.find("测试：游戏字段发生变化") != std::string::npos &&
        text.find("RUNTIME SNAPSHOT BEGIN") != std::string::npos &&
        text.find("已退出对局；published=123") != std::string::npos &&
        text.find("REPORT COMPLETE; matching classes=2") != std::string::npos &&
        text.find("MISSING EXPORT il2cpp_field_set_value") == std::string::npos &&
        text.find("MISSING EXPORT il2cpp_object_new") == std::string::npos &&
        text.find("MISSING EXPORT il2cpp_string_new") == std::string::npos &&
        text.find("MISSING EXPORT il2cpp_runtime_class_init") == std::string::npos &&
        text.find("Other.Namespace.Unrelated") == std::string::npos &&
        attaches && detaches && attaches() == 1 && detaches() == 1;
    if (!ok) std::fprintf(stderr, "metadata report failed: %s\n%s\n", path.c_str(), text.c_str());
    const auto schema_path = ark::stronghold::diagnostics::write_metadata_report(
        ark::stronghold::path_utf8(root).c_str(), "schema-only compatibility failure");
    std::ifstream schema_file(ark::stronghold::utf8_path(schema_path), std::ios::binary);
    const std::string schema_text{std::istreambuf_iterator<char>(schema_file), std::istreambuf_iterator<char>()};
    schema_file.close();
    const bool schema_ok = schema_text.find("REPORT COMPLETE; matching classes=2") != std::string::npos &&
        schema_text.find("MISSING EXPORT il2cpp_field_set_value") == std::string::npos &&
        schema_text.find("RUNTIME SNAPSHOT BEGIN") == std::string::npos && attaches() == 2 && detaches() == 2;
    FreeLibrary(mock);
    std::filesystem::remove_all(root);
    std::printf("%s template backend: read-only metadata enumeration / renamed fields / signatures / UTF-8 / thread detach\n", ok ? "PASS" : "FAIL");
    return ok && schema_ok ? 0 : 1;
}
