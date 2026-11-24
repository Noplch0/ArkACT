# AGENTS.md — 卫戍协议伤害统计插件维护与重建指南

本文件面向继续开发本仓库的 agent。目标不仅是说明如何编译现有代码，还要保存足以重新实现插件的架构、数据语义、已验证入口、失败经验和游戏更新适配方法。

- 文档基线：2026-11-24，源码版本 `common/stronghold_version.h` 中的 **1.2.0**（保留原内部 2.1.2 采集基线；主界面显示只读种子/Boss 信息并新增商店记牌器，不保留出怪预览功能）。
- 源码、当前客户端元数据及可复现测试优先于本文中的历史快照。更新实现后必须同步本文件，不能为了符合旧文档强行恢复旧结构。
- 产品：仅适用于《明日方舟》卫戍协议的只读统计插件；支持 1–4 人本地模拟数据，不是通用伤害插件、独立注入器或游戏修改器。
- 这份文档不代表已完成所有实机验收，尤其不能把模拟测试通过等同于当前游戏版本联机实测通过。产品契约是维护目标，不应误读成所有防护已经完备；当前实现的明确边界见第 9.1 节。

## 1. 工作边界：先读再动

1. **不得修改游戏玩法数据。** 不写伤害、血量、技能、资源、战斗状态、存档、结算或服务器数据；不添加游戏命令执行、网络上报、作弊或绕过检测功能。
2. 允许的采集路径是 ArkFramework 原生 Hook + IL2CPP 元数据/只读 getter/集合查询。Hook 跳转、线程附着、GC handle 保活和浮层输入保护属于实现机制，不能宣传为“进程内存完全零修改”。
3. **用户可能仍在对局。** 除非明确要求，不关闭/重启游戏，不附加调试器，不主动执行插件重载，不覆盖正在使用的 `D:\tools\ArkFramework\plugins`。默认只在项目内构建，由用户决定何时部署和重载。
4. 游戏目录会清理非原版 DLL。依赖框架的加载机制，框架与插件保持在游戏目录外；不要把本项目改成往游戏目录投放 DLL 的独立加载器。
5. 保持单一 `stronghold_stats.dll`，零前置**插件**依赖。运行仍需要兼容的 ArkFramework 和配套 ArkImGui，不能把“零插件依赖”说成“不需要宿主/图形运行库”。
6. 不恢复 `damage_meter.dll`、`operator_stats`、RDPS、buff 服务、`buff_capture`、`-EnableBuffService` 或 `ark.battle.events` 消费链。旧 damage_meter 可共存，但不能成为本插件的数据来源或前置条件。
7. 严格区分读到的事实、静态分析推断和待实机验证假设。缺数据应显示不兼容/部分采集/未归属告警，不能用全零、房间名单或猜测的玩家状态伪造“正常”。
8. 不依赖工作区一定有 Git；没有 `.git` 时用文件清单、哈希和备份记录改动。不要清理用户文件或整目录删除 `.build` 来省事。
9. 诊断与提交内容不得泄露原始 UID、昵称编号等账户标识。诊断只记录昵称是否存在，不写昵称值；报表按当前设计仅保留显示昵称和 P1–P4 槽位，不额外导出账户标识。
10. **每次更新都必须同步维护 AGENTS.md，作为交付前的强制收尾步骤，包括本次对话。** 凡涉及本项目的功能、修复、游戏适配、测试、构建、配置、诊断、文档或维护约束更新，都必须在本次任务结束前修订受影响章节，并在第 9.2 节记录日期、主要变更、实际验证结果及未完成/待实机事项；不能只在回复中说明、留待下次补写或只改日期。不涉及技术内容变化时，也须复核现有说明并记录“已复核，无技术变更”。记录只保留必要结论，不复制完整对话或敏感信息；未同步本文不得宣称本次更新完成。

## 2. 产品契约与计量口径

- 已归属玩家主排名：友方干员造成的 **`abs(Modifier.realDelta)` 实际扣血**。计算伤害、治疗和元素损伤是附加指标，不可替换主排名。
- DoT、召唤物、陷阱、弹射物在证据充分时归回所属干员和玩家；来源或宿主不明时明确保留未归属行，不猜测归属。不单列已确认宿主的召唤物或 DoT 行；同一玩家的同 ID 干员实例合并并显示实例数，不同玩家同名干员必须隔离。
- **整局/本轮** 与 **战斗/协防** 是两个独立维度。准备、暂停和结算不累计战斗时间；自己等队友时只冻结自己的主战时间，其他未完成玩家仍可继续计时。协防的伤害与时间不并入主战阶段。Boss 战属于战斗阶段。
- 每个玩家使用自己的有效战斗时长，干员 DPS/HPS 也使用所属玩家在所选范围、所选阶段的有效战斗时长，不用全房间最长时间，也不用插件加载以来的墙钟时间。
- 当前客户端正数 `currentRound` 已是显示轮次，**不要再加 1**。准备阶段 `raw <= 0` 映射为第 1 轮，`0 -> 1` 不应制造换轮事件。
- 只读轮次选择：可查看已完成轮次；选择历史后保持固定，选择当前轮后跟随。新轮次不能清空整局累计。
- 对局外保留刚结束的一局用于查看；下一场会话开始才替换。当前缓存仅在插件内存中，卸载/重载不会自动从报表恢复。
- 实际本地模拟集合覆盖全部在线房间玩家才称全房间；只有自己称本机；其余称部分采集。不按画质选项直接决定统计能力。
- 1–4 人是产品边界；`kMaxPlayers = 8` 是内部容错容量，不代表支持 8 人房间。
- 主界面直接显示只读获取的 `stageRandomSeed` 和由 `hiddenBossId` 解析的 Boss 名称。原始 Boss ID 留作内部查找键。不显示隐藏同步弹窗或逐轮出怪预览。`roundEnemies` 不采集、不输出。商店计数只消费准备状态中的货架/干员观察，不展示完整隐藏货架快照。字段缺失显示不可用。

## 3. 源码地图与重建模块

| 文件/目录 | 责任及重建时应保留的接口 |
| --- | --- |
| `include/ark/plugin.h` | ArkFramework ABI、host API、插件声明及生命周期导出 |
| `include/ark/imgui_config.h` | 与共享 ArkImGui 一致的 ImGui import/export 配置 |
| `common/stronghold_version.h` | 插件唯一版本常量，不要在多个位置维护不一致版本 |
| `common/il2cpp_exports.h` | 项目使用的精简 IL2CPP 导出定义 |
| `common/il2cpp_runtime.h/.cpp` | 元数据查类/字段/方法、签名校验、字段读取、调用、集合与 GC Root/线程管理 |
| `common/unity_input.h/.cpp` | 浮层输入保护、原生输入 Hook 和卸载生命周期 |
| `stronghold/types.h` | POD 事件、枚举、线程安全有界事件队列 |
| `stronghold/capture_policy.h` | 会话身份、范围判断等可脱离游戏测试的策略 |
| `stronghold/match_intel.h` | 只读快照 POD：普通数据来源、隐藏种子/Boss 与准备状态商店观察；不包含敌人预览条目 |
| `stronghold/shop_counter.h` | 纯逻辑记牌器：按玩家统计货架/盟约出现频率、观测到的获得/可关联商店来源、持有数和推定卖出 |
| `stronghold/capture.h/.cpp` | NativeCapture：绑定元数据、8 个核心 Hook、归属、轮次/阶段/计时采样；可选 AllStateSync 与 PreparationStatus.Read 只读复制商店、种子和 Boss 信息 |
| `stronghold/model.h` | 纯逻辑统计模型：整局、当前轮、历史轮、双阶段、去重与玩家时钟 |
| `stronghold/view_state.h` | 只读 UI 范围/阶段/轮次选择，不驱动游戏状态 |
| `stronghold/plugin.cpp` | 宿主生命周期、`pump_events()`、模型/报表协调、伤害统计/记牌器页及主界面的种子/Boss 信息 |
| `stronghold/csv_export.h` | 共用报表行语义和兼容 CSV；现有 UI 主导出不是 CSV |
| `stronghold/xlsx_export.h` | 不依赖 Excel 的 C++ OOXML/ZIP-store/CRC32 生成器 |
| `stronghold/report_archive.h` | 整局与逐轮快照、结束时间命名、失败重试与归档 |
| `diagnostics/metadata_report.h/.cpp` | 元数据报告和保留的运行态快照；升级排障的首要入口 |
| `third_party/il2cpp-mod-template/` | 直接编入插件的精简只读后端，须保留 MIT 与 UPSTREAM 说明 |
| `reference/il2cpp-mod-template/` | 用户提供工具的完整参考源码，**不是**另一个需要部署的运行时 DLL |
| `tests/fixtures/` | 公开回归用的元数据最小子集及显式 status/source 补充；无运行态/账户数据 |
| `reference/current-schema/` | 本机私有原始证据（Git 忽略），不作为干净检出或 CI 的输入 |
| `reference/dump-audit/` | 历史 dump 研究，仅作定位线索，不作当前偏移事实 |
| `tests/` | 纯逻辑、模拟元数据、商店计数、隐藏同步快照和 ImGui 回归测试 |
| `tools/inspect_binary.py` | PE/导出/依赖/旧功能及禁用写能力检查，含实际加载 query 检查 |
| `tools/inspect_workbook.py` | 不依赖插件生成器的独立 ZIP/XML/XLSX 内容校验 |
| `tests/test_inspect_workbook.py` / `tools/test-excel-workbook.ps1` | 独立校验器的匿名元数据反例；可选的已安装 Excel 原生只读打开检查 |
| `build-plugin.ps1` / `verify.ps1` | 发布构建 / MSVC 测试及七个生产源文件编译检查；支持可移植路径与自动发现 |
| `tools/dependencies.lock.json` / `prepare-dependencies.ps1` / `package-release.ps1` | 固定工具链/ArkImGui 来源、干净检出准备与不捆绑宿主的发布包 |
| `.github/workflows/release-draft.yml` | 历史文件名保留；标签校验、验证/构建、正式发布并核验 Latest |
| `README.md` / `AUDIT.md` / `dist/部署说明.txt` | 面向用户的安装、操作、口径、导出与排障入口 / 历史修复证据 / 部署说明；源码构建、Actions 和升级适配细节由本文承载，改功能须同步相应文档 |
| `SEED.md` | 客户端 seed、随机流、敌人生成和逐回合敌人数据的详细静态研究；明确可读取、可推导和待实机验证的边界 |

## 4. 从零准备与构建

### 4.1 依赖来源与目录

- 宿主源码：<https://github.com/wuyu8512/ArkFramework>。使用与目标宿主发布版配套的 `plugin.h`、ImGui 头文件、`ArkImGui.dll` 与 GNU 导入库 `libArkImGui.dll.a`，不能混用不兼容版本。
- IL2CPP 参考：<https://github.com/Mr-Courage/il2cpp-mod-template>。本项目 vendored 基线为 commit `ee2afbf528f73012c493277aff934f18440f21e6`，来源见 `third_party/il2cpp-mod-template/UPSTREAM.md`。
- 只移植该模板的 API 表、解析器、不透明类型和 Windows 符号适配器；不接入它的独立注入入口、独立 ImGui 上下文、示例业务 Hook 或网络功能。
- 目前精简后端 API 表为 65 项，保留元数据、只读调用、线程与 GC 所需能力。禁止直接用上游完整表替换而恢复 setter、对象/字符串/数组创建、装箱、程序集加载或反射类型构造。
- Windows x64，C++20，Zig/Clang GNU 目标。发布 DLL 与共享 ArkImGui 使用相容的 GNU/Itanium C++ ABI；MSVC 在这里用于测试和编译检查，不能直接替代发布构建。
- MSVC Build Tools 和 Windows SDK 用于离线验证，Python 3 用于两个独立产物检查脚本；**运行插件不需要 Python、Excel 或额外报表 DLL**。

默认目录布局如下，路径可以迁移，但必须同步脚本的真实约定：

```text
D:\Projects\
  ArkF-ACT\
    include\ark\plugin.h
    .tools\zig\ziglang\zig.exe    # 可用 -Zig 替代
    dist\ArkImGui.dll
    dist\libArkImGui.dll.a
  ArkFramework-main\
    third_party\imgui-1.91.9b\
```

`build-plugin.ps1` 默认仍取相邻 `ArkFramework-main\third_party\imgui-1.91.9b`，现已支持 `-ImGuiDir`。干净检出可先运行 `tools/prepare-dependencies.ps1`：按 `tools/dependencies.lock.json` 固定 ArkFramework commit 94c616fc4c115603983a62fd0fc6f3928db482b1 和 Zig 0.17.0（下载核对 SHA256），校验 SDK 两个头文件一致，只编译共享 ArkImGui 和导入库到项目 dist，并输出 `.build/dependencies.json`。不会构建/启动宿主或注入器；不向游戏部署。不能假设旧 dist 或本机相邻源码存在。

### 4.2 可执行构建步骤

在项目根目录 PowerShell 执行；以下命令只构建项目产物，不部署、不重载游戏：

```powershell
Set-Location 'D:\Projects\ArkF-ACT'

# 先检查依赖；不存在就先准备，不要用空文件或不匹配的库绕过。
Test-Path '.tools\zig\ziglang\zig.exe'
Test-Path '..\ArkFramework-main\third_party\imgui-1.91.9b\imgui.h'
Test-Path 'dist\libArkImGui.dll.a'
Test-Path 'dist\ArkImGui.dll'

& '.\build-plugin.ps1'
# 自动查找失败时，用 -Zig 指定实际存在的 zig.exe，不要求本机必须有 .tools 中的副本。
# 构建输出：dist\plugins\stronghold_stats.dll
Get-Item 'dist\plugins\stronghold_stats.dll'
Get-FileHash 'dist\plugins\stronghold_stats.dll' -Algorithm SHA256
```

脚本真实参数为 `-Zig`、`-DistDirectory`、`-ArkImGuiImportLibrary`、`-ImGuiDir`。指定其他输出目录时，也要提供对应导入库，或用 `-ArkImGuiImportLibrary` 显式指定。Zig 查找顺序含显式参数、项目 `.tools`、PATH 和 WinGet 路径。历史构建实测过 Zig 0.17.0；换版本必须重跑验证，不保证所有 Zig 版本兼容。用户 README 不再承担构建说明。

发布构建的七个 translation unit：

```text
stronghold/plugin.cpp
stronghold/capture.cpp
common/il2cpp_runtime.cpp
common/unity_input.cpp
diagnostics/metadata_report.cpp
third_party/il2cpp-mod-template/il2cpp/il2cpp_api.cpp
third_party/il2cpp-mod-template/platform/il2cpp_module_windows.c
```

编译器由脚本统一使用 `zig c++ -x c++`（包括最后的 `.c` 文件），主要参数：

```text
-target x86_64-windows-gnu -O2 -std=c++20
-DNOMINMAX -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN
-DIMGUI_USER_CONFIG=<ark/imgui_config.h>
```

包含 `include`、匹配 ImGui、`stronghold`、`common`、`diagnostics` 和精简第三方后端目录；最终 `-shared`，链接匹配的 `libArkImGui.dll.a` 与 `-luser32`。不要在插件内再编一套生产 ImGui。

### 4.3 公开仓库与 Release 流程

