# DUI
仅支持Windows，体积小，无依赖，性能好，内存占用低的GUI开发框架

## 构建

用 Visual Studio 打开 `DUI.slnx`（x64），或命令行：

```cmd
msbuild DUI.slnx /p:Configuration=Release /p:Platform=x64
```

产物：`DUI.lib`（静态库）与 `Samples/` 下的示例可执行文件
（`HelloWorld` / `Layout` / `Style` / `Icon` / `Position` / `Canvas` / `Scroller` / `Dialog` 等），统一在 `Build\<工程>\<配置>\`。

## 文档

设计文档见 `Doc/Arch/`，从 [00-Overview.md](Doc/Arch/00-Overview.md) 进入；
[系统对话框设计](Doc/Arch/17-Dialogs.md) 与 [对话框使用说明](Doc/Dialogs.md)；
`Samples/` 下的示例同时充当"目标 API 的可执行规格"。
