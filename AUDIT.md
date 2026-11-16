# 1.0.1 正式发布准备（2026-11-16）

- 用户授权推送并直接作为 Latest Release 发布。工作流显示名改为 Release，保留历史路径；只有所有验证和打包步骤通过才调用 `gh release create --verify-tag --latest`，不保留为草稿或预发布。
- 发布后检查标签、两个预期附件、draft=false、prerelease=false 与 Latest API。既有同标签 Release 不自动覆盖，发布失败先检查远端，避免替换正式附件。
- 重新运行 actionlint 1.7.12、七项 Python 校验器回归、独立 XLSX 和 DLL query 1.0.1/零前置插件依赖检查，通过。Python 临时目录在受限沙箱内被 Windows 拒绝写入，移到沙箱外同一测试全部通过，没有为此放宽测试或修改生产实现。
- 本轮提交前扫描 61 个公开树文件、97 个 Git 对象（71 个 blob，包含不可达对象）、refs/reflog，未发现本机用户名、个人邮箱、私钥/令牌或实际账户编号；仓库 SSH 地址、noreply 和匿名测试占位数值按语义排除。远端分支/标签已核对，现有 1.0.0 正式 Release 保持不变。远端新流水线、日志和附件结果待观察后补录。

# 1.0.1 Excel 工作簿属性修复（2026-11-16）

- 用户报告 Excel 打开导出的 XLSX 时要求修复，恢复日志指向 `/xl/workbook.xml` 的“工作簿属性”。在项目内复制问题报表，原始文件保持不变；所有真实表格和复现副本仍在 Git 忽略范围内。
- 本机 Excel 16.0 build 20527 的独立实例正常只读 `Workbooks.Open` 对原文件失败；仅去掉 `<fileVersion appName="xl"/>` 后全部 16 张工作表正常打开，补完整版本属性组也能打开。仅删除 `workbookPr` 或 `calcPr` 仍失败。ZIP CRC、普通 XML 解析不足以检出该兼容性错误。
- `make_workbook_xml()` 省略可选的 Excel 应用版本记录，不伪造 Excel 的编辑版本。除 `xl/workbook.xml` 中该记录外，复现对照的所有 Sheet、样式及其他 ZIP 条目逐字节相同，不改变统计值、隐私列、百分比、工作表命名或布局。
- C++ XLSX 回归新增禁止生成该错误记录的检查，在生产修复前实测失败，修复后通过。独立 Python 校验器先验证工作簿元数据，并增加七项匿名单元测试；发布草稿流程执行这些测试。该校验器是插件产物策略检查，不声称完整实现 OOXML Schema 验证。
- 新增可选 `tools/test-excel-workbook.ps1`：只创建独立 Excel 实例，关闭宏、事件、链接，以正常模式只读打开，不要求自动修复、不保存、不连接用户已有 Excel。插件与无 Excel CI 均不增加 Office 运行依赖。
- 验证：九组 MSVC 测试、七个生产 TU 编译、七项 Python 校验器测试、生成报表独立 ZIP/XML/数值/百分比检查通过；Excel 正常打开修复后的四 Sheet 生产夹具与十六 Sheet 故障文件的最小修复对照，原文件负例仍失败。已复核原始文件 SHA256 不变。
- Zig 0.17.0 构建 `dist/plugins/stronghold_stats.dll`，实际 query 版本 **1.0.1**，零前置插件依赖及禁用游戏写 API 扫描通过。DLL 大小 **1,416,192** 字节，SHA256 **FA21A9AB3A2851431AEB403033CF16B70F76061F8D8D69A31D7B9E5B4E988197**。这是本地修复产物，不是现有 1.0.0 Release 附件。
- 同步 README、部署说明和 AGENTS。未推送、打标、修改现有 Release、改写用户旧报表或覆盖运行中插件；未关闭、重启、调试或重载游戏，未改动游戏采集和玩法数据。旧报表可用 Excel 修复另存；新版仅对之后导出/重新导出文件生效。新版游戏内导出仍待用户自行选择时机验收。

# 1.0.0 首次公开发布与自动草稿（2026-11-16）

