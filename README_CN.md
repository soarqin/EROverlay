# ELDEN RING Overlay Hook

## 更新记录
* 2024-??-??: v1.5.0
  + 重构：覆盖层功能被拆分为`overlays`文件夹中的独立dll模块
    + `Boss`现在是一个覆盖层模块
    + 新模块：`Achievements`用于追踪Steam成就进度
    + 新模块：`Minimap`用于显示游戏内小地图
  + 将`EROverlay.ini`拆分为`configs`文件夹中的多个独立`.ini`文件，以灵活支持覆盖层模块
  + 尝试修复启动崩溃，感谢[samjviana](https://github.com/samjviana)在[#8](https://github.com/soarqin/EROverlay/issues/8)中的建议
* 2024-10-15: v1.4.0
  + 尝试从系统字体链接中读取字体，如果当前语言的字符集不是拉丁或西里尔文且字体文件未设置
  + 现在可以手动设置显示文本格式，详细信息请查看`EROverlay.ini`
  + 添加了`挑战模式`，可以通过在`[boss]`中将`challenge_mode`设置为`true`来启用
* 2024-07-03: v1.3.0
  + 更改数据文件夹结构，以支持将来多个数据文件用于不同语言，并更改配置项如下：
    - `[boss]`中的`data`变为`data_file`并改为表示数据文件名
    - 在`[common]`中添加`language`
  - 在boss数据中删除`boss_display`和`place_display`，因为基本没用
  + 现在通过Flag ID解析boss数据的内存地址
  + 修复一些boss标志数据
  + 修复mod卸载后鼠标被锁定的问题
  + 修复玩家位置区域判断问题
* 2024-06-30: v1.2.0
  + 优化了DX12钩子代码以修复一些崩溃并支持全屏模式(仍然存在一些渲染错误，但至少现在可以使用alt+tab了)
  + 如果没有安装DLC则不会在列表中包含DLC boss
  + 如果在`[boss]`中将`data`设置为空，会根据游戏语言来加载数据，请注意手动设置`[common]`中的`font`和`charset`以显示非拉丁字符
  + `[common]`中的`charset`现在默认为空，这会使用与游戏语言相关的字符集
  + 在配置文件里添加了一个新的`[style]`来自定义面板的样式
  + 在配置文件里添加了一个新的`[input]`来自定义快捷键
  + 使用`Open Sans`作为默认字体，并将默认字体大小设置为20以提高可读性
  + 防止Mod被多次加载
* 2024-06-24: v1.1.3
  + 修复了由错误的内存释放引起的崩溃
  + 使用 [Pattern16](https://github.com/Dasaav-dsv/Pattern16) 以提高特征代码扫描速度
* 2024-06-23: v1.1.2
  + 添加了缺失的boss：`劳弗古遗迹`中的`神兽舞狮`
* 2024-06-23: v1.1.1
  + 更改了确认应用钩子补丁时机的代码，以避免崩溃风险
  + 现在在加载画面或返回主菜单时不再检查boss数据
  + 在README中添加了一个关于不要与某些覆盖层一起使用的说明
* 2024-06-22: v1.1.0
  + 添加对带有41个新boss的DLC“Erdtree之影”的Elden Ring v1.12的支持(这是所有我所知的boss，可能遗漏其他可选boss)
  + 对原版boss数据进行了一些修复
* 2024-01-13: v1.0.1
  + 在 `EROverlay.ini` 中添加 `boss.panel_pos` 以调整boss面板的位置
  + 修复boss数据中的一些bug
* 2023-08-27: v1.0.0
  + 第一个发布版本

## 注意
* 请不要与一些覆盖层(如`Nvidia GeForce Experience`中的`FPS Counter`、`MSI Afterburner`、`RivaTuner Statistics Server`等)一起使用此Mod，它们可能导致游戏崩溃。

## 用法
* 按自己需求修改 `configs` 目录内的 .ini 文件
* 将 `EROverlay.dll` 注入艾尔登法环游戏，你可以：
  + 使用Mod加载器(你可以选择[EldenModLoader](https://www.nexusmods.com/eldenring/mods/117) 或 [ModEngine2](https://github.com/soulsmods/ModEngine2))
  + 运行Mod附带的 `injector.exe` 注入 (不推荐，因为这种方法不太稳定且被一些安全软件阻止)
* 等待数秒等Mod加载完成后，可以按 `=` 切换迷你/完全模式

## 编写Overlay插件
* 请阅读 [src/api.h](src/api.h) 中的代码注释
* 请参考 [src/bosses/dllmain.cpp](src/bosses/dllmain.cpp) 中的代码作为示例

## [代码许可证](https://github.com/soarqin/EROverlay/blob/master/LICENSE)

## 鸣谢
* 修改自 [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
* [minhook](https://github.com/TsudaKageyu/minhook)
* [imgui](https://github.com/ocornut/imgui)
* [stb](https://github.com/nothings/stb)
* [JSON for Modern C++]( https://github.com/nlohmann/json)
* [inih](https://github.com/benhoyt/inih)
* [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
* [Open Sans](https://fonts.google.com/specimen/Open+Sans)
