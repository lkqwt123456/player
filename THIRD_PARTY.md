# 第三方组件

项目依赖 Qt 与 FFmpeg，它们保留各自的版权和许可证。本仓库的原创代码不改变第三方组件的授权条款。

- Qt：<https://www.qt.io/licensing/>。模块和构建方式可能影响适用许可。
- FFmpeg：<https://ffmpeg.org/legal.html>。构建时启用的组件决定实际许可；`ffmpeg -buildconf` / `ffmpeg -L` 可查看命令行构建信息，库的配置应检查对应 SDK。
- 本机使用的 FFmpeg SDK 名称包含 `gpl`，打包文件包含 GPL 构建的依赖。不要把这一产物作为闭源商业发行的许可依据。
- SRT：<https://github.com/Haivision/srt>。与 FFmpeg 一起构建时须保留相应第三方许可信息。

`scripts/package-windows.ps1` 用于本地运行包整理，不是许可合规自动化工具。向他人分发二进制前，应随包提供所使用 Qt、FFmpeg 及其传递依赖的完整版权/许可文本，并履行适用的源码提供等义务；商业闭源场景应选择合适的许可及依赖构建配置。