- 仓库为 git@github.com:Noplch0/ArkACT.git，默认分支 main；首次公开版本 1.0.0 对应标签 v1.0.0，已于 2026-11-16 正式公开发布。不重写已有远程历史、不移动已发布标签。
- 当前公开 Latest 为 **v1.0.1**（2026-11-16），构建源 commit `3c33ea2787fb1d1322e0a38ef75d2ce6c437baf8`，Actions `43821344832` 成功；截至 2026-11-24，源码版本 1.2.0 已完成本地构建与验证但尚未发布，种子/Boss 主界面信息与商店记牌器均不在公开 v1.0.1 附件中。发布后的文档验证记录可继续提交 main，但不能据此移动版本标签或重传已验证的正式附件。
- .github/workflows/release-draft.yml 保留历史文件名，显示名为 Release，监听 v* 标签，也支持 workflow_dispatch 的既有 tag；标签必须与版本头一致并指向构建 commit。固定第三方 Actions commit，Windows runner 自动发现工具链，并设置 PYTHONUTF8=1，避免英文 runner 打印中文 Sheet 名时因 cp1252 中断。
- 用户于 2026-11-16 授权从 1.0.1 起不再停留为草稿，而是在验证通过后直接正式发布并设为 Latest。流程：锁定依赖 -> 九组测试/七个 TU -> Zig GNU x64 DLL -> inspect_binary/七项 Python/inspect_workbook -> tools/package-release.ps1 -> `gh release create --verify-tag --latest`。不带 `--draft`/`--prerelease`；验证/构建失败不进入发布步骤，已有同标签 Release（含残留草稿）拒绝覆盖，应先检查状态再决定恢复，不自动重传/替换正式附件。CLI 上传失败可能留下未完成 Release，必须检查，不盲目重跑或宣称成功。需要该 job 的 contents:write 权限，不需要额外个人 token secret。
- 发布附件只有 ArkACT-v<版本>-windows-x64.zip 与 .sha256。ZIP 含 plugins/stronghold_stats.dll、部署/README、GPL 与精简后端 MIT 许可及 build-info.json（源码 commit、依赖、DLL hash/大小）；不捆绑 ArkImGui/宿主/注入器。DLL 的最终哈希以附件 build-info 和校验和为准，不把本机旧 DLL 哈希写成 CI 哈希。
- .gitignore 排除 .build、.tools、原始 reference、真实诊断、导出报表和 dist 二进制。公开测试仅使用 tests/fixtures 的元数据声明/显式静态补充，不含运行态快照、UID、昵称值或游戏模块。参考源码需要时按上游固定 commit 另取；不把用户教程或完整 dump 推上仓库。
- 发布前实际审查暂存文件及账号隐私，使用 GitHub noreply 提交身份。隐私审计必须覆盖当前树、全部 reachable/unreachable Git blob、refs/reflog、远端分支与标签、Actions 日志、Release 正文和公开附件；公开内容不应含游戏 UID、昵称编号、私钥/令牌、个人邮箱或本机 Windows 用户名。`.build`、`.tools`、`reference` 和 PDB/导出报表即使被 Git 忽略，仍可能残留本机路径、会话哈希或玩家显示名，分享整个工作目录前必须另行排除或脱敏。推送 main 和版本标签后必须观察 Actions 结果；确认 draft=false、prerelease=false、标签和两个附件正确，且 `/releases/latest` 指向目标标签；重新下载校验 ZIP/清单/哈希，不能只提交 YAML 就宣称 Release 已完成。
- 本次版本重标不改变采集、归属、时钟、导出与只读玩法边界；内部 2.x 审计保留原编号。离线/CI 通过仍不代表联机实测通过。

### 4.4 宿主 ABI 与生命周期

- SDK ABI 为 1；初始化核验 host struct size、ImGui 版本和所需回调、输入、Hook API，不可盲用旧结构。
- 四个 C 导出必须存在：`ark_plugin_query`、`ark_plugin_initialize`、`ark_plugin_shutdown`、`ark_plugin_can_unload`。插件 query 声明零插件依赖。
- `initialize()` 先建立插件模型/归档状态、创建 UnityInput 对象并注册宿主输入适配器及 frame/draw 回调，随后返回等待运行时；它不代表元数据和采集 Hook 已同步绑定成功。实际的 `initialize_capture()` 与 `UnityInput::initialize()` 由 `on_frame()` 中的 `retry_runtime_connection()` 延迟连接/重试。未 ready 时不能让采集回调访问半初始化状态。
- 实际卸载顺序：先由 `can_unload()` 检查被捕获的按键/鼠标已释放，`UnityInput::shutdown()` 安全停止输入 Hook；再由 `shutdown_capture()` 禁止新采集、移除采集 Hook 并等待在途回调排空，释放采集器 Root；随后 `pump_events()` 消费复制后的事件并归档，最后解除 input adapter、销毁输入对象和插件状态。任一步失败都拒绝卸载，必须保留仍会被回调访问的状态，不能强行释放 DLL。
- 归档失败也要保留快照以便重试。宿主进入 shutdown 前可能已移除帧回调，**不能承诺卸载失败后后台还会自动重试**；用户需再次请求卸载。
- `ark_plugin_can_unload` 当前只是输入按键/鼠标预检，返回 true 不保证随后 shutdown 一定成功；Hook 排空、待写报表、输入 adapter 注销等检查实际发生在 shutdown。

## 5. 从零实现的数据管线

```text
游戏线程上的生命周期/Update/伤害 Hook
  -> 元数据校验 + 卫戍模式与会话识别
  -> 读取已结算 Modifier，解析来源与玩家/宿主干员
  -> 复制数值与字符串到 POD Event
  -> EventQueue（有界、线程安全）
渲染回调 pump_events()
  -> 纯逻辑模型（整局/轮次 × 战斗/协防）
  -> 只读 ViewSelection / ImGui 浮层
  -> ReportArchive / 纯 C++ XLSX 导出
```

跨线程只传复制后的事件，不能把 `Character*`、Manager/DataCenter、托管字符串地址或枚举器存进 UI/报表模型。游戏对象读取限于受控游戏线程；渲染、导出和卸载线程不要遍历玩法对象。对需要跨回调保留的托管引用使用现有 Root/GC handle 封装，不缓存裸地址冒充存活证明。

### 5.1 动态 IL2CPP 解析规则

使用 `common/il2cpp_runtime.*` 与精简后端从已加载 `GameAssembly.dll` 导出解析运行时 API，按 namespace + class + field/method 名称和**完整签名**查找。禁止把 dump 的 RVA、文件偏移、字段偏移直接写进业务代码。

- 核验值/引用类型、static/instance、返回类型、参数类型、by-ref、泛型闭合后的集合类型和可执行入口。新增 source/playerSide 的元数据证据在 `reference/current-schema/stronghold-source-schema.txt`，它是显式静态补充而非新局内导出。只按名字或参数数量找到一个方法不够安全。
- 当前 `Torappu.FP.ONE == 4294967296`（Q32.32），读取后除以该比例转为数值；比例/布局不符即拒绝采集，不把 FP 当 float/double 解释。
- `Modifier` 是 value type；其字段偏移由元数据取得，当前运行时报告包含对象头基准，因此代码减去 `2 * sizeof(void*)` 转为未装箱 `Modifier&` 内偏移，并用 value size 做边界检查。这是**经验证的动态布局处理**，不是允许写死旧字段偏移；运行时布局变更必须重新验证。
- `IS_CONTINUOUS_FLAG`、`IS_ENVIRONMENT_DAMAGE` 常量必须与支持逻辑一致；状态枚举值按名称解析，不能凭旧 dump 的整数复用。
- `TryGetValue(key, out value)` 的 out 存储大小由真实值类型决定：`ChessInst&` 对应指针大小，`Int32&` 才是 4 字节。
- `runtime_invoke` 只用于经审查的只读 getter/集合查询；不得因为名字像 getter 就默认无副作用。不要调用 Ensure/Create/Set/Add 等玩法方法来“帮助”查询成功。

### 5.2 当前元数据锚点与八个核心采集 Hook

核心类型：`Torappu.Battle.{Entity, Character, Projectile, Modifier, BattleLogger, BattleController, BattlePlayerData, BattleCharacterData}`、`Torappu.FP`；卫戍类优先位于 `Torappu.Battle.DataCenter` 与 `Torappu.Battle.AutoChess`，实际回退逻辑以 `bind_classes()` 为准。`ScenePlayerData` 是 `AutoChessPlayerDataModel` 的嵌套类，不是独立 namespace 类。

`NativeCapture::bind_hooks()` 安装下列八个核心入口，全部是实例 `System.Void` 方法；另有一个可选、失败不影响核心采集的隐藏全状态解析 Hook：

| 类 | 方法/参数 | 用途 |
| --- | --- | --- |
| `Torappu.Battle.Entity` | `OnAppliedModifier(Torappu.Battle.Modifier&)` | 主伤害入口，原函数执行后读取已赋值的 realDelta |
| `Torappu.Battle.BattleLogger` | `LogModifier(Torappu.Battle.Modifier&)` | 伤害后备入口 |
| `Torappu.Battle.BattleLogger` | `LogEpModifier(Torappu.Battle.Modifier&)` | 元素/相关 Modifier 后备入口 |
| `Torappu.Battle.BattleController` | `StartGame()` | 控制器生命周期、识别开始 |
| 同上 | `ClearRunTimeData()` | 清理前保存/结束；注意 Time 的真实大小写 |
| 同上 | `OnDestroy()` | 销毁生命周期兜底 |
| 同上 | `Update()` | 游戏线程状态与时钟采样 |
| `Torappu.GlobalInitializerAndUpdater` | `Update()` | Manager 尚未存在/消失时的全局生命周期采样 |
| `AutoChessBattleAllStateSyncData` | `Read(Torappu.DataStream.IStreamReader)`（可选） | 只读复制协议隐藏字段；value-type `this` 原始指针，失败不影响核心八 Hook |

框架 Hook ABI 只适用于已确认的 Windows x64 整数/指针寄存器参数形式（至多四个），不能把复杂结构体、浮点或栈参数方法直接套进同一 detour 模板。修改入口时连同原函数指针签名和 IL2CPP 附带方法参数约定一起核验。所有 Hook 的元数据/ABI 先完整解析，验证通过后才开始创建和启用；任一个关键入口不兼容就安全停采，不带病运行。

### 5.3 伤害事件：原函数时机与幂等

- `Entity.OnAppliedModifier` 是主入口；当前客户端 Logger 调用位于可选 XLua 分支，只 Hook Logger 可能昵称正常但永远无伤害。
- 原函数前保存当前阶段上下文，原函数后读取 `realDelta`。不能在扣血结算前读取默认 0，也不能修改传入 Modifier。
- 以 **线程 ID + Modifier 指针** 在受互斥保护的在途 applied 栈记录当前调用范围，抑制同一次应用内部触发的 Logger 重复记录；它不是跨 tick 地址去重，也不要求编译器 TLS。模型的 sequence 检查只过滤同序号/旧序号回放，不能替代采集入口的逻辑事件去重。
- **禁止按 Modifier 地址、相同伤害数值或短时间窗口跨 tick 去重。** 持续伤害会复用对象和数值，这样会吞掉合法 DoT。连续伤害标志只标识性质，仍并入实际总量。
- 读取 `get_source` / `get_originalSource`、`get_target` / `get_playerSide`、`get_finalDelta`、`get_damageType`、`get_isDamage`、`get_isHeal`、`get_isCancelled`、`get_isElementDamage`。完整直接来源优先；直接来源非空但回溯失败、玩家未知或宿主不完整时也尝试 originalSource。每个候选单独捕获 C++/托管调用错误，不让一次 getter 失败阻止安全回退；不尝试恢复访问冲突或猜地址。
- **原来源不能抢归属**：已有完整玩家/干员不被 originalSource 覆盖；玩家已知但 token 宿主不完整时，只接受同一玩家的完整宿主。多层召唤物和弹射物均限制 16 层并防环。回退前保留并检查未经替换的直接来源链；若明确为 Enemy，则直接过滤，不能让友方 originalSource 将敌方攻击重新归给玩家。
- **目标证据**：applied Hook 的 `self` 是权威受击对象；Logger 后备读取 `Modifier.target`。新增未知/环境回退只对运行时实际 `Torappu.Battle.Enemy` 实例的扣血生效，绝不单靠 `SideType.ENEMY` 判断（右侧队友也可能是该视觉侧别）。当前不将仅有“敌方 Token”标志的非 Enemy 对象作为已支持目标，缺证据计诊断，适配见第 6 节。
- **无来源保留**：没有可确认的来源，但受击对象为实际 Enemy、事件为已结算实际 HP 伤害且 `abs(realDelta)>0` 时，以 `kUnknownPlayer / unknown_effect / 无来源附加伤害` 保存。未知项参与“已采集总量”和占比，但不属于任何真实玩家/干员；可能包含无法区分的环境扣血，不可宣传为全是友方输出。全局未知桶没有可靠玩家时钟，DPS/HPS 安全为 0，不借用全房间时长。
- **环境标志不是自动丢弃条件**：带 `IS_ENVIRONMENT_DAMAGE` 的已确认敌人 HP 伤害可保留；有直接/原来源则按证据归属，没有则进入上述全局未知行。友方/未知/null 目标的环境事件、明确 Enemy 来源的攻击/自伤、取消事件和未知来源的治疗/元素积累不混入此回退。未知有效来源、保留的无来源伤害各自每轮至多一次 UI 告警；正常敌方攻击仅过滤，不触发不兼容告警。
- 诊断保留 `original_source_fallback/source_side_recovered/environment_recovered/source_errors/target_rejected/missing_host/side_conflicts`、当前与最后未归属样本；只记录类名、flags、数值和槽位，不记录 UID、昵称编号或昵称值。`unresolved_source` 同时可能包含被保留的全局未知项，不能单凭它断言丢伤害。计算/治疗/元素各自保持语义，不把治疗计入伤害。
- 当前模型 `damage_type_index()` 的类型快照为 1=物理、2=法术、3=真实、5=元素实际伤害，`StatsModel::add_to()` 将 type 4 计入治疗；`ElementalBuildup` 则只将 calculated_damage 计入元素损伤，不计进实际扣血排名。游戏更新须重新核对 `DamageType` 元数据及这两处消费者，不能因为事件都有 actual_damage 字段就全部当扣血。

### 5.4 玩家、干员与召唤物归属

当前客户端的主归属链：

```text
Modifier.source / originalSource
  -> 若为 Projectile：递归 Projectile.source
  -> Character
  -> 若为 token：BattleCharacterData.isToken / tokenOrHostUniqueId 回溯宿主
  -> 宿主 Character.cardUid（UInt32）
  -> AutoChessDataCenter.indexer.runtimeCard2Chess（ListDict<uint, ChessInst>）
  -> ChessInst.instId（Int32）
  -> instId2PlayerIndex（Int32 -> Int32）
  -> ScenePlayerData / PlayerCard.nickName
  -> (玩家索引, 宿主干员 ID) 聚合键
```

