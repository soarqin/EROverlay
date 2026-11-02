# 旧 Boss 207 项到当前静态候选记录的逐项审计

固定 C++ 提交：`afab7e14db2b62ae0bede980d5d0c96634321350`。本产物复用生产 Param 资源计划、事件发现和 `encounter_groups`；另直接遍历显式归档目录形成完整资源上集，不依赖临时库存或冻结读取器。

| 核对项 | 结果 |
|---|---:|
| 旧记录 | 207 |
| 当前静态候选记录 | 211 |
| 生产计划脚本／全目录脚本 | 555 / 588 |
| 生产具名条指令／全目录具名条指令 | 347 / 349 |
| 旧项主旗标定位一对一 | 207 |
| 旧项具名成员完整 | 207 |
| 缺少原归档显示指令的旧项 | 0 |
| 多成员候选组 | 111 |
| 同旗标跨资源地图／跨 runtime variant | 9 / 0 |

「静态候选」不等于已确认存在的游戏战斗；本工具未读 MSB 或观察实时生命条。模板及实体不存在的附加候选需另外核对，不能把候选总数宣称为真实 Boss 总数。

旧 JSON 没有实体／事件身份。完成旗标仅用于定位候选组；各组完整成员和死亡事件证明见 `audit.json`，逐项映射见 `old-to-rust.csv`。名称差异只标记待核对，不作为合并或缺失判定。成员完整性比较 map、variant、entity、name ID、slot、原事件／指令／实例旗标、guards 和 related entities；两个输入都使用同一生产解析器，因此这是资源计划覆盖证明，不是独立解码器差分。

## 缺少直接结构连接的组

分量之间没有本工具列出的直接事件连接时需要人工核对，不自动认定为不同战斗。

| 当前组 | 完成旗标 | 名称 | 分量 | 旧项 |
|---|---:|---|---:|---|
| R020 | 13000800 | Beast Clergyman / Maliketh, the Black Blade | 2 | O140 |
| R023 | 14000800 | Rennala, Queen of the Full Moon | 2 | O060 |
| R027 | 16000800 | God-Devouring Serpent / Rykard, Lord of Blasphemy | 2 | O121 |
| R032 | 19000800 | Radagon of the Golden Order / Elden Beast | 2 | O165 |
| R034 | 20010800 | Promised Consort Radahn / Radahn, Consort of Miquella | 2 | O207 |
| R036 | 21010800 | Messmer the Impaler / Base Serpent Messmer | 2 | O187 |
| R082 | 31220800 | Godskin Apostle / Godskin Noble / Spiritcaller Snail | 3 | O131 |
| R153 | 1044360800 | Mad Pumpkin Head | 2 | O007 |
| R154 | 1044530800 | Deathbird | 2 | O110 |
| R155 | 1045390800 | Tibia Mariner | 2 | O015 |
| R156 | 1045520800 | Draconic Tree Sentinel | 2 | O109 |
| R184 | 2044450800 | Romina, Saint of the Bud | 2 | O189 |
| R185 | 2044470800 | Rugalea the Great Red Bear | 2 | O175 |
| R186 | 2045440800 | Ghostflame Dragon | 2 | O166 |
| R190 | 2046450800 | Red Bear | 2 | O179 |
| R191 | 2046460800 | Divine Beast Dancing Lion | 2 | O188 |
| R203 | 2050480800 | Scadutree Avatar | 3 | O204 |

## 全部多成员候选组

原始指令数包含重新显示、换名及 LOD 资源分支，不能作为物理角色数。名称字符串去重只影响显示，不参与成员身份或归并。

