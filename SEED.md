# 卫戍协议 Seed 与怪物生成机制

> 文档基线：2026-11-24
> 相关插件版本：1.2.0（已发布）
> 客户端证据：`reference/current-schema/stronghold-schema-4708.log`、`.build/analysis-2.0.7-methods.tsv`、`.build/shop-analysis-2026-11-22/`、`reference/dump-audit/`
> 性质：静态分析记录，不是实机网络验证，也不是玩法修改或自动化实现说明。

## 0. 结论摘要

一局卫戍协议**不是只使用一个 seed**。当前客户端可以确认三类 seed 字段、一个 DataCenter 副本、按回合派生的主随机流，以及若干辅助随机状态。

- **直接整数 seed 字段：3 个**
  - `AutoChessGame.seed`
  - `SceneGameData.stageSeed`
  - `BattleController.randomSeed`
- **DataCenter 副本：1 个**
  - `AutoChessDataCenter.stageRandomSeed`，不是独立 seed
- **每回合主随机流**
  - `stageRandomSeed + currentRound`
  - 每个实际加载的战斗回合通常生成一个 `AutoChessRandomWrapper`
  - 通过 `BattleController.ResetImpRandom` 成为该回合的 `randomImp`
- **辅助随机状态**
  - `randomTrivial`
  - `DicePRD` / PRD 状态
  - 其他独立 `AutoChessRandomWrapper` 实例

因此：

> 按 seed 字段数是 3 个；按一局内实际 RNG 根/状态数不是固定值。
> 每回合的“子 seed”不是客户端从服务器收到的独立 `roundSeed`，而是用 `stageRandomSeed + currentRound` 派生出的主随机流 seed。

seed 只能控制参与相应随机流的算法，不能单独决定完整对局。

## 1. 术语

### 1.1 Seed

本文中的 seed 是指：

- `Int32` seed 字段，或
- `AutoChessRandomWrapper` 构造时传入的整数 seed，或
- 战斗随机系统初始化时使用的 seed。

seed 不是最终随机结果。一个 seed 只有和当时的 RNG 状态、配置、权重、调用顺序一起才有意义。

### 1.2 Random stream

随机流是 seed 初始化后的运行态：

- 当前 cursor
- 缓存随机值
- 已消费次数
- 当前 map layer/hash offset
- 调用过的 `Next/Range/Dice/...`

`AutoChessRandomWrapper` 内部的 `AutoChessRandom` 有以下状态：

- `m_cursor: UInt32`
- `cachedResults: Double[]`
- `CACHE_RESULT_COUNT`

这些是随机状态，不是新的独立 seed。

### 1.3 显式状态

以下内容不是 seed，也不是随机结果：

- 模式 `modeId`
- 场景 `stageId`
- Boss / 隐藏 Boss
- `enemyTypes`
- `bannedBonds`
- 玩家 ban、阵容和商店
- 当前轮次 `currentRound`
- 服务端下发的 `roundEnemies`

它们必须单独读取。

## 2. Seed 分层

### 2.1 活动/存档 seed

字段：

```text
Torappu.PlayerActivity.PlayerAutoChessV1Activity.AutoChessGame.seed
类型：System.Int32
```

作用边界：

- 属于活动存档层的 seed。
- 当前客户端静态调用图中没有找到它对战斗或商店随机流的直接消费者。
- 不能把 `AutoChessGame.seed` 当作一局战斗的主 seed，也不能仅凭它重放整局。

### 2.2 场景 seed

字段：

```text
Torappu.Battle.AutoChess.SceneGameData.stageSeed
类型：System.Int32
```

来源：

- 联机：`AutoChessBattleSceneAllStateSyncDn.allStateSyncData.stageSeed`
- 本地教学/模拟：`AutoChessDataBridgeLocalPlayer.GenerateLocalSave` / `_GenerateTrainingData` 的本地构造路径

流向：

```text
SceneGameData.stageSeed
  -> AutoChessDataCenter.InitData
  -> AutoChessDataCenter.stageRandomSeed
  -> BattleController.ResetSeed(stageSeed)
```