- 用户指定将版本统一重标为 1.0.0，保留原内部 2.1.2 采集、归属、计时、UI 和报表实现；历史记录保持原编号。
- 初始化目标 GitHub 空仓库 Noplch0/ArkACT 的 main 发布流程，使用 noreply 提交身份。不提交原始 reference、游戏模块、用户日志/快照、导出报表、缓存、工具或 dist 二进制。
- 为干净检出保留 tests/fixtures 中 29 类/两个集合的元数据声明子集以及显式 status/source 补充，不伪造新局内采样。抽取最初漏 DamageType enum，生产 binder 回归按预期红灯；补回原报告真实声明后通过，未放宽生产 ABI 校验。
- tools/dependencies.lock.json 固定 ArkFramework 94c616fc4c115603983a62fd0fc6f3928db482b1 与 Zig 0.17.0 下载 SHA256。prepare-dependencies 只构建匹配 ArkImGui，不启动宿主/注入器；SDK 头须与固定源码一致。build-plugin 支持 ImGuiDir，verify 自动发现 MSVC/SDK 并支持显式参数。
- 标签工作流校验版本/commit，执行全部验证后生成只含插件与部署/许可/构建信息的 ZIP 和 SHA256，仅创建/更新 draft，拒绝覆盖正式 Release；不自动正式发布。第三方 Actions 固定 commit，只有发布 job 申请 contents:write。
- 本地实际验证：依赖构建、九组测试与七个生产 TU 编译通过（MSVC 14.51.36231 / SDK 10.0.26100.0）；Zig GNU x64 构建、独立 DLL query 1.0.0/零前置插件依赖/禁用写能力扫描、独立 XLSX 校验、PowerShell 语法检查、actionlint 1.7.12 均通过。
- 本地 DLL：dist/plugins/stronghold_stats.dll，1,416,704 字节；SHA256 **7B950E9067AFD47C74AC04FB41A2499CE74C50037844D3DDA9EDB1AAFCAAA338**。这是本机产物，不冒充尚未观察的 CI 产物；最终附件另有 build-info.json 与 ZIP 校验和。
- 推送与远程工作流结果需观察后记录，不能把配置存在说成成功发布。
- 未改变游戏业务逻辑、增加网络上报/玩法写入能力，未关闭、重启、附加、重载游戏或替换正在使用的插件。联机实机边界仍与原基线一致，未新增实机验收结论。

## 首次远程流水线与 CI 编码修正

main 和 v1.0.0 已推送，标签对应 9fbf0c4。Actions 43811914402 已完成依赖、九组测试、七个 TU、GNU DLL 构建及实际 query 1.0.0。XLSX 检查在打印中文 Sheet 名时触发英文 Windows runner 的 cp1252 UnicodeEncodeError，并非报表结构校验失败；流程正确中止，没有创建草稿。

工作流明确设置 PYTHONUTF8=1；不移动或强推已存在的 v1.0.0，使用 main 上修正后的 workflow_dispatch 构建该固定标签。该配置修正不影响标签所指的插件源码、计量或只读边界，重新运行结果另记。

## 最终远程验收

- Actions **43812543420** 成功（workflow_dispatch 从 main 的修正配置运行，checkout 固定 v1.0.0/9fbf0c4），包含九组回归、七个生产 TU、依赖/GNU DLL 构建、DLL query、XLSX 独立检查、打包与 draft 创建。未移动已推送标签，未强推。
- 认证 API 核实 Release 作者 github-actions[bot]、tag=v1.0.0、draft=true、两附件均 uploaded。没有正式发布。草稿页面：https://github.com/Noplch0/ArkACT/releases/tag/untagged-6a4317b00941312cba7e （需仓库协作者登录）。
- 实际下载 CI 附件，独立校验 ZIP 允许清单/CRC、SHA256 文件和 build-info 的源码 commit/DLL hash；用本机匹配 ArkImGui 在独立 Python 进程加载该 DLL，只调用 query，版本 1.0.0、零前置插件依赖及禁用写 API 扫描均通过。
- CI DLL：1,416,704 字节，SHA256 **C6B08FA21D52D3137D25A51179D05EC1C460680FCA177CF95DF34405F74676C2**。
- CI ZIP：615,502 字节，SHA256 **7654220c71882e5b15846ad61f2c6b1e7b5506a428ae1729fa62abcbd9c4f1b7**。完整源码 commit：9fbf0c4a204dfad4af4caff3f7c6b986f8939473。
- 本地 DLL 与 CI DLL hash 不同，不能互相冒充；发布使用 CI build-info 和附件校验和。草稿仅创建未发布，不承诺匿名可下载。
- Runner 对固定的 Node20 Actions 给出弃用告警并自动转 Node24，未导致失败；未来更新依赖 Actions 要重新验证。未增加实机验收结论，未操作游戏或覆盖正在运行的插件。

# 2.1.2 盟约附加伤害、无来源承伤与动态召唤物（2026-11-15）

## 已证实的根因与修复