- 同时核对 `ChessInst.cardUid` 与 `ChessInst.playerIndex`，不能只拿一个看似合理的整数认定归属。
- `bind_indexer()` 根据字段存在性选择兼容分支：新结构 `runtimeCard2Chess -> ChessInst`；旧结构 `runtimeCard2ChessDict<uint,int> -> instId2PlayerIndex`。旧 `runtimeToken2CardDict` 只是反向运行卡片索引，**不是召唤物宿主表**，新结构中不能强制要求它存在。
- 宿主角色查找以 `BattlePlayerData.characters/tokens` 及 `BattleCharacterData.uniqueId/isToken/tokenOrHostUniqueId` 为依据，递归必须限制深度并检测循环。陷阱/弹射物的归属也走同一来源链，不另猜玩家。
- `rebuild_host_cache()` 从 `BattleController.get_playerDataList` 遍历上述角色数据；干员 ID 优先 `id`、回退 `tmplId`，名称优先 `nameCn`、回退 `nameEn`。`resolve_character()` 也会使用 `Character.get_characterId`/`get_data` 补全；名称只能作显示，缺名称时导出转换为“未知干员”，不能把 ID 当用户可读名称泄露出去。
- `rebuild_host_cache()` 在正常 250ms 运行态 tick 每次刷新，不能只在玩家人数或轮次改变时刷新；动态生成的 token 也会即时登记其宿主链。同一轮内保留**复制后的**宿主描述和经卡片索引验证过的 owner，支持宿主离场后的迟到弹射物/DoT；token 宿主链改变或 owner 冲突时使旧归属缓存失效。新会话、换轮、controller 更换/新战斗、离开模式必须清理，不能跨生命周期沿用重用 UID。
- `live_host_cards_` 仅表示当前扫描到的角色，覆盖范围只用它和真实伤害事件，不能从保留的 `hosts_ / host_players_` 历史描述伪造仍在模拟的玩家。扫描失败清当前集合与 side 映射，但可保留本轮复制的宿主用于迟到事件。
- 非 Character 的来源 Entity/陷阱代理，可读取其**自身继承的** `BObject.get_playerSide()`。仅当 `BattlePlayerData.playerSide` 下的角色经实际卡片索引指向唯一玩家，且 side→player 和 player→side 均无冲突时，才能将明确 Enemy 目标的附加伤害计入该玩家的 `unknown_effect / 未归属附加伤害` 行；不能猜干员。
- `PlayerSide` 不是房间槽位，不能用 enum 数字/顺序直接换算 P1–P4。`Modifier.playerSide` 是调用者指定的上下文，当前仅记诊断，不用于推定玩家；它与来源 `BObject.playerSide` 不可混淆。`INVALID` 由运行时名称解析并排除。字段和 getter 都校验精确 `Torappu.PlayerSide` 类型。
- 来源判友方不能只按左右场地的 `get_initSideType` 一刀切：右侧模拟角色若有已验证 AutoChess 玩家归属也应允许统计。无有效玩家映射且不是友方的来源才被侧别过滤，避免把队友全当敌方。
- 玩家当前 raw index 为 1–4，对应 P1–P4，不再 +1；0 回退 P?，负数视为未归属。不要把 room list 下标、显示槽位和实例 ID 混用。
- nickname 只作显示，不作身份或聚合键；不采集 `nickNumber`。结构里若仍有未使用兼容字段，不等于运行时还在读取/报表导出该值，修改前核对读写链。
- 当前未知干员显示可读回退名称，不在报表里暴露内部干员 ID；保留问题标记供定位，不用名字反推身份。

### 5.5 卫戍模式、会话与轮次

- 先查 `AutoChessGameManager.get_instanceOrNull`，确认 Manager 存在后才读 `manager.dataCenter`。**不要直接调用 AutoChessDataCenter.get_instanceOrNull**：已见客户端在内部解引用空 Manager，局外会抛 `System.NullReferenceException`。
- 会话身份组合 `stageRandomSeed + 初始固定 UID 名单 + DataCenter identity`。UID 仅用于内部身份比对，不写日志/报表；退出改变在线状态，不改变固定名单，避免误触发新会话。
- 新模拟开始发 SessionStart，清旧模型和 UI 选择；跨轮发 RoundStart，只切轮快照，不清整局。手动重置请求必须等新 SessionStart 成功后再消费，绑定暂时失败不能吞掉请求。
- `currentRound` 正值直接作为显示轮次；`raw <= 0` 准备值映射为 1。
- **`AutoChessSettleDataModel.endingStatus != 0` 不能代表会话已结束。** 该值仅诊断；曾错误用它停采，导致局内能读到昵称却所有伤害为 0。
- 真正游戏 END 锁定结束墙钟时间并只发布一次 SessionEnd；Manager/DataCenter 消失作为兜底。阶段 END/SETTLE 与最后连续 Modifier 落在同 fixed tick 时允许继承前一阶段；正常 Update 确认终态后清该 latch，不能无限延长旧阶段。

### 5.6 每玩家时钟与协防分离

`NativeCapture::bind_timing()` / `sample_timing()` 是计时适配入口。基本原则是**先用旧阶段/旧 timing mask 结算刚过去的间隔，再读取新状态用于下一段**，避免把等待时长补进协防。

- Manager 的 `gameStatus.get_currentState` 按名称解析 `BATTLE`、`BATTLE_WAITING`、`HELP_BATTLE`、`BOSS_BATTLE`、`PAUSE`、`SETTLE`、`END`。
- 玩家有效计时需 `ScenePlayerData.gameState == BATTLE_NOT_COMPLETE`、连接 `ONLINE` 且 `!get_isDead()`。淘汰只由 `isDead` 确认，不能凭 hp==0 或 gameFinished 猜。
- 主战还需查询 `battleStatus.m_selfBattleStatusInfo.TryGetValue(playerIndex, out status)` 的 `battleFinished`。只查已有行，不调用可能修改状态的 Get/Ensure 路径；缺行保留已确认的 gameState 判断并告警，不能猜该玩家已打完。
- 自己进入 `BATTLE_WAITING` 时冻结自己的主战时间，其他仍未完成的玩家继续各自计时；不能冻结整房间，也不能让自己的 DPS 在等队友期间持续下降。
- `HELP_BATTLE` 单独计 Assist。参与者优先用有效 `battlePlayerIndex`（至多两人），否则用有效 `helpBattleData.players`；冲突或缺失要告警，不用“所有在线玩家”伪造参与名单。
- 协防名单外观测到的真实伤害仍保留，并提示计时/DPS 不完整，不能为了漂亮 DPS 丢实际伤害。
- 当前客户端 Help/Boss 的 `get_battleFinished` 原生实现固定 false，不能将其作为完成判断。Boss 归 Main，不能误混到协防。
- 明确退出/掉线/淘汰后保留历史总量并冻结计时；连接状态未知时保留可归属的有效伤害，但不凭空增加未知战斗时间，显示不完整警告。
- 模型中的个人 `active_time` 是 DPS/HPS 分母，所选 view 的 `battle_seconds` 不是每个玩家共用的分母。零时间必须安全输出 0，不能 NaN/Inf 或除零。

### 5.7 覆盖范围、队列与模型

- 将真正观察到的本地角色卡片归属与有效伤害事件集合，对比在线房间玩家集合；不能把房间名单、画质档位或 `battlePlayerIndex` 本身当成本地模拟证明。
- 角色可能早于索引就绪，首次归属失败后必须在后续游戏帧重新确认，不能永久缓存为“只有自己”。
- `EventQueue` 容量当前 32768；每次 publish 分配新 sequence，模型拒绝 `sequence <= last_sequence` 的陈旧回放。相同逻辑伤害以**新序号**重复 publish 仍会重复统计，必须靠入口去重解决。溢出必须显式标记不完整，不可悄悄丢事件后仍声称完整。
- Main/Assist 各自维护 session、current round 和 completed rounds；Idle 不计伤害时间。轮结束保存只读快照，历史数据不再被当前计时污染。
- `find_round_view(round, phase)` 读取历史或当前阶段视图，不存在返回 nullptr，不能自动创建空历史轮。
- `ViewSelection.selected_round == 0` 表示跟随当前轮，其余为固定历史轮；只在 SessionStart 重置选择，不在每次 RoundStart 强制重置。

### 5.8 界面、局外查看与导出

- HunterPie 式紧凑浮层：玩家按实际伤害降序，显示昵称/P 槽位、伤害、DPS、占比、条形图；展开显示所属干员，自己高亮。
- 两个切换按钮分别控制整局/本轮和战斗/协防；标题显示“范围-阶段”。本轮模式提供历史轮选择及前后翻页。
- 行是否显示根据**所选阶段**的实际数据/时长和对应 live timing mask，不能拿另一阶段成员掩码显示一排假零玩家。
- 表格使用剩余空间、固定表头并可滚动，详细说明进 popup；不要恢复占据底部的大块不可见 child 区域。
- 局外只读显示上一局，不能因当前 AutoChessDataCenter 不存在就把缓存清空/窗口全隐藏。无数据且不在模式内时才按现有 UI 策略处理。
- 插件已加载但尚无 Session 时，不得因 `statistics_visible()` 为 false 而完全隐藏窗口：显示同一 window ID 的局外等待状态，明确“正在连接/运行时已就绪，等待进入卫戍协议/版本不兼容”；只有已有历史 Session 时才显示统计表。该窗口不回退为猜数据，也不改变 Session 生命周期。
- 主窗口以手动标签在“伤害统计/记牌器”间切换；新 Session 默认重置为伤害统计页。记牌器内可切换“出现频率/玩家干员”，出现频率再按“干员/盟约”查看，并可按 P1–P4 筛选。
- 记牌频率按每个货架槽计数；同一有效 `refreshSeq` 的重复快照不重复统计，冻结货品在新序号中留在相同槽位时不重计。没有可用序号时按槽位内容变化降级计数并标记不完整；同结果刷新无法从快照差异中识别。干员频次同时累计到其全部基础盟约。
- 玩家干员表按实际干员实例数显示获得、可确认的商店获得、推定卖出和持有。analytics 获得增量与持有增量取较大值而非相加；第一次看到的持有阵容作为基线。售出只由持有数下降推定，商店来源只在同一刷新序号下获得记录与货架槽位消失可对应时标记。无法关联的来源不猜；黄金/高星干员不折算为基础副本数。
- 渲染线程只消费复制的 `ShopObservation`、`capture_hidden_sync()` 和 `capture_match_intel()` 快照，不能解引用 DataCenter、Character、List 或托管字符串；不得把原始 UID/昵称编号放入快照或导出。主界面显示当前 Session 的 `stageRandomSeed` 和与所选隐藏 Boss ID 匹配的名称。
- UI 主导出为一个 XLSX：整局累计 Sheet + 每轮 Sheet，每张按战斗/协防分行；报表阶段文字实际为“主战/协防”，UI 按钮为“战斗/协防”。保留兼容 CSV 代码不意味着需要恢复 CSV 主导出。
- 当前 15 列依次为：**阶段、维度、轮次、玩家槽位、玩家、干员、实例数、实际伤害、战斗时间(秒)、DPS、伤害占比、计算伤害、治疗量、HPS、元素损伤**。
- 不导出原始 UID、昵称编号、干员 ID、统计范围或单独状态列；在线/中途退出等状态附在玩家显示名括号中。P1–P4 是房间槽位，不是账户昵称编号。
- 数值单元格必须是数值；占比保存数值并使用百分比 style，不拼成文本；昵称等用 OOXML inlineStr，不能形成表格公式。正确转义 XML 和处理 UTF-8 中文。
- 报表占比分母为所选范围、所选阶段的 `View::actual_total()`（含未归属实际伤害），干员行也是对该总量占比，不是“玩家内部占比”。可归属玩家时长用自己的 active_time；未归属行暂用该 view 的 battle_seconds 回退，不能将这个回退值宣传为已确认的个人 DPS。
- 完整结束后用**结束时的本机本地时间**命名 `yyyy-MM-dd HH-mm.xlsx`，例如 `2026-11-14 19-30.xlsx`（Windows 文件名不能用冒号）。同分钟冲突添加 `(2)` 等后缀，不覆盖其他对局。
- 进行中或中断使用含 pid/session 的备用名称，不冒充正常整局结束时间；导出失败保留快照以便重试，重试应保留原来锁定的结束时间，通过临时文件/原子替换避免半写文件。
- XLSX 实现是自包含 ZIP store + CRC32 + OOXML，无压缩库/Office/外部报表运行时依赖；改生成器时要同步独立解析检查，不仅测试“文件存在”。
- **Excel 工作簿属性兼容性（1.0.1）**：`make_workbook_xml()` 不写可选 `fileVersion`。旧 `<fileVersion appName="xl"/>` 虽能通过 XML/ZIP 校验，却会让 Excel 16.0（本机 build 20527）正常打开失败，交互打开提示修复 `/xl/workbook.xml` 工作簿属性。保留实际 `workbookPr`、视图、Sheet、样式与数值；不要为修复它删除数据、隐藏警告或请求 Excel 自动修复。导出器不是 Excel，不伪造 Excel 编辑版本。独立检查器的受支持产物策略允许省略该元素；若存在，只接受同命名空间、直属且完整的已验证 Excel 版本属性组（`appName=xl` 与数值 `lastEdited/lowestEdited/rupBuild`），这不是完整 OOXML Schema 验证器。
- XLSX 兼容性修复只影响新导出/重新导出文件，不自动扫描或重写用户旧报表。原始报表、真实玩家信息和本地复现副本不进入 Git。原生 Excel 检查仅是开发机的可选验证，不成为插件运行或无 Excel CI 的依赖；也不代表游戏实机验收。

### 5.9 商店刷新机制的静态研究边界（非采集功能）

本节记录 2026-11-22 对现有客户端元数据和本地静态反汇编的研究结论。它不改变统计插件的只读采集、归属、报表或发布契约；没有据此新增 Hook、请求发送或玩法写入。证据为 `reference/current-schema/stronghold-schema-4708.log`、`.build/analysis-2.0.7-methods.tsv` 及本次在 `.build/shop-analysis-2026-11-22/` 生成的静态反汇编。`.build` 内容仅为本地证据，不是干净检出的构建输入，也不进入公开 Release。

