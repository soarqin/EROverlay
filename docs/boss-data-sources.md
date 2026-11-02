# Boss 目录、Param 和游戏文本

本文记录 Boss 移植使用的数据来源、字段契约和离线验证范围。Boss 的判定条件是游戏在画面下方显示带名称的大生命条；击败旗标、血量、获得卢恩的方式和旧版名单都不能单独证明这一条件。事件生命条的识别和关联由 `crates/game/src/boss_events.rs` 负责，本文说明 Param 和文本如何提供补充数据。

## 旧字段与运行时来源

| 旧 Boss 数据字段 | 新来源 | 限制 |
|---|---|---|
| `boss` | EMEVD 生命条指令的名称 ID → 当前语言 `NpcName*.fmg` | 不能把 `GameAreaParam.foundBossTextId` 当作 NPC 名称；多条生命条和阶段需要保留事件关系 |
| `flag_id` | 生命条与胜利事件中的条件读取、置真写入及实体关系 | `GameAreaParam.defeatBossFlagId` 仅按精确值匹配以补充地图／位置；Param 中有无对应行不决定是否收录 |
| `place` | 优先使用与完成旗标精确关联的 `WorldMapPointParam` 地点文本，否则使用同地图近邻赐福的地点文本；只取 `textType=0` 的 `PlaceName` | 地图标记或赐福名称不等于旧版作者填写的方位描述；没有可用地点文本时显示地图标识 |
| `region_name` | 赐福子类别／标签的 `textId` → `GR_MenuText*.fmg` | 这是游戏传送菜单的分组，与旧版手工划分的地区不保证一致 |
| `regions` | Param 的地图、位置及赐福子类别关系 | 相同地图中的最近赐福属于位置归类，不能把结果描述成 Boss 自带的地区外键 |
| `dlc` | 实际加载的 Param 行、事件文件和 DLC FMG 成员 | 不再用每种语言各自复制的名单表示 DLC |
| `revive_flags` | 外部复活配置 | Param 没有表达完整的竞技场／阶段复位操作；不能由击败旗标推测额外写入 |
| 跨场计数关系 | `configs/boss-encounters.json` 的三组主场／成员关系 | 只表达用户批准的计数边界，不保存全名单、名称、地区或位置，不推测复活写入 |

自己的窗口、设置和错误文案与游戏 Boss／地区文本分开维护。发行包不携带旧版 14 种语言的 `bosses.json` 或游戏 FMG 文件。

## Param 字段契约

`crates/game/src/boss_params.rs` 在更新线程读取 Param。读取前检查表头中的实际 `ParamType` 和数据版本，再复用 `params::param_rows` 的有界整表快照。以下组号是 `SoloParamRepository` 的运行时组号，不是 PARAMDEF 文件的 `<Index>`；两者不同，例如 `GameAreaParam` 的编辑器索引为 197，运行时组号为 29。

| 表 | 运行时组号 | 数据版本 | 使用的字段及行内偏移 |
|---|---:|---:|---|
| `GAME_AREA_PARAM_ST` | 29 | 3 | `foundBossTextId` `0x38`；`defeatBossFlagId` `0x44`；`bossPosX/Y/Z` `0x48/0x4c/0x50`；`bossMapAreaNo/BlockNo/MapNo` `0x54/0x55/0x56` |
| `BONFIRE_WARP_PARAM_ST` | 43 | 6 | `eventflagId` `0x04`；`bonfireSubCategoryId` `0x14`；`clearedEventFlagId` `0x18`；地图 `0x20`；位置 `0x24`；文本槽 `0x30` 起 |
| `BONFIRE_WARP_TAB_PARAM_ST` | 44 | 2 | `textId` `0x04`；`sortId` `0x08` |
| `BONFIRE_WARP_SUB_CATEGORY_PARAM_ST` | 45 | 1 | `textId` `0x04`；`tabId` `0x08`；`sortId` `0x0a` |
| `WORLD_MAP_POINT_PARAM_ST` | 87 | 6 | `iconId` `0x0c`；`clearedEventFlagId` `0x14`；地图 `0x20`；位置 `0x24`；文本槽 `0x30` 起 |
| `MAP_DEFAULT_INFO_PARAM_ST` | 133 | 6 | 行 ID 按十进制编码 `AA*1000000 + BB*10000 + CC*100 + DD`，对应资源地图 `mAA_BB_CC_DD` |