- 旧 `record_damage()` 只有 `source == null` 才尝试 originalSource。新增“非空但来源失效”的弹射物回归，在修复前实际失败：`FAIL capture 1511: count_kind(recovered, EventKind::Damage) == 1`，退出码 1；修复后通过。现在来源候选单独处理异常，已有完整施放者不被原来源抢占，玩家冲突的未知 token 宿主不被另一玩家覆盖。
- 原 `rebuild_host_cache()` 仅在列表人数/轮次等条件改变时刷新，且刷新时清空宿主；无法及时学习动态生成的嵌套召唤物，也可能丢掉已离场宿主的迟到效果。现在 250ms 运行态采样刷新角色/token，同轮保留已复制身份和已验证 owner，换轮、新局、controller/战斗切换和离开模式清除。实时覆盖集合与保留的身份缓存分开，旧宿主不伪造全房间覆盖。
- 原先环境 flag 和完全无法解析的来源会在发事件前直接丢弃。现在 applied 的 self / Logger 的 Modifier.target 必须证明是实际 Enemy，才对无来源实际 HP 伤害和环境标志开放保守保留。完全无来源进入全局 `unknown_effect / 无来源附加伤害`，不归自己或最近攻击者；**该项可能含环境承伤，表示已采集敌方扣血，不等于已证实的玩家输出**。它参与已采集总量/占比，未知玩家没有可靠时钟，DPS/HPS 为 0。明确 Enemy 来源的攻击/自伤、友方/空/未知目标的环境事件、未知来源的治疗/元素积累仍过滤。
- 子代理复核发现并修复：直接来源链明确为 Enemy 时，先过滤，禁止用友方 originalSource 将敌方攻击重新归给玩家。新增直接 Enemy 与 Projectile→Enemy 各带友方原来源的反例。复核提出的“无来源 environment 进入全局未知桶”已明确为本次产品语义选择，非玩家归属；相关风险说明、反例和零未知时钟回归均保留。复核未再发现其他阻塞性 P1/P2。
- 非 Character 的来源 Entity 可用其自身 `BObject.playerSide` 加实际卡片索引唯一验证玩家，进入该玩家“未归属附加伤害”。双向 side/player 冲突不猜归属；`Modifier.playerSide` 仅诊断，不以 enum 顺序猜槽位，也不把调用方上下文当来源事实。
- 诊断新增恢复/冲突计数与最后未归属样本；只记类型、flags、数值和槽位。诊断类白名单新增 BObject/Unit/Enemy/Token/Trap 等。`stronghold-source-schema.txt` 明确标注当前客户端静态证据补充和测试枚举模拟，不伪造新的局内导出。

## 本轮实际验证

- `verify.ps1`：九组测试运行通过，七个生产 translation unit 编译通过；最终日志 `.build/verify-2.1.2-final.log`。模型 18 项，CSV 6 组，XLSX 4 组均通过。新增覆盖动态/嵌套召唤、同轮宿主离场、环、side 冲突、非空与异常来源回退、已确认直接归属优先、来源/目标敌我反例、无来源/环境保留、同 tick Logger 抑制及连续同值 DoT。
- 无来源模型/报表回归覆盖战斗/协防的整局与轮次隔离、历史/新局重置、sequence 幂等、中文、未知行有限比率和报表不泄露 UID/昵称编号/干员 ID。
- `build-plugin.ps1`：Zig 0.17.0 GNU x64 ABI 实际生成新版；日志 `.build/build-2.1.2.log`。
- `tools/inspect_binary.py`：独立进程加载并 query 2.1.2，确认四个导出、零前置插件依赖、无 damage_meter/RDPS 与禁用 managed 写入/创建 API。
- `tools/inspect_workbook.py`：独立校验测试 XLSX 的 ZIP/XML、UTF-8、列隐私、无公式、百分比与分阶段 DPS/HPS，通过。

产物：`dist/plugins/stronghold_stats.dll`，1,416,704 字节。
SHA256：`DFAA3A6EB4FAC13B8431DBCFD89A73CA637FE307130679563C6828182B73FA62`。

本轮未关闭/重启/附加/主动热重载游戏，未覆盖正在使用的框架插件，也没有新增游戏数据修改或网络功能。
**待实机**：用户指出的具体盟约/干员、动态召唤物及离场后效果；新入口之外直接改血或非 Enemy 敌方 Token 等仍需真实局内证据，不承诺任意脚本全部精确归属。建议用户方便时加载 2.1.2，在具体效果发生后于局内导出新诊断；热重载前已漏掉的伤害不能补回。

# 2.1.1 轮次、历史查看与局外界面（2026-11-14）

本轮处理用户反馈的五个界面和轮次问题；仅修改项目、运行本地测试并构建 dist，
没有关闭、附加、重启、主动热重载游戏，也没有替换运行中的框架插件。

## 改动与依据

- `AutoChessDataCenter.currentRound` 的正值直接作为游戏轮次，删除多余的 `+1`。
  准备阶段的非正数映射第 1 轮；0 到 1 不再发布虚假的换轮事件。生产链回归覆盖 raw 8 显示 8，
  第 1/2/8 轮快照互相隔离，累计不受影响。
- 模型新增只读 `find_round_view(round, phase)`，分别访问当前轮和已保存历史轮。
  无效轮次、0、Idle 返回空，不假装读取当前轮。历史读取不改变累计、时钟或快照。
- 新增 `ViewSelection`，用 0 表示跟随本轮，具体历史轮次保持固定。
  上一轮、下一轮和下拉列表只提供已采集轮次；选轮自动进入本轮范围。
  RoundStart 不改用户历史/阶段选择，新 SessionStart 重置到整局-战斗。
- 两个按钮分别切换范围（整局/本轮）与阶段（战斗/协防），顶部使用“范围-阶段”标题。
  同一选择同时决定摘要、玩家及干员数据；成员 mask 只来自所选阶段的真实数据和有效计时。