| 当前组 | 完成旗标 | 名称 | 指令数 | scoped entity 数 | name ID 数 |
|---|---:|---|---:|---:|---:|
| R004 | 11000800 | Morgott, the Omen King | 2 | 1 | 2 |
| R006 | 11050800 | Godfrey, First Elden Lord / Hoarah Loux, Warrior | 2 | 1 | 2 |
| R009 | 12010850 | Dragonkin Soldier | 2 | 1 | 1 |
| R010 | 12020800 | Valiant Gargoyle / Valiant Gargoyle (Twinblade) | 2 | 2 | 2 |
| R011 | 12020830 | Dragonkin Soldier | 2 | 1 | 1 |
| R013 | 12030390 | Crucible Knight Siluria | 2 | 1 | 1 |
| R014 | 12030800 | Fia's Champion / Sorcerer Rogier / Lionel the Lionhearted | 5 | 5 | 5 |
| R020 | 13000800 | Beast Clergyman / Maliketh, the Black Blade | 2 | 2 | 2 |
| R023 | 14000800 | Rennala, Queen of the Full Moon | 2 | 2 | 2 |
| R025 | 15000800 | Malenia, Blade of Miquella / Malenia, Goddess of Rot | 2 | 1 | 2 |
| R027 | 16000800 | God-Devouring Serpent / Rykard, Lord of Blasphemy | 2 | 2 | 2 |
| R029 | 16000860 | Abductor Virgin (Swinging Sickle) / Abductor Virgin (Wheel) | 2 | 2 | 2 |
| R032 | 19000800 | Radagon of the Golden Order / Elden Beast | 2 | 2 | 2 |
| R034 | 20010800 | Promised Consort Radahn / Radahn, Consort of Miquella | 2 | 2 | 2 |
| R036 | 21010800 | Messmer the Impaler / Base Serpent Messmer | 2 | 2 | 2 |
| R039 | 28000800 | Midra, Lord of Frenzied Flame | 2 | 1 | 1 |
| R051 | 30100800 | Crucible Knight Ordovis / Crucible Knight | 2 | 2 | 2 |
| R053 | 30120800 | Misbegotten Warrior / Perfumer Tricia | 2 | 2 | 2 |
| R055 | 30140800 | Erdtree Burial Watchdog (Sword) / Erdtree Burial Watchdog (Scepter) | 2 | 2 | 2 |
| R062 | 31000800 | Patches | 2 | 1 | 1 |
| R063 | 31000850 | Patches | 2 | 1 | 1 |
| R069 | 31060800 | Crystalian (Staff) / Crystalian (Spear) | 2 | 2 | 2 |
| R070 | 31070800 | Kindred of Rot | 2 | 2 | 2 |
| R072 | 31100800 | Beastman of Farum Azula (Cleaver) / Beastman of Farum Azula (Throwing Knife) | 2 | 2 | 2 |
| R073 | 31110800 | Putrid Crystalian (Spear) / Putrid Crystalian (Ringblade) / Putrid Crystalian (Staff) | 3 | 3 | 3 |
| R075 | 31150800 | Demi-Human Chief | 4 | 2 | 2 |
| R077 | 31180800 | Miranda the Blighted Bloom / Omenkiller | 2 | 2 | 2 |
| R080 | 31200800 | Cleanrot Knight (Spear) / Cleanrot Knight (Sickle) | 2 | 2 | 2 |
| R082 | 31220800 | Godskin Apostle / Godskin Noble / Spiritcaller Snail | 4 | 3 | 3 |
| R087 | 32050800 | Crystalian (Ringblade) / Crystalian (Spear) | 2 | 2 | 2 |
| R093 | 34140850 | Fell Twin | 2 | 2 | 2 |
| R105 | 1033430800 | Erdtree Avatar | 2 | 1 | 1 |
| R107 | 1034420800 | Glintstone Dragon Adula | 2 | 1 | 1 |
| R108 | 1034450800 | Glintstone Dragon Smarag | 2 | 1 | 1 |
| R110 | 1034500800 | Glintstone Dragon Adula | 2 | 1 | 1 |
| R111 | 1035420800 | Omenkiller | 2 | 1 | 1 |
| R113 | 1035530800 | Magma Wyrm | 2 | 1 | 1 |
| R114 | 1036450800 | Death Rite Bird | 2 | 1 | 1 |
| R115 | 1036480800 | Night's Cavalry | 2 | 1 | 1 |
| R117 | 1036540800 | Full-Grown Fallingstar Beast | 2 | 1 | 1 |
| R118 | 1037420800 | Deathbird | 2 | 1 | 1 |
| R119 | 1037460800 | Bell Bearing Hunter | 2 | 1 | 1 |
| R120 | 1037510800 | Ancient Dragon Lansseax | 2 | 1 | 1 |
| R121 | 1037530800 | Demi-Human Queen Maggie | 2 | 1 | 1 |
| R122 | 1037540810 | Ulcerated Tree Spirit | 2 | 1 | 1 |
| R124 | 1038480800 | Erdtree Avatar | 2 | 1 | 1 |
| R126 | 1038520800 | Tibia Mariner | 2 | 1 | 1 |
| R127 | 1038540800 | Fallingstar Beast | 2 | 1 | 1 |
| R128 | 1039430800 | Night's Cavalry | 2 | 1 | 1 |
| R129 | 1039440800 | Tibia Mariner | 2 | 1 | 1 |
| R131 | 1039510800 | Night's Cavalry | 2 | 1 | 1 |
| R133 | 1040520800 | Black Knife Assassin | 2 | 1 | 1 |
| R135 | 1041500800 | Fallingstar Beast | 2 | 1 | 1 |
| R136 | 1041510800 | Tree Sentinel | 4 | 2 | 1 |
| R137 | 1041520800 | Ancient Dragon Lansseax | 2 | 1 | 1 |
| R138 | 1041530800 | Wormface | 2 | 1 | 1 |
| R140 | 1042360800 | Tree Sentinel | 2 | 1 | 1 |
| R142 | 1042380800 | Deathbird | 2 | 1 | 1 |
| R143 | 1042380850 | Bell Bearing Hunter | 2 | 1 | 1 |
| R144 | 1042550800 | Godskin Apostle | 2 | 1 | 1 |
| R146 | 1043330800 | Erdtree Avatar | 2 | 1 | 1 |
| R147 | 1043360800 | Flying Dragon Agheel | 2 | 1 | 1 |
| R148 | 1043370800 | Night's Cavalry | 2 | 1 | 1 |
| R149 | 1043530800 | Bell Bearing Hunter | 2 | 1 | 1 |
| R150 | 1044320800 | Deathbird | 2 | 1 | 1 |
| R151 | 1044320850 | Night's Cavalry | 2 | 1 | 1 |
| R153 | 1044360800 | Mad Pumpkin Head | 2 | 2 | 1 |
| R154 | 1044530800 | Deathbird | 4 | 2 | 1 |
| R155 | 1045390800 | Tibia Mariner | 4 | 2 | 1 |
| R156 | 1045520800 | Draconic Tree Sentinel | 4 | 2 | 1 |
| R157 | 1047400800 | Putrid Avatar | 2 | 1 | 1 |
| R158 | 1048370800 | Decaying Ekzykes | 2 | 1 | 1 |
| R159 | 1048400800 | Mad Pumpkin Head (Flail) / Mad Pumpkin Head (Hammer) | 2 | 2 | 2 |
| R160 | 1048410800 | Bell Bearing Hunter | 2 | 1 | 1 |
| R161 | 1048510800 | Night's Cavalry | 2 | 1 | 1 |
| R162 | 1048570800 | Death Rite Bird | 2 | 1 | 1 |
| R163 | 1049370800 | Night's Cavalry | 2 | 1 | 1 |
| R164 | 1049370850 | Death Rite Bird | 2 | 1 | 1 |
| R165 | 1049380800 | Commander O'Neil | 2 | 1 | 1 |
| R166 | 1049390800 | Nox Monk / Nox Swordstress | 2 | 2 | 2 |
| R168 | 1049520800 | Black Blade Kindred | 2 | 1 | 1 |
| R169 | 1050560800 | Great Wyrm Theodorix | 2 | 1 | 1 |
| R170 | 1050570800 | Death Rite Bird | 2 | 1 | 1 |
| R171 | 1050570850 | Putrid Avatar | 2 | 1 | 1 |
| R172 | 1051360800 | Misbegotten Warrior / Crucible Knight | 2 | 2 | 2 |
| R173 | 1051400800 | Putrid Avatar | 2 | 1 | 1 |
| R174 | 1051430800 | Black Blade Kindred | 2 | 1 | 1 |
| R176 | 1052410800 | Flying Dragon Greyll | 2 | 1 | 1 |
| R177 | 1052410850 | Night's Cavalry | 2 | 1 | 1 |
| R178 | 1052560800 | Erdtree Avatar | 2 | 1 | 1 |
| R180 | 1248550800 | Night's Cavalry (Glaive) / Night's Cavalry (Flail) | 4 | 2 | 2 |
| R183 | 1254560800 | Borealis the Freezing Fog | 2 | 1 | 1 |
| R184 | 2044450800 | Romina, Saint of the Bud | 2 | 2 | 1 |
| R185 | 2044470800 | Rugalea the Great Red Bear | 4 | 2 | 1 |
| R186 | 2045440800 | Ghostflame Dragon | 4 | 2 | 1 |
| R188 | 2046400800 | Demi-Human Queen Marigga | 2 | 1 | 1 |
| R190 | 2046450800 | Red Bear | 2 | 2 | 1 |
| R191 | 2046460800 | Divine Beast Dancing Lion | 4 | 2 | 1 |
| R192 | 2047390800 | Death Rite Bird | 2 | 1 | 1 |
| R193 | 2047450800 | Black Knight Garrew | 2 | 1 | 1 |
| R194 | 2048380850 | Ghostflame Dragon | 2 | 1 | 1 |
| R196 | 2049410800 | Jagged Peak Drake | 2 | 1 | 1 |
| R197 | 2049430800 | Ghostflame Dragon | 2 | 1 | 1 |
| R198 | 2049430850 | Black Knight Edredd | 2 | 1 | 1 |
| R200 | 2049450800 | Ralva the Great Red Bear | 2 | 1 | 1 |
| R202 | 2050470800 | Tree Sentinel | 2 | 1 | 1 |
| R203 | 2050480800 | Scadutree Avatar | 3 | 3 | 3 |
| R204 | 2050480860 | Tree Sentinel | 2 | 1 | 1 |
| R206 | 2051450800 | Count Ymir, Mother of Fingers | 2 | 1 | 1 |
| R209 | 2052480800 | Fallingstar Beast | 2 | 1 | 1 |
| R211 | 2054390850 | Ancient Dragon Senessax | 2 | 1 | 1 |

