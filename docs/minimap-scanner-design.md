# Minimap 通用扫描方案

2026-10-02。目标是让游戏的函数地址、菜单成员偏移和参数组数量变化由扫描自动处理，不再每次更新游戏都生成并发布逐 EXE 的 game profile。本文件说明扫描依据、启用条件、离线验证范围和运行时接入计划。

## 结论与交付状态

可以采用与原 GameDataMan 相同的特征扫描思路，并增加结构提取和调用关系校验。推荐以「共享指令特征定位 → 提取地址和成员偏移 → 校验接口约定 → 等待游戏对象就绪」生成一份运行时绑定信息。未知 SHA-256 可以进入相同扫描流程。

已有工作区中的哈希适配实现保持原样。本次交付是 Python idapro 离线原型与接入方案，尚未将通用扫描器编译进 DLL，也没有部署或提交。当前 DLL 仍拒绝未知哈希，不能把本报告解释为已发布自动兼容版本。

共享文件层代码在 1.02～1.17.1 之间保持相同的非重定位逻辑；菜单、地图 view 和赐福的历史差异可以从构造或创建指令读取。这为常规地址移动、参数组扩充和已识别的成员位移提供了跨版本依据。未来版本能自动启用的条件是扫描特征仍可识别，且使用的 ABI 和资源格式通过校验。「游戏架构没有大改」不能保证编译器一定保留相同特征。

交付文件：

- [离线扫描原型](../tools/verify_minimap_scanner.py)：通过 IDA 加载原 EXE，扫描时不选择逐版本 profile；解析结束后才可与历史证据对照。
- [共享特征规则](minimap-scanner-signatures.json)：函数的共享窗口、掩码与 ABI 指令约定，无逐 EXE 哈希、VA 或 RVA 表。局部字段模板及关系约束在原型中。
- [扫描证据](minimap-scanner.evidence.json)：逐样本定位结果、布局、两段命中位置和 ABI 校验结果。
- [历史基准](minimap-version-compatibility.evidence.json)：仅作为回归期望值，不能进入未知版本解析路径。

## 1. 扫描范围与候选确认

在核心 DLL 中增加独立的扫描组件，输入是当前进程主 EXE 的 PE 映像。读取可执行节、异常展开记录和 MSVC RTTI；使用现有 Pattern16 或等效的有界字节扫描，不引入运行期 IDA、Python 或通用反汇编器。

扫描器需要以下行为：

1. 按 PE 节属性限定函数扫描范围，并记录模块基址和映像边界。函数结果必须位于可执行节，全局槽必须位于可读的数据节；确认所有操作数和 RVA 都在边界内。
2. 为一个函数使用两个分散的指令片段，屏蔽 RIP displacement、rel32 和已确认可变化的操作数。两个片段必须属于同一个函数。
3. 利用异常目录的 RUNTIME_FUNCTION 确认函数入口，沿 UNW_FLAG_CHAININFO 合并同一函数的展开范围。不能把尾部片段的起点作为函数入口。
4. 叶函数没有异常展开记录时，使用入口片段、第二片段的函数内相对位置、完整指令约定及实际调用引用共同确认。原型的事件读取函数已校验入口、第二片段和完整指令；实际调用引用的运行时校验需在接入时补齐。
5. 扫描所有候选，用 RTTI、参数组号和调用关系消除歧义。校验结束后仍有多个候选时，返回具体的歧义状态，不取第一条结果。

现有 Signature::scan 返回第一次命中，并默认扫描整个 module，不足以承载上述校验。可以保留它给旧调用者使用，为新组件增加 scanAll 和节范围输入。

RTTI 只辅助识别类型，不独立决定函数角色：WorldMapViewModel 和 WorldMapWarpPinData 的 vtable 引用可以同时来自构造与析构；CSMenuManImp 也有多个引用。必须先用语义特征确认目标，再核对 RTTI 归属。

### 相对地址必须按有符号数计算

所有 RIP 和 E8/E9 位移使用 int32_t：

~~~cpp
target = instructionAddress + instructionLength + signedDisplacement;
~~~

现有 Hooking 和 params/pointers.cpp 把位移读为 uint32_t。新组件不能沿用此实现；接入时统一修正这些调用点。后向引用产生负位移，零扩展会得到错误地址。

## 2. 文件层扫描与调用约定

需要定位 submit、flush、enqueue、cancel，以及用于证明生命周期的 allocate、pending_insert、request_init、complete、cleanup 和 retire。辅助 textlist 函数仅用来定位同一 allocator 全局，不调用它。