- 表格直接使用窗口剩余高度，删除 `min 150 / max 430 / -92` 的固定占位策略；固定表头并允许明细滚动。
  轮次按钮按字体与实际剩余宽度换行，最小窗口高度随字体适配。提示与导出路径置于独立弹窗，
  不为其常驻预留底部区域。
- ModeExit 后继续显示模型和历史轮，标记“上一局数据（只读）”，仍可切换范围/阶段、选轮和导出。
  局外禁用清零，不向已经销毁的玩法对象提交请求。只保留当前/最近一局，新局覆盖，不提供多局切换。
  保留范围是本次插件生命周期；热重载或关闭进程后已有 XLSX 仍在，但不会自动导入旧内存历史。

没有新增游戏字段写入、玩法调用或网络能力；既有游戏业务数据只读边界不变。

## 验证与交付

九组 MSVC 测试和全部七个生产源文件编译通过，完整输出在 `.build/verify-2.1.1-final.log`。
新增 UI 测试直接运行生产 plugin.cpp 与真实 Dear ImGui，游戏和框架接口均为测试替身：

- 模拟范围/阶段按钮、上一轮/下一轮和下拉选轮，核对摘要标题与实际值。
- 历史选择固定、跟随本轮、新局清理、局外保留和禁止局外清零。
- 不混入另一阶段的零数据成员，不把当前计时成员带到历史轮。
- 900x560、620x260 和放大 1.5 倍字体布局，表格底部不留额外区域、不越界。
- 打开长提示弹窗前后表格边界不变；展开 40 个干员可滚动，表头固定。

Zig x64 构建成功，输出在 `.build/build-2.1.1-final.log`。
`tools/inspect_binary.py` 实际加载项目 DLL 并查询版本 2.1.1，确认四个插件入口、零前置插件依赖，
无 damage_meter/RDPS 和禁用的 managed 写入/创建接口残留。
`tools/inspect_workbook.py` 独立校验 ZIP/XML、中文、多 Sheet、百分比与 DPS/HPS，通过。
独立 UI 代码评审保存在 `.build/review-2.1.1-ui.md`，没有未决 P0/P1/P2 问题。

产物：`dist/plugins/stronghold_stats.dll`，1,397,248 字节。
SHA256：`7081E97660B365725E90FFA48F8C5885892A90CCB20BA5E4EF89EAFC609210A7`。
新版未进行游戏内验收；真实联机轮次、界面布局与退出后的复查仍需用户加载新版验证。

# 2.1.0 等待计时、协防分离、工作簿与只读能力（2026-11-14）

用户已确认 2.0.7 能统计真实伤害，本轮保留这条采集链，修复其后的计时、阶段和报表问题。
本轮只读当前客户端静态文件、修改项目源码、运行测试和构建项目 dist；没有关闭、重启、
调试附加或主动热重载游戏，没有替换运行框架目录中的插件。

## 阶段与计时证据

静态检查使用用户提供的 il2cpp-mod-template 参考和当前 GameAssembly / metadata，
分析与反汇编保存在 `.build/analysis-2.1.0-waithelp/`。生产仍通过运行时元数据解析字段、
方法、枚举常量，不使用研究输出的 RVA 或字段偏移。

- `BATTLE_WAITING` 不属于 `inSelfBattleState`，所以等待时继续采集队友事件，单独冻结自己。
- 主战读取每玩家 `ScenePlayerData.gameState` 和 `SelfBattleRoundStatus.battleFinished`，
  不通过“几秒没伤害”或暂时没有敌人推断结束。主战状态字典缺行时，以已确认的玩家战斗状态
  为计时依据并告警，不把缺行猜成完成；字典使用只读 TryGetValue，不调用修改型 Ensure。
- 当前客户端 Help / Boss 的 `get_battleFinished()` 原生路径固定返回 false，不能用于停止计时。
  协防按本客户端 HELP_BATTLE、实际成员名单和每玩家 gameState 计时；Boss 归主战。
- 协防优先 `battlePlayerIndex`，空或明显过期的多人名单回退到有效 `helpBattleData.players`。
  名单不确定时告警、保留已观察到的真实伤害，不凭空补齐该玩家的时间或数据。
- 每游戏帧先结清旧阶段的有效时间，再更新下一段阶段和玩家掩码，避免把等待间隔补进协防。
  主战与协防的累计、当前轮、每玩家时钟和数值完全隔离。
- 伤害 Hook 在原函数前记住阶段，原函数后读取实际值；同一模拟 tick 的连续结算事件沿用原阶段，
  最后一击先入队，下一次正常更新才发布 END，防止伤害被提前结束的模型拒绝。
- 手动清零请求到新 SessionStart 发布成功才消费；名单暂时不可读不会吞掉请求。
  清零前在游戏线程结清旧段。卸载不在渲染线程调用玩法 getter，报表使用最后游戏帧的复制数据。

新增静态结构夹具 `reference/current-schema/stronghold-status-schema.txt` 仅补充原局内日志未含的
嵌套状态类、封闭字典签名和枚举值，明确与真实运行日志区分。结构回放拒绝错误 out 引用类型、
缺失的战斗枚举和错误的 gameState 字段类型。