两个地图表都保留 8 个文本槽。第 `i` 个槽的 `textId`、显示旗标、隐藏旗标分别位于 `0x30 + 12*i`、`0x34 + 12*i`、`0x38 + 12*i`，类型位于 `0x90 + i`。实际资产核对表明，类型 0 使用 `PlaceName`，类型 1 使用 `NpcName`；两种命名空间存在相同数字 ID，不能混查。生产目录的地点标签仅接受类型 0，不把 NPC 名称作为地点名称。

地点候选的文本和显示／隐藏条件缓存为独立的条件地名计划，实现位于 `data.rs` 的 `Labels`。目录准备不读存档旗标；菜单只选显示、隐藏旗标均为零的明确无条件文本，否则使用地图标识。进入有效存档后，再批读候选实际需要的去重条件旗标；显示条件要求已知为真，隐藏条件要求已知为假，未知值不能满足任一条件。值发生变化时只更新地点与搜索快照，不重新扫描 Param、事件或 FMG。本轮 207 行已选地点候选的条件旗标数和受影响标签数均为 0；该结果只覆盖这组归档输入，Mod 仍按条件计划处理。

字段布局与本机可用的 Elden Ring PARAMDEF XML、既有 Minimap 的相同前缀及实际 Param 行尺寸交叉核对。行不足、类型／数据版本不符、无效地图字节和非有限位置均返回失败，不猜测新的布局。新增字段或数据版本需要补充真实布局证据。

`NpcParam.nameId` 确实存在，`isSoulGetByBoss` 也确实存在，但后者描述卢恩结算，未表达「显示具名大生命条」。同一角色类型的普通敌人和 Boss 实例不能由这些字段独立区分。当前目录不把 `NpcParam` 当作 Boss 的判定入口。

## 真实 Param 核对

2026-11-02 使用本机归档 `D:/Games/fog/eldendata/Vanilla/regulation.bin`，仅将解密、解压后的选定 Param 导出到忽略目录 `target/research/`。原文件 SHA-256 为 `7b6d07c357b639c902d48403ffe3612db35e0cf8d6fcc82d3fb24ea6eb6cf30a`，内部 BND 版本字符串为 `11611000`。正常构建、测试和安装包不依赖这些文件，也不包含解密脚本或游戏数据。

生产读取器得到：

| 表 | 行数 | 核对结果 |
|---|---:|---|
| `GameAreaParam` | 216 | 212 行具有非零击败旗标，合并为 207 个唯一旗标；4 行无击败旗标 |
| `BonfireWarpParam` | 422 | 全部通过生产解析器 |
| `WorldMapPointParam` | 472 | 全部通过生产解析器 |
| `BonfireWarpSubCategoryParam` | 55 | 55 个标题 ID 均在 `GR_MenuText` 解析成功 |
| `BonfireWarpTabParam` | 19 | 19 个标题 ID 均在 `GR_MenuText` 解析成功 |
| `MapDefaultInfoParam` | 875 | 去掉 ID 为 0 的默认行后，提供 874 个资源地图 |

例如，`GameAreaParam` 行 `10000850` 给出完成旗标 `10000850`、地图 `[10,0,0]` 和局部位置约 `[-16.94,-2.75,-17.60]`。其 `foundBossTextId=1000101` 是另一套用途的文本 ID，不能替代生命条事件给出的 Margit 名称 ID。

这份原版数据中，207 个击败旗标与 `WorldMapPointParam.clearedEventFlagId` 的交集为 0，与全部文本隐藏旗标的交集仅为 1；与赐福的解锁、完成、文本显示／隐藏旗标交集均为 0。因此，不能依靠「击败旗标必然对应某个地图标记或赐福」重建目录。其他 Mod 中存在的对应关系不能推广到原版。

Param 与事件的完成旗标也不保证完全一致。例如这份归档的 Fire Giant 位置行写有 `1052520800`，实际胜利事件关联的完成旗标为 `1252520800`。生产目录保留事件得出的后者，不按数值相似度改写；没有精确 Param 旗标匹配时使用事件地图，地点／分组只作近邻参考。

