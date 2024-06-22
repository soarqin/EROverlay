# ELDEN RING Overlay Hook

## 更新记录
* 2024-06-22: v1.1.0
  + 添加对带有41个新boss的DLC“Erdtree之影”的Elden Ring v1.12的支持(这是所有我所知的boss，可能遗漏其他可选boss)
  + 对原版boss数据进行了一些修复
* 2024-01-13: v1.0.1
  + 在 `EROverlay.ini` 中添加 `boss.panel_pos` 以调整boss面板的位置
  + 修复boss数据中的一些bug
* 2023-08-27: v1.0.0
  + 第一个发布版本

## 用法
* 按自己需求修改 `EROverlay.ini`
* 将 `EROverlay.dll` 注入艾尔登法环游戏，你可以：
  + 使用Mod加载器(你可以选择[EldenModLoader](https://www.nexusmods.com/eldenring/mods/117) 或 [ModEngine2](https://github.com/soulsmods/ModEngine2))
  + 运行Mod附带的 `injector.exe` 注入
* 等待数秒等Mod加载完成后，可以按 `=` 切换迷你/完全模式，按 `-` 卸载Mod

## [代码许可证](https://github.com/soarqin/EROverlay/blob/master/LICENSE)

## 鸣谢
* 修改自 [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
* [minhook](https://github.com/TsudaKageyu/minhook)
* [imgui](https://github.com/ocornut/imgui)
* [stb](https://github.com/nothings/stb)
* [JSON for Modern C++]( https://github.com/nlohmann/json)
* [inih](https://github.com/benhoyt/inih)