- **联机权威边界**：`AutoChessBattlePreparationStatus.store` 随准备状态快照下发；商店状态字段与 `ShopData`/`AutoChessShopDataModel` 对应。`AutoChessBattleSceneShopRefreshUp` 和 `ShopUpgradeUp` 只有协议 ID、没有客户端随机种子/商品列表/价格参数，`ShopBuyChessUp` 只带 `slotId/isSpecial`，`ShopFrozeUp` 只带布尔值。因此商品生成、扣费校验、刷新结果和失败回滚不能仅凭客户端代码断言由本地决定；客户端可见的同名方法还普遍存在 XLua Hotfix 覆盖桩。
- **状态模型**：`ShopData` 包含 `level`、`currentCoin`、`upgradePrice`、`refreshPrice`、`goodsType`、`freeRefreshCnt`、`specialRefresh`、`chessGoods`、`refreshSeq`；每个 `ChessGoods` 包含 `chessId/slotId/price/isFrozen`。`goodsType` 数值为 `NORMAL=0`、`COMB=1`、`BUFF=2`。`refreshSeq` 用于 UI 识别一次新刷新，不应被当作购买事务 ID。
- **等级与槽位**：`ActAutoChessShopLevelData` 提供 `initialUpgradePrice/charChessCount/itemCount`；原生 UI 的槽位数是同一等级配置的 `charChessCount + itemCount`。最大等级取当前 mode 的 `shopLevelDataDict` 最大键；升级初始价由各等级 `initialUpgradePrice` 生成映射，实际价格以运行时 `upgradePrice/refreshPrice` 为准。技术文件未保存具体模式/赛季的等级表数值，不能把历史截图或通用自走棋经验写成当前常量。
- **普通刷新（本地原生回退路径）**：`ReqShopRefresh()` 在未拖拽锁定时读取 `refreshPrice`；`currentCoin < refreshPrice` 直接返回。成功路径先清空商品列表、`refreshSeq++`、扣除 `refreshPrice`，再执行 `_RefreshShopData(false)` 并更新 DataCenter/UI；`_RefreshShopData` 随后按当前等级目标数量补货。`freeRefreshCnt` 是独立状态并由 UI 显示，但该原生回退没有直接按计数做免费扣减分支，免费次数最终如何结算必须以服务器/热更实现和局内观测为准。
- **冻结的静态边界**：`ReqShopFrozen(bool)` 对当前商品列表逐项写入同一个 `isFrozen` 值；`AutoChessShopDataModel.allFrozen` 汇总为 UI 三态（全冻/部分/全解），协议下行 `ShopFrozeDn` 带 `uidIdx/isFrozen`。`_RefreshShopData` 内部另有“未冻结项移除、冻结项保留”的循环，但原生 `ReqShopRefresh()` 在调用它前已经清空整个列表，且这些方法存在服务器/XLua Hotfix 覆盖。因此现有静态代码只能证明“逐项冻结状态和 UI 语义”，不能单独证明手动刷新后冻结项必保留；该跨刷新规则需用当前联机状态或热更实现确认。
- **升级**：本地回退在金币不少于 `upgradePrice` 时只提升一级，从下一等级配置取新的 `initialUpgradePrice`，并走升级后的商店状态更新；等级表没有下一级时不升级。联机只发送 `ShopUpgradeUp`，由服务器重算价格、等级和商品。
- **特殊/奖励货架**：本地 `_RefreshBonusShop()` 在普通货架状态下将 `goodsType` 设为 `COMB(1)`，清空普通商品后按 `min(当前等级 + 1, 训练配置上限)` 选择特殊商品，最多补 3 个槽；`specialRefresh`/`isSpecial` 是独立于普通刷新的状态与购买参数。该路径首先在训练/本地模拟桥接中确认，不能直接套用到所有联机模式。
- **轮次刷新**：本地 `_MoveToNextRound()` 推进轮次后调用 `_RefreshShopData(false)`，所以进入新一轮准备阶段会执行普通补货，冻结项仍按上述规则保留。联机准备阶段由 `AutoChessBattlePreparationStatus` 整体同步，客户端不再以本地生成结果为准。
- **随机与概率未知**：商品候选关联 `ActAutoChessCharShopChessData`、`ActAutoChessTrapShopChessData`、`shopLevelDataDict` 和 `AutoChessRandomWrapper`，但当前公开/私有技术文件没有给出各等级概率权重、候选池去重规则、玩家共享池或保底表。不得从类名、字段顺序或通用自走棋规则推导具体百分比；若后续需要精确概率，必须补充当前客户端的实际配置表或协议/局内抓取证据，而不是继续猜测静态偏移。

### 5.10 对局种子、怪物生成与 ban 信息的可读边界

- **存在可读的种子字段**：活动存档 `PlayerAutoChessV1Activity.AutoChessGame.seed` 有 `seed:Int32`；场景数据 `SceneGameData.stageSeed` 有 `stageSeed:Int32`；战斗中心暴露 `AutoChessDataCenter.stageRandomSeed`，战斗控制器另有 `BattleController.randomSeed`（内部为 `ObscuredInt`）和 `randomImp/randomTrivial` 两个随机流。它们不是同一个字段，不能默认认为数值相同。
- **场景 seed 的可验证消费点**：`AutoChessDataCenter.InitData(SceneGameData, ScenePlayerStaticData)` 会复制 `SceneGameData.stageSeed` 到 `stageRandomSeed`，随后以该值调用 `BattleController.ResetSeed(int)`。原生 `ResetSeed` 通过 `BattleRandomFactory.Create(int)` 创建/替换 `randomImp` 与 `randomTrivial`，并重置 PRD 随机状态。因此 `SceneGameData.stageSeed` 是**本地可读取且确实参与战斗随机流初始化**的最明确种子。
- **活动 seed 的边界**：`AutoChessGame.seed` 是活动/对局存档的一层字段，但现有技术文件没有给出它到 `SceneGameData.stageSeed` 的完整赋值链，不能断言二者相等，也不能把它当作当前局内唯一根种子。
- **怪物种类**：`SceneGameData.enemyTypes` 是显式字符串列表，和 `stageSeed` 并列存在；`AutoChessDataCenter.InitData` 会复制它。`AutoChessLevelEnemyManager.Init` 再初始化 `RandomEnemyGenerater` 并生成 round enemy data。也就是说，怪物生成同时依赖显式 `enemyTypes`/关卡配置和随机流；只知道 `stageSeed` 不足以独立算出最终怪物列表，还需要完整配置、轮次、RNG 调用顺序和服务器下发的显式数据。
- **盟约 ban 位**：`SceneGameData.bannedBonds` 与 `SceneGameData.selfBannedUnits` 是显式列表，`AutoChessDataCenter` 分别有 `bannedBonds` 和 `selfBannedUnits` 属性；`InitData` 直接复制这些列表。现有客户端文件没有展示从 seed 本地重新计算 ban 位的算法，因此应把它们视为服务器/场景输入结果；不能仅凭 seed 复现或断言 ban 位由客户端随机决定。
- **其他随机器**：`AutoChessRandomWrapper.Reset(int)` 会用 `LegacyRandom(seed)` 初始化并生成缓存，说明它是可种子化的独立随机器；但现有调用链没有直接证明所有 wrapper 都由同一个 `stageSeed` 初始化。`BattleController` 的 `randomImp/randomTrivial` 与 AutoChessRandomWrapper 也不能混为一谈。
- **协议中的可见性**：`AutoChessBattleSceneAllStateSyncDn.allStateSyncData` 的字段包含 `stageSeed`、`bannedBonds`、`enemyTypes` 和 `staticPlayers`；`AutoChessBattlePlayerStaticInfo.bannedUnits` 另有每个静态玩家的 ban 单位列表。因此只要已经获得客户端可解析的全状态同步数据，seed、怪物类型集合和 ban 列表是直接可读字段，不需要从 seed 反推。未授权解密、绕过传输层保护或修改/伪造协议不属于本项目工作范围。
- **在线与本地来源**：`AutoChessBattleSceneAllStateSyncDn` 是服务器下发协议，`AutoChessBattleAllStateSyncData.Read` 从下行数据读取 `stageSeed`，所以联机对局的 `stageSeed` 来源是服务器。另有 `AutoChessDataBridgeLocalPlayer.GenerateLocalSave` -> `_GenerateTrainingData` 的本地教学/模拟构造路径，不能把该本地路径的生成规则套到联机房间。
- **一局中可见的种子/结果目录**：活动存档层 `AutoChessGame.seed`；场景层 `SceneGameData.stageSeed`；数据中心副本 `stageRandomSeed`；战斗控制器当前 `randomSeed` 及其 `randomImp/randomTrivial`；另外有可独立设置种子的 `AutoChessRandomWrapper`。它们不是固定数量的同一个 seed，必须分层记录。显式结果或输入还包括 `stageId/modeId/bossId/hiddenBossId`、`enemyTypes`、`bannedBonds`、`staticPlayers[].bannedUnits`、玩家阵容与 `store`；这些字段可以直接缩小范围，但不应伪装成从 seed 精确计算。
- **结论**：本地“能读到种子”是事实，但不能把单个整数当成“整局行为根因/可离线重放的完整钥匙”。若要做到可复现，需要同时保存：活动/场景 seed、`stageId`、模式、`enemyTypes`、`bannedBonds`、`selfBannedUnits`、当前轮次、客户端版本与配置，以及实际 RNG 流；其中 ban 和 enemyTypes 的直接值不能由现有客户端代码从 seed 反推。即使拥有 seed 和全状态快照，玩家操作、服务器校验和其他 RNG 消费者的时序仍影响最终对局。
- **战斗随机流的轮次派生**：`AutoChessGameManager.DoLoadGameInternal` 读取 `stageRandomSeed` 和 `currentRound`，在战斗状态下以 `stageRandomSeed + currentRound` 构造 `AutoChessRandomWrapper`，再通过 `BattleController.ResetImpRandom` 替换 `randomImp`；初始 `ResetSeed(stageSeed)` 还创建/重置 `randomTrivial` 和 PRD 状态。直接调用 `randomImp` 的游戏侧消费者包括 `RandomEnemyGenerater._GenerateRandomEnemyData/_GenerateSpecialEnemyTypesLocal`、`AutoChessEnemyPreviewManager._DoSpawnPreviewEnemy`、`AutoChessGarrisonManager` 的随机羁绊选择、`AutoChessDataBridgeLocalPlayer._GiveChessViaShopLevel`、`AutoChessAdvancedSelector` 以及通用技能/目标/弹道随机逻辑。活动层 `AutoChessGame.seed` 的客户端直接消费者仍未找到，不能声称其精确控制字段。
- **`randomTrivial` 与 PRD 的边界**：`randomTrivial` 的直接消费者主要是投射物初始化、随机效果旋转、随机发射器选择、部分移动/位置和部分场景元素；这些是通用战斗随机系统，是否在某一 AutoChess 干员/效果中实际触发取决于该功能是否调用。`ResetSeed` 同时调用 `RandomExtensions.ResetPRDRandom`，而 `DicePRD` 使用独立的 PRD 实体/类别状态；`stageSeed` 只能确定其重置/初始随机状态，具体概率效果由实际 `DicePRD` 调用者决定。
- **随机敌人数据结构**：`RandomEnemyGenerater` 持有 `m_randomNormalEnemyResult`、`m_randomEliteEnemyResult`、`m_randomSpecialEnemyResult`、`m_specialEnemyTypeResultTypeList` 和 `m_templateEnemyInfo`。`AutoChessRandomEnemyAttributeData` 的候选字段为 `enemyKey`、`level`、`extraEnemyIdentifier`、`extraEnemyKeyList`、`isFlyEnemy`、`enemyBattleEffectivenessFactor`；`AutoChessEnemyTypeData` 还有 `type`、`typeIdentifier`、`involveRandom`。这些结构把“随机敌人数据”定义为可选敌人键、等级/附加敌人信息、飞行属性和战斗效能参数，而不是仅一个敌人数值。
- **特殊敌人类型与配置**：`AutoChessSpecialEnemyType` 的枚举为 `NONE/FLY/TIMES/ELEMENT/DOT/INVISIBLE/REFLECTION/SPECIAL`；`AutoChessRandomEnemyType` 分为 `NormalWalk/NormalFly/EliteWalk/EliteFly/SpecialWalk/SpecialFly/NonTemplateWalk/NonTemplateFly`。特殊敌人配置 `ActAutochessSpecialEnemyTypeEntry` 提供 `count/countSingle/weight/showEnemyId`，`ActAutochessSpecialEnemyEntry` 提供 `type/specialEnemyKey/randomWeight/isInFirstHalf/attachedNormalEnemyKeys/attachedEliteEnemyKeys`。实际敌人 key 和具体权重由模式配置表/活动数据决定，当前 schema 不包含当前赛季的具体表值。

### 5.11 隐藏全状态同步（1.1.0）

全状态同步只读 Hook 不参与伤害排名、计时、会话身份或玩法写入。它用于复制 `stageSeed`、`hiddenBossId` 及所有玩家准备状态字段；`AutoChessBattleAllStateSyncData.Read(Torappu.DataStream.IStreamReader)` 是 value-type 实例方法，原生入口的 `rcx` 是原始结构指针、`rdx` 是 reader；Hook 调用原函数后，按字段元数据减对象头读取并复制，再发布互斥保护的 `HiddenSyncSnapshot`。

- **根字段**：`hiddenBossId/stageSeed/staticPlayers/sceneDetail`。其中 raw offsets 由 `il2cpp_field_get_offset - 2 * sizeof(void*)` 得到；不能写死 RVA/偏移。
- **保留字段**：同步快照可复制 `bannedUnits`、`preparationStatus.store`、`charChess` 与 `roundAnalytics.chessGained`，但面板不渲染 ban 或完整货架；`sceneDetail.roundEnemies` 不再读取/展示。
- **Boss 名称**　游戏线程从 `AutoChessDataCenter.gameData` 读取 `Torappu.AutoChessData.bossInfoDict`，按隐藏 Boss ID 查询 `AutoChessBossInfoData`，使用 `enemyId` 或 `handbookEnemyId` 查询 `EnemyHandBookDB.get_data()` 所给图鉴数据的 `enemyData` 字典，读取 `EnemyHandBookData.name`。活动 `ActAutoChessData.bossInfoDict` 的条目描述权重和血量等活动配置。两个 `TryGetValue(String, …&)` 的字符串参数直接传托管引用，输出引用传地址。仅有同步 ID 时，从同一 Boss 字典中有界读取匹配的已有键。图鉴绑定失败不影响核心采集。
- **主界面显示**　显示当前 Session 的种子和 Boss 名称。名称快照的 ID 必须与当前选中的隐藏 Boss ID 一致。已读到 ID 但缺名称时显示「名称未读取」，缺 ID 时显示「不可用」。普通可见阵容/货架不重复呈现。
- **可选边界**：`AutoChessBattleAllStateSyncData.Read` 不是核心兼容门槛；类/字段/方法/宿主 Hook API 缺失时只禁用对应快照功能，八个伤害/生命周期 Hook 仍可运行。字段缺失不得用 0/空值伪装正常。
- **隐私/线程**：快照不得保存原始 UID/昵称编号；回调线程按需附着 IL2CPP，所有 managed 访问在回调内完成，渲染线程只读复制数据。Hook 不调用 `Write`、刷新、购买、冻结、`Ensure/Create/Set/Add` 或任何玩法/网络写入口。新对局切换 Session 后清理记牌数据，离开模式保留当前局供查看。

### 5.12 商店记牌器（1.2.0；功能引入于 1.1.1）

