# Boss 原生 VFS 契约审计

故障基线为 `ca8dd4ee24d80169084821dbf25a13ce77e16f56`。输入为磁盘上的 1.17.2 `eldenring.exe`，SHA-256 为 `f9118fea2cfd8fa3901e8060bfadb1552cb2471771ded8a5b662c4e298ffb328`。只读分析未启动或扫描游戏进程，也未修改产品源码。

原生证据确认了一处请求契约差异：高层资源适配函数 `0x14265ca30` 在 `0x14265cb1c` 显式追加 `.dcx`，随后在 `0x14265cb2a` 设置 `0x40`；raw Submit → RequestInit → PathStore 仅复制调用者文件名。故障基线的 Boss Host 对无后缀的 event/msg/MSB 名称直接传 `0x40`，跳过了文件名转换。当前修复在 Boss 请求边界只追加一次 `.dcx`，保留逻辑 EMEVD 名称用于解析和链接。

`0x40` 是 DCX 读取标志。header callback `0x1426f9fa0` 检查 `DCX\0`、`DCS\0` 和版本，未证明支持未压缩 loose 文件自动回退。修复继续使用原生 VFS，读取与原生游戏相同的压缩资源名称。

别名复核结果：事件消费者使用 `event:/`，DLC02 eventflagalloclist 也在该挂载下；当前 PE 未找到 `event_dlc2` 或 `event_dlc02` 替代别名。文本为 `msg:/%s/item_dlc02.msgbnd`、`msg:/%s/menu_dlc02.msgbnd`，15 项 native 语言目录与 Rust 映射一致。场景沿此前已审计的原生 config/area 条件选择 `mapstudio:/` 或 `mapstudio_dlc2:/`，随后追加 `.dcx`。

## 源码对照

C++ 行号直接以 `git show afab7e1:src/gamefiles.cpp` 核实，不依赖导出副本。

| 固定源码 | 行号 | 事实 |
| --- | --- | --- |
| `afab7e1:src/gamefiles.cpp` | 35–48 | 64 字节 ReadRequest，路径与 flags 分别传入 |
| 同上 | 110–145 | callback status=1 成功；allocator vtable[13] 释放；等待 callback 与 generation 退休 |
| 同上 | 183–233 | 队列 mutex 内收集工作，锁外提交、flush、cancel；原样传入存储路径，alignment=128 |
| 同上 | 268–297 | 只允许 `0x40`；规范化大小写和斜杠，不补后缀；保留请求最多 128 项 |
| `ca8dd4e:crates/minimap/src/host.rs` | 109–116 | 仅显式 `.dcx` 路径设置 `0x40` |
| `ca8dd4e:crates/minimap/src/resources.rs` | 18–24、501 | 压缩 metadata、atlas 和 tile 路径显式带 `.dcx`；GFX/BHD 不带 |
| `ca8dd4e:crates/boss/src/host.rs` | 67–75 | 故障基线直接传无后缀逻辑路径和 `0x40` |

这些结构、成功状态、释放方式、ready/cancel 和 generation 契约与 Rust core 一致。原生 flush 的静态指令只在 native lock 下拆队列并投递 worker；这次未确认死锁，也未证明任意 native 调用必定及时返回。

## 当前加载修复的上限与限制

- 每个 generation 最多规划 4096 个不同路径；Boss 最多持有 4 个活动 token。每次 update 分别最多提交 32 次、完成或过期处理 32 项，与 discovery 共用 2 ms 软预算。预算在工作单元之间检查，不能中断单次解析或原生调用。
- 每个 job 从首次 request 调用开始计 10 秒，token=0 也计时。传输失败的一次重试不会重置起点；可选路径后来被明确 link 升级为必需时，可获得一次新的必需任务，但仍受 generation 期限限制。成功取得的坏 bytes 不重复读取。
- 60 秒是 generation 的资源等待期限。到期后不发起新读取，也不继续文本 fallback；队列按有界批次清理。最坏仍可有 4096 项待清理，若每次吃满 32 项需要 128 次 update；软预算可能使次数更多。事件 discovery、场景过滤和目录生成也可能在 60 秒之后完成，不能承诺“60 秒内整个初始化必定结束”。
- 超时 release 仅归还 token 并请求取消。原生 callback 存储继续保留，直到 callback 完成且 generation 退休，所以 4 不等于所有原生在途请求上下文的总数。core 共享上限仍为 128 项；容量满返回 token=0，进入同样的期限路径。
- Boss 单文件允许复制最多 64 MiB，core 所有插件共享的已复制文件预算为 256 MiB；成功接受的 event 和 scene 源数据分别累计最多 32 MiB（读取后拒绝的 bytes 不计入这两项）。这不等于总内存上限，未包含原生解压前的分配、解析对象和容器开销。
- 在 update 持续运行且 Host/native 调用返回的条件下，token=0、busy 和 pending 都不会无限等候；过期清理后会结束该 generation。零条且失败显示数据不可用，成功的空目录有独立状态，均不会显示 Ready。仅显式 reload 或实际非零 Param 指针替换开启新 generation，临时 context/Param=0 不重启。

## 可签入材料与证据边界

仅 `evidence.json` 与本报告作为发布证据。JSON 已去除本机绝对路径，不含用户或账户标识、凭据、带 token 的链接、可执行文件、归档资源、内存转储或 base64 数据；保留 SHA-256、原生地址、虚拟路径及少量文本指令。其余中间文件继续放在忽略目录中。

归档解析与模拟 native-contract fixture 不等于真实 VFS 完成。修复包仍需现场确认目录、文本和加载体验；本审计不能证明 `.dcx` 修复解释了本轮所有卡顿。
