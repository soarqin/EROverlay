# 旧 C++ Boss 地区与交互审计

审计日期：2026-11-03。C++ 基线固定为 `afab7e14db2b62ae0bede980d5d0c96634321350`；Rust 对照固定为 `f7210103f7002bf149f1d0c165ec0b126fa9ae04`。只读取源码和已有归档；未运行或注入游戏。C++ 所需文件由 `git show <commit>:<path>` 导出到本报告旁的 `cpp/`，未改运行时代码。

## 结论

- 旧数据为 32 个大区、207 项，基础游戏 23 区 165 项，DLC 9 区 42 项。地区标题和 Boss 归属按旧 JSON 原顺序读取，没有按赐福子类别拆分。
- 旧版在「当前大区变化」时只展开当前区，强制收起其余各区；同区期间保留手动展开状态。不是每帧强行保持当前区展开。
- 旧版没有搜索框、「只看未完成」或地区下拉。当前 Rust 的这些控件是新增交互；不能按旧版有这些功能来验收。
- C++ 的 `mapId` 实际读取 `FieldArea.current_play_region_id`。它不是 Rust `MapState.raw_map_id` 的 packed 地图 ID。旧版不读取玩家坐标，也不按最近赐福推算当前大区。
- 「207 项但显示资源警告」确有既有归档反例：警告由缺少 `event:/common_macro.emevd` 触发。该提示不能证明地区数据缺失；当前地区文本 fallback 也不会单独增加 `failures`。

## 旧大区完整清单

下列顺序、数量、`regions` 值来自固定 C++ 的 `src/boss/data/engus/bosses.json`，中文标题来自同版 `zhocn/bosses.json`；未按 Boss 名称推断地区。

| 序号 | 中文标题 | 英文标题 | 项数 | DLC | `current_play_region_id / 1000` 键 |
|---:|---|---|---:|---:|---|
| 1 | 宁姆格福 | Limgrave | 19 | 0 | 1800, 3002, 3004, 3011, 3100, 3103, 3115, 3117, 3201, 3410, 6100, 6101 |
| 2 | 啜泣半岛 | Weeping Peninsula | 10 | 0 | 3000, 3001, 3101, 3102, 3200, 6102 |
| 3 | 史东薇尔城 | Stormveil Castle | 2 | 0 | 1000 |
| 4 | 湖之利耶尼亚 | Liurnia of the Lakes | 25 | 0 | 3003, 3005, 3006, 3104, 3105, 3106, 3202, 3411, 3900, 3920, 6200, 6201 |
| 5 | 月光祭坛 | Moonlight Altar | 2 | 0 | 6202 |
| 6 | 魔法学院雷亚卢卡利亚 | Academy of Raya Lucaria | 2 | 0 | 1400 |
| 7 | 盖利德 | Caelid | 15 | 0 | 3014, 3015, 3111, 3121, 3207, 3208, 6400 |
| 8 | 桂奥尔龙墓 | Greyoll's Dragonbarrow | 10 | 0 | 3016, 3110, 3120, 3413, 6402 |
| 9 | 亚坛高原 | Altus Plateau | 20 | 0 | 3007, 3008, 3012, 3118, 3119, 3204, 3205, 6300 |
| 10 | 王城城墙外 | Capital Outskirts | 6 | 0 | 3010, 3013, 3412, 6301 |
| 11 | 格密尔火山 | Mt. Gelmir | 7 | 0 | 3009, 3107, 3109, 6302 |
| 12 | 火山官邸 | Volcano Manor | 3 | 0 | 1600 |
| 13 | 王城罗德尔 | Leyndell, Royal Capital | 5 | 0 | 1100, 3414, 3415, 3500 |
| 14 | 禁域 | Forbidden Lands | 2 | 0 | 6500 |
| 15 | 巨人山顶 | Mountaintops of the Giants | 9 | 0 | 3017, 3018, 3122, 6501, 6502 |
| 16 | 逐渐崩毁的法姆·亚兹拉 | Crumbling Farum Azula | 3 | 0 | 1300 |
| 17 | 化圣雪原 | Consecrated Snowfield | 8 | 0 | 3019, 3020, 3112, 3211, 6503 |
| 18 | 米凯拉的圣树 | Miquella's Haligtree | 2 | 0 | 1500 |
| 19 | 希芙拉河 | Siofra River | 5 | 0 | 1202, 1207 |
| 20 | 蒙格温王朝庙 | Mohgwyn Dynasty Mausoleum | 1 | 0 | 1205 |
| 21 | 安瑟尔河 | Ainsel River | 3 | 0 | 1201, 1204 |
| 22 | 深根底层 | Deeproot Depths | 3 | 0 | 1203 |
| 23 | 灰城罗德尔 | Leyndell, Ashen Capital | 3 | 0 | 1105, 1900 |
| 24 | 墓地平原 | Gravesite Plain | 7 | 1 | 4100, 4102, 4300, 6800, 6801, 6804, 6805, 6806, 6810, 6815, 6820 |
| 25 | 幽影亚坛 | Scadu Altus | 15 | 1 | 4000, 4001, 4101, 6900, 6901, 6902, 6903, 6904, 6905, 6906 |
| 26 | 劳弗古遗迹 | Ancient Ruins of Rauh | 2 | 1 | 6940 |
| 27 | 青蓝海岸 | Cerulean Coast | 3 | 1 | 2200, 6830, 6835 |
| 28 | 卡罗隐藏墓地 | Charo's Hidden Grave | 2 | 1 | 6840 |
| 29 | 尖刺山 | Jagged Peak | 5 | 1 | 4301, 6841, 6850 |
| 30 | 望影露台 | Scaduview | 5 | 1 | 6920, 6930, 6960 |
| 31 | 谷底森林 | Abyssal Woods | 2 | 1 | 2800, 4002, 6860 |
| 32 | 艾尼尔·伊利姆 | Enir-Ilim | 1 | 1 | 2001 |