| 结果 | 扫描依据 | 必须一致的关系 |
|---|---|---|
| 文件函数 | 两段共享 masked 指令窗口，异常展开入口 | submit→allocate；allocate→pending_insert；flush→enqueue；complete→cleanup；cleanup→retire |
| manager 全局 | cancel 中 RIP load 与请求索引/generation 检查 | 全局为可读槽，运行时指向文件 manager |
| allocator 全局 | 辅助函数的两次 RIP load | 两次解析为同一槽；虚表释放槽可执行 |
| retire owner 全局 | complete 的 generation 检查与 cleanup 末尾 RIP load | 两者指向同一槽，cleanup 调用已确认 retire |

只找到四个入口不足以启用游戏调用。需要核对以下接口约定：

- submit 使用的 ReadRequest 布局：path、allocator、alignment、callback、stage、context、flags、parameter、priority；Overlay 的请求结构大小为 64 字节。
- manager 的 ending、pending 列表和任务对象；当前共同偏移为 +0xE38、+0xD70、+0x20。
- complete 使用 RCX/RDX/R8/R9 传递 status、context、buffer、size；成功状态为 1，取消状态为 2。
- buffer 使用请求中的同一 allocator 释放，当前虚表槽为 +0x68。
- pool generation 仍是低 16 位索引和高 16 位 generation，数组在 pool+0x50；cleanup 在回调返回后才 retire 请求。

离线原型对这些文件层函数检查共享的完整 masked 指令体和局部分支，再核对关键跨函数关系。该检查有意保守：不认识的重新编译结果会拒绝通过。运行时接入可先沿用保守规则；后续若需容忍无关指令重排，须为上述语义分别实现局部模板和关联校验，不能仅缩短入口特征后跳过 ABI 检查。

## 3. 从指令提取布局

以下字段由扫描结果提取，不由 PE 版本或 EXE 哈希选择。

| 字段 | 提取方法 | 历史扫描结果 |
|---|---|---|
| menu→owner | 调用已定位 view create 的 caller 中读取成员；支持 disp8 与 disp32 | +0x78 / +0x80 |
| owner→view | 创建序列先比较成员，分配后调用 view ctor，最后写回同成员 | +0x248 / +0x250 |
| view 分配大小 | 同一创建序列的分配 size 立即数 | 0x3C8 / 0x450 |
| MenuInfo | CSMenuManImp 构造器中 LEA 的 displacement 与后续 ctor call | +0x708 / +0x718 / +0x720 |
| screenState | MenuInfo ctor 内 U16 store 的成员偏移，加上 MenuInfo 起点 | +0x718 / +0x728 / +0x730 |
| graceStride | view ctor 的 imul size 与复制循环 add；要求两者一致 | 0x2D0 / 0x350 |
| graceNormalOffset | grace ctor 的末尾 U8 初始化，检查 normal+8==stride | +0x2C8 / +0x348 |
| alternateIcons | grace ctor 是否写入正常与禁止两组 alternate Icon 成员；两组同时存在才启用 | 1.02 系列关闭，后续开启 |
| m61 能力 | view ctor 的区域转换器初始化参数；m60 必须存在 | m60 或 m60+m61 |
| repositoryGroups | 参数仓库 getter 的上限立即数 | 185 / 186 / 194 |

这是「按结构识别的规则集合」，不是把旧版 RVA 换成另一个版本表。多条指令模板用于识别已有的编译变体；所有变体都对未知 EXE 开放，并必须导出唯一且互相一致的结果。

### 共同字段也需要证明

玩家、死亡和编号字段目前仍使用共同偏移；扫描全局之后不能默认这些字段永远不变。

原型已检查玩家 raw/map/XY、角度和 underground U8 的写入，死亡 XY 转换、valid U8 与 map I32 写入，以及标记列表绑定、保存 slots/capacity/count、16 字节记录、map/icon 字节、slot+1 数字、十个保留槽和正常上限 5。还核对死亡使用同一坐标转换器，转换器调用已定位的 legacy lookup，数组/数量/步长及转换算法符合已确认格式。

文件层以完整指令约定保护；事件 type 1/type 2、地图每组 32 个碎片的算法、legacy tree 和坐标转换也有完整指令校验。尚未覆盖的运行时接入校验列在第 7 节，不能把「找到 28 个函数」等同于证明所有未来字段都兼容。

可以扩展 ERGameLayout 的 size-gated 尾部以传递新增偏移，也可以在核心读取后返回稳定的值快照。优先让核心持有文件和游戏 ABI，Minimap 只消费快照。保持已有 EROverlayAPI 布局，避免破坏旧插件。

## 4. 参数与资源继续按实际内容读取