- **观察入口**：`refresh_match_intel()` 将本机 `AutoChessDataCenter.shopDataModel.chessGoods` 的可读货架独立发布为 `ShopObservation`。可选的 `AutoChessBattlePreparationStatus.Read(IStreamReader)` 在原函数完成后复制单玩家准备状态。AllStateSync 回调也复制内嵌的 `preparationStatus`，覆盖可能只在全状态同步中出现的队友状态。三个入口共享有界队列，由 `ShopCounter` 消费。
- **名称和身份**　协议 `chessIdentifier` 从本局 `ActAutoChessData.charChessDataDict` 的 `identifier` 对齐棋子，原生 `GetChessBasicInfo(Int32)` 的参数属于部署实例 `instId`。本机 `ChessGoods.chessId` 优先采用 `GetChessBasicInfo(String)` 的干员和基础盟约信息，也可按棋子 ID 匹配活动配置。采集在游戏线程从 `charShopChessDatas` 读取角色 ID，调用 `CharacterUtil.GetCharName(String)` 取名称，并用 `bondInfoDict` 取 `ActAutoChessBondInfo.name`。`GetBondInfo(String)` 按返回类型绑定嵌套配置作为本机描述路径。`trapChessDataDict` 可排除已确认的其他商品。缓存只保留复制后的数据，随新局替换。界面仅用 ID 聚合，缺名称时显示「干员名称未读取」或「盟约名称未读取」。同键后续获得名称可补齐已有统计行。
- **去重与频率**：以玩家槽和轮次分桶，有序号时按 `refreshSeq` 划分货架。同序号内相同槽位的同一干员只记一次，新槽位或新商品可补计，序号不变却产生新记录时标为不完整。序号递增时统计新货架，同一干员前后均冻结并留在同槽时跳过。过期序号不回滚货架，序号回退或跳跃标为不完整。缺序号时按相邻货架槽位内容变化降级统计，缺有效槽位时用列表顺序。相同结果的刷新可能漏记。
- **跨回合冻结**：每位玩家的首个有效货架快照以其上一相邻回合最后观察到的冻结商品为基线。开局货架分批观察时，仍按同玩家、同槽位、同干员且前后均冻结识别保留商品，干员和盟约频次都沿用原计数。每个槽位首次观察到可识别的商品后消耗匹配资格，确认本轮刷新序号递增后清除剩余资格。上一回合没有有效基线时，按首次观察计数。
- 干员和盟约的出现频率按所选玩家范围取值并降序排列。选「全体」时显示已采集货架的合计和 P1–P4 分列。选单个玩家时只显示在其货架出现过的条目及其出现次数。
- **获得/持有/售出**：`roundAnalytics.chessGained` 的元素为 `AutoChessBattleChessCountInfo`，`charChess` 的元素为 `AutoChessBattleCharChess`，两个类型分别绑定 `chessIdentifier`。轮内累计获得增量与持有数量增量按干员取最大值，首次持有快照只设基线。持有数下降累计为“推定卖出”，可能包含其他减少原因。只有持有/获得增加与同序号货架槽位消失相匹配时才记“其中商店”。所有数量按干员实例数。
- **范围和边界**：统一显示为 P1–P4，名册含 0 且不含 4 时启用零基槽位兼容，协议槽位与名册的对应关系仍待实机核对。队友只在本机实际可读他们的准备数据时纳入。本机定时采样可能跳过两次采样之间的刷新。数据仅在插件内存中保留，队列溢出和刷新序号缺口会标为不完整。

## 6. 游戏更新后的适配流程

以下章节给出升级排障步骤与入口替换准则。适配必须以当前元数据、实际调用链和测试证据为依据，不允许猜偏移或靠吞异常恢复假正常。

### 6.1 先建立可比较的客户端证据

1. 记录插件版本、宿主/ArkImGui 版本、DLL SHA256、游戏版本/模块时间戳、复现人数、实际发生故障的阶段、重载时机。不要仅凭文件修改时间认定用户加载了新 DLL。
2. 优先请用户在**局内、实际有伤害发生后**点“导出诊断”；计时故障最好分别在主战、等待、协防采样。不要为了拿日志让用户退出正在玩的对局。
3. 当前报告路径为 `<ArkFramework root>\diagnostics\stronghold-schema-<pid>.log`，同进程再次导出会覆盖原文件；需要对比时复制到带日期/阶段的项目证据目录。不要把用户的原始附件覆盖成自制夹具。
4. 报告中 `Snapshot tick` 是**采样时刻，不是导出时刻**；`CURRENT`、`LAST IN-MODE SNAPSHOT`、`LAST COMBAT SNAPSHOT` 和最多 16 个状态变化历史要结合看。计数器从插件加载起累计；退出后 CURRENT 无 Manager 并不意味着局内也没有 Manager。
5. 诊断导出可在局外枚举元数据，并附上此前游戏线程复制的状态字符串；**不能为导出最新数据在渲染线程重新遍历 live DataCenter**。插件重载后旧内存快照不再存在。
6. 报告缺失类型时先确认 `REPORT COMPLETE` / `REPORT INCOMPLETE`、模块加载和导出解析情况。泛型实例和继承的 closed singleton 方法不一定出现在普通 image 类列表中，缺记录不等于运行时一定不存在。

当前本机游戏文件定位线索（仅只读，换机先核实路径）：

```powershell
$gameRoot = 'D:\Program Files\Hypergryph Launcher\games\Arknights Game'
$assembly = Join-Path $gameRoot 'GameAssembly.dll'
$metadata = Join-Path $gameRoot 'Arknights_Data\il2cpp_data\Metadata\global-metadata.dat'
Get-Item -LiteralPath $assembly, $metadata | Select-Object FullName,Length,LastWriteTime
Get-FileHash -LiteralPath $assembly, $metadata -Algorithm SHA256
```

路径不存在时按用户安装目录重新定位，不扫描/修改整个磁盘，也不要把不同更新批次的 GameAssembly 与 metadata 配对。静态研究输出放项目证据目录，不放游戏目录。

### 6.2 先走诊断漏斗，再改代码

按下列顺序找第一个失败层。上层不通时不要盲改下层；一个昵称能读到，只证明房间数据的一小部分可读。

| 层 | 核对证据 | 首要源码位置 / 处理 |
| --- | --- | --- |
| 1. 宿主与渲染 ABI | 四导出、ABI=1、host size、ImGui 版本、回调/输入/Hook API | `include/ark/plugin.h`、`plugin.cpp::host_compatible/initialize`；先解决宿主兼容，不碰游戏偏移 |
| 2. IL2CPP 模块/导出 | `GameAssembly timestamp/size`、`Resolved exports`、`MISSING EXPORT` | `common/il2cpp_runtime.*`、精简 API resolver；分清尚未初始化和真正导出变更 |
| 3. 元数据绑定 | 类、继承、字段类型、方法签名、闭合集合 out 类型 | `NativeCapture::bind_classes/bind_fields/bind_getters/bind_timing/bind_indexer` |
| 4. 模式/会话 | `mode_present`、`capture_active`、`controller_present`、session、round | `tick_runtime/begin_battle/leave_mode`；检查 nullable Manager 链与 END，而非看 endingStatus 猜结束 |
| 5. Hook 真命中 | `hooks applied/logger_hp/logger_ep`、8 个绑定记录及 RVA | `bind_hooks` 与 `on_*`；地址仅作当前证据，不复制回生产代码 |
| 6. 事件过滤 | `outside_mode/cancelled/wrong_type/unresolved_source/environment/zero/exceptions`、`last_modifier/last_unresolved_modifier`、`last_read_failure` 及来源恢复计数 | `record_damage`；找真实过滤原因，不一口气删除保护条件 |
| 7. 归属/覆盖 | `runtime_cards/instance_owners/host_cache/live_cards/verified_source_sides/roster`、`unknown_player`、local/observed/simulated/active masks | `refresh_index_maps/rebuild_host_cache/resolve_character/player_for_card/read_players` |
| 8. 队列/模型 | `events published` 与 plugin 导出的模型/队列快照、sequence、session/round | `types.h`、`pump_events()`、`model.h`；published 增长但 UI 不动时先查消费/筛选 |
| 9. 时钟/阶段 | `game_state/stats_phase/timing_mask/assist_members`、各玩家 gameState/online/isDead | `sample_timing/player_in_combat/read_damage_phase`；逐阶段验证，不用全局开关代替每人时钟 |
| 10. 视图/报表 | 所选范围/阶段/轮次、find_round_view、报表快照及磁盘写入错误 | `view_state.h`、`draw_header/draw_table`、`report_archive.h/xlsx_export.h` |

常用本地搜索（不启动游戏或加载游戏模块）：

```powershell
Select-String -Path 'reference\current-schema\stronghold-schema-14412.log' `
  -Pattern 'CLASS |COLLECTION |OnAppliedModifier|runtimeCard2Chess|TryGetValue'
Select-String -Path 'stronghold\capture.cpp' `
  -Pattern 'bind_hooks|record_damage|tick_runtime|sample_timing|resolve_character'
```

### 6.3 更新类型/字段/集合绑定

1. 用新报告对比 `reference/current-schema/stronghold-schema-14412.log` 与 `stronghold-status-schema.txt`。34052/4708 是历史失败证据，不能任选旧报告替代新客户端。
2. 按**语义和真实字段类型**定位替代结构：DataCenter 持有的 playerDataModel/indexer、ScenePlayerData 的玩家索引/连接/死亡状态、Character 的 cardUid、集合的真实 key/value。优先从已知引用的 field type/return type 获取类，而不是全库碰运气猜名字。
3. 类型搬 namespace 可在 `find_class_with_fallback` 增加有依据的分支；字段改名或值类型变成引用类型则需显式新分支，不能宽泛 catch 后当空集合继续。
4. 闭合集合通过**字段类型**取 class，再验证其 `TryGetValue`。新 `ListDict<uint,ChessInst>` 与旧 `Dictionary<uint,int>` 不能共用 4 字节 out buffer；同时复核 reference/value type、返回 Boolean、instance 标志。
5. 自动属性 backing field 可包含 `<...>k__BackingField`；优先用存在且已确认只读的 getter。继承字段/方法需沿父类查找，嵌套类需走 nested lookup。不要修改托管对象来补一个缺失字段。
6. 若 FP 比例、Modifier value type/header 基准、枚举 underlying type 或 Hook ABI 变更，先写边界/负例测试，再更新解析；关键结构无法证实时停止该采集功能并显示不兼容。
7. 变更 binder 的同时扩充 `metadata_report.cpp::wanted()` 和闭合集合报告逻辑，使用户下次一份诊断就能提供所需结构，而不是反复索要无关截图。

### 6.4 伤害入口重命名或不再命中时如何定位

不要把“重新 Hook 另一个名字”当成适配方案；需要重新证明**实际扣血写入 -> 通知调用 -> 插件读取时机**：

1. 先确认局内 `capture_active=1`、伤害阶段非 Idle，并在产生真实攻击后观察计数变化。如果 `applied` 增加而 `outside_mode` 增加，先修模式/会话状态，不能直接换伤害入口。
2. 若旧 Hook 已绑定却 0 命中，检查新元数据中 `Entity`/其父类的 Modifier 应用/扣血通知方法以及调用者；检查是否移到其他类、签名改为 by-value、增加参数或分离元素通道。方法存在不证明该模式会执行它。
3. 用同一客户端的 GameAssembly + metadata 做只读离线分析：从 `Modifier.realDelta` 的赋值点和 HP 应用路径回溯到合适通知；对比可选 Logger/XLua 分支，确认主战、DoT、召唤物、协防都经过该链。
4. 反汇编/反编译时保存函数完整签名、调用关系、字段类型、赋值发生在回调前还是后、线程与生命周期证据。RVA 和地址只写研究记录，生产仍由元数据找 MethodInfo/入口。
5. 框架可支持的 ABI 才能直接 Hook。新方法超出四个整数/指针寄存器或使用 FP/struct/栈参数时，选择语义等价且 ABI 可证明的邻接通知入口；不要强转函数指针后试运行。
6. 先保存原函数前阶段，再调用原函数，原函数后读取已结算的 realDelta。保持嵌套应用抑制机制，测试主入口+Logger 同次只计一次、下一 tick 同值 DoT 仍计一次。
7. 若必须临时增加探测，只在同一框架插件内增加受控只读计数/结构采样，限制频率与日志大小，不记录玩家账户信息；由用户选择部署/重载时机。不另塞独立探测 DLL 到游戏目录。
8. 形成最小复现测试：旧逻辑在新证据下失败，修复后通过。一次只改必要兼容分支，不同时重写模型、界面和导出以掩盖入口问题。

#### 6.4.1 附加伤害 / 无来源 / 召唤物专项（2.1.2）

1. 在具体干员/盟约发生效果时于**局内**导出，先看 `applied/published/unresolved_source/environment_recovered/missing_host`。不把局外 nullable singleton 消失误当伤害入口缺失。
2. 若 `original_source_fallback` 增长且数据正常，说明直接来源失效但原来源已恢复；直接来源存在不代表完整，不要恢复旧的 null-only 回退。若 `source_errors` 增长，结合 `last_read_failure` 定位哪个只读 getter 变更，不能吞异常后假装来源为空且都归自己。
3. 查看 `last_unresolved_modifier` 的 `source_class/original_class/target_class/modifier_side`。对新增来源类型，先让诊断 `wanted()` 输出它的完整继承、只读 getter 和字段；确认是否确为 Character/Token/Trap，或仅是 Entity/BObject。`Projectile` 与 `Entity` 在当前基线是 BObject 下的兄弟，不能凭图形外观假设 Modifier.source 一定是 Projectile。
4. 卡片/实例/玩家索引、`BattlePlayerData.playerSide` 及来源 BObject.playerSide 必须相互验证。若 `side_conflicts` 增长，保持未知，定位是否数组混入其他玩家或 side 语义已改变；不以多数票/数组顺序/Modifier.playerSide 代替。
5. `missing_host` 增长时追查真实 token 数据的 `isToken/uniqueId/tokenOrHostUniqueId` 与当前 controller 的角色/召唤物数组。先检查新 token 是否及时刷新、宿主退场前是否采过身份、卡片是否换轮重用，再考虑其他有证据的宿主 getter。不得猜最近攻击者、按伤害大小分摊或把 token 自身 ID 冒充宿主。
6. 来源全空且 target 是实际 Enemy 时，全局未归属承伤是**有意保留的边界**，并非已经找到玩家。若未来要精确归盟约/干员，必须找到该效果的可靠源上下文并增加反例测试；不能把 `Modifier.playerSide` 从“诊断线索”直接改成所有场景的来源证明。
7. 当前只对实际 Enemy 类型开启无来源/环境回退。若新内容用非 Enemy 的敌方 Token，须先取得真实类型和 `isEnemySideToken` 等可靠目标证明，补右侧队友/召唤物、敌方自伤、友伤和空目标的反例，再扩大目标谓词。不要改成 `SideType.ENEMY` 就全收。
8. 无来源总量可能含环境扣血；给用户说明它表示未能归属的敌方承伤。源身份、目标证据、有效伤害或时钟缺失时如实提示，不许借恢复总量的名义伪造玩家排名/DPS。

### 6.5 如何使用用户提供的 il2cpp-mod-template

该工具已经真正参与当前实现：`third_party/.../il2cpp_api.cpp` 与 Windows module adapter 直接链接进发布 DLL；不是“文档里提了一次但运行时仍依赖 damage_meter”。参考副本可以用来理解运行时反射和校验流程：

- `reference/il2cpp-mod-template/il2cpp_core/src/`：API/元数据访问实现。
- `reference/il2cpp-mod-template/src/probe.cpp`：类结构探测，迁移其只读逻辑到现有 `diagnostics`，不照搬独立引导。
- `reference/il2cpp-mod-template/docs/quickstart.md`：类定位与 dump 思路。原模板的 `dump_class`/`dump_max_fields` 是它自己的 `mod.ini` 功能，**不是本插件现有配置项**；本插件入口是“导出诊断”。
- `reference/il2cpp-mod-template/docs/pitfalls.md`：继承、backing field、API 表跨 TU 布局、旧 dump 偏移等陷阱。改 API 表后必须重编全部七个源文件，避免不同 translation unit 认为表布局不同。
- `third_party/il2cpp-mod-template/UPSTREAM.md` 与 `LICENSE`：当前移植范围、来源 commit、精简理由与 MIT 授权。上游变动不能未经审查覆盖本项目精简 API。

离线 dump/原生分析是补充，不要求用户换加载器。项目 `.tools` 中可能保留 Il2CppDumper 的 static/inspect 研究版及源码；它们是机器上的辅助工具，**不是发行依赖或必定可用的通用更新工具**：