## 报表与隐私

新增原生 C++ XLSX 写入器与 ReportArchive：整局累计及每轮为独立 Sheet，每张表按主战/协防分行。
最终文件以实际 END 观察时的本机本地时间命名，精确到分钟，Windows 中用 `HH-mm`；同分钟不同局加后缀。
对局中更新进行中文件，清零或未结束卸载保存中断快照，不把中断时刻当成整局结束。

删除导出中的昵称编号、干员 ID、统计范围和独立状态列；玩家状态合并到昵称，占比采用百分比格式。
昵称编号已停止采集。文本格使用 inlineStr，数值格保留数值类型，不执行玩家名称中的公式文本。
写入器使用 ZIP/CRC32 + OOXML，无 Excel/Python/额外 DLL 运行依赖；失败保持旧文件和待写快照，
运行期间节流重试，跨会话不丢失败任务。卸载时若仍写入失败，会保留 DLL 和快照供再次卸载时重试；
此时框架已停止回调，不宣称还会在后台自动重试。

## 游戏数据写入能力

生产调用链没有写角色、敌人、伤害、血量、技能、资源、关卡、结算、存档或服务器数据。
删除 common Runtime 中未使用的 setter、Nullable 写入、对象/字符串/数组创建和泛型闭合功能；
third_party API 表同步从 83 项缩减为 65 项，删除相应写入、创建、初始化、装箱和程序集加载接口。
编译期测试要求这些接口不存在，实际 DLL 检查也拒绝残留写接口字符串。

框架安装原生 Hook 会改变代码跳转，GC handle 会改变运行时生命周期管理状态，
浮层输入保护会屏蔽被浮层捕获的鼠标或按键。因此本项目承诺的是没有修改游戏业务数据的功能，
不作“进程中零内存改动”或“完全不改变输入行为”的不实承诺。

## 验证范围

验证涵盖八组 MSVC 测试、七个生产源文件编译、Zig x64 DLL 构建、实际 DLL 加载查询，
以及独立 Python 标准库解析生成的 XLSX，核对 ZIP CRC、XML、中文、多 Sheet、数值型 DPS/HPS、
百分比格式、隐私列、冻结首行、筛选与无公式。测试运行时 DLL 仅在 .build 中，未放入游戏或框架。

新增生产链回归覆盖四人主战、自己等待而队友继续、所有玩家完成、两人协防、单人协防结束、
准备阶段、换轮累计、最后一击与同 tick 后续事件、END 幂等和手动清零请求保留。
2.1.0 的新行为仍须用户实机验收；模拟测试和静态代码证据不能代替真正联机对局结果。

最终验证：`verify.ps1` 八组测试及七个生产源文件编译全部通过；Zig x64 构建成功。
`tools/inspect_binary.py` 实际加载项目产物，查询版本为 2.1.0、零前置插件依赖，
确认无 damage_meter/RDPS 及禁用的 managed 写入/创建接口残留。
`tools/inspect_workbook.py` 独立解析测试工作簿，ZIP/XML、中文、多 Sheet、百分比与 DPS/HPS 校验通过。
测试步骤与最终结论保存在 `.build/verify-2.1.0-final.log`，构建输出保存在 `.build/build-2.1.0-final.log`。
编译器及测试程序的详细输出由 verify 的 Out-Host 显示在本次执行记录中。

最终产物：`dist/plugins/stronghold_stats.dll`，1,308,672 字节。
SHA256：`6195AAA57A6872654A637AAC1114DDCF0200D7CDC618AB4340B32663CCC1B9D9`。
仅准备项目 dist；没有替换或重载用户正在运行的插件，也没有关闭游戏。

# 2.0.7 局内采集提前停止修复（2026-11-14）

本轮依据用户在四人联机对局内导出的 `stronghold-schema-14412.log`。
用户仍在游玩；本轮只读取客户端静态文件、修改项目源码与构建本地测试，
不关闭、重启、调试附加或热重载游戏，不替换运行中框架的插件文件。

日志直接确认：

- 四个槽位的 `nickname_present=1`，不能把界面只显示自己归因于昵称读取失败。
- `OnAppliedModifier` 命中 21 次，嵌套 Logger 抑制 42 次，发布伤害事件为 0。
- 21 次全部计入 `outside_mode`；没有进入伤害数值或来源解析，不能据此判断它们失效。
- 历史中控制器已经处于 `PLAYING=2`，但 `capture_active=0`；名单和索引留在准备阶段。

生产代码错误地把 `endingStatus > 0` 当作模拟结束条件，提前发布 SessionEnd，
随后每次刷新都在名单和索引更新前返回。这也解释了画面中的“模拟已结束”和零计时。
2.0.7 将该字段降为诊断值；会话结束交给 Manager/dataCenter 生命周期及会话身份，
战斗计时继续由 BattleController 状态管理。旧日志没有记录该字段的原值，
不能用这份日志声称已确定当时具体的结算状态。