早期组合回归使用这 6 张真实 Param、归档事件和当前语言 FMG，从 1,075 个候选事件路径读取到 555 份脚本，发现 347 条具名生命条显示指令，形成 211 条静态候选。211 是应用当前范围规则之前的候选数，包含三组跨场记录及一个引用缺失实体的残留模板，不能称为 211 场真实战斗。当前结果及范围选择见下文和 [207 项范围说明](boss-207-scope-2026-11-02.md)；运行时未内置 207 个 Param 旗标或 211 条候选名单。

`GameAreaParam` 的位置和地图可以缩小事件文件范围，并辅助归类。洞窟内部地图与入口赐福在大地图上的位置可能不同，不能直接比较两套局部坐标。没有经过转换验证的跨地图位置不用于推定归属；事件缺失或归类不确定必须保留其限制。

同地图最近赐福只提供近邻参考，也不是官方的 Boss 地区关系。有匹配的 GameArea 行时，使用其 Boss 位置选择赐福；没有匹配行时，使用地图局部原点选择，近邻可靠性更低。高台、地下空间及同一 tile 内的多套地理区域可能使近邻落在另一地区。例如 Forlorn Hound 的近邻标签可能为 `Bridge of Sacrifice`，Omenkiller 的近邻也可能落在 `Moonlight Altar` 所属分组。这类结果不能称为原版准确地点，界面将地点标为「参考地点」；没有可用近邻时显示地图编号。位置归类不影响生命条事件给出的 Boss 名称和完成条件。

资源地图不能只取 Boss 位置所在的 tile。例如完成旗标 `2048380850` 的战斗脚本位于 `m61_48_38_00`，但其 `GameAreaParam` 位置归属 `[61,48,39]`。`MapDefaultInfoParam` 明确列出行 `61483800`，因此可以通过游戏提供的资源目录补全，毋须为这个 Boss 写例外名单。874 个地图 ID 与本机 544 份已解码地图 EMEVD 对比，仅有 `m60_45_36_10`、`m60_45_52_10`、`m61_44_45_10`、`m61_54_39_00`、`m61_55_39_00` 不在这张表中；目录仍需与其他 Param 的地图及已验证的地图层级／仪式资源关系合并。`MapDefaultInfoParam` 表示资源地图，不能当作 Boss 名单，也不保证每一行都有 EMEVD 文件。

## 选定范围与场景实体证据

当前按用户批准的旧版 207 项范围计数，Hinterland 两场独立保留。Param 和事件不能自行决定跨场记录的产品计数边界，因此仅将下列三组关系保留在外部 `configs/boss-encounters.json`：

| 主旗标 `primary` | 额外成员 `members` | `progress` | 完成含义 |
|---:|---:|---|---|
| Adula 后场 `1034420800` | 早场 `1034500800` | `primary` | 只认后场完成，不单列早场退场或晚场补写早场 |
| Lansseax 后场 `1041520800` | 早场 `1037510800` | `any` | 后场或早场击杀任一为真；正常撤退 `1037510810` 不算击杀 |
| Patches 初战 `31000800` | 后战 `31000850` | `primary` | 初战含投降完成，后战不单列；后战旗标不参与初战复活前置条件 |

此配置仅含版本、主旗标、额外成员和完成规则，不包含名称、地区、坐标或全量 Boss 目录。重复、重叠、链式关系、循环、零旗标、自关联或未知字段使整份关系配置失效；插件记录错误并禁用关系，没有内置默认映射。配置在初始化时读取，「重新读取游戏数据」只刷新游戏资源。Mod 缺少主场的已证明完成事件时，关联场次保留原事件旗标，不凭配置生成不存在的主场完成条件。`any` 的三态规则是：任一为真则完成；全部为假则未完成；没有真值且存在未知值则保持未知。

对缺少精确 Param 关联的候选，生产管线按事件地图选择场景路径，再用有界 MSBE 实体索引补充负证据。归档 EXE 的 `0x140e07940–0x140e07c39` 调用 `0x140e09fa0`：全局 `CS.DLC.IsEnableDlcFileMount` 为假时使用 `mapstudio:/<map>.msb`；开关为真且地图最高区域字节不超过 61、命中位集 `0x20000f0012700000` 时，才使用 `mapstudio_dlc2:/<map>.msb`。位集对应区域为 20、21、22、25、28、40、41、42、43、61。以上为资源逻辑名，实际 raw request 同样由 Boss Host 追加 `.dcx` 并设置 `0x40`。

