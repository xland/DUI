# DUI
仅支持Windows，体积小，无依赖，性能好，内存占用低的GUI开发框架

## 构建

用 Visual Studio 打开 `DUI.slnx`（x64），或命令行：

```cmd
msbuild DUI.slnx /p:Configuration=Release /p:Platform=x64
```

产物：`DUI.lib`（静态库）与 `Samples/` 下的示例可执行文件
（`HelloWorld` / `Layout` / `Style` / `Icon` / `Position` / `Canvas` / `Scroller` / `Dialog` / `CircleWindow` / `Menu` 等），统一在 `Build\<工程>\<配置>\`。

## 文档

设计文档见 `Doc/Arch/`，从 [00-Overview.md](Doc/Arch/00-Overview.md) 进入；
[样式使用指南](Doc/Styles.md) 与 [布局使用指南](Doc/Layout.md) 与 [输入控件指南](Doc/Input.md) 与 [事件系统指南](Doc/Event.md)；
[系统对话框设计](Doc/Arch/17-Dialogs.md) 与 [对话框使用说明](Doc/Dialogs.md)；
`Samples/` 下的示例同时充当"目标 API 的可执行规格"。