- 先核验新客户端 metadata 版本和所用工具支持情况；自动搜索失败就报告失败，不猜注册表地址、元数据版本或旧 RVA。
- 工具必须按数据文件读取 PE/metadata，不能为了离线分析触发 `GameAssembly.dll` 的初始化或不受控 `PELoader.Load`。只用已审查的 static 分支；不要直接运行来源不明的二进制。
- 历史 `ARKF_STATIC_INSPECT` 特殊分支使用过特定客户端的 codegen module 地址/数量，**不能将该分支当成更新后自动定位器**。重用前需从新版本重新定位并只在研究代码中记录依据，生产不得继承这些常量。
- 模板运行时枚举也有局限：closed generic 往往需沿真实 field type/class 查询，不能只枚举 image 普通类。诊断应补足该证据，而不是从旧 dump 复制签名伪装成当前实测。
- `%USERPROFILE%\Downloads\教程` 中的旧 dump（2026-10-09）只作历史导航；新实测优先。`.build` 内旧分析也不是从零重建的必备输入。

### 6.6 常见症状与禁止的“修复”

| 症状 | 优先核对 | 禁止的假修复 |
| --- | --- | --- |
| 局外/结束后 NullReferenceException | 是否直接读 DataCenter singleton；比较 LAST COMBAT 与 CURRENT | 给空指针硬填地址、在 UI 线程强读玩法对象 |
| 能读自己昵称，但伤害全 0 | applied 命中、outside_mode、endingStatus 误判、realDelta 读取时机 | 认为昵称正常就元数据全兼容，改用 calculated 冒充实际伤害 |
| 刷新商店后出现频率仍为空，或有干员行但无盟约行 | 核对加载的 DLL 哈希、`localShopShelf` 和 `localShopDescriptors` 绑定、`shop observations` 的 `local/hidden/rejected`、P1–P4 筛选。`local` 增长表示采到货架观察，还须确认其中有有效干员条目。干员已有记录时单独检查盟约描述映射 | 把空快照当成统计零次，或按采样次数冒充刷新次数 |
| 记牌器提示名称未读取 | 核对 `shopOperatorName`、`shopBondName` 绑定与本局活动配置。协议编号应与 `charChessDataDict.identifier` 对齐，并通过 `charShopChessDatas` 匹配角色 ID | 将部署实例 ID 用来查协议条目，或将内部 ID 当成显示名称 |
| 高画质仍只有自己 | 玩家 raw 1–4、真实 ChessInst 集合、索引就绪时序、TryGetValue out ABI、覆盖 mask | 从画质/房间名单造队友零数据，强制 badge 为全房间 |
| DoT 少计或完全消失 | 是否错误按地址/相同数值去重、originalSource/宿主链、主入口 | 在 UI 单加一个 DoT 估值或跨 tick 去重 |
| 同次伤害翻倍 | applied 内 Logger 是否被抑制，sequence 是否重放 | 按相同伤害大小统一除二 |
| 打完等队友时 DPS 降低 | 玩家 battleFinished、BATTLE_WAITING 和逐人 timing_mask | 整房间一有人打完就停止全部计时，或用“没伤害若干秒”猜等待 |
| 协防混入主战或等待补进协防 | 旧 phase/mask 先结间隔、HELP_BATTLE 名单及独立容器 | 用同一累加器换标题，或照计所有在线玩家 |
| 当前轮永远多 1 | currentRound 是否重复 +1、UI 与导出是否同一语义 | 只在 UI 减 1，模型/导出仍错轮 |
| 切历史后马上跳回当前 | ViewSelection 是否被 RoundStart 重置 | 改游戏 currentRound 来切界面 |
| 局外看不到刚结束数据 | 窗口可见性与 Manager 生存周期是否耦合 | 把残留游戏指针缓存到局外继续读 |
| 表格底部大片空白/内容被挡 | ImGui 剩余高度、child/表格滚动与 popup 布局 | 隐藏数据行或固定极小显示人数 |
| XLSX 有文件但打不开/Excel 修复工作簿属性/占比无百分号 | ZIP CRC、OOXML rels、XML 转义、数值与百分比 style、原子替换；`workbook.xml` 是否恢复了不完整的 `fileVersion appName=xl` | 只凭 XML 可解析就宣称 Excel 兼容；把 CSV 重命名为 xlsx、强制 Excel 修复或用文本占比掩盖错误 |

### 6.7 保存新证据并收敛适配

每次适配在 `AUDIT.md` 记录：客户端标识/哈希、复现步骤、旧观察、根因、替代入口/类型及 ABI、修改文件、测试与实机结果、剩余风险。必要时在 `reference/current-schema` 保存脱敏的真实报告。

- 真实报告和静态/模拟补充必须分开，补充应注明来源与假设，不篡改原报告让测试绿。
- `stronghold_schema_tests.cpp` 目前重放 14412 报告 + status schema，并显式补充已核验但未被报告枚举到的 closed singleton；测试里为边界检查设置的尺寸不是声称运行时真实布局。
- 增加新版本夹具和负例，不删除有价值的旧结构兼容测试。关键字段缺失、by-ref 变更、引用/值混淆应在安装任何 Hook 前失败。
- 兼容修复通过后再按第 7 节完整验证，再让用户安排实机。源码/模拟通过不能宣称已经证明中高画质全员可采。

## 7. 验证、验收与交付门槛

### 7.1 离线验证命令

`verify.ps1` 通过 PATH/vswhere 自动查找 x64 MSVC，选择已安装且同时有头文件/库的最新 Windows SDK；支持 `-ClPath`、`-ImGuiDir` 和 `-SdkVer` 显式覆盖。使用 CI 依赖时从 .build/dependencies.json 读取 ImGuiDir，不能把工具链缺失当业务测试失败。

```powershell
Set-Location 'D:\Projects\ArkF-ACT'
& '.\verify.ps1'
# 非默认安装时按实际位置传 -ClPath 与 -ImGuiDir。
if ($LASTEXITCODE -ne 0) { throw 'verify failed' }

& '.\build-plugin.ps1'
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

# 以下两个检查使用 Python 标准库。Windows 下请用 64 位 Python。
python '.\tools\inspect_binary.py' '.\dist\plugins\stronghold_stats.dll'
if ($LASTEXITCODE -ne 0) { throw 'binary inspection failed' }

# verify 的 XLSX 测试会生成这个固定夹具。名称虽含2.1.0，仍是当前测试输出路径。
python -B -m unittest discover -s tests -p 'test_inspect_workbook.py'
if ($LASTEXITCODE -ne 0) { throw 'workbook inspector regression failed' }

python '.\tools\inspect_workbook.py' '.\.build\export-2.1.0-fixture\exports\报告.xlsx'
if ($LASTEXITCODE -ne 0) { throw 'workbook inspection failed' }

# 可选：仅在已安装 Excel 的 Windows 开发机运行；创建独立实例、禁宏/事件/链接，
# 以 xlNormalLoad 默认模式只读打开，不请求修复、不保存，也不连接用户已有 Excel。
& '.\tools\test-excel-workbook.ps1'
# 可用 -Path 显式指定一个或多个本地测试文件；CI/插件运行不要求安装 Excel。

Get-FileHash '.\dist\plugins\stronghold_stats.dll' -Algorithm SHA256
```

`inspect_binary.py` 默认取项目 dist 下的插件；Windows 下会在**独立检查进程**加载插件并调用 query，使用上一级 dist 中的配套 ArkImGui，**不会调用插件 initialize 或附加游戏进程**。缺配套依赖时应补齐依赖，不跳过 DLL/query 检查后声称产物可用。

检查至少包括 x64 PE32+、四个导出、ABI/版本、dependency_count=0、无 damage_meter/RDPS、导入白名单、无禁用的 IL2CPP setter/创建/状态 Ensure 符号。符号扫描不是完整安全证明，仍需代码审查 `runtime_invoke` 调用的语义，确认没有新增游戏写功能。

### 7.2 回归测试各自保护什么

| 测试 | 主要保护范围 |
| --- | --- |
| `stronghold_model_tests.cpp` | 1/2/4 人聚合、玩家/干员隔离、整局/本轮、双阶段与独立时钟、退出冻结、队列/sequence/重置 |
| `stronghold_capture_tests.cpp` | 模拟生产采集代码的归属/来源/DoT、Hook 嵌套、时机、会话/轮次、计时和生命周期，覆盖隐藏同步快照及 Boss ID 到图鉴名称的解析、仅同步 ID、图鉴别名回退与缺项 |
| `stronghold_schema_tests.cpp` | 真实报告与显式静态补充重放，覆盖核心 Hook、可选同步 Hook、本机货架、干员描述两个重载，以及持有和获得的不同元素类型 |
| `il2cpp_runtime_tests.cpp` | 元数据查找、签名/继承、类型/集合与运行时安全边界 |
| `metadata_report_tests.cpp` | mock GameAssembly 导出解析、报告完整性/兼容性与隐私边界，不加载真实游戏 |
| `shop_counter_tests.cpp` | 多槽干员/盟约频率、跨刷新和跨相邻回合冻结槽去重、分批货架及基线失效、重复及过期快照、缺失或不变的刷新序号、获得与货架匹配、持有基线、推定售出、全体/单玩家频率取值与排序和 Session 重置 |
| `stronghold_ui_tests.cpp` | ImGui 实际按钮/布局、选择生命周期、阶段成员、局外可见、历史与滚动、Boss 名称显示及 ID 不一致时的回退、记牌器和玩家范围切换后的频率读数 |
| `stronghold_csv_tests.cpp` | 共用行语义、兼容 CSV 转义/中文/零分母、隐私列等回归 |
| `stronghold_xlsx_tests.cpp` | ZIP/CRC32、OOXML、多 Sheet、15 列、中文、百分比、文本安全、原子写入/失败；不生成使 Excel 修复的应用版本记录 |
| `stronghold_report_tests.cpp` | 多轮归档、结束时间固定、同分钟冲突、进行中/中断、失败保留重试与默认写入器集成 |

`verify.ps1` 还会编译全部七个生产源文件。UI 测试单独编译测试版 ImGui 是测试机制，不应照搬为发布插件内置另一套 ImGui。测试不能依赖游戏正在运行，也不能通过启动/关闭真实游戏获得绿灯。

另外 `tests/test_inspect_workbook.py` 的七项标准库单元测试使用匿名临时 ZIP，保护元数据校验器本身；`inspect_workbook.py` 先检查工作簿元数据再解析 Sheet。该测试已接入 Release draft 的独立检查步骤，不改变 `verify.ps1` 的九组 C++ 测试数量。Excel 原生打开检查不能用自动修复模式代替；正例须正常打开，历史错误负例须失败，不能仅凭普通 XML parser 通过判断兼容。

### 7.3 每次兼容修复至少增加的反证测试

- 类型/ABI：关键方法 static/返回类型/by-ref 不符、FP 布局不符、缺字段、引用 out 被误当 Int32 时必须拒绝；失败不留下启用一半的 Hook。
- 伤害：普通/DoT/动态及嵌套召唤物/弹射物、非空失效来源与候选异常回退、未知/环境 Enemy 承伤与友方/空目标反例、side 冲突、迟到效果和生命周期清缓存、最后一击改变阶段、Logger 嵌套；同次只计一次，下一 tick 同值连续伤害仍累加。
- 模式：endingStatus 非零但仍战斗；局外 Manager 为 null；同局跨轮不清总量，名单退出不误开新局，真正新 session 清旧局。
- 计时：四人中先结束一人的主战时间冻结，队友继续；等待后协防 1–2 人单计；名单不明有警告；掉线/淘汰/未知状态不造计时。
- 轮次/UI：raw 0->1 不换轮；正 8 显示 8；历史固定、当前跟随、整局/本轮 × 战斗/协防四种组合；局外保留一局；长表可滚动且底部无挡板。
- 报表：完整整局+每轮、两阶段、有限数值、零分母、昵称括号状态、百分比样式、无账户标识/干员 ID、中文/特殊字符/以等号开头的文本安全；不完整 Excel fileVersion 必须被独立检查拒绝，有 Excel 时核对正常打开而非修复打开。
- 生命周期：手动 reset 请求失败不吞；Hook 移除失败不释放状态；归档失败卸载延期且再次请求可重试，不能依赖已移除的帧回调。

### 7.4 实机验收矩阵（由用户安排时机）

| 场景 | 通过条件 |
| --- | --- |
| 单人 | 昵称、直伤/DoT/召唤物归属正确，实际伤害与采集到的扣血口径一致 |
| 2 人/4 人，中高画质 | 若实际全员本地模拟，则所有已确认玩家有真实数据且 badge 正确；不同玩家同干员不混 |
| 低画质/部分模拟 | 本机/部分采集准确，不造缺席玩家数据；角色和索引稍晚就绪仍能恢复范围 |
| 先打完等待 | 自己伤害与主战时间均冻结，DPS 不因等队友下降；其他人继续计时 |
| 协防 | 参与者真实伤害/时间进入协防，主战历史不变；旁观者不加协防时间 |
| 离线/淘汰 | 状态正确，历史保留，后续计时冻结，不从固定会话名单删除 |
| 轮次 | 第 8 回合显示第 8 轮，跨轮整局不清；可翻阅历史并返回跟随 |
| 结束/局外 | 无 singleton 空异常，可查看刚结束整局和各轮；新局替换；报表按真正结束时间命名 |
| 框架热重载 | 用户主动触发，无悬挂回调/崩溃；只采重载后的数据，不承诺恢复旧内存 |

实机报告要写明游戏版本、插件版本、人数、范围、阶段和观察结果，未测试项明确标“待实机验证”。不得用模拟房间测试代替联网房间结论。

## 8. 真正从空项目重建时的实现顺序

若生产源码丢失，只有本文和依赖源码可用，按以下顺序重建，每步都保留纯逻辑测试；不要先写漂亮面板再临时拼数据：

1. **宿主骨架**：取得匹配 ArkFramework SDK/ArkImGui，建立四导出、零插件依赖、版本常量、初始化/卸载保护，发布工具链用 GNU ABI。只显示“等待运行时/不兼容”诊断，先证明宿主可加载。
2. **只读运行时与报告**：移植指定 commit 的精简 il2cpp_core API/Windows resolver，构建元数据查找/签名校验/GC Root 封装和结构报告；先让一次局内导出证明所有必要真实类型，不猜偏移。
3. **纯数据层**：定义 SessionStart、Roster/Coverage、RoundStart、Clock、Damage/Elemental、Warning、SessionEnd 等事件语义与 sequence；建立有界队列、双阶段模型、玩家时钟、历史轮视图。事件从会话开始到轮次/时钟/伤害再结束按顺序消费。
4. **生命周期/模式**：安装经验证的 StartGame/ClearRunTimeData/OnDestroy/两个 Update；用 nullable Manager->DataCenter 识别模式，建立固定身份，正确区分回合与整局。
5. **实际伤害链**：接入 OnAppliedModifier 后置采样与两个 Logger 后备，抑制同次嵌套，正确读取 FP 和动态 value layout；原函数必须恰好调用一次，原始返回/异常行为不能被探测逻辑改变。
6. **归属与覆盖**：实现 Projectile.source、token 宿主、cardUid->ChessInst->玩家，保留未知归属和警告；按真实本地证据判范围，不用 UI 名单冒充可采证据。
7. **独立时钟**：实现先结旧区间再采新状态、逐人主战完成判断、等待冻结、协防参与名单和双阶段；将最终伤害归阶段与是否继续计时分离，避免漏最后一击。
8. **只读 UI**：从模型构造排序行，加入两个 toggle、历史选择/跟随、可滚动表、自己高亮、范围/错误提示、局外缓存查看和手动 reset 请求。UI 不得直接访问玩法对象。
9. **归档与隐私**：生成单 XLSX 多 Sheet，15 列、数值百分比、inlineStr、正确结束时间、原子写入、进行中/中断和重试；后台/卸载失败不丢快照。
10. **可交付产物**：补齐九组测试与两个独立检查，构建真实 x64 DLL并核对导出/依赖，记录哈希；准备框架外置部署说明，再由用户选择实机测试/热重载。

