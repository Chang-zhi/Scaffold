# Scaffold

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE.md)

一个扩展《红色警戒2：尤里的复仇》游戏功能的 DLL，基于 Phobos 二次开发，面向任务/地图作者，可自由用于任务包与模组制作。

**运行时不依赖 Ares 或 Phobos，可独立使用**，推荐与它们一同使用。

> **关于本项目的性质**：由作者 Chang_zhi 个人开发维护的**非官方**扩展，与[官方 Phobos](https://github.com/Phobos-developers/Phobos) 项目组无关；版本与功能不与官方同步，问题请向作者反馈，勿报告给官方项目组。

> **曾用名**：本项目曾名 **PhobosExt_Changzhi**。为与官方 Phobos 区分、避免混淆，现更名为 **Scaffold**。

## 文档

| 文档 | 内容 |
|---|---|
| [说明文档](assets/说明文档.html) | 新增游戏机制、触发行为/事件、脚本动作、按键命令，以及兼容性与已知问题 |
| [更新日志](assets/更新日志.txt) | 各版本变更记录 |

## 使用

游戏版本需为 **YR 1.001**。安装、配置与全部功能说明见 **[说明文档](assets/说明文档.html)**。

## 构建

需要 Visual Studio（**v143** 工具集，即 VS 2022 及以上）。

```bat
scripts\build_release.bat
```

产物位于 `Release\Scaffold.dll`。

## 反馈

遇到 bug 或兼容性问题，欢迎通过以下方式反馈：

- B站私信：<https://space.bilibili.com/423792550>
- 邮箱：3071564490@qq.com

## 致谢

- [Ares](https://github.com/Ares-Developers/Ares) 项目组
- [Phobos](https://github.com/Phobos-developers/Phobos) 项目组
- [YRpp](https://github.com/Phobos-developers/YRpp) 项目组
- 韩大妈 [@B站主页](https://space.bilibili.com/2229647)
- 九千天华 [@B站主页](https://space.bilibili.com/362533219)
- 偏微whyffu [@B站主页](https://space.bilibili.com/41073096)
- 妖妖酱 [@GitHub](https://github.com/yaoyaojiang)

排名不分先后。

## 许可

本项目代码采用与 Phobos 相同的 [GPL-3.0](LICENSE.md) 许可证。

《红色警戒2：尤里的复仇》及其相关素材归 Electronic Arts 所有。
