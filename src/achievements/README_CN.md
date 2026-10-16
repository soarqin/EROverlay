# ELDEN RING Overlay Hook

## 注意
* 当前核心使用 UI_SCENE 渲染。游戏 1.05–1.17.1 已通过静态地址检查；1.02–1.04.1 当前不绘制覆盖层。
* 你可以使用`EROverlay.dll`来加载多个覆盖层，我会保持向后兼容性，所以你只需要确保`覆盖层加载器`的版本满足特定覆盖层的最低要求即可。

## 用法

* 按需修改 `configs/achievements.ini`，使用 UTF-8 保存，修改后重启游戏。
* 中文注释模板在 `configs_CN/`，键名和默认值与英文版一致。先备份自定义文件，再将对应模板复制到 `configs/`，重新填写设置；加载器读取 `configs/`。
* 将 `EROverlay.dll` 注入艾尔登法环游戏，你可以：
  + 将 `EROverlay.dll` 改名为 `winhttp.dll` 放到游戏 `eldenring.exe` 所在目录 (同时把 `configs` 和 `data` 目录也放到游戏目录)
  + 使用Mod加载器(你可以选择[EldenModLoader](https://www.nexusmods.com/eldenring/mods/117) 或 [ModEngine2](https://github.com/soulsmods/ModEngine2) 或 [me3](https://github.com/garyttierney/me3))
  + 运行Mod附带的 `injector.exe` 注入 (不推荐，因为这种方法不太稳定且被一些安全软件阻止)

## [代码许可证](https://github.com/soarqin/EROverlay/blob/master/LICENSE)

## 鸣谢
* 修改自 [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
* [minhook](https://github.com/TsudaKageyu/minhook)
* [imgui](https://github.com/ocornut/imgui)
* [stb](https://github.com/nothings/stb)
* [JSON for Modern C++]( https://github.com/nlohmann/json)
* [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