common 和 piece 的 accessor 必须引用同一 repository 槽，并调用同一 getter。getter 的组数可以提取；stride 72、entry+0x88 与两次 cap/table+0x80 作为当前接口约定校验。组号不能随意猜测：接入时对 43/87/88/141 同时核对原生 accessor 或 PARAM 类型名，再读取对应数据。

PARAM 的目录格式、行边界与字段长度继续由现有 ParamRows 检查。目录有长度信息，不代表字段语义能随意移动；若未来在行中插入字段，需要字段访问证据或匹配的数据 schema 才能使用。

图集仍来自内部 TextureAtlas 和 GFX 引用，数量采用动态容器，兼容超过三个图集的 mod。扫描器不增加三张或四张图集的限制，也不复用游戏 GPU 贴图句柄。游戏文件层返回 CPU 数据，Overlay 创建自己的 DDS 纹理、SRV、队列与 fence。

地图类别同时受转换器能力与实际资源目录约束；旧版只初始化 m60 时使用 M00/M01。全开底图继续只在 Renderer 中覆盖显示掩码，不修改游戏碎片状态，也不增加新的游戏调用 ABI。

## 5. 启动、缓存与失败处理

~~~mermaid
flowchart LR
    A[当前 EXE 的 PE 映像] --> B[共享特征与候选关系]
    B --> C[地址和布局提取]
    C --> D[ABI 校验]
    D --> E[运行时绑定信息]
    E --> F[等待对象及资源就绪]
    F --> G[CPU 快照和自有纹理]
~~~

初始化期间一次扫描，在 Overlay 工作线程生成并发布不可变 ResolvedGameBindings。GameFiles、Hooking 和参数访问使用同一份结果。Present 线程不扫描 EXE、不查询配置，也不读取正在构建的结果。

代码定位成功与游戏就绪分别记录。manager、view 或 PARAM 尚未初始化时进入等待状态；保持已解析的地址，仅重试对象读取与资源请求。读档或返回标题时增加快照 generation，清除死亡和编号旧状态。不能因对象暂时为空而重新扫描整个 EXE。

推荐先只保留进程内缓存。若后续加入磁盘缓存，保存 RVA、布局、扫描器规则版本和校验信息；以 EXE 哈希区分缓存内容。命中缓存也重新检查入口、局部约定和目标边界，缓存失效则完整扫描。未知哈希必须能走完整扫描，不能退回白名单。

函数间隔、preferred ImageBase、PE FileVersion 和哈希均不能参与未知版本候选选择。实际调用地址为当前 module base 加已确认 RVA；离线首选基址与运行时 ASLR 不需要共享固定地址。

建议记录如下状态及原因：

| 状态 | 含义 | 后续动作 |
|---|---|---|
| Resolving | 正在定位代码及布局 | 完成后发布结果 |
| WaitingForGame | 扫描通过，对象或资源尚未就绪 | 重试对象读取和资源请求 |
| Ready | 所需能力已通过并就绪 | 正常读取与绘制 |
| SignatureMissing | 没有满足规则的候选 | 日志列出缺失角色与规则；需要新版本分析 |
| SignatureAmbiguous | 关系校验后仍有多个候选 | 列出候选 RVA，不调用未确认函数 |
| AbiMismatch | 入口已找到，但字段、调用约定或生命周期不匹配 | 仅关闭依赖该 ABI 的能力 |
| ResourceUnsupported | EXE 已通过，资源格式或必需图片不满足条件 | 报告具体路径、格式和图集项 |

文件调用 ABI 失败时关闭原生文件桥；死亡或编号字段失败时只关闭对应标记能力；参数读取失败不能发布「所有碎片未获得」的假快照。已有完整布局都通过时保持当前功能行为。离线原型为单次整体成功/失败，以上能力分组属于运行时接入工作。

## 6. 离线验证方法与边界

所有 EXE 分析使用 Python idapro 和 IDA 9.4 的加载字节与指令解码。没有设置断点、附加调试器、调用游戏函数或修改 EXE、存档。旧目录原数据库不改写；工作区 build/ida 保存分析缓存。

全目录验证先运行扫描器，再加载历史证据对照。29 个目录／28 个独立 EXE 全部通过，覆盖 1.02～1.17.1。其中 1.13 与 1.13.2 完全同文件，按一次独立扫描计数。每份结果包含 28 个语义函数、9 个全局、动态布局，以及文件/事件/转换等 15 个完整指令约定。四个 1.02 系列样本通过 24 组字段模板，其余通过 26 组，多出的两组是 alternate 图标字段。

独立解析模式在 Steam 最新 EXE 上通过；没有加载历史期望值，函数、全局、布局和 ABI 检查结果与批量模式完全一致。共享规则文件不含逐 EXE 哈希或地址表。