Godskin Duo 使用一个公共具名条 `13000850`；两名实际角色并不各有独立具名条，因此不能用标题的 Duo 或当前名称字符串数判断漏成员。回归直接核对 chiefs 的两个实体、Crystalian 三个实体、Gargoyles 两个实体、Scadutree Avatar 三阶段实体，以及 Godskin 的单公共条。

## 同完成旗标跨资源地图／variant

| 当前组 | 完成旗标 | 名称 | 资源地图 | runtime variants |
|---|---:|---|---|---|
| R153 | 1044360800 | Mad Pumpkin Head | m60_44_36_00, m60_44_36_10 | [0] |
| R154 | 1044530800 | Deathbird | m60_44_53_00, m60_44_53_10 | [0] |
| R155 | 1045390800 | Tibia Mariner | m60_45_39_00, m60_45_39_10 | [0] |
| R156 | 1045520800 | Draconic Tree Sentinel | m60_45_52_00, m60_45_52_10 | [0] |
| R184 | 2044450800 | Romina, Saint of the Bud | m61_44_45_00, m61_44_45_10 | [0] |
| R185 | 2044470800 | Rugalea the Great Red Bear | m61_44_47_00, m61_44_47_10 | [0] |
| R186 | 2045440800 | Ghostflame Dragon | m61_45_44_00, m61_45_44_10 | [0] |
| R190 | 2046450800 | Red Bear | m61_46_45_00, m61_46_45_10 | [0] |
| R191 | 2046460800 | Divine Beast Dancing Lion | m61_46_46_00, m61_46_46_10 | [0] |