本文提供的名字和算法是插件 1.0.0（原内部 2.1.2）使用的客户端基线，重建到未来版本时仍需执行第 6 节确认入口。不能声称仅靠一份旧文档就无需新游戏元数据。

## 9. 交付检查与知识维护

- 功能改动只触及必要文件；纯文档任务不要改版本、生产源码、DLL 或运行中的框架。
- 更新 `README.md`（用户行为/操作）、`AUDIT.md`（证据/根因/验证）、`dist/部署说明.txt`（部署要求）和本文（实现/升级知识）；仅改文档时不需要为形式而重编/部署。
- 修改第三方移植范围时更新 UPSTREAM/许可证说明。项目沿用 GPL-3.0-or-later 约束，模板的 MIT 版权与许可必须保留，不能把二者混写成无条件闭源授权。
- 交付说明列出实际改动、离线命令及结果、DLL路径/版本/哈希（确实构建才写）、待实机项；不要把历史验证冒充本轮新跑结果。
- 未经用户要求，不在游戏目录部署、不替换使用中的插件、不主动重载。统计插件永远只观察，不增加游戏数据修改能力。

### 9.1 当前实现边界：不要把维护目标当成已修复声明

以下宿主边界来自此前 2.1.1 文档审查，未在本次来源修复中扩大处理；第 3 项已随 2.1.2 实施保守回退。仍未声称实机验收通过。后续处理其他边界时，先确认契约、补反例再局部变更：

1. **帧对象信任边界**：`plugin.cpp::host_compatible()` 检查 host API struct 大小、ABI、ImGui 版本及必要函数指针，但 `on_draw()` 直接使用 `frame->imgui_context`，没有独立验证 frame 的大小/有效性。当前信任宿主传入有效帧，不能声称已做完整帧 ABI/上下文防护。重建时应明确该前置条件或补上可用的校验。
2. **部分注册失败的回滚**：`initialize()` 先注册 frame 再注册 draw；若前者成功、后者失败，本地失败分支没有保存句柄并显式 remove_callback 已注册项。最终是否由宿主 owner 清理由框架加载失败契约决定，不能把本地分支描述为完整事务回滚；后续适配宿主必须核查这一契约。
3. **来源边界（2.1.2 已部分修复）**：已增加来源失败回退、动态宿主缓存、来源对象 side 的验证映射，以及实际 Enemy 无来源承伤的未归属保留和一次告警。仍不声称任意脚本效果均可恢复：没有实际 Enemy 目标或可信友方来源、敌方非 Enemy Token、Hook 链外直接改血、完全失去宿主身份等仍需新局内诊断/对应干员盟约验证。无来源未知项可能包含环境承伤，不能当成某位玩家输出，也不能把 unresolved_source 全部盲加给自己。
4. **有限去重保证**：sequence 是队列回放游标，不是全局事件身份。当前 applied 调用范围抑制解决已知嵌套 Logger，不能承诺跨线程、任意新入口或重复 publish 都会自动幂等。新增入口必须重做同次/跨 tick 对照测试。
5. **隐藏同步/记牌器边界（1.2.0）**：普通 DataCenter 只保留本机完整货架和 `selfBannedUnits`。可选 AllStateSync `Read` 与 PreparationStatus `Read` 可复制全状态/单玩家准备状态。主 UI 显示种子和隐藏 Boss 名称，原始 Boss ID 留作查找键。逐轮出怪预览 UI/本地敌人缓存采集已移除。商店历史只有在相应状态实际被本机观察到时才计数，冻结、序号缺口、来源/售出推定边界见 5.12。新 Hook 的回调线程、准备状态更新频率、队友货架覆盖、名称解析和购买/售出推断准确度均待当前客户端实机验证。

### 9.2 文档维护记录

每次完成项目更新时追加一条简明记录，并同步相关正文；本表不能代替技术内容维护，也不能把未执行的验证写成已通过。

| 日期 | 主要变更 | 验证与剩余事项 |
| --- | --- | --- |
| 2026-11-15 | 本次对话新增“每次更新必须同步维护 AGENTS.md”的强制收尾规则，明确适用范围、正文同步及维护记录要求。已复核，无技术变更。 | 仅检查文档 UTF-8 与新增规则/记录读回；未运行编译、业务测试或实机验收。未修改插件源码、版本或 DLL，未部署、重载或操作游戏；第 9.1 节已知边界与既有待实机项保持不变。 |
| 2026-11-15 | 2.1.2：修复非空失效来源回退；动态/嵌套召唤物刷新、同轮迟到效果身份与覆盖集合分离；来源 side 唯一验证；无来源/环境 Enemy 承伤单列未知，不伪造玩家；补源类诊断、ABI/模型/报表回归；复核后拦截 Enemy 直接来源被友方原来源重标；同步正文适配步骤。 | 修复前来源回归实际红灯，修复后九组 MSVC 测试/七个 TU、Zig GNU DLL、query 2.1.2 零前置插件依赖/禁用写 API 扫描、XLSX 独立检查通过；模型18项、CSV6组、XLSX4组。本轮仅构建项目 dist，未部署/重载/操作游戏；具体盟约/干员与非 Enemy 目标边界待局内验证。 |