总计 123 个地区键，去重后 123 个，无跨区重复键。`BossDataSet::load` 使用 `ordered_json` 顺序，按 JSON 大区顺序创建 `RegionData`，按该区 `bosses` 顺序创建 Boss；`regionMap_[key] = regionIndex`。不装 DLC 时跳过 `dlc=1` 的整个区，实际只创建 23 个基础游戏区。证据：`cpp/src/boss/data.cpp:140–200`、`cpp/src/boss/dllmain.cpp:11`。

## 当前地区与展开的准确状态机

1. `BossDataSet` 初始 `mapId_=0`、`regionIndex_=-1`；Renderer 初始 `lastRegionIndex_=-1`、`showFull_=false`。证据：`cpp/src/boss/data.hpp:119–122`、`cpp/src/boss/render.hpp:29–34`。
2. C++ DLL 每 32 个 update tick 调用一次更新。`screenState()!=0` 或 IGT 不为正时，不刷新 Boss／地区，旧地区保留。证据：`cpp/src/boss/dllmain.cpp:18–22`、`cpp/src/boss/data.cpp:212–232`。
3. 已有 FieldArea global 解引用后，按原始文件版本选择偏移：`version < 0x0002000200000000` 为 `0xE4`，否则 `0xE8`。读到非零且不同的完整 ID 时先更新 `mapId_`，再查 `regionMap_[mapId/1000]`。只在查到映射时赋值 `regionIndex_`；未知 ID、零 ID、暂未构造的 FieldArea 均不会主动清空原地区。证据：`cpp/src/boss/data.cpp:323–335`、`cpp/src/boss/data.hpp:17–20`。
4. 该字段的语义由本机 `E:/Projects/game-information-counter/vendor/fromsoftware-rs/crates/eldenring/src/cs/field_area.rs:10–23` 交叉核对：现代布局 `0xE0=map_place_name_id`、`0xE4=save_map_name_id`、`0xE8=current_play_region_id`。原 C++ 文件版本来自 `VS_FIXEDFILEINFO.dwFileVersionMS/LS`；不能误用面向用户的游戏版本字符串比较。已有 EXE：1.10.1 的文件版本为 2.0.1.0，1.12 为 2.2.0.0，1.17.1 为 2.7.1.0，均含 primary RTTI `.?AVFieldArea@CS@@`。这只是静态证据，不是实时字段读取验收。
5. `renderFull` 只有在 `state.regionIndex != lastRegionIndex_` 时设置 `autoExpandRegion`。只要该值非负，每个树节点均先调用 `SetNextItemOpen(i==autoExpandRegion)`，因此当前区展开，其他区全部收起。一个大区内的不同 play-region ID 切换不会重置手动折叠状态。证据：`cpp/src/boss/render.cpp:106–111`、`135–139`。
6. 首次完整列表若尚未知地区，`-1 == -1`，不会强制展开；使用新 ImGui context 时 TreeNode 默认关闭。若先在紧凑模式运行、已得到有效地区，再首次打开完整模式，则首次 `renderFull` 会只展开该区。紧凑模式不更新 `lastRegionIndex_`；紧凑期间跨多区，下一次打开完整模式只应用最新区。
7. 旧版未自动滚动至当前区，也不按当前位置筛掉其他区；只是重置节点打开状态。手动展开其他区可与当前区同时打开，直到下一次大区变化。

旧 C++ 不读取玩家 X/Y/Z；开放世界的 6100、6800 等键已经是游戏提供的 play-region 分类。packed `m60_xx_yy_zz` 或 `m61_xx_yy_zz` 仅提供地图块，不能直接取 `/1000` 或用最近 Boss 当成上述分类。当前 Rust `BossState.raw_map_id` 来自 menu view `+0x14`（`crates/game/src/map.rs:42–76`），而非 FieldArea。

## 地区标题的文本来源