`stageSeed` 是场景/战斗初始随机系统的主要输入。

### 2.3 DataCenter 副本

字段：

```text
Torappu.Battle.DataCenter.AutoChessDataCenter.stageRandomSeed
类型：System.Int32
```

它是 `stageSeed` 在 DataCenter 中的副本/可读属性。

**不要把它算成第 4 个独立 seed。**

### 2.4 战斗控制器 seed

字段/属性：

```text
BattleController.m_randomSeed
BattleController.randomSeed
类型：ObscuredInt / Int32 读取
```

它记录当前战斗控制器的随机 seed，并可由 `BattleLogger.LogSquadAndRandomSeed` 记录到战斗日志。

它不等于：

- 活动 seed
- 场景 seed
- 当前回合的 `randomImp` seed
- `randomTrivial` 的 seed

### 2.5 每回合派生主 seed

当前客户端在战斗加载时使用：

```text
roundSeed = stageRandomSeed + currentRound
```

构造：

```text
AutoChessRandomWrapper(roundSeed)
```

随后通过：

```text
BattleController.ResetImpRandom(roundSeed, wrapper)
```

替换该回合的主 `randomImp`。

这条不是“服务器每回合下发一个新 seed”，而是客户端可推导的回合主随机流 seed。

### 2.6 辅助随机流

#### `randomTrivial`

`BattleController.randomTrivial` 是辅助随机流，和主 `randomImp` 分开。

已知直接消费者包括：

- 投射物初始化
- 随机旋转
- 随机发射器/弹道选择
- 部分移动、位置和场景元素

#### PRD

`RandomExtensions.ResetPRDRandom` 和 `DicePRD` 使用独立的概率状态。

PRD 不是普通的“第 N 个 seed”，但会影响概率触发结果。具体触发还要看：

- PRD 类型
- 当前状态
- 概率表
- 调用顺序
- 实际调用者

## 3. 总数和计数方式

### 3.1 按 seed 字段计

```text
1. AutoChessGame.seed
2. SceneGameData.stageSeed
3. BattleController.randomSeed
```

答案为：**3 个可直接识别的 seed 字段**。

`AutoChessDataCenter.stageRandomSeed` 是 `stageSeed` 的副本，不计入。

### 3.2 按每回合随机流计

设一局实际加载 N 个战斗回合：

```text
N 个回合主 randomImp 实例
+ randomTrivial 辅助流
+ PRD 状态
+ 可能的其他独立 AutoChessRandomWrapper
```

`N` 不是固定值，取决于模式、回合数和实际加载路径。

所以不能回答“整局固定有多少个随机实例”。能确定的是：

> 每实际加载一个战斗回合，通常有一份按 `stageRandomSeed + currentRound` 派生的主随机流。

## 4. 初始化时序

```text
服务器/本地场景数据
  |
  +-- stageSeed
  +-- currentRound
  +-- enemyTypes / bannedBonds / roundEnemies 等显式状态
        |
        v
AutoChessDataCenter.stageRandomSeed
        |
        v
BattleController.ResetSeed(stageSeed)
        |
        +--> randomImp
        +--> randomTrivial
        +--> PRD 初始化/重置

战斗回合加载
        |
        +-- stageRandomSeed + currentRound
        +-- AutoChessRandomWrapper
        +-- ResetImpRandom
        +-- 当前回合 randomImp
```

本地训练/模拟路径可能使用另一条 `GenerateLocalSave` 构造链；其 seed 生成规则不能直接套到联机房间。

## 5. 怪物生成机制

### 5.1 主要类型

```text
AutoChessLevelEnemyManager
  m_randomEnemyGenerater
  m_roundEnemyData
  m_allBattleLevelIds
  m_originActionDatas
  m_realActionDatas
  m_levelActionDatas
  m_levelActionDatasWithUid
  m_levelSpActionDatas
```

其中：