正常回归：

~~~powershell
python3 tools/verify_minimap_scanner.py
~~~

仅扫描单份 EXE，完全不加载历史期望值：

~~~powershell
python3 tools/verify_minimap_scanner.py --resolve-only --rules docs/minimap-scanner-signatures.json --exe 'D:/Steam/steamapps/common/ELDEN RING/Game/eldenring.exe' --database build/ida/minimap.i64 --output build/ida/versions/scanner/standalone.json
~~~

批量模式默认从已有 IDA 导出生成规则；独立模式只需共享规则和对应 EXE 的 IDA 数据库，数据库输入哈希必须对应 EXE。输入哈希核对证明分析对象正确，不决定是否启用。

原型依赖 IDA 来验证指令边界，处理没有异常记录的叶函数时也可能利用数据库的已有函数信息。运行时实现必须用 PE 元数据、入口约定和调用引用复现所需结果，不能假设玩家安装了 IDA。完整运行时启动耗时、ASLR 及实际内存映像状态仍需在接入后验证；原型总耗时包含数据库打开、哈希和 IDA 引用索引，不能作为 DLL 启动性能指标。

已有历史布局与真实/合成资源回归见[历史适配报告](minimap-version-compatibility.md)。本次扫描成功不替代对应旧游戏、旧 GFX/TPF/PARAM 和画面的逐版实测。

## 7. 运行时接入开发计划

| 阶段 | 将完成的工作与结果 | 验收条件 |
|---|---|---|
| S1：共享规则离线确认 | 使用同一规则解析全部 EXE，并保留独立解析入口 | 每份 EXE 的地址和提取布局与历史证据一致；解析不读取 profile |
| S2：核心扫描组件 | 用有界 PE/异常展开/RTTI 与现有 Pattern16 实现 ResolvedGameBindings | 从映像解析，与离线结果一致；零候选、多候选、越界及负 rel32 正确返回 |
| S3：补齐所有读取约定 | 逐项覆盖仍硬编码的列表、参数及 MenuInfo 字段，提取或明确校验 | 下述字段均有证据约束；不以对象当前数值合理作为唯一兼容证明 |
| S4：替换哈希启用路径 | GameFiles、Hooking、参数与 Minimap 读取同一绑定信息 | 未收录哈希可按共享规则启用；MenuInfo 不再按版本号分支；保留插件 size gate |
| S5：运行与回归 | 检查正常启动、读档、两个标记、死亡、碎片/全开、动态图集与卸载 | 旧基准无回退；就绪等待和能力失败不留下旧快照；回调退休与 GPU 回收通过 |

S1 的原型已完成；S2～S5 尚未执行。本次没有修改运行时代码。

S3 必须补齐以下约定，原型尚未完整提取或逐字段校验：

- MenuInfo 的 menuLoaded 浮点字段、Minimap 使用的菜单状态，以及核心既有菜单行为读取/写入字段。
- view 的赐福容器起点、begin/end、记录 ID 和全部被使用字段。原型已校验图标与 normal/stride，但不把这些局部结果当成整份 view 结构证明。
- PARAM 43/87 的实际组归属、基础地图与位置字段、alternate 条件字段；PARAM header 类型与 row span 只解决格式/边界，不能单独证明字段语义。
- 所有原型用于识别的 RIP、E8/E9 目标归属；临时分配、构造和退休的调用/虚表约定，以及当前内存映像与磁盘映像的一致处理。
- 每个局部字段模板从可靠指令边界开始，并验证寄存器与对象来源；不把任意字节子串解码成功视为充分证明。

运行时可以先采用已有共同字段的明确 ABI gate，遇到不匹配关闭该能力。需要支持成员位移时，在已识别的数据流内增加 capture，并通过 size-gated 布局或核心快照传递新偏移。不能把「尚未提取」的字段默认为兼容。

接入时，将现有 gameprofiles.inc 从长期运行期依赖移为开发回归 fixture；第一轮建议以扫描为唯一启用依据，避免维护第二条哈希快路径。若保留已核对地址缓存，必须执行相同 ABI 校验，也不能阻挡未知哈希进入扫描。

## 后续维护原则

当更新只移动函数或全局、扩充参数组，或产生已覆盖的构造布局时，通用扫描器直接生成绑定信息，无需增加 game profile。图集数量变化继续由资源解析处理。

当共享特征消失、编译器产生无法识别的寄存器/控制流、接口调用约定改变或资源格式变化时，仍需要分析并补充规则。维护对象是经过证据确认的「结构或接口规则」，而不是每个游戏版本的地址清单。这能降低日常更新成本，但不能承诺所有未知未来版本永久零维护。