`origin.txt` 的节标题原是英文；`replace.py:53–64` 用已导出的 PlaceName 英文文本反查，写成 `{Place<ID>}`，`generate.py:61–77` 再按对应语言替换。`regions` 键则原样转发，不由这些名称推导。因此旧地区标题使用 PlaceName，而非当前 Rust 使用的 GR_MenuText。

本次复用生产 `assets::parse_binder` 和 `text::parse_fmg`，读取已有 `target/research/item_dlc02.bnd`，32 个英文标题全部能在 PlaceName 中找到。逐项候选位于 `place-name-ids.tsv`，复现工具位于 `place_names.rs`。多个 ID 可有同文：例如 Limgrave 为 61000 和 8000000；Gravesite Plain 为 68000 和 680000。仅按字符串相等不能证明应选哪个 ID，也不能把标题与 play-region 键简单作十进制倍数运算。原 `replace.py` 对同文反向映射采用后读覆盖前读，模板本身未签入该基线；不能声称已恢复其唯一历史 ID。

## Rust 当前行为差异与可复现情况

| 项目 | 旧 C++ | Rust f721 当前实现 |
|---|---|---|
| 分组 | 静态 32 大区，Boss 明确归属 | 同精确地图最近赐福 → subcategory/tab；没有就按地图号分组；该归档为 95 组 |
| 标题 | PlaceName 离线翻译 | `GR_MenuText` 的 tab/subcategory 拼接；缺文本时地图标识 |
| 地区定位 | FieldArea play-region `/1000` | 首个目录 row 的 `map_id>>8 == raw_map_id>>8`，不读坐标，同块多组时取目录顺序第一组 |
| 首次展开 | 首次已知当前区时只展开该区 | `closed` 初始空，因此全部组展开 |
| 切区展开 | 新区展开，其余全部收起 | 只从 `closed` 移除新 current，不关闭其余组 |
| 未知地区 | 保留最后已知 regionIndex | 当前 map 没匹配 row 时 current=None |
| 搜索 | 无 | 全部小写 substring；匹配 name/place/group 的拼接串，未 trim |
| 筛选 | 无 | selected 地区与 Remaining only、搜索取交集；unknown 状态视作未完成，故 Remaining only 仍显示 unknown |
| 组计数 | 全区 killed/total | 仍为全组统计，不按搜索后匹配行数计数 |
| 地点显示 | Boss 悬停展示旧 place | 悬停展示「参考地点」；来源为精确关联点或同图近邻赐福，可能不是旧地点 |

Rust 证据：`crates/boss/src/ui.rs:69–93,150–202,326–341,394–410`；`crates/boss/src/data.rs:939–1005,1036–1054`。

具体缺陷：有效 save 状态先于目录到达时，`ui.rs:326–341` 仍记录当前 `self.map`，但 `catalog=None` 使 `current=None`；目录随后发布而 raw map 未变，不会重算 current。Reload／语言或目录重建也有同一情况。应把「目录首次出现／版本变化」纳入当前地区重算条件，不能仅等待 raw map 变化。新目录重排组索引时，`selected`、`closed` 的旧 usize 索引也需避免套到新组。

搜索当前还保留手动折叠集合；输入命中已折叠组时不会自动展开，用户只看见组标题。旧版无搜索，故这是新增功能的产品选择／行为，不是旧版契约。

## 207 项与资源警告的关系

既有受控真实 DLL 日志 `target/boss-menu-layer-final/boss-probe-final.log:10–11` 记录：

```text
boss-loading: Ready; some game resources are unavailable
boss-catalog: 207 encounters, 0 unknown, 555 scripts, 1 unavailable game resources, 519 absent optional resources, 9 unresolved event calls, 2 absent scene bars excluded, 0 request timeouts, first_failure=Some("event:/common_macro.emevd: resource unavailable or invalid")
```

同一日志 39–40、51–52 再次记录相同原因。该结果足以证明全数 207 与警告可以同时出现；不能据此归因为地区来源缺失。也不能把截图中的实际警告无条件等同为此原因，只能说已有明确可复现路径。

当前 `Data::update_at` 在有行时根据 `loading.failures != 0 || relation_error` 选择通用警告（`data.rs:747–750`）。可能来源包括语言不可识别、required 资源最终失败、读到无效格式、解析／展开失败、scene 不确定、deadline，或关系配置失败。optional 的普通缺文件只进入 `absent`，单独不触发警告；`missing_events` 只累计 `missing`，单独也不触发该状态。`common_macro` 被实际脚本的声明 link 排成 required，失败后可触发上述警告。

缺少某个地区 FMG ID 的现行分支只是退到 heading 或地图编号，不增加 `failures`（`data.rs:961–969`）；缺少 Boss 名称同样生成 `NpcName#ID`。反之，整份菜单 BND 缺失／无效会增加失败，并可使地区退回地图编号。故 207 总数既不能证明所有名称／地区完整，也不能证明地区源缺失；应按已有细分计数／first_failure 或独立文本覆盖事实区分。

本次没有现场截图对应进程日志，未把本机归档故障归因为用户的真实根因；没有要求用户重测。