- `m_roundEnemyData: ListDict<Int32, List<RoundEnemy>>`
  - key 是回合
  - value 是该回合的敌人预览/生成条目
- `m_allBattleLevelIds: ListDict<Int32, String>`
  - key 是回合
  - value 是对应关卡 ID

`m_allBattleLevelIds` 只能说明有哪些战斗关卡/回合，不代表所有回合的最终敌人条目都已经预生成。

### 5.2 初始化调用

当前静态调用顺序大致为：

```text
AutoChessLevelEnemyManager.Init(SceneRoundEnemyData)
  -> _GenerateAllBattleLevelIds()
  -> RandomEnemyGenerater._InitData()
  -> RandomEnemyGenerater._GenerateSpecialEnemyTypes(...)
  -> RandomEnemyGenerater._GenerateRandomEnemyData()
  -> RandomEnemyGenerater._GenerateRoundEnemyData(...)
```

它的结果进入：

```text
m_roundEnemyData
  -> GetRoundEnemies(round)
  -> SceneRoundEnemyData.RoundEnemy
  -> 敌人预览/实际生成
```

### 5.3 敌人条目字段

协议类型：

```text
Torappu.UI.AutoChess.Server.AutoChessBattleRoundEnemyInfo
```

字段：

| 字段 | 含义 |
| --- | --- |
| `round` | 所属回合 |
| `enemyType` | 内部敌人类型/随机敌人分类 |
| `enemyKey` | 具体敌人配置/资源 key |
| `actionIndex` | 关卡敌人生成 Action/Wave 索引 |
| `count` | 该条目数量 |

本地经过 `SceneRoundEnemyData.RoundEnemy` 后，供敌人预览和战斗 Action 生成使用。

### 5.4 随机敌人数据结构

`AutoChessRandomEnemyAttributeData`：

```text
enemyKey
level
extraEnemyIdentifier
extraEnemyKeyList
isFlyEnemy
enemyBattleEffectivenessFactor
```

`AutoChessEnemyTypeData`：

```text
type
typeIdentifier
involveRandom
```

`RandomEnemyGenerater` 的结果缓存包括：

```text
m_randomNormalEnemyResult
m_randomEliteEnemyResult
m_randomSpecialEnemyResult
m_specialEnemyTypeResultTypeList
m_templateEnemyInfo
```

这些字段可以在已经生成后读取；不要把它们当成纯 seed 字段。

### 5.5 特殊敌人

已知分类：

```text
AutoChessRandomEnemyType:
  NormalWalk
  NormalFly
  EliteWalk
  EliteFly
  SpecialWalk
  SpecialFly
  NonTemplateWalk
  NonTemplateFly

AutoChessSpecialEnemyType:
  NONE
  FLY
  TIMES
  ELEMENT
  DOT
  INVISIBLE
  REFLECTION
  SPECIAL
```

特殊敌人配置还涉及：

- `count`
- `countSingle`
- `weight`
- `showEnemyId`
- `randomWeight`
- `isInFirstHalf`
- `attachedNormalEnemyKeys`
- `attachedEliteEnemyKeys`

实际权重和具体赛季表值不在当前 schema 快照中，需要额外配置证据。

### 5.6 预览和实际战斗生成

`roundEnemies` / `RoundEnemy` 更接近**某轮敌人组成/预览和生成输入**，不是已经生成的 `Enemy` 实例，也不是击杀/伤害记录。

实际战斗侧还会调用：

```text
GenerateSelfBattleEnemyData(LevelData, List<SelfEnemyInfo>)
  -> _GenerateSelfBattleEnemyData(...)
  -> _InsertLocalActions(...)
  -> _DoLoadLevelEnemy(...)
```

还有：

- `GenerateHelpBattleEnemyData`
- `GenerateBossBattleEnemyData`
- `_GenerateAction`
- `_GenerateActionFromEnemyInfo`
- `_CalculateEnemyCountByBattleEffectiveness`

因此：