只读检查当前 GameAssembly 与 metadata 进一步确认 `EndingStatus.NONE=0`，
其余值为 1..4，`_CalcEndingStatus` 根据结算数据分类结果，不检查实时战斗状态。
排除“NONE 常量为 1”的猜测。`DoLoadGameInternal` 用 `stageRandomSeed + currentRound`
派生该轮随机流，支持继续将种子用于整场身份、轮次单独切分。
证据保存在 `.build/analysis-2.0.7-ending-status-report.md`，分析地址未写入生产。

同时修正本地模拟范围的初始化顺序：角色列表可以先于卡片归属索引就绪，
旧缓存仅在列表数量变化时重建，可能长时间保留空范围。现在每次刷新索引后，
用缓存中真实存在的角色 cardUid 重新验证玩家集合，不用名单数量推测模拟范围。

统一 UI、模型后备标签与 CSV 的槽位格式：当前 raw 1..4 对应 P1..P4，
不会再次加 1；归属索引与位掩码保持原值。raw 0 不猜成 P1，标记 P?。
修正空可见掩码误显示整份名单的问题；没有本地采集证据的队友仍不生成零伤害记录。

本次日志的 connection/hp/索引全零来自提前停采前的初始化名单，
不足以证明恢复刷新后仍为零；因此不擅自把未知连接状态改为已在线。
未知状态继续保留可归属伤害、暂停个人计时，明确离线/退出/淘汰继续冻结。

诊断增加 `settlement_status` 原值，历史按阶段变化保留，
避免 250ms 一次的时钟变化挤掉开局的关键转折。最新完整快照与退出后留存仍保留。

结构回放改用本次局内的 14412 报告，实际消费其中的 Entity 和 COLLECTION 签名，
不再用模拟 TryGetValue 替代这份报告已包含的方法。报告遗漏的封闭泛型单例方法
仍作为有当前客户端静态依据的显式补充夹具，不向生产写入 RVA 或字段偏移。

六组 MSVC 测试与全部七个源文件编译通过，Zig x64 构建成功。
新增完整生产采集流程测试：四人准备、相同索引字典延迟就绪、
endingStatus 为 1/2/3/4/99、名单和索引替换、两轮累计/快照/计时、
真正退出仅产生一次 SessionEnd/ModeExit；另补 UI 与两张 CSV 的 P1..P4 验证。
在 `.build/regression-2.0.7-old/` 隔离副本恢复旧提前停采分支，
新采集测试按预期失败；正式源码测试全部通过，确认测试能抓住本次回归。

`tools/inspect_binary.py` 实际加载项目产物并查询到版本 2.0.7，
确认四个插件导出、零前置插件依赖，无 damage_meter/RDPS 内容。
产物：`dist/plugins/stronghold_stats.dll`，1,236,992 字节。
SHA256：`1D15A96283F6B59DB863AB491457364C42531A1B2831A1C6CFCB4DB690835BA5`。

运行中的 `D:/tools/ArkFramework/plugins/stronghold_stats.dll` 未替换，
仍为 2.0.6，SHA256 与本轮开始时一致：
`3D8C0E7CE8587314A26634BE48A00EBBF6A58ED4574A6CDDFFBC6B6265E4CB9A`。
本次没有执行游戏内测试；真实伤害归属、画质覆盖和队友状态仍待新版实机验收。

# 2.0.6 伤害采集与退出诊断修复（2026-11-14）

用户明确说明 `stronghold-schema-4708.log` 导出时已经退出对局。
这份旧报告主要是结构枚举，不能用缺少当时的 DataCenter 推断对局内也缺失，
也不能从结构存在推断伤害已采集。2.0.5 在真实游戏中仍有零伤害问题。

对当前安装的 GameAssembly.dll / global-metadata.dat 做只读静态检查，结合用户日志与模板 API 后端，确认：

- 当前 `ScenePlayerData.isDead` 的原生逻辑要求非 QUIT、gameFinished 且 hp <= 0，并允许 XLua 覆盖。旧代码仅凭 gameFinished 或在线 hp <= 0 就判淘汰，可能冻结未初始化/未死亡玩家。现在调用游戏 getter，并区分未知、在线、掉线、退出和淘汰。
- `AutoChessDataCenter.instanceOrNull` 在没有 Manager 时可能解引用空实例。现在动态查找 Manager 泛型父类的 `get_instanceOrNull`，检查空值后读取 dataCenter；离开模式属于正常生命周期。
- `Entity.OnAppliedModifier(ref Modifier)` 原生体末尾调用两个 Logger，但 XLua 分支会绕开该原生体。增加原方法完成后的采集，保留 Logger 后备；同线程同 Modifier 的内部 Logger 被抑制，其他事件与相同地址的后续 DoT tick 保持独立。
- controller 现在由 GC handle 保活，在 ClearRunTimeData / OnDestroy / 离开模式时清理。未知连接状态仍保留可归属伤害、暂停计时，明确断线/退出/淘汰维持冻结。

