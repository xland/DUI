# 系统托盘图标指南（Tray 开发者文档）

系统托盘（任务栏通知区）图标用于"关掉窗口但应用继续跑"的场景：点关闭后窗口销毁、图标仍在托盘，左键点图标重新唤出窗口，右键点图标弹菜单（退出等）。

框架**不默认**创建任何托盘或后台窗口：只有业务创建 `dui::Tray` 并 `Show()` 时才有托盘图标。不用托盘时没有任何额外窗口 / 资源。完整示例见 `Samples/Tray`；架构与实现约束见 [12-Windowing.md](Arch/12-Windowing.md) §6.4。

---

## 1. 快速开始

### 1.1 三个前提（缺一不可）

托盘常驻是三件事叠加：

1. **关窗不退出**：`app.SetQuitOnLastWindowClosed(false)`。默认 `true` 时关掉最后一个窗口就退出，托盘图标也随之消失。
2. **创建托盘图标**：`dui::Tray` + `Show()`。
3. **回调里重建窗口**：关窗后 `WindowCount() == 0`，在 `OnClick` 里重新 `AddWindow()`。

### 1.2 完整代码

```cpp
#include "DUI/DUI.h"

namespace {
dui::App* g_app = nullptr;

// App 只存 Window*，不负责释放；窗口对象须活到 Run() 返回。
std::vector<std::unique_ptr<dui::Window>> g_windows;

class TrayWindow final : public dui::Window {
public:
    TrayWindow() : dui::Window(dui::WindowOptions{.title = L"托盘示例",
                                                   .width = 460.f, .height = 300.f}) {}
protected:
    void OnCreate() override {
        Root()->AddChild<dui::Text>(L"关掉窗口，应用不退出，托盘图标仍在。", dui::Style());
    }
};

// 每次重开都新建 Window 对象 —— 复用旧对象会让 OnCreate 重复往 root_ 加控件。
void OpenWindow() {
    auto w = std::make_unique<TrayWindow>();
    if (!g_app->AddWindow(*w)) return;   // 失败原因查 g_app->LastError()
    g_windows.push_back(std::move(w));
}
}  // namespace

int WINAPI wWinMain(...) {
    dui::App app;
    g_app = &app;
    app.SetQuitOnLastWindowClosed(false);

    dui::Tray tray;
    tray.SetIconFromFile(L"tray.ico");   // 框架加载并接管图标；失败回退系统默认
    tray.Show(L"我的应用");

    tray.OnClick([&] {
        if (app.WindowCount() == 0) OpenWindow();
    });
    tray.OnContextMenu([&](dui::Vec2 screenPx) {
        dui::Menu menu;
        menu.Add(L"打开窗口", [&] { if (app.WindowCount() == 0) OpenWindow(); });
        menu.Add(L"退出应用", [&] { app.Quit(); });
        dui::ShowPopupMenu(menu, tray.Owner(), screenPx);   // owner 不能传 nullptr
    });

    OpenWindow();
    return app.Run();   // 关窗不返回；直到菜单里点"退出应用"
}
```

`Samples/Tray` 就是这个例子的完整可编译版（含图标文件，由工程 `PostBuildEvent` 拷到 exe 同目录）。

---

## 2. 设置图标图像

推荐用框架封装的加载接口 —— 只需给 `.ico` 路径或内存字节，`Tray` 完成加载并**接管 `HICON` 所有权**（替换旧图 / 析构时 `DestroyIcon`），调用方无需管生命周期：

```cpp
// 1) 从 .ico 文件加载（框架读文件 + 解析）
tray.SetIconFromFile(L"tray.ico");

// 2) 从内存中的 .ico 文件字节加载（嵌入资源 / 网络下发 / 自己读文件）
const void* bytes = /* .ico 完整文件内容 */;
size_t size = /* 字节数 */;
tray.SetIconFromMemory(bytes, size);   // 只在调用期间读，返回后可释放

// 3) 不设图标 → 用系统默认应用图标
// 直接 tray.Show(L"我的应用")，不调 SetIcon*
```

设置后 `tray.Show(L"我的应用")` 用已加载图标显示；未加载过则用系统默认图标。想继续用外部 `HICON`（自己管生命周期）也支持：`tray.Show(myIcon, L"我的应用")`，这一重载**不接管**所有权。

要点：