- 预览数量不必然等于实际最终生成数量
- `count` 可能被战斗效能、特殊配置或服务端 Action 调整
- 实际战斗还依赖 Wave/Action 配置和当前战斗状态

### 5.7 服务端边界

`RandomEnemyGenerater.ShouldActionUpToServer` 表明部分 Action 可以交给服务端处理。

此外，`sceneDetail.roundEnemies` 可能直接来自同步包。此时客户端只是读取服务器已经确定的敌人条目，本地 seed 不是最终权威。

## 6. 哪些内容能计算，哪些不能

### 6.1 可以直接读取的既有结果

如果客户端已经生成了某个回合的数据：

```text
m_roundEnemyData[round]
  -> List<RoundEnemy>
  -> enemyType/enemyKey/actionIndex/count
```

可以直接汇总。

也可以读取：

```text
SceneRoundEnemyData.roundEnemies
allRoundEnemyData
```

如果所有回合的数据都已经存在，就可以整理出客户端的逐回合敌人表和数量。

这属于**读取已生成/已同步状态**，不是 seed 预测。

### 6.2 仅凭 seed 计算

仅凭：

```text
stageSeed + currentRound
```

不能保证得到完整敌人表，还缺：

- `enemyTypes` 候选池
- 模式/关卡/Wave/Action 配置
- 普通/精英/特殊敌人权重
- `randomImp` cursor
- 之前所有 RNG 调用
- `RandomEnemyGenerater` 当前状态
- 服务端是否覆盖结果
- 当前玩家操作导致的调用分支

只有这些条件全部一致，才可能复现本地随机敌人的选择结果。

## 7. 三个怪池随机选择的情况

假设某个回合有三个候选怪池 A/B/C。

### 情况 A：客户端已有本轮结果

如果 `m_roundEnemyData` 或 `roundEnemies` 已经包含该回合条目：

- 直接读取实际选中的池和敌人 key
- 不需要 seed 预测

### 情况 B：三个候选都合法，但没有 RNG cursor

如果：

- 三个池都满足模式/关卡/回合条件
- 没有其他候选过滤
- 不知道当前 RNG 状态和调用顺序

那么只能得到：

```text
候选：A/B/C
```

以及可能的权重/概率，不能仅凭 seed 确定哪一个。

### 情况 C：完整 RNG 状态已知

如果同时知道：

- 三个候选的顺序和权重
- 当前 `randomImp` seed
- cursor
- 选择使用的随机函数
- 之前调用次数

则可以复现：

```text
randomImp.Range(...) / Dice(...) / UniformWithWeight(...)
  -> 选中 A、B 或 C
```

### 情况 D：服务端决定

如果有 `ShouldActionUpToServer` 分支、服务器直接下发 `roundEnemies`，或 XLua 热更覆盖了本地路径，则不能仅靠本地 seed 保证 3 选 1。

## 8. 能否算出整局每一轮敌人的种类和数量

分两种目标：

### 8.1 读取整个客户端已有数据

可以尝试读取：

```text
m_allBattleLevelIds     // 回合 -> 关卡 ID
m_roundEnemyData        // 回合 -> 敌人列表
SceneRoundEnemyData.roundEnemies
```

如果每个目标回合都有条目，就可以按 `round` 汇总：

```text
round
  -> enemyType
  -> enemyKey
  -> count
  -> actionIndex
```

得到客户端当前持有的逐回合敌人表和数量。

注意：

- 只有已生成/已同步的回合才是确定的
- 没有条目的未来回合不能由空列表推断为“没有敌人”
- 不要为了补全数据随意调用会触发懒生成或更新缓存的方法

### 8.2 只从 seed 离线计算整局

不能保证。

可能得到的是一组候选或概率范围：

- 每个敌人 key 的可能集合
- 普通/精英/特殊敌人候选
- 特殊敌人组合
- 每轮的可能敌人数量范围

只有本地完整确定性生成器、完整配置、完整 RNG 调用顺序且无服务端覆盖时，才可能复现完整结果。

## 9. Seed 能预测的最大边界