## 全目录上集与生产资源计划的 LOD 差异

只对大地图 60／61 的同 tile `_10`／`_20` 资源与 `_00` 做别名核对；runtime variant 不折叠。以下差异还要求展开后的 bar 操作数、事件／指令／实例旗标、guards 和 related entities 相同。

| 旧项 | 原归档地图 | 生产地图 | entity / name ID | 原指令 | 生产指令 |
|---|---|---|---|---|---|
| O178 | m61_47_45_10 | m61_47_45_00 | 2047450800 / 905840501 | 90005870#12 | 90005870#12 |
| O178 | m61_47_45_10 | m61_47_45_00 | 2047450800 / 905840501 | 90005870#37 | 90005870#37 |

## 不同完成旗标之间的相关关系

这些超边仅报告相关性，未参与生产归并。共同全局／任务 guard、同一个 AI／死亡对象，或共用收尾函数均不能独立证明是一场战斗。JSON 保留完整 guard 超边、scoped actor 角色及仅共享 guard 的候选对。

| 共享 guard | 是否当前候选主完成旗标 | 涉及候选数 |
|---|---|---:|
| 3683 | false | 2 |
| 3691 | false | 2 |
| 9000 | false | 76 |
| 9290 | false | 74 |
| 9291 | false | 74 |
| 2050470800 | true | 2 |
| 2050480860 | true | 2 |

