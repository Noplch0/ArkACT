# 卫戍协议伤害统计（stronghold_stats）

基于 [ArkFramework](https://github.com/wuyu8512/ArkFramework) 的插件，仅适配《明日方舟》PC 端「卫戍协议」模式。

当前发布版本：**1.0.0**。这是首次公开仓库发布的版本重标，保留此前内部 2.1.2 的完整功能和修复，并非回退实现；历史审计记录中的 2.x 编号保留用于追溯。

插件直接通过 ArkFramework 原生 Hook 读取已应用的伤害与战斗日志，不再依赖或消费 `ark.battle.events`，因此不需要 `damage_meter.dll`、`buff_capture.dll` 或任何增益扩展。旧 `operator_stats.dll` 与本插件不要同时部署，避免重复统计。

> 本项目仅供学习与研究。第三方工具可能违反游戏用户协议，后果自负。

## 功能

- 按玩家和干员两级展示实际伤害、DPS 与占比，界面采用 HunterPie 式紧凑浮层。
- 支持 1-4 名本地模拟玩家；中/高画质下游戏模拟整个房间时显示全部在线玩家，只确认本机模拟时显示自己；确认了一部分玩家时明确显示“部分采集”。
- 同名干员按“玩家 + 干员 ID”隔离；同一玩家的重复实例合并为一行并显示实例数。
- 实际伤害以 `|realDelta|` 累计，计算伤害、治疗、HPS、元素损伤保留在干员明细和工作簿中。
- DoT、召唤物、陷阱、弹射物伤害回溯到所属干员，并计入该干员与玩家总量；不单列 DoT。
- 两个切换按钮分别控制整局 / 本轮、战斗 / 协防，顶部明确显示“整局-协防”“本轮-战斗”等当前选择。战斗和协防的伤害、时间、DPS、治疗及元素损伤分别累计。
- 轮次直接对应游戏内回合，不额外加一。可通过上一轮、下一轮或下拉列表查看已采集轮次；历史选择在换轮时保持，选择当前轮则继续跟随新轮次。选择轮次会自动切到本轮范围。
- 离开卫戍协议后仍可查看刚结束那一局的整局与各轮数据；进入新模拟后替换上一局，不提供多局切换。数据保留在本次插件内存中，重载插件或关闭进程后仍可通过已导出的 XLSX 查看。
- 表格使用剩余窗口高度，首行固定、干员明细可滚动；提示和导出路径收进弹窗，不再为底部空白预留区域。
- 每位玩家完成本轮主战后立即停止个人计时；等待队友、准备、商店和结算时间不计入 DPS。仍在战斗的队友继续单独计时。
- 协防仅给明确参与且尚未完成的玩家计时，最多两人；不把主战后的等待间隔或主战时间带入协防。
- 玩家淘汰、掉线或退出后保留累计数据并停止计时，不会继续拉低其 DPS。
- 新模拟自动清零，也可在浮层中手动清零。
- GameAssembly/IL2CPP 尚未就绪时自动重试；游戏类型结构不兼容时停止采集并自动导出诊断，避免无限重试。
- 注册 Unity 原生输入适配器，浮层捕获鼠标/键盘时不会把输入透传给游戏。
- 自动保存整局多 Sheet 工作簿，包含整局累计和各轮数据；主战与协防按阶段分行。

## 数据归属与口径

核心 IL2CPP 类型均按类名/方法名动态解析，不硬编码 dump RVA 或字段偏移。函数签名、值大小和字典键值类型在安装采集 Hook 前检查：

- `Torappu.Battle.Entity.OnAppliedModifier`：原方法执行后采集已经结算的 `realDelta`。保留 `BattleLogger.LogModifier/LogEpModifier` 后备入口，并按线程和当前 Modifier 抑制内部嵌套 Logger 的重复统计；连续 DoT tick 不按地址或数值去重。
- `Torappu.Battle.Modifier`：`realDelta`、`finalDelta`、伤害类型、来源、持续伤害标记和环境伤害标记。
- `Torappu.Battle.Character` / `Torappu.Battle.Projectile`：沿弹射物来源回溯实际施放者。
- `Torappu.Battle.DataCenter.AutoChessDataCenter`：卫戍协议会话和轮次；实际 `AutoChessGameStatus.END` 锁定结束时间，Manager/dataCenter 消失为后备结束信号，结算 outcome 仅用于诊断。
- `Torappu.Battle.DataCenter.AutoChessDataIndexer`：当前客户端按 `runtimeCard2Chess[cardUid] -> ChessInst.instId -> instId2PlayerIndex` 完成玩家归属；旧客户端使用独立的整数索引分支。
- `Torappu.Battle.AutoChess.AutoChessPlayerDataModel.ScenePlayerData`：玩家索引、昵称、连接状态和阵容。
- `AutoChessGameStatus.currentState`、`SelfBattleRoundStatus.battleFinished`、`ScenePlayerData.gameState`：逐帧确定各玩家的有效战斗时间；枚举值按运行时名称解析。

协防成员优先来自 `battlePlayerIndex`，为空或含超过两人的过期名单时，回退到有效的 `helpBattleData.players`。名单冲突、缺失或超过两人会明确告警；不以完整房间名单替代。主战完成判断读取 `m_selfBattleStatusInfo` 的只读 `TryGetValue`，避免缺失条目时反复触发游戏自身日志，不调用创建条目的 `_EnsureSelfBattleStatus`。当前客户端的协防与 Boss `get_battleFinished()` 原生实现固定返回 false，因此计时不使用这两个属性。Boss 归主战，完成状态读取每位玩家的 `gameState`。

DoT 使用 `Modifier.IS_CONTINUOUS_FLAG` 识别，但只并入实际伤害总量。召唤物通过 `Character.isToken` 与 `tokenOrHostUniqueId` 递归归还宿主。当前客户端的索引值为 `ChessInst` 引用，读取时使用指针大小的 out 缓冲区，并检查 `cardUid` 和 `playerIndex` 一致性。当前结构不再要求 `runtimeToken2CardDict`；只有旧客户端分支使用该字典反向确认玩家，不把它当作宿主映射。无法确认的宿主保留在“未归属宿主”行，并显示警告。

### 附加伤害与未知来源（原 2.1.2 功能基线）

- 直接来源仍非空但来源链失效时，也会尝试 `originalSource`，不会覆盖已有的完整归属；明确的 Enemy 来源也不能被友方原来源改记为玩家输出。
- 每次运行态采样刷新动态召唤物，保留本轮已确认的宿主/玩家描述，支持宿主离场后的迟到伤害；换轮、新局和 controller 切换清除旧身份。
- 来源是非干员 Entity 时，仅用该来源对象自己的 `playerSide` 与实际角色卡片映射验证玩家，进入该玩家的“未归属附加伤害”，不凭效果/最近攻击猜干员。
- 完全无来源，但确认为实际敌人（`Enemy`）已经受到的 HP 伤害时，保留到独立“未归属 → 无来源附加伤害”。环境标志不再自动丢掉已证实的敌方扣血；明确敌方来源、友方/未知目标仍排除。
- **全局未归属可能包含无法区分的环境扣血**，只表示已采集的敌方承伤，不证明由某名玩家造成；该项参与已采集总量和占比，不分配给任何玩家/干员，未知玩家时长不可确认时 DPS/HPS 为 0。不能识别的目标或宿主仍有诊断与提示，不承诺恢复 Hook 链外直接改血。
- `Modifier.playerSide` 只用于诊断，不当成房间编号或来源证明。局内导出的诊断包含来源/目标类型、恢复计数及最后未归属样本，不输出账户标识。

## 统计范围检测

插件比较在线玩家集合与本地战斗数据中通过卡片映射确认的玩家集合，并结合已捕获事件里的玩家身份判断范围。`battlePlayerIndex` 用于协防成员选择，不作为全房间采集证明；不把队伍数量、阵营数或单个队友的一次伤害当作全房间覆盖证明。

角色列表先于卡片索引就绪时，会在后续刷新中重新确认归属；不要求角色数量发生变化才更新模拟范围。

- 确认只采集本机玩家：显示“仅本机”。
- 确认覆盖全部在线玩家：显示“全房间”。
- 确认了部分玩家：显示“部分采集”。尚无证据时显示“检测中”。
- 退出或淘汰玩家的已采集记录仍然可见，后续事件不会继续改变其累计值或 DPS 分母。
- 未采集的队友不会被导出为看似完整的零伤害记录。

## 导出

运行需要 **ArkFramework 与该版本配套的 ArkImGui**，不需要其他前置插件。请把整个框架放在游戏目录之外，插件放在框架自己的 `plugins` 目录。

输出目录为 ArkFramework 根目录下的 `exports`：

- 整局结束后：`2026-11-14 19-30.xlsx`。名称使用本机观察到整局结束的本地时间，精确到分钟；Windows 文件名不允许 `:`，故时间采用 `19-30`。同一分钟的不同对局自动加 `(2)` 等后缀。
- 对局中换轮或手动导出：`进行中-<pid>-<session>.xlsx`，每次更新同一份工作簿，结束文件成功保存后移除该进行中文件。
- 手动清零或未结束时卸载：`中断-<pid>-<session>.xlsx`，保留已采集快照，不把清零或卸载时间称作对局结束时间。

每个文件含 `整局累计`、`第01轮`、`第02轮`……。各 Sheet 包含以下列：

`阶段、维度、轮次、玩家槽位、玩家、干员、实例数、实际伤害、战斗时间(秒)、DPS、伤害占比、计算伤害、治疗量、HPS、元素损伤`。

玩家状态合并为 `昵称(在线)`、`昵称(中途退出)` 等；伤害占比显示百分号。工作簿不包含原始 UID、昵称编号、干员 ID、统计范围或独立状态列。玩家槽位是房间内 P1–P4，不是账户编号。昵称编号已停止从游戏中采集。内部干员 ID 仅用于归属和聚合，不导出；未知干员名称显示“未知干员”。

XLSX 由插件内的 C++ 写入器直接生成，不需要 Excel、Python 或额外 DLL。文本作为普通字符串保存；数字为可计算的数值，百分比采用实际百分比格式。写入失败保留待导出快照并重试；原子替换失败不会破坏旧工作簿。源码仍保留精简 CSV 兼容接口供测试，界面与自动导出使用 XLSX。

## 游戏数据只读边界

插件没有修改伤害、血量、技能、资源、结算、存档或服务器数据的功能，也没有网络发送功能。未使用的字段 setter、对象/字符串/数组创建、Nullable 写入和泛型构造接口已删除，第三方 API 表也缩减为所需的读取能力。

ArkFramework 为观察游戏回调安装原生 Hook，会改写代码跳转；GC handle 管理读取期间的对象生命周期，浮层输入适配器会屏蔽被浮层捕获的鼠标与按键。这些是框架、运行时和界面机制，不能表述为“整个进程内完全不改动内存”。统计回调保留原游戏函数和参数，只读取结果并更新插件自己的模型。

## 构建

Windows x64、PowerShell、MSVC C++ Build Tools、Windows SDK 和 64 位 Python 3 用于验证；运行插件不需要这些构建工具。可复现发布锁定 Zig **0.17.0** 和 ArkFramework commit **94c616fc4c115603983a62fd0fc6f3928db482b1**，完整来源及 Zig SHA256 见 `tools/dependencies.lock.json`。

从干净源码检出开始（仅编译共享 ArkImGui 依赖，不构建/运行宿主或注入器，不操作游戏）：

```powershell
.\tools\prepare-dependencies.ps1
$deps = Get-Content .build\dependencies.json -Raw | ConvertFrom-Json
.\verify.ps1 -ImGuiDir $deps.ImGuiDir
.\build-plugin.ps1 -Zig $deps.Zig -ImGuiDir $deps.ImGuiDir -ArkImGuiImportLibrary $deps.ArkImGuiImportLibrary
python tools\inspect_binary.py
python tools\inspect_workbook.py
```

已有匹配 ArkFramework 构建时，仍支持默认相邻 `ArkFramework-main\third_party\imgui-1.91.9b` 和 `dist/libArkImGui.dll.a` 布局：

```powershell
.\build-plugin.ps1
```

产物：

```text
dist\plugins\stronghold_stats.dll
```

自定义路径：

```powershell
.\build-plugin.ps1 -Zig "C:\...\zig.exe" -ArkImGuiImportLibrary "C:\...\libArkImGui.dll.a" -DistDirectory "D:\...\ArkFramework\dist"
```

现在可用 `-ImGuiDir` 指定匹配的头文件目录；`verify.ps1` 自动发现 MSVC/SDK，也支持 `-ClPath`、`-SdkVer` 和 `-ImGuiDir`。私有 `reference/`、原始日志、游戏文件和本机构建缓存不会提交；测试只依赖 `tests/fixtures/` 中的最小元数据声明和显式补充。

## 自动 Release 草稿

版本唯一来源为 `common/stronghold_version.h`。提交代码后推送同名版本标签（本次为 `v1.0.0`），会触发 `.github/workflows/release-draft.yml`；也可在 Actions 手动选择既有标签运行。

工作流从锁定依赖构建，执行九组测试、七个生产源文件编译、DLL/query 与 XLSX 独立检查，再生成 `ArkACT-v1.0.0-windows-x64.zip` 和 SHA256 文件，**仅创建或更新 draft，不自动正式发布**。压缩包只有本插件、部署/许可说明和构建清单，不捆绑框架、游戏文件或原始诊断。已有正式 Release 不会被该工作流覆盖。发布草稿需要仓库协作者登录查看；由维护者检查后自行决定是否发布。

许可证：GPL-3.0-or-later；第三方精简后端保留 MIT。见 `LICENSE` 和 `THIRD_PARTY_NOTICES.md`。

## 验证

```powershell
.\verify.ps1
```

脚本会用 MSVC 编译并运行：

- `tests/stronghold_model_tests.cpp`：四人聚合、同名干员隔离、累计/当前轮、DoT 合入、离线冻结、低画质仅自己、范围检测、事件队列，以及历史轮次双阶段读取、退出保留和新局清理。
- `tests/stronghold_csv_tests.cpp`：UTF-8 BOM/中文路径、未知干员与治疗、CSV 转义、零分母、写入失败、无 UID 泄漏。
- `tests/stronghold_xlsx_tests.cpp`：ZIP/CRC32、OOXML、多 Sheet、中文、百分比、普通文本单元格、隐私字段和原子写入。
- `tests/stronghold_report_tests.cpp`：各轮双阶段快照、真正结束时间命名、同分钟冲突、失败重试、跨会话保留和清零/卸载中断报告。
- `tests/il2cpp_runtime_tests.cpp`：对象/字段参数顺序、父类字段、自动属性后备字段、存储大小检查。
- `tests/stronghold_capture_tests.cpp`：运行生产采集代码，验证实际卡片映射、多层弹射物、召唤物/嵌套宿主、DoT、阵营与环境过滤，以及静态 getter ABI；完整执行四人准备、战斗、非零结算状态、换轮和退出，检查名单/索引刷新、累计与轮次视图、战斗计时。
- `tests/stronghold_schema_tests.cpp`：用公开 tests/fixtures 中的最小元数据子集重放生产绑定流程（来源为历史局内报告，另附显式静态补充），检查字段、getter、八个 Hook 及实际集合签名；报告未包含的封闭泛型父类方法使用显式补充夹具，拒绝错误 out 类型与安装前缺失的 Hook。
- `tests/metadata_report_tests.cpp`：用测试运行时验证真实元数据枚举、改名字段、属性、完整方法签名、UTF-8 与线程解除附加。测试 DLL 仅在 `.build/verify` 下构建，不发布。
- `tests/stronghold_ui_tests.cpp`：以真实 Dear ImGui 离屏运行生产界面，模拟两个切换按钮、前后翻页和下拉选轮；验证历史固定、跟随当前轮、阶段成员隔离、局外查看、清零禁用、小窗口与放大字体下表格边界、提示弹窗、展开明细滚动和固定表头。游戏接口均为测试替身，不加载或操作游戏。

随后对全部插件、运行时适配与诊断源文件做语法编译。构建后运行 `python tools/inspect_binary.py`，验证实际 DLL 可加载、导出入口、版本号、零前置插件依赖，以及无旧 damage_meter/RDPS 内容。

## 运行时诊断

插件将你提供的 `il2cpp-mod-template` 中的 **il2cpp_core API 表与解析器直接编入同一个插件**。来源版本与 MIT 许可证见 `third_party/il2cpp-mod-template/UPSTREAM.md` 和 `LICENSE`。框架负责加载插件；探测直接通过游戏导出的 IL2CPP API 枚举当前客户端元数据。

遇到缺类、缺字段或调用签名不匹配，插件会在安装采集 Hook 前停采，并自动生成：

```text
<ArkFramework目录>\diagnostics\stronghold-schema-<游戏进程PID>.log
```

日志分为运行时快照与元数据两部分。游戏线程复制 Hook 命中、过滤原因、发布事件数、玩家槽位状态、本地模拟集合和最近的伤害样本；诊断导出只消费这些原生文本，不在渲染线程访问对局对象。2.0.7 新增结算状态原值，历史按阶段变化保留，避免时钟更新淹没开局转折。`LAST IN-MODE SNAPSHOT` 与 `LAST COMBAT SNAPSHOT` 在退出后保留，明确区分采样时间与导出时间。退出模式会自动生成同一份诊断，即使面板已隐藏也能获取。

元数据部分包含模块版本标记、导出 API、字段类型、属性、方法签名，以及索引集合的实际 `TryGetValue` 签名。生成元数据时不调用玩法方法；正常采集仍会在游戏线程读取状态 getter。日志不写玩家原始 UID 或昵称值，只标记昵称是否存在。不会生成独立探测 DLL，也不需要向游戏目录复制文件。

当前已收到 `stronghold-schema-34052.log`：客户端将旧 `runtimeCard2ChessDict: Dictionary<uint,int>` 改成 `runtimeCard2Chess: ListDict<uint,ChessInst>`，并移除了 `runtimeToken2CardDict`。2.0.5 根据该记录调整索引读取和归属链。记录保存在 `reference/current-schema/`，测试不向运行时代码写入其中的字段偏移。教程 dump 日期为 2026-10-09，仅用作旧结构参考。

2.0.6 另外修正了淘汰判断与菜单单例生命周期：使用 `ScenePlayerData.isDead`，不会仅因 `hp == 0` 或 `gameFinished` 就冻结玩家；先查询 Manager 的可空实例，再读 `dataCenter`，退出后不调用会解引用空 Manager 的 DataCenter getter。连接状态尚未初始化时保留已归属伤害，停止其未知状态下的计时；明确退出/断线/淘汰仍冻结。

2.0.7 根据局内的 `stronghold-schema-14412.log` 修正了提前停采：旧版将非零 `endingStatus` 当作模拟结束，即使控制器仍在战斗中也会停采并停止刷新四人名单。现在只记录该结算值，不用它阻断伤害与名单刷新。已确认的本地模拟集合仍是显示范围依据，不把未采集队友伪装成零伤害。

2.1.0 修正主战等待计时并分离协防；单工作簿包含整局和所有轮次，清理敏感与冗余列，删除未用游戏数据写接口。界面与报表按当前客户端原始槽位显示 P1–P4；内部索引与掩码不变。空可见掩码不会显示尚未确认采集的名单。

2.1.1 修正轮次额外加一，接入历史轮次查看、两个独立切换按钮和局外上一局查看。表格填满剩余高度并固定表头，窄窗口按需换行控件；提示内容在弹窗查看。各阶段成员只按该阶段实际数据或当前有效计时显示，不混入另一阶段的零统计行。

本次检查与修复详情见 `AUDIT.md`。

## 部署

保持 ArkFramework 在游戏目录之外。只将本项目的 `stronghold_stats.dll` 覆盖到框架发布目录的 `plugins` 下；不改游戏本体目录：

```text
ArkFrameworkLoader.exe
ArkFramework.dll
ArkImGui.dll
plugins\
  stronghold_stats.dll
```

启动游戏和 `ArkFrameworkLoader.exe`，按 `Insert` 打开浮层。第一次进入卫戍协议后开始显示；离开对局后保留上一局供查看，下一局开始后自动替换。也可以使用框架已有的插件热重载功能；战斗中重载后仅累计重载后的事件，重载前的历史轮次不会转移到新版内存，已有 XLSX 仍保留。本次开发仅准备项目 `dist` 产物，没有主动替换或重载运行中的插件。

## 实机验收边界

用户已实机确认伤害采集正常，并反馈 2.1.0 的轮次与界面问题。2.1.1 的轮次/界面和 2.1.2 的来源回退、动态召唤物与未归属承伤保留均有离线生产链回归；这些检查不能代替新版的游戏内验收。以下项目仍须在实际卫戍协议对局中验收：

- 单人、2 人和 4 人房间的全房间伤害与本机模拟结果一致。
- 低画质只确认自己；中/高画质下确实有全员本地模拟证据时显示全部在线玩家。
- 触发此前漏统的具体盟约/干员：附加伤害不漏；DoT、动态/嵌套召唤物及宿主离场后的迟到效果归属正确；真正无来源项单列且不强塞玩家。
- 队友掉线/淘汰后累计冻结。
- 轮次切换不重置累计，新模拟会重置。
- 自己清完怪后 DPS 冻结，未完成的队友继续计时；波次之间暂时无怪不会误判结束。
- 协防伤害与时间只进协防视图，主战 DPS 不受协防和等待阶段影响。
- 整局结束自动生成以结束时间命名的 XLSX，各轮与整局数据齐全、占比带百分号且无昵称编号。
- 游戏第 8 回合显示第 8 轮，导出的 Sheet 轮次一致；可查看已采集历史轮，选择历史后换轮不跳回。
- 离开对局后仍能查看上一局整局、各轮的战斗/协防数据；下一局开始只保留新局。
- 小窗口与展开干员时能滚动到最后一行，底部没有额外遮挡数据的空白区域。

游戏更新若改掉关键类名或字段，插件会安全停采并显示不兼容提示，不会回退到其他模式采集。

## 许可证

参考框架 ArkFramework 及其 common 辅助代码采用 GPL-3.0-or-later；本插件按相同许可证约束发布。
