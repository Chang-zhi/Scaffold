# Scaffold

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE.md)

[中文 README](README.zh-CN.md)

An extension DLL for *Command & Conquer: Yuri's Revenge*, developed as a derivative of Phobos for mission and map authors. It can be freely used to create mission packs and mods.

**Scaffold does not depend on Ares or Phobos at runtime and can run independently.** Using it together with them is recommended.

> **About this project:** This is an **unofficial** extension developed and maintained by Chang_zhi. It is not affiliated with the [official Phobos](https://github.com/Phobos-developers/Phobos) project. Its versions and features are not synchronized with the official project. Please report issues to the author rather than to the official project team.

> **Former name:** This project was previously named **PhobosExt_Changzhi**. It has been renamed to **Scaffold** to distinguish it from official Phobos and avoid confusion.

## Documentation

| Document | Contents |
|---|---|
| [Documentation](assets/说明文档.html) | New game mechanics, trigger behaviors/events, script actions, key commands, compatibility, and known issues |
| [Changelog](assets/更新日志.txt) | Changes in each version |

## Usage

The game must be **YR 1.001**. See the **[Documentation](assets/说明文档.html)** for installation instructions, configuration, and a complete description of all features.

## Building

Visual Studio with the **v143** toolset (VS 2022 or later) is required.

```bat
scripts\build_release.bat
```

The build output is placed at `Release\Scaffold.dll`.

## Feedback

If you encounter a bug or compatibility issue, please report it through one of the following channels:

- Bilibili direct message: <https://space.bilibili.com/423792550>
- Email: 3071564490@qq.com

## Acknowledgements

- [Ares](https://github.com/Ares-Developers/Ares) project team
- [Phobos](https://github.com/Phobos-developers/Phobos) project team
- [YRpp](https://github.com/Phobos-developers/YRpp) project team
- 韩大妈 [Bilibili profile](https://space.bilibili.com/2229647)
- 九千天华 [Bilibili profile](https://space.bilibili.com/362533219)
- 偏微whyffu [Bilibili profile](https://space.bilibili.com/41073096)
- 妖妖酱 [GitHub](https://github.com/yaoyaojiang)

Listed in no particular order.

## License

The code in this project is licensed under the same [GPL-3.0](LICENSE.md) license as Phobos.

*Command & Conquer: Red Alert 2 — Yuri's Revenge* and its related assets are property of Electronic Arts.