- **两种输入都是完整 `.ico` 文件字节**：内存版要传 ICO 头 + 目录 + 图像数据（不是裸 PNG / BGRA8）。PNG 先转成 `.ico`。
- **帧选择**：框架从 `.ico` 里挑宽度最接近系统小图标（`SM_CXSMICON`）的一帧生成 `HICON`（`CreateIconFromResourceEx`，`dwVer=0x00030000`，支持 PNG 压缩帧 / 32bpp alpha）。故 `.ico` 里放 16 / 20 / 32 多个尺寸能适配不同 DPI。
- **换图标**：重复 `SetIconFromFile/SetIconFromMemory` 会销毁旧图再挂新的；托盘已显示时立即换图（只换图标、不动提示文本）。运行时切在线 / 离线状态直接再调一次即可。
- **失败**：返回 `false`，查 `LastError()`（文件打不开 / 不是合法 ICO / `CreateIconFromResourceEx` 失败）；此时旧图标仍保留。
- **所有权**：`SetIcon*` 加载的图标归 `Tray`；`Show(HICON, tip)` 传入的仍归调用方（用 `LR_SHARED` 时不要 `DestroyIcon`，不带 `LR_SHARED` 的 `LoadImageW` 在 `Tray` 析构后再释放）。

---

## 3. 回调与交互

### 3.1 三种回调（均在 UI 线程）

```cpp
tray.OnClick([&]{ ... });                       // 左键单击
tray.OnDoubleClick([&]{ ... });                 // 左键双击
tray.OnContextMenu([&](dui::Vec2 posPx){ ... }); // 右键，参数是屏幕物理像素坐标
```

- `OnDoubleClick` 未绑定时，双击**退化为一次 `OnClick`**。
- Windows 双击会先发一次单击抬起再发双击，所以**同时绑单击和双击会各触发一次**；通常只绑其一。

### 3.2 右键菜单

框架不自动弹菜单 —— 在 `OnContextMenu` 里自己弹：

```cpp
tray.OnContextMenu([&](dui::Vec2 p) {
    dui::Menu m;
    m.Add(L"退出", [&]{ app.Quit(); });
    dui::ShowPopupMenu(m, tray.Owner(), p);   // owner 必须是 tray.Owner()
});
```

`ShowPopupMenu` 的 owner **不可为 `nullptr`**（会直接返回、不弹菜单）；`Tray::Owner()` 返回内部 message-only 窗口句柄，正是为此设计。菜单的构造与子菜单 / 复选项见 [Menu.md](Menu.md)。

---

## 4. API 一览

| 成员 | 说明 |
| --- | --- |
| `SetIconFromFile(const wchar_t* path)` | 从 `.ico` 文件加载图标，框架接管所有权；已显示则立即换图 |
| `SetIconFromMemory(const void* data, size_t size)` | 从内存中的 `.ico` 文件字节加载，同上；`data` 只在调用期间使用 |
| `Show(const wchar_t* tip)` | 用已加载图标（或系统默认）显示 / 更新；推荐配合 `SetIcon*` 使用 |
| `Show(HICON icon, const wchar_t* tip)` | 用外部 `HICON` 显示 / 更新，`icon=nullptr` 用系统默认；**不接管**所有权 |
| `Hide()` | 撤掉图标（不销毁内部窗口） |
| `Visible()` | 图标当前是否显示 |
| `SetTip(const wchar_t* tip)` | 只更新提示文本（图标已显示时） |
| `Owner()` | 内部消息窗口句柄，作为 `ShowPopupMenu` 的 owner |
| `OnClick` / `OnDoubleClick` / `OnContextMenu` | 三种回调，链式返回 `Tray&` |
| `LastError()` | 最近一次失败原因 |

---

## 5. 实现要点与坑

1. **message-only 窗口自包含**：`Show()` 时才创建（`HWND_MESSAGE`，不显示、不进任务栏），析构时销毁。它**不进** `App::windows_`、不参与 `WindowCount()` 与退出判定。不用托盘则完全没有这个窗口。
2. **重开窗口要 `new` 新对象**：`App` 只存 `Window*`，关窗不会销毁 `Window` 对象；复用旧对象会让 `OnCreate()` 再次往 `root_` 追加控件、界面重复。像示例那样把每次新建的对象存进容器。
3. **策略 `false` 后必须自己 `Quit()`**：否则 `Run()` 永不返回。`Quit(code)` 任意线程可调，`code` 成为 `Run()` 的返回值。
4. **一个进程一个 `App`**：焦点与任务队列是进程级全局的。
5. **关窗不退出期间后台仍工作**：`app.SetTimer(ms, fn)` 是窗口无关的定时器（所有窗口关掉也触发，回调在 UI 线程），`app.KillTimer(id)` 取消。见 [Window.md](Window.md) §1.4。
6. **关窗前确认**是另一层面的事：`Window::OnClosing()` 返回 `false` 阻止单个窗口关闭，与"关窗后是否退出应用"的应用级策略不同。见 [Window.md](Window.md) §1.4。
