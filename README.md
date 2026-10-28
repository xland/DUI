# DUI
仅支持Windows，体积小，无依赖，性能好，内存占用低的GUI开发框架

## 构建

用 Visual Studio 打开 `DUI.slnx`（x64），或命令行：

```cmd
msbuild DUI.slnx /p:Configuration=Release /p:Platform=x64
```

产物：`DUI.lib`（静态库）与 `x64\Release\Samples.exe`、`ListSelect.exe` 两个示例。

## 文档

设计文档见 `Doc/Arch/`，从 [00-Overview.md](Doc/Arch/00-Overview.md) 进入；
`Samples/` 下的示例同时充当"目标 API 的可执行规格"。