仅共享 guard、没有本工具列出的 actor／显示事件 scope 关联的候选对为 2850 对。它们不会被合并。

| 地图／variant | 共享对象 | 各候选中的角色 |
|---|---:|---|
| m61_50_47_00 / 0 | 2050470800 | R202: HandleBossDefeat_target, defeat_event_dead_or_hp_operand, display_entity, display_event_ai_or_death_operand; R204: display_event_ai_or_death_operand |
| m61_50_47_00 / 0 | 2050480860 | R202: display_event_ai_or_death_operand; R204: HandleBossDefeat_target, defeat_event_dead_or_hp_operand, display_entity, display_event_ai_or_death_operand |

Hinterland 的 `2050470800` 与 `2050480860` 保持两条独立持久完成记录。回归同时核对两个 group 及各自显示实体；共享 guards／相关对象没有触发 union。更强的阶段、伤害桥和独立奖励语义需结合原指令核对，不能从这张相关图自动推断。

## 旧名单之外的组

| 当前组 | 完成旗标 | 名称 |
|---|---:|---|
| R063 | 31000850 | Patches |
| R110 | 1034500800 | Glintstone Dragon Adula |
| R120 | 1037510800 | Ancient Dragon Lansseax |
| R127 | 1038540800 | Fallingstar Beast |

## 旧标题与当前游戏名称不同的项