生产纯函数 `scene_mounts` 复用该区域规则。非候选区域只请求 base，因此本轮 `m31_00_00_00` 和 `m60_38_54_00` 只读 `mapstudio:/`。插件尚未读取全局开关，对候选区域保守请求两个别名，本轮为 `m61_49_44_00`；任一索引找到角色都保留候选，另一别名的未知结果不能用于排除。只有局部实体及其关联对象均不在受支持的场景角色／角色组中、待查路径没有未知结果、没有代理或跨地图歧义、事件实际调用可完整展开且对象操作已知时，才筛除残留模板。实体 `1038540800` 在归档 `m60_38_54_00` 场景中不存在，是该模板的引用，不能据名称称为第一场陨星兽。

本轮显式读取以下解压场景，共 4,530,744 B：

| 场景 | 字节数 |
|---|---:|
| `m31_00_00_00.msb` | 465,336 |
| `m60_38_54_00.msb` | 1,238,752 |
| `m61_49_44_00.msb` | 2,826,656 |

这组真实输入在外部关系与生产场景筛选后，将 347 条显示指令保留为 345 条，生成 207 项目录和 95 个分组；筛除的是残留模板的两条显示指令。旧 207 项全部一对一对应，具名成员完整、无缺少的归档显示指令，Hinterland 两项分别保留。最终审计产物位于 `target/boss-207-delivery-audit`，包含成员、原事件／指令、完成证明、关系配置和场景诊断。

场景请求、解析和累计字节数均有预算；所选路径存在未知失败或预算不足时保留候选，不把未知当作实体不存在。对可能使用 DLC2 的区域，此规则覆盖两个别名。离线审计每张显式场景只提供一次，不代表两个候选别名均已读取；原生选择规则有 EXE 证据，实际 VFS 调用、全局开关取值和 Mod 覆盖结果仍未在游戏内实测。没有场景证据时，当前外部关系仍保留 208 条候选，不强行筛到 207；不同 Mod 或资源版本也可能产生不同数量。场景实体存在只能证明成员关系，不能证明任务可达或实时生命条已经出现。

## 当前语言与 Mod 文本

`text::language_directory` 将 Steam 的语言名或已有区域代码转换为原生目录，例如 `schinese → zhoCN`、`english → engUS`。初始化先采用非空 `common.language`，否则查询已加载 Steam API。语言名尚不可用时保留等待状态，每 500 ms 检查一次，不先发布一套缺文本的目录；六张 Param 表同样可以在菜单期等待就绪。已取得但不受支持的语言名、缺少的命名空间或具体名称 ID 仍采用明确的资源不可用状态／数字标识，不静默加载一套离线英文名单。

归档 1.17.2 EXE 中的 UTF-16 字符串给出以下原生资源逻辑路径：

```text
msg:/%s/item_dlc02.msgbnd
msg:/%s/menu_dlc02.msgbnd
```

分别位于该归档 EXE 的文件偏移 `45999440` 和 `45999496`。这些是 VFS 挂载逻辑名，不是磁盘路径。Boss Host 在 raw request 边界为逻辑名（包括 base 回退）追加一次 `.dcx` 并设置 `0x40`；例如实际提交 `msg:/engUS/item_dlc02.msgbnd.dcx`。原生 raw file submit 不会自行追加后缀；文件服务按实际 VFS 路径读取 Mod 覆盖并执行 DCX 解压，Rust 解析解压后的 BND4。契约与验证见 [首次读取修复](boss-loading-fix-2026-11-02.md)。item 任务要求 `NpcName` 和 `PlaceName` 两个命名空间，menu 任务要求 `GR_MenuText`。DLC02 binder 不可用或缺少所需命名空间时，请求对应的 `item.msgbnd` 或 `menu.msgbnd`，只合并尚缺的命名空间，避免 base 回退覆盖已读取的同层 Mod 文本。