当前常量 ONLINE=1、MISS_CONN=2、QUIT=3、PLAYING=2、FINISHED=3 与旧代码相同，
因此不把“枚举变化”列为零伤害根因；生产绑定改为按运行时名称读取。
模板文档关于 runtime_invoke 值参数的经验不能证明本项目调用失败，未据此全局改成裸函数调用。
旧模型已经把确认模拟的玩家加入可见集合，单改 effective_mask 不是本次根因修复。

诊断增加加载以来的 Hook / 事件 / 过滤计数、槽位原始状态、本地集合、最近伤害、
读取错误、渲染模型总量和有界历史。单独保留最后对局内快照与最后战斗快照，
退出后不访问已销毁的对局对象。离开模式自动写报告；按钮导出也包含保留快照。
日志不记录原始 UID / 昵称值。不能从旧报告确定唯一零伤害原因，新增入口与已确认逻辑修复仍需实机验证。

六组测试新增覆盖嵌套入口去重、无 Logger 完成入口、重复 DoT、原回调异常清理、
实际 isDead getter、可空 Manager / dataCenter、退出快照留存与隐私、未知连接状态、
明确退出冻结、诊断运行时文本与纯元数据兼容。
结构测试使用 4708 记录；Entity 和封闭泛型父类方法是有当前静态分析依据的显式补充夹具，
并非声称旧导出包含它们。分析地址只用于研究，生产仍按元数据动态解析，无写死 RVA。

交付保持一个 ArkFramework 插件 stronghold_stats.dll，不增加独立探针、注入器或游戏目录文件。
验证结果：`verify.ps1` 六组测试与全部源文件编译通过；Zig x64 构建通过。
`tools/inspect_binary.py` 分别实际加载构建和部署副本，确认版本 2.0.6、四个插件导出、
零前置插件依赖、无旧 damage_meter/RDPS 内容。
产物大小 1,235,968 字节；SHA256：
`3D8C0E7CE8587314A26634BE48A00EBBF6A58ED4574A6CDDFFBC6B6265E4CB9A`。
已替换 `D:/tools/ArkFramework/plugins/stronghold_stats.dll` 并校验哈希一致。
旧版备份：`D:/tools/ArkFramework/backups/stronghold_stats-2.0.5-B1992B39F465.dll.bak`。
本次没有运行游戏内功能测试，没有修改游戏目录。以下是历史记录。

# 2.0.5 当前客户端适配（2026-11-14）

依据用户提供的 `stronghold-schema-34052.log`，已确认并修复以下结构差异：

| 项目 | 当前客户端记录 | 处理 |
| --- | --- | --- |
| 卡片到棋子索引 | `runtimeCard2Chess: Torappu.ListDict<uint,ChessInst>` | 按引用类型 out 参数取出 ChessInst，再用 `instId -> instId2PlayerIndex` 确认玩家 |
| 玩家索引集合 | `instId2PlayerIndex: Torappu.ListDict<int,int>` | 按完整 TryGetValue 参数、bool 返回值和实例方法标记验证，兼容原 Dictionary 分支 |
| 旧 token 字典 | `runtimeToken2CardDict` 不存在 | 当前结构不绑定旧字段；宿主仍沿 `tokenOrHostUniqueId` 递归确认 |
| 棋子身份 | `ChessInst.cardUid / instId / playerIndex` | 校验字段类型；UID 或玩家不一致、空值、缺索引时不猜归属，事件保留未归属警告 |
| Hook 安装 | 七个 Hook 全部与记录匹配 | 先验证全部元数据，再创建任何 Hook；签名错误归类为不兼容并输出诊断 |
| 封闭泛型诊断 | 2.0.4 日志没有列出集合的实际方法 | 2.0.5 通过索引字段类型补充 COLLECTION 与 TryGetValue 完整签名 |

增加 `stronghold_schema_tests`，将该日志中的类、字段、方法和 static 标记送入生产绑定流程。
集合方法和日志遗漏的类型大小是显式模拟，测试不加载游戏、不执行玩法方法。
生产采集链测试分别运行旧整数索引与当前引用索引，检查四个槽位、空值/身份错配、
召唤物、嵌套弹射物和连续 DoT。当前一共六组测试。

交付版本为 **2.0.5**，依旧只有一个框架插件 `dist/plugins/stronghold_stats.dll`。
字段与签名适配有当前日志依据，真实对局的采集、画质覆盖与计时仍待实机验收。

最终验证：`verify.ps1` 六组测试与源码编译通过，Zig 构建成功，
`tools/inspect_binary.py` 实际加载 DLL 并确认 2.0.5、零前置插件依赖、无旧 damage_meter/RDPS 内容。
产物大小 1,207,296 字节，SHA256：
`B1992B39F46598D3F0D484960766399283DCB0B0509C820CAD00996681FD47E7`。
已校验并替换 `D:/tools/ArkFramework/plugins/stronghold_stats.dll`，
原 2.0.4 保存在 `D:/tools/ArkFramework/backups/stronghold_stats-2.0.4-DB29AF3F98C9.dll.bak`。
未启动游戏，也未修改游戏安装目录。