| 旧项 | 旧标题 | 当前组 | 当前游戏名称 |
|---|---|---|---|
| O003 | Demi-Human Chief(x2) | R075 | Demi-Human Chief |
| O024 | Miranda the Blighted Bloom | R065 | Miranda Blossom |
| O035 | Crystalian (Staff) & Crystalian (Spear) | R069 | Crystalian (Staff) / Crystalian (Spear) |
| O036 | Crystalian (Ringblade) | R085 | Crystalian |
| O062 | Erdtree Burial Watchdog (Sword) & Erdtree Burial Watchdog (Scepter) | R055 | Erdtree Burial Watchdog (Sword) / Erdtree Burial Watchdog (Scepter) |
| O066 | Putrid Crystalian (Ringblade) & Putrid Crystalian (Spear) & Putrid Crystalian (Staff) | R073 | Putrid Crystalian (Spear) / Putrid Crystalian (Ringblade) / Putrid Crystalian (Staff) |
| O067 | Mad Pumpkin Head (Hammer) & Mad Pumpkin Head (Flail) | R159 | Mad Pumpkin Head (Flail) / Mad Pumpkin Head (Hammer) |
| O069 | Nox Monk & Nox Swordstress | R166 | Nox Monk / Nox Swordstress |
| O074 | Crucible Knight & Misbegotten Warrior | R172 | Misbegotten Warrior / Crucible Knight |
| O076 | Cleanrot Knight (Spear) & Cleanrot Knight (Sickle) | R080 | Cleanrot Knight (Spear) / Cleanrot Knight (Sickle) |
| O077 | Beastman of Farum Azula (Cleaver) & Beastman of Farum Azula (Throwing Knife) | R072 | Beastman of Farum Azula (Cleaver) / Beastman of Farum Azula (Throwing Knife) |
| O089 | Crystalian (Spear) & Crystalian (Ringblade) | R087 | Crystalian (Ringblade) / Crystalian (Spear) |
| O092 | Omenkiller & Miranda the Blighted Bloom | R077 | Miranda the Blighted Bloom / Omenkiller |
| O093 | Perfumer Tricia & Misbegotten Warrior | R053 | Misbegotten Warrior / Perfumer Tricia |
| O100 | Tree Sentinel(x2) | R136 | Tree Sentinel |
| O108 | Crucible Knight & Crucible Knight Ordovis | R051 | Crucible Knight Ordovis / Crucible Knight |
| O113 | Kindred of Rot(x2) | R070 | Kindred of Rot |
| O119 | Abductor Virgin (Swinging Sickle) & Abductor Virgin (Wheel) | R029 | Abductor Virgin (Swinging Sickle) / Abductor Virgin (Wheel) |
| O121 | God-Devouring Serpent & Rykard, Lord of Blasphemy | R027 | God-Devouring Serpent / Rykard, Lord of Blasphemy |
| O123 | Fell Twin(x2) | R093 | Fell Twin |
| O131 | Godskin Apostle & Godskin Noble & Spiritcaller Snail | R082 | Godskin Apostle / Godskin Noble / Spiritcaller Snail |
| O140 | Maliketh, the Black Blade | R020 | Beast Clergyman / Maliketh, the Black Blade |
| O147 | Night's Cavalry (Glaive) & Night's Cavalry (Flail) | R180 | Night's Cavalry (Glaive) / Night's Cavalry (Flail) |
| O150 | Malenia, Blade of Miquella & Malenia, Goddess of Rot | R025 | Malenia, Blade of Miquella / Malenia, Goddess of Rot |
| O155 | Valiant Gargoyle & Valiant Gargoyle (Twinblade) | R010 | Valiant Gargoyle / Valiant Gargoyle (Twinblade) |
| O161 | Fia's Champion & Sorcerer Rogier & Lionel the Lionhearted | R014 | Fia's Champion / Sorcerer Rogier / Lionel the Lionhearted |
| O164 | Godfrey, First Elden Lord | R006 | Godfrey, First Elden Lord / Hoarah Loux, Warrior |
| O165 | Radagon of the Golden Order & Elden Beast | R032 | Radagon of the Golden Order / Elden Beast |
| O167 | Blackgaol Knight | R189 | Knight of the Solitary Gaol |
| O187 | Messmer the Impaler & Base Serpent Messmer | R036 | Messmer the Impaler / Base Serpent Messmer |
| O207 | Promised Consort Radahn & Radahn, Consort of Miquella | R034 | Promised Consort Radahn / Radahn, Consort of Miquella |