在最理想条件下：

```text
已知 seed
+ 当前回合
+ 完整配置
+ 当前 RNG cursor
+ 完整调用顺序
+ 精确玩家输入
+ 无服务端覆盖
```

可以复现客户端本地随机分支，例如：

- 本地随机敌人选择
- 本地敌人预览
- 本地随机羁绊
- 本地训练/模拟中的等级赠棋
- 部分技能/目标/弹道随机
- `randomTrivial` 随机
- PRD 概率判定

即使如此，也不等于整局预测，因为还必须知道：

- 其他玩家操作
- 服务端状态和独立 RNG
- 实际敌人 Action/位置/时序
- 商店购买和刷新
- 战斗伤害、击杀和最终结果

## 10. 不能由 seed 决定的内容

- 模式、Stage、Boss、隐藏 Boss
- `enemyTypes`
- `bannedBonds`
- 玩家单位 ban
- 玩家静态阵容
- 玩家实际站位和战斗操作
- 完整商店货架、价格、购买结果
- 服务端下发的最终 `roundEnemies`
- 服务端的 RNG、校验、结算和奖励
- 实际伤害、击杀、血量和胜负
- 其他玩家的本地 RNG 状态

## 11. 当前插件的读取边界

当前插件的主要统计链路只读：

- 游戏会话和轮次
- 战斗伤害 Modifier
- 玩家/召唤物归属
- 战斗时间

`SEED.md` 描述的是客户端静态研究和可选的全状态同步分析，不代表当前插件已经实现离线整局预测。

如果后续要读取怪物数据，优先只读字段：

- `AutoChessLevelEnemyManager.m_allBattleLevelIds`
- `AutoChessLevelEnemyManager.m_roundEnemyData`
- `SceneRoundEnemyData.roundEnemies`
- `RandomEnemyGenerater` 的已生成结果字段

不要：

- 调用可能触发敌人生成的 `Init` 或 `Generate*`
- 调用服务端 Action 请求
- 修改 RNG cursor、敌人列表或战斗状态
- 将 UID、昵称编号写入日志或报表

## 12. 证据索引

### 12.1 元数据

- `reference/current-schema/stronghold-schema-4708.log`
  - `SceneGameData.stageSeed`
  - `AutoChessDataCenter.stageRandomSeed`
  - `BattleController.randomSeed`
  - `AutoChessLevelEnemyManager`
  - `RandomEnemyGenerater`
  - `AutoChessRandomWrapper`
  - `AutoChessBattleRoundEnemyInfo`
  - `SceneRoundEnemyData.RoundEnemy`

### 12.2 静态方法/反汇编

- `.build/analysis-2.0.7-methods.tsv`
- `.build/shop-analysis-2026-11-22/seed-disasm.txt`
- `.build/shop-analysis-2026-11-22/random-enemy-disasm.txt`
- `.build/shop-analysis-2026-11-22/metadata-types.txt`
- `.build/current-client-static.txt`

主要入口：

```text
BattleController.ResetSeed
BattleController.ResetImpRandom
AutoChessGameManager.DoLoadGameInternal
RandomEnemyGenerater._InitData
RandomEnemyGenerater._GenerateSpecialEnemyTypes
RandomEnemyGenerater._GenerateRandomEnemyData
RandomEnemyGenerater._GenerateRoundEnemyData
AutoChessLevelEnemyManager.Init
AutoChessLevelEnemyManager.GetRoundEnemies
AutoChessLevelEnemyManager.UpdateRoundEnemies
AutoChessLevelEnemyManager._GenerateAllBattleLevelIds
AutoChessLevelEnemyManager.GenerateSelfBattleEnemyData
AutoChessBattleSceneRoundEnemyData.Fill
AutoChessBattleUIEnemyViewModel._BuildRoundEnemyKeys
```

### 12.3 结论等级