下面保留 2.0.4 的历史检查记录；其中“等待日志”描述当时的状态，当前日志已收到。

## 2.0.4 历史检查记录

本次交付仍然是 **ArkFramework 插件**，只有 `dist/plugins/stronghold_stats.dll`。
请将框架放在游戏目录之外。没有发布另一份独立探测 DLL，也不向游戏目录安装文件。

## 已确认并修复

| 问题 | 原有影响 | 修复与验证 |
| --- | --- | --- |
| `il2cpp_field_get_value_object` 参数反序 | 把对象当 FieldInfo，字段找到之后仍可能读错内存或崩溃 | 统一使用经过类型检查的 `read_reference(object, field)`，内部按 API 的 `(field, object)` 顺序调用；模拟 API 检查实际参数 |
| 战斗开始时强制刷新同时强制新会话 | 每轮 StartGame 都可能清空整场累计 | 区分刷新与手动清零；由数据中心、种子和固定 UID 名单判断会话，缺席玩家不改变身份 |
| 静态 `get_fixedPlayTime` 收到 controller 指针 | 隐藏 MethodInfo 参数错误，潜在 ABI 问题 | 传入对应 MethodInfo；绑定 getter 时检查静态/实例属性与返回值大小 |
| 把队伍数和少量队友伤害当作房间覆盖 | 可能错误显示“全房间”，包括未采集玩家 | 使用卡片映射确认的本地玩家身份集合；按在线集合覆盖关系判定范围 |
| token 字典被当作宿主映射 | 可能归错干员、把召唤物单独列行 | 旧原生二进制确认字典值为 token 自身 cardUid；仅用于归属玩家，宿主沿 `tokenOrHostUniqueId` 追溯 |
| 弹射物回溯只允许 4 层，无循环检测 | 多层来源丢失或错误回溯 | 有界回溯 16 层并检测环；未知宿主明确保留警告行 |
| 离线冻结只依赖采集端掩码 | 延迟或陈旧事件仍可增加退出玩家的时间和伤害 | 模型再次检查在线/淘汰状态，冻结累计值及分母 |
| 重复/过期事件无模型防护 | 重放可能重复累计，跨会话或跨轮事件可能污染新数据 | 按事件序号幂等，并过滤错误会话、旧轮次和结算后事件；连续的真实 DoT tick 仍分别累计 |
| 队列丢弃已消费历史也标为不完整 | 长时间正常运行也会出现误报警告 | 仅在读游标确实落后于保留范围时报告丢失 |
| 轮次快照只有一个待导出槽位 | 写入失败或连续切轮时覆盖尚未导出的快照 | 按序保留待导出的轮次与当时的范围 |
| CSV 失败仍报告成功、轮次文件名不补零 | 用户看不到导出失败，文件命名与计划不一致 | 检查写入/关闭结果，统一 r01/r02 文件名；不输出未采集队友的假零值 |
| 缺字段被显示为等待启动，错误文本被裁切 | 反复重试但无法提供适配证据 | 明确区分启动等待和结构不兼容；后者在安装采集 Hook 前停止并自动生成当前游戏元数据报告 |

## 实际使用了提供的模板

`third_party/il2cpp-mod-template/` 来自用户指定仓库的
`ee2afbf528f73012c493277aff934f18440f21e6` 版本，与教程 ZIP 的提交标记相同。
将其中的 IL2CPP API 表和解析器直接编译进同一个插件；没有复用模板的独立载荷入口、覆盖层或注入器。

报告路径：`<ArkFramework目录>/diagnostics/stronghold-schema-<pid>.log`。
在结构不兼容时自动生成，正常界面也可手动导出。只枚举元数据，不调用玩法方法，
不输出玩家 UID 等字段的值。方法签名包含参数、返回值、static 标记，字段包含真实类型与反射偏移。

## 验证范围

`verify.ps1` 编译并运行统计模型、CSV、IL2CPP API、生产采集链和模板诊断后端的五组测试，
然后编译全部插件源文件。诊断测试会在 `.build/verify/mock-runtime/` 中创建一个假的
GameAssembly 供测试进程加载；它不是游戏文件，不发布、不部署。

`tools/inspect_binary.py` 检查真实产物的 x64 架构、导出、系统/ArkImGui 导入和旧符号，
在 Windows 上实际加载插件并查询版本与零前置插件依赖。

教程 dump 来自 2026-10-09。对配套 `GameAssembly_old.dll` 的静态反汇编确认了
`ConstructRunTimeIndexer` 和 `TryGetCardUidByInstId` 的卡片/token 映射语义。
这些反汇编地址仅用于分析旧二进制，未写入运行时代码。

**当前截图中的缺字段差异仍待当前客户端日志确认，不能凭旧 dump 声称完成当前版本适配。**
本轮的可执行交付是修复已确认错误的框架插件，并内置已通过模拟运行时验证的结构探测。
1/2/4 人、画质模拟范围、真实伤害和断线时机仍需游戏对局验收。