合法 BND4 不一定包含目标 FMG，其他命名空间的内容不能使任务成功。读取器根据成功解析的目标 FMG 成员报告命名空间是否存在，不按字符串数量判断。存在但没有条目的 FMG、全部为空指针的 FMG 和合法空字符串都不等于命名空间缺失；空字符串仍保留为空文本。base 回退后仍缺少所需命名空间时计入游戏资源不可用状态，对缺少的具体 ID 使用数字标识。

真实 `item_dlc02` binder 同时包含 `NpcName.fmg`、`NpcName_dlc01.fmg`、`NpcName_dlc02.fmg`，以及三层 `PlaceName`。`menu_dlc02` 同样包含三层 `GR_MenuText`。`TextCatalog` 按 FMG 成员层级覆盖，DLC02 高于 DLC01，高于 base；空指针条目保留较低层文本，空字符串仍是有效覆盖。合并不依赖 binder 的成员顺序，解析失败时不留下半套覆盖结果。

2026-11-02 另用本机 `D:/Games/EROrigin/mod_/msg/engus/{item_dlc02,menu_dlc02}.msgbnd.dcx` 验证生产 FMG/BND 读取器。这是包含 Mod 附加文本的资产，不将其数量称为原版文本总数。两个 binder 分别含 78、50 个成员，成功读取 3996、1515 次有效文本覆盖。例如：

| 命名空间与 ID | 实际文本 |
|---|---|
| `NpcName:904750000` | `Godrick the Grafted` |
| `NpcName:905210000` | `Divine Beast Dancing Lion` |
| `PlaceName:10000` | `Stormveil Castle` |
| 子类别 `6800 → GR_MenuText:241353` | `Gravesite Plain` |
| 标签 `6800 → GR_MenuText:241417` | `Gravesite Plain` |

这份 Mod 的 `PlaceName` 还含有冲突的重复 ID `9205486`。读取器将冲突 ID 标记为无明确文本，其他条目仍可用；不会任取一个冲突名称，也不会使整份 Boss 目录失效。FMG 的尺寸、组索引、偏移、UTF-16 和总文本预算均有界检查。

## 可选验证与证据边界

当前准备流程在可用 Param 和语言就绪后即可于菜单期执行，不依赖有效存档。Boss 最多保留 16 个活动文件 token，文件完成和地图展开共享每次更新约 2 ms 的软预算及 32 项批次上限。新发现的必需事件依赖优先尝试，原有必需重试、可选升格、10 秒单任务和 60 秒资源等待期限保持不变。没有减少合法地图或 Mod 资源范围。菜单存档状态不可用或 `valid=0` 时不批读进度旗标、不复活、不保存挑战结果。详细状态、失效策略、基准和局限见 [菜单预加载与层级修复](boss-menu-layer-fix-2026-11-02.md)。

普通进档、地图或 context 变化以及暂时零 Param 地址不使目录失效。完整非零表地址组真正变化时，取消旧 token 并重建目录及条件地名计划；同地址资源或文本变化通过「重新读取游戏数据」显式刷新。外部遭遇关系和复活配方仍只在插件初始化时读取。上一轮 4 token 和现场复测记录保留在 [首次读取修复](boss-loading-fix-2026-11-02.md)，属于当时实现的历史证据。

常规测试包含 Param 表型／版本／行边界检查、FMG 空条目和 UTF-16、损坏偏移拒绝、三层文本覆盖、失败合并回滚、按目标命名空间补缺、空 FMG／空字符串与缺失成员的区分，以及生产存档读取／批量旗标／复活前置条件测试。外部资产检查需显式提供自行导出的数据：

```powershell
$env:EROVERLAY_BOSS_PARAM_DIR='E:/path/to/exported-params'
cargo test -p eroverlay-game --locked archived_param_assets_use_production_readers -- --ignored --nocapture
$env:EROVERLAY_BOSS_TEXT_BINDERS='E:/path/item_dlc02.bnd;E:/path/menu_dlc02.bnd'
cargo test -p eroverlay-game --locked archived_text_binders_use_production_parser -- --ignored --nocapture
```

第一个目录应含上述 6 张同名 `.param` 文件。第二项接收已解压 BND，不会启动游戏或调用原生 VFS。这些检查证明给定归档的解析、ID 和字段契约，不证明游戏内文本加载时机、所有 Mod、游戏画面或完整帧性能已经通过验收。