- `stageSeed` 由服务器全状态同步或本地模拟构造：**已确认**
- `stageRandomSeed + currentRound` 构造回合主随机流：**当前客户端静态调用证据**
- 3 个可识别 seed 字段：**字段证据**
- 每局实际 RNG 实例数：**不固定**
- 未来所有回合敌人种类/数量可从 seed 精确计算：**未证明，条件严格**
- 完整商店、玩家操作和战斗结果可由 seed 决定：**否**

## 13. 待验证事项

以下需要当前版本实机或更完整配置证据：

1. 联机全状态同步中 `stageSeed` 的实际下发频率和生命周期。
2. `roundEnemies` 在联机包中是否始终是服务器最终结果，还是客户端可本地补算。
3. 每个回合 `AutoChessRandomWrapper` 的创建时机与存活周期。
4. `randomTrivial` 和 PRD 是否在每回合都重新初始化。
5. 当前赛季普通/精英/特殊敌人的实际权重和候选表。
6. `ShouldActionUpToServer` 分支在实战中的命中条件。
7. `m_roundEnemyData` 是否会在开局时预生成所有回合，还是按回合增量生成。
8. `roundEnemies.count` 与实际生成数量的差异规则。
9. 本地训练模式和联机模式是否使用完全相同的怪物生成路径。
10. 其他独立 `AutoChessRandomWrapper` 的实际数量和 seed 来源。

## 14. 插件内对局信息与商店记牌器（工作区开发功能）

当前 1.2.0 源码在主界面显示只读取得的 `stageRandomSeed` 和隐藏 Boss 名称。插件用 `hiddenBossId` 查 `AutoChessDataCenter.gameData.bossInfoDict`，再按 `enemyId` 或 `handbookEnemyId` 读取敌人图鉴名称。逐轮出怪预览 UI 和相关缓存采集已移除。

商店记牌器独立采样本机 `AutoChessDataCenter.shopDataModel.chessGoods`。可选的 `AutoChessBattlePreparationStatus.Read` Hook 和 AllStateSync 内嵌 `preparationStatus` 快照另外提供本机可读的货架、获得和持有状态。

- 协议 `chessIdentifier` 由当前活动 `charChessDataDict` 映射棋子。干员名称经 `CharacterUtil.GetCharName(String)` 读取，盟约名称取活动盟约配置中的 `name`。显示名称缺失时标为未读取，ID 用于内部统计。
- 货架频率按玩家槽、轮次、商品槽位累计。有 `refreshSeq` 时用它区分货架。同一序号内同一槽位的同一干员只记一次，新出现的槽位或干员可以补计。缺可用序号时按相邻货架槽位变化计数并标记不完整，结果相同的刷新可能漏记。一个干员占多个槽位时分别计次，并计入可识别的各基础盟约。前后均冻结且留在同一玩家同一槽位的商品，在跨刷新或跨相邻回合保留时只计首次。跨回合判断使用上一回合最后观察到的货架，可匹配本回合开局分批读到的冻结槽位。
- `roundAnalytics.chessGained` 的增加量与 `charChess` 持有量变化用于计获得，二者取最大值避免重复相加。首次观察的持有量仅作为基线；获得与同一 `refreshSeq` 的货架槽位消失相匹配时才标记商店来源。
- 持有数量下降以“推定卖出”显示，因为观察快照不能证明减少一定是出售；不匹配货架的获得不伪造来源。数量按实际干员实例，不把高星干员折算成三份基础干员。
- 全体视图按已采集货架的合计次数排序，单玩家视图按对应 P1–P4 槽位的货架出现次数显示和排序。队友频率在本机可读其准备状态时纳入。刷新序号缺失时使用货架差异回退；序号跳跃、降级回退、队列溢出和必需字段缺失会把记牌器标为不完整。计数只保存在插件内存，不跨局恢复、不写报表。

截至 2026-11-23，公开 Latest 为 v1.0.1，不包含种子/Boss 主界面字段或商店记牌器。模拟测试只验证模型/UI 接线；当前客户端 Hook 实际稳定性、队友数据覆盖、刷新快照频率和归属推定准确度仍待实机验证。