| 2026-11-16 | 首次公开发布重标 1.0.0，保留原内部 2.1.2 采集功能；初始化 GitHub main 发布路径、固定 Zig/ArkFramework 依赖与 ArkImGui 准备脚本，新增标签触发且只建 draft 的 Actions；构建脚本支持 ImGuiDir，验证自动发现 MSVC/SDK。公开回归改用元数据声明子集，排除原始报告、完整 reference 和本地二进制；补 GPL/MIT 许可及打包清单。 | 本地依赖准备、九组 MSVC 测试与七个 TU 检查已通过；抽取夹具首次缺 DamageType enum 导致 ABI 测试红灯，补回真实声明后通过。Zig 0.17.0 构建、独立 DLL query 1.0.0/零前置插件依赖/禁用写 API 扫描、XLSX 独立检查及 actionlint 1.7.12 已通过；本地 DLL 1,416,704 字节，SHA256 7B950E9067AFD47C74AC04FB41A2499CE74C50037844D3DDA9EDB1AAFCAAA338。远程 Actions/草稿结果将在观察后另记；未操作游戏，实机验收边界未改变。 |
| 2026-11-16 | 首次推送 main/v1.0.0（9fbf0c4），本地发布 ZIP 允许清单/CRC/build-info/hash 校验通过。远程首次 Actions 已通过九组测试、七个 TU、Zig 构建和 DLL query；XLSX 校验打印中文时遭 cp1252 编码错误，未创建草稿。修复工作流 PYTHONUTF8=1，保持已推送标签不动，由 main 的 workflow_dispatch 构建既有 v1.0.0。 | 该修复只改变 CI 环境，不改已打标源码或插件行为；Windows runner 的中文日志问题不得误判为 XLSX 数据损坏。重新运行与草稿观察结果完成后另记。 |
| 2026-11-16 | 1.0.0 远程交付收尾：main 的 UTF-8 修正工作流 43812543420 成功，构建固定 v1.0.0/9fbf0c4；github-actions[bot] 创建单一 draft，标签未移动，未正式发布。同步记录实际附件及只读边界。 | 远程九组测试、七个 TU、Zig 构建、DLL/XLSX 检查和打包均成功；认证 API 确认 draft=true 与 ZIP/.sha256 两附件。重新下载验证 ZIP 允许清单/CRC/清单 commit/哈希，实际加载 CI DLL query=1.0.0、零前置插件依赖、禁用写能力扫描通过。CI DLL 1,416,704 字节，SHA256 C6B08FA21D52D3137D25A51179D05EC1C460680FCA177CF95DF34405F74676C2；ZIP SHA256 7654220c71882e5b15846ad61f2c6b1e7b5506a428ae1729fa62abcbd9c4f1b7。Node20 Actions 被 runner 自动转为 Node24，仅弃用告警、未失败；后续升级固定 Actions 版本需重验。未操作游戏，联机实机项仍待用户验证。 |
| 2026-11-16 | 将 README 全面重写为面向用户的文档：以功能、前置条件、安装更新、界面操作、统计口径、XLSX、常见排障、卸载和只读边界组织内容；移除 IL2CPP 入口、历史版本修复、构建和 Actions 维护细节，这些仍由 AGENTS/AUDIT 保存。同步源码地图与 Zig 构建说明；插件代码、版本、标签和 Release 状态不变。 | 按生产 UI 控件、报表路径和当前 1.0.0 元数据复核文档，执行 UTF-8、Markdown 本地链接及变更范围静态检查；仅文档更新，不重复运行编译/业务测试，未部署、重载或操作游戏。既有联机与具体盟约/干员实机验收边界不变。 |
| 2026-11-16 | 1.0.1：定位 Excel 修复工作簿属性为不完整 `fileVersion appName=xl`，生成器省略可选版本声明；加入 C++ 红灯回归、独立校验器七项匿名测试和 CI 检查，可选 Excel 原生正常只读打开脚本。同步导出兼容性、排障/验证步骤、README、部署说明和 AUDIT。 | 修复前 C++ 回归及 Excel 正常打开均失败；修复后九组 MSVC/七个 TU、七项 Python、独立 XLSX、Zig 构建、实际 DLL query=1.0.1/零前置插件依赖/禁用写 API 检查通过。Excel 16.0 build 20527 正常打开四 Sheet 生产夹具及十六 Sheet 最小修复对照，原错误负例拒绝；原报表 hash 与所有 Sheet 数据/样式保持不变。DLL 1,416,192 字节，SHA256 FA21A9AB3A2851431AEB403033CF16B70F76061F8D8D69A31D7B9E5B4E988197。本轮未推送/打标/更新 Release，未部署、重载或操作游戏；旧报表不自动改写，游戏内导出仍待用户安排验证。 |
| 2026-11-16 | 脱敏隐私审计：审计开始时 59 个跟踪文件、97 个 Git 对象（含 4 个不可达 blob），并补扫审计期间出现的未提交 1.0.1 改动；核对 refs/reflog、远端 main/tag、两条 Actions 日志、Release 正文与公开 ZIP。公开树、历史及发布附件未发现游戏 UID、昵称编号、私钥/令牌、个人邮箱或本机 Windows 用户名；公开身份只有仓库所属 GitHub 账号及 noreply 提交/标签身份。同步修正 v1.0.0 已正式公开的状态，并补充本地忽略目录的隐私边界。 | 公开 Release ZIP 615,502 字节，SHA256 `7654220C71882E5B15846AD61F2C6B1E7B5506A428AE1729FA62ABCBD9C4F1B7`，重新下载后未含本机用户路径、PDB 或游戏账号字段；Actions 日志无本机用户路径，GitHub secret-scanning alerts=0，代码扫描未配置。本机忽略目录仍存有 `dist`/`.build` PDB 与日志中的 Windows 用户路径、`reference/current-schema` 的运行态会话哈希，以及 `.build/xlsx-excel-repro` 中 4 个疑似真实玩家显示名；这些内容未进入 Git、远端或 Release，本轮未清理。未提供明文 UID/昵称作精确比对，短昵称结论仍依赖账户字段、长数字、本地隔离和已知标记；未编译/测试、未部署/重载或操作游戏。 |
| 2026-11-16 | 复核 GitHub 公开可见性口径，已复核，无技术变更：公开仓库与公开 Release 的既有脱敏审计结论仍适用，未发现游戏 UID、昵称编号、个人邮箱、令牌或本机 Windows 用户名；公开的 GitHub 账号用户名及 noreply 提交/标签身份属于预期元数据。 | 本次仅解释既有审计边界，未重新联网复核远端、未编译/测试、未修改生产源码或产物、未部署/重载或操作游戏。本地忽略目录中的 PDB、日志、会话哈希和 XLSX 玩家显示名仍只在本地，分享整个工作目录前需排除。 |
| 2026-11-16 | 按用户授权将 1.0.1 发布流程从仅建草稿改为正式 Release/Latest，保留工作流文件名，加入发布后标签、附件名、公开状态及 Latest API 核验；更新发布说明与部署文档，准备提交既有 Excel 兼容性修复。 | 本轮 actionlint 1.7.12、七项 Python 回归、XLSX 独立校验、实际 DLL query=1.0.1/零前置插件依赖检查通过；本机临时目录测试需在沙箱外执行，权限恢复后通过，未改业务测试。公开树与全部 97 个 Git 对象（71 个 blob，含不可达内容）、refs/reflog 规则扫描通过；远端 main 与本地基线一致，v1.0.1 尚未存在。正式发布及远端 CI/附件核验在观察后另记；未操作游戏或部署。 |
| 2026-11-16 | 1.0.1 正式发布完成：main 与新标签原子推送，构建 commit 3c33ea2；Actions 43821344832 成功并以 github-actions[bot] 正式发布，API 确认非草稿/非预发布，Latest 指向 v1.0.1，两附件 uploaded；1.0.0 标签与公开附件保持不变。 | 远端九组 C++/七个 TU/七项 Python、Zig、DLL/XLSX 独立检查及发布全部通过。重新下载 ZIP 后核验 CRC、允许清单、build-info 源 commit/大小/哈希，实际加载 CI DLL query=1.0.1、零前置插件依赖/禁用写能力扫描通过；CI DLL 1,416,192 字节，SHA256 F9DB274F098BE0B32F02F3D541E22F9BDB166E10963907114FF9779B2C06AD89；ZIP 611,276 字节，SHA256 50F305F2DBD2932FD23A59B1A28416189818FD7A50665E46AB7756C5C7CB6CA6。已检查当前/既有 Actions 日志、Release 正文、公开附件与 Git 全对象/refs/reflog，无规则发现；本地忽略目录仍不公开。此处补录验证只更新 main，不移动标签或更换 Release；未操作游戏/部署，游戏内导出仍待用户安排验证。 |
| 2026-11-22 | 新增 5.9 商店刷新静态研究边界：区分联机服务器权威状态与本地教学桥接，记录普通刷新/冻结/升级/特殊货架/轮次刷新和 `refreshSeq` 语义；明确概率、免费次数最终结算及具体等级表数值仍缺证据。仅更新文档，未改插件源码、版本或 DLL。 | 复核元数据报告 4708、方法映射与本地静态反汇编；确认协议请求字段和原生回退路径，未运行编译/业务测试，未部署、重载、附加调试器或操作游戏；联机实机与配置概率仍待验证。 |
| 2026-11-22 | 新增 5.10 对局种子边界：确认 `SceneGameData.stageSeed` 会被 `AutoChessDataCenter.InitData` 传入 `BattleController.ResetSeed`，活动层 seed 另有字段；明确 `enemyTypes`、`bannedBonds`、`selfBannedUnits` 是显式场景输入，种子不能单独替代完整配置和 RNG 调用序。仅更新文档。 | 复核当前元数据、方法映射与 `AutoChessDataCenter.InitData`/`BattleController.ResetSeed` 静态反汇编；未编译、未测试、未部署、未重载或操作游戏；完整对局/联机随机复现仍需实机证据。 |
| 2026-11-22 | 补充种子协议可见性：`AutoChessBattleSceneAllStateSyncDn.allStateSyncData` 直接包含 `stageSeed`、`bannedBonds`、`enemyTypes`，静态玩家条目包含 `bannedUnits`；说明这些字段可通过已解密的全状态同步读取，但不会提供绕过鉴权/传输保护、篡改数据或作弊的实现。 | 复核元数据字段和协议声明；未抓取网络包、未解密流量、未编译/测试、未部署、未重载或操作游戏。预测能力仍受客户端版本、配置、RNG 消费顺序、服务器校验和玩家输入限制。 |
| 2026-11-22 | 补充来源判断：联机 `stageSeed` 由服务器经 `AutoChessBattleSceneAllStateSyncDn` 下发并由 `AutoChessBattleAllStateSyncData.Read` 读取；本地教学/模拟另有 `GenerateLocalSave`/`_GenerateTrainingData` 构造路径。未改变只读边界。 | 复核协议类型、字段声明和现有静态反汇编；未抓取/解密网络流量，未编译/测试、未部署、未重载或操作游戏。实机网络封装是否明文仍待验证。 |
| 2026-11-22 | 补充一局种子/结果目录：区分活动 seed、场景 `stageSeed`、`stageRandomSeed`、`BattleController.randomSeed` 和独立 `AutoChessRandomWrapper`；将 `enemyTypes`、ban、静态玩家阵容、Boss/场景字段列为可直接缩窄范围的显式信息，不把候选范围或历史结果说成 seed 的精确预测。 | 复核 `AutoChessGame`、`SceneGameData`、`AutoChessBattleAllStateSyncData`、`AutoChessLevelEnemyManager` 与 `BattleController` 的字段和方法声明；未抓包、未实机验证、未编译/测试或操作游戏。配置权重和完整 RNG 消费顺序仍未知。 |
| 2026-11-22 | 深入核对种子消费者：确认战斗加载时主 `randomImp` 使用 `stageRandomSeed + currentRound` 构造 `AutoChessRandomWrapper`；静态调用图显示其覆盖随机敌人/特殊敌人、敌人预览、随机羁绊、商店等级赠棋、随机选目标及通用技能/目标/弹道随机。活动层 seed 未找到客户端直接消费者。 | 复核 `AutoChessGameManager.DoLoadGameInternal`、`BattleController.ResetSeed/ResetImpRandom`、`RandomEnemyGenerater` 等静态反汇编并对 `BattleController.randomImp` 做直接调用者扫描；未运行游戏、未抓包、未编译/测试、未部署或重载。服务器端活动 seed 和完整权重表仍待证据。 |
| 2026-11-22 | 已复核 seed 可决定的随机行为清单，无技术变更：场景 seed 初始化 `randomImp/randomTrivial/PRD`，战斗主随机流按 `stageRandomSeed + currentRound` 派生，覆盖随机敌人、特殊敌人、敌人预览、随机羁绊、商店等级赠棋及通用战斗随机。 | 仅按既有 5.10 静态结论作答；未运行分析工具、编译/测试、抓包、部署或游戏操作。 |
| 2026-11-22 | 补充 `randomTrivial`/PRD 解释：前者用于投射物、随机旋转/发射器、移动位置和部分场景元素等通用随机；后者是 `DicePRD` 的概率分布状态，具体触发由实际调用者决定。仅文档更新。 | 复核既有直接调用者扫描和 `ResetPRDRandom/DicePRD` 方法声明；未运行分析工具、编译/测试、抓包、部署或游戏操作。 |
| 2026-11-22 | 细化随机敌人结构：记录 `RandomEnemyGenerater` 的普通/精英/特殊随机结果与模板结果字段，`AutoChessRandomEnemyAttributeData` 的敌人键/等级/附加敌人/飞行/效能因子，以及 `AutoChessSpecialEnemyType` 和 `AutoChessRandomEnemyType` 分类、特殊敌人 count/weight/attachment 配置。 | 通过元数据字段探针和 `RandomEnemyGenerater` 静态反汇编核对；未读取具体赛季表值、未实机验证、未抓包、未编译/测试、未部署或操作游戏。 |
| 2026-11-22 | 已复核种子职责说明，无技术变更：`stageSeed` 初始化战斗随机流，影响怪物与随机战斗行为；显式 `enemyTypes`、ban 列表不由客户端 seed 推导；活动 seed 和独立随机 wrapper 的完整作用域仍未证实。 | 仅压缩并复核既有 5.10 结论；未运行工具分析、编译/测试、抓包、部署或游戏操作。 |
| 2026-11-22 | 1.1.0：隐藏同步 UI 按用户反馈收窄。保留可选只读 AllStateSync Hook 与内部快照，但 UI 只显示 `stageRandomSeed`、`hiddenBossId` 和同步状态；`bannedUnits`、所有玩家完整货架、`sceneDetail.roundEnemies` 明确不显示。确认 `roundEnemies` 是某一轮的敌人预览/生成输入，不是实时刷怪日志。补字段抑制 UI 回归和 schema 第 9 Hook 验证；不添加抓包、预测或玩法写入。 | `verify.ps1` 九组 MSVC 回归、七个生产 TU、隐藏 capture/UI 回归和 9 Hook schema 回归通过；Zig 0.17.0 构建成功；`inspect_binary.py` 实际 query=1.1.0、零前置插件依赖、无禁用写/创建/状态初始化 API；七项工作簿独立校验通过。DLL 1,465,856 字节，SHA256 `B38B11A93AB5669C71F59A952FEB79E66240047E0DBF6B9C26EA64C72DE768ED`。未部署、重载、关闭/附加游戏或抓包；AllStateSync Hook、回调线程、实际包频率和隐藏字段可用性仍待实机验证。 |
| 2026-11-22 | 1.1.0：修复插件启动后未进入首局时无界面。新增 `draw_idle_window()`：插件加载后即显示同一窗口 ID 的连接/等待状态，运行时和输入就绪后提示等待进入卫戍协议；进入首个 Session 后切换统计窗口，历史局外数据仍显示统计表。补 idle window UI 回归。 | `verify.ps1` 九组 MSVC 回归、七个生产 TU、idle window/隐藏同步 UI 回归通过；Zig 0.17.0 构建成功；`inspect_binary.py` 实际 query=1.1.0、零前置插件依赖、无禁用写/创建/状态初始化 API；七项工作簿独立校验通过。DLL 1,465,856 字节，SHA256 `B38B11A93AB5669C71F59A952FEB79E66240047E0DBF6B9C26EA64C72DE768ED`。未部署、重载、关闭/附加游戏或抓包；实际游戏启动时序、首局 Session 和窗口表现仍待实机验证。 |
| 2026-11-23 | 新增 `SEED.md`：按客户端证据分层记录活动 seed、场景 `stageSeed`、DataCenter 副本、`BattleController.randomSeed`、每回合 `stageRandomSeed + currentRound` 派生主随机流，以及 `randomTrivial`/PRD；详细整理 `RandomEnemyGenerater`、`AutoChessLevelEnemyManager`、`roundEnemies` 的生成/预览/实际战斗调用链，区分“读取已生成数据”和“仅凭 seed 计算”，并列出权重、服务端权威、RNG cursor、未来回合等未决边界。 | 仅新增和同步文档；检查 UTF-8、Markdown 代码围栏/结构、文件差异和文档链接，不运行游戏、不抓包、不编译、不部署、不重载或修改玩法数据。未把静态推导写成实机结论；当前赛季权重、服务端 roundEnemies 权威、回调/包频率和逐回合预生成策略仍待实机验证。 |
| 2026-11-23 | 新增“出怪预览”标签页：AllStateSync `roundEnemies` 按回合优先，本地已生成 `m_roundEnemyData` 补缺，`m_allBattleLevelIds` 提供回合目录；新 Session 清除旧表。显示原始 `enemyKey`/条目 `count`，缺项/缺数量明确未知，不从 seed 预测、不调用敌人生成逻辑，也不把预览数等同战斗最终刷怪数。工作区版本标为 1.1.1，仅本地构建、未发布；公开 Latest 仍为 v1.0.1。同步更新 README、部署说明、SEED 与本文件。 | `verify.ps1` 九组 MSVC 回归、七个生产 TU 编译检查通过；Zig 0.17.0 构建成功。`inspect_binary.py` 实际加载 query=1.1.1、零前置插件依赖，禁用写/对象创建能力扫描通过。DLL 1,478,656 字节，SHA256 `3A51BCA018B989CE0888C21072AC0CB5758597F08C0680F0CEFD37031951E820`。本次未运行独立 XLSX 校验或发布打包；未部署、重载、抓包或操作游戏。`AutoChessLevelEnemyManager` 本地缓存实际可读性、AllStateSync 完整度、名称映射和未来轮次覆盖仍待实机验证。 |
| 2026-11-23 | 按后续产品决定移除逐轮出怪预览页及本地敌人缓存采集，`roundEnemies` 不再采集/展示；种子、隐藏 Boss ID 和可解析名称直接显示在主界面。完成商店记牌器：按玩家/轮次/刷新序号/货架槽位统计干员及其全部基础盟约频次，并展示观察到的获得、可匹配商店获得、推定卖出和持有实例数；修正 Hook 槽位边界以接受原始 P1–P4（避免漏记 P4），Session/轮次读取改为原子快照；更新 README、部署说明、AUDIT、SEED 与本文件。 | `verify.ps1` 十组 MSVC 测试（含 P1–P4 边界）和七个生产 TU 编译检查通过；独立工作簿七项回归与夹具内容检查通过。Zig 0.17.0 构建和 `inspect_binary.py` 实际 DLL query 1.1.1/零前置插件依赖/禁用写能力扫描通过。DLL 1,538,048 字节，SHA256 `5FCB7EDE167BEC670620A820F20A076DC5D2C91557C9C23A7EEC355AF33FA75D`。仅生成项目本地构建，未发布、打包或部署，未重载/操作游戏；AllStateSync/PreparationStatus Hook、队友状态覆盖、刷新频率、名称映射及获得/售出推定仍待实机验证。公开 Latest 仍为 v1.0.1。 |
| 2026-11-23 | 修复 1.1.1 商店出现频率空表。原先本机快照已读取，但没有发布给记牌器。接通 `shopDataModel.chessGoods` 入队，按本机字符串 ID 和协议整数 ID 分别解析干员及基础盟约。修正不完整、重复和过期货架的处理，兼容可识别的零基名册，并分别绑定持有和获得条目的标识字段。补采集诊断与干员、盟约独立空状态，同步 README、部署说明、AUDIT 和 SEED。 | `verify.ps1` 十组 MSVC 测试与七个生产编译单元通过，含本机模拟货架到计数器的回归。七项独立工作簿测试与生成夹具检查通过。Zig 0.17.0 构建及实际 DLL query 1.1.1、零前置插件依赖、禁用写入和对象创建能力扫描通过。DLL 1,551,872 字节，SHA256 `174030938411E240FA4A761DC06A8932B7AB8639867F7B22225A99BD0A67E9AA`。产物留在项目 `dist`。当前客户端货架更新频率、实际干员和盟约映射、协议槽位与名册对应及队友覆盖仍待实机检查。 |
| 2026-11-23 | 修复 1.1.1 记牌器干员和盟约名称。改从活动目录识别协议 `chessIdentifier`，用角色名称查询和盟约配置填充文本。名称独立于累计次数缓存，同一货架补到名称时更新已有行。缺名称时使用明确占位，内部键留在模型中。同步 README、部署说明、AUDIT 与 SEED。 | 十组 MSVC 测试及七个生产编译单元通过，含模拟活动配置解析协议干员和盟约名称、重复观察补名称与去重。Zig 0.17.0 构建、DLL 实际 query 1.1.1 和静态能力检查通过。七项 XLSX 校验器测试与生成夹具检查通过。本地 DLL 1,562,112 字节，SHA256 `BF0D937056BCA0E8B41AC531D59D89D7B3B4313318D5235204F6C1A0319B5B3D`。本局配置的实际解析覆盖和游戏内显示仍待实机验证。 |
| 2026-11-23 | 修复 1.1.1 跨回合冻结商品重复计数。开局货架按同玩家上一相邻回合的冻结槽位去重，覆盖分批观察，同时修正干员和盟约频次。同步 README、部署说明、AUDIT 与 SEED。 | 新回归在旧逻辑下复现重复计数。修复后十组 MSVC 测试与七个生产编译单元通过，涵盖刷新序号有无、连续跨轮、分批货架和商品重新出现。独立 XLSX 校验器七项测试与生成夹具检查通过。Zig 0.17.0 构建和实际 DLL query 1.1.1、二进制能力扫描通过。本地 DLL 1,562,112 字节，SHA256 `E10782763C81BF78474A57DB129F38B777A135DDBB8F191358F85A3FAE171EA7`。修复版仍待局内复测换轮后两张频率表的读数。 |
| 2026-11-23 | 修复 1.1.1 记牌器玩家范围。干员和盟约两张频率表按所选玩家的次数显示和排序，单玩家表只显示名称和出现次数。选「全体」时显示合计和各槽位次数。同步 README、部署说明、AUDIT 与 SEED。 | 回归在旧逻辑下复现取值和排序错误。十组 MSVC 回归和七个生产编译单元通过，含 P1/P4 模型测试和实际 ImGui 范围切换读数。独立 XLSX 校验器七项测试与生成夹具检查通过。Zig 0.17.0 构建与实际 DLL query 1.1.1、二进制能力扫描通过。项目内 DLL 1,562,112 字节，SHA256 `4AF8622C448CA82B34D3FDEAE2359CFD0AC1ACDA3E1A23868B81710592E4E470`。局内范围切换待实机复核。 |
| 2026-11-23 | 修复 1.1.1 隐藏 Boss 名称。通过 `gameData.bossInfoDict` 和敌人图鉴解析显示名称，支持仅同步快照先读到 ID 的情况。主界面只显示名称或可用性状态，原始 ID 留作内部查找键。同步 README、部署说明、AUDIT 与 SEED。 | 从本地元数据和静态调用链核对 Boss 索引及敌人图鉴来源。十组 MSVC 回归和七个生产编译单元通过，含模拟 `boss_16`、图鉴别名回退和 UI 名称检查。七项独立工作簿测试及生成夹具校验通过。Zig 0.17.0 构建、DLL query 1.1.1 与能力扫描通过。项目内 DLL 1,566,208 字节，SHA256 `7D2F2B8747A5D9F3FEDB66F757851D07A700D99FE04F38A71BF415D1EE46E976`。当前客户端实际图鉴数据和局内 `boss_16` 名称仍待复测。 |
| 2026-11-24 | 将当前源码版本从 1.1.1 提升为 1.2.0，纳入此前未发布的种子/Boss 主界面、商店记牌器及后续修复；同步 README、部署说明、SEED、AUDIT 与本文件。 | `verify.ps1` 十组 MSVC 回归和七个生产编译单元通过；独立 XLSX 校验器七项测试及生成工作簿夹具检查通过。Zig 0.17.0 构建成功，`inspect_binary.py` 实际加载 DLL query 版本 1.2.0、零前置插件依赖，禁用写入/对象创建能力扫描通过。项目内 DLL 1,566,208 字节，SHA256 `0442A4B0A609471A8E89C47879D280A3F0AB82AFEDEA669D892BC6C357C8136A`。未部署、重载、操作游戏或创建 GitHub Release；Boss 名称映射和记牌器联机覆盖等实机项目仍待复测。 |
