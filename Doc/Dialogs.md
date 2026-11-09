# 对话框使用说明（调用方式）

> 状态：**v1.0 — 已实现**（2026-11-07），Debug/Release x64 编译通过。人工实测项见设计文档 §7 D4。
> 设计、分层、取舍与验收见 [Arch/17-Dialogs.md](Arch/17-Dialogs.md)；可运行示例 `Samples/Dialog/main.cpp`
> （另含 `MacroCompat.cpp`：MessageBox 同名宏两种包含顺序的编译期守卫）。

---

## 1. 一句话

在 `dui::Window` 派生类里直接调用四个成员方法，同步拿到结果：

```cpp
auto files   = OpenFile(...);     // 打开文件（可多选）→ DialogResult<std::vector<String>>
auto target  = SaveFile(...);     // 保存文件          → DialogResult<String>
auto folder  = OpenFolder(...);   // 选择文件夹        → DialogResult<String>
auto clicked = MessageBox(...);   // 消息/警告          → MessageResult
```

只返回**路径**和**用户按了哪个按钮**，不替你读写文件（保存对话框也不创建文件）。对话框由 Windows 提供，DPI 自动适配，无需任何初始化。

---

## 2. 完整示例（可直接作为 `Samples/Dialog/main.cpp`）

```cpp
#include <windows.h>          // wWinMain 入口；必须先于 DUI.h（见 §4 第 8 条）
#include "DUI/DUI.h"

namespace {

// C++20：数组直接传成 std::span，不需要 filterCount。
// span 是视图，底层数组必须活到调用返回 —— 用 static/具名数组最稳妥。
static constexpr dui::FileTypeFilter kFilters[] = {
    { L"文本文件", L"*.txt;*.md" },
    { L"图片文件", L"*.png;*.jpg;*.gif" },
    { L"所有文件", L"*.*" },
};

// dui::Button 无内建外观，背景与 hover 全靠 Style + When 状态变体。
static const dui::Style kButton = dui::Style()
    .FontSize(13.f).Color(0xFFFFFFFF)
    .Background(0xFF374151)
    .Padding(dui::Thickness(6.f, 12.f))
    .CornerRadius(4.f)
    .TextAlign(dui::TextAlign::Center)
    .Cursor(dui::Cursor::Hand)
    .FlexShrink(0.f);

static const dui::Style kButtonHover = dui::Style()
    .Background(0xFF1F6FE5).Color(0xFFFFF2CC);

class DialogWindow final : public dui::Window {
public:
    DialogWindow()
        : dui::Window(dui::WindowOptions{
              .title = L"DUI 对话框示例", .width = 560.f, .height = 360.f }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style().Gap(12.f).Padding(24.f).Background(0xFFF5F5F5));
        result_ = root->AddChild<dui::Text>(L"（尚未选择）", dui::Style().FontSize(16.f));

        // ① 打开文件（多选 + 过滤器）
        root->AddChild<dui::Button>(L"打开文件…", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                auto r = OpenFile(dui::OpenFileOptions{
                    .title = L"选择文件", .filters = kFilters,
                    .filterIndex = 0, .multiSelect = true });
                if (r.IsCancelled()) { result_->SetText(L"已取消"); return; }
                if (r.IsError())    { result_->SetText(r.Error()); return; }
                dui::String s = L"已选择：";
                for (const dui::String& p : r.Value()) s += L"\n" + p;
                result_->SetText(s);
            });

        // ② 保存文件（只拿路径，不写文件）
        root->AddChild<dui::Button>(L"保存文件…", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                auto r = SaveFile(dui::SaveFileOptions{
                    .title = L"保存为", .filters = kFilters, .defaultExtension = L"txt" });
                if (r.IsCancelled()) { result_->SetText(L"已取消"); return; }
                if (r.IsError())    { result_->SetText(r.Error()); return; }
                result_->SetText(L"目标：" + r.Value() + L"（框架未创建文件）");
            });

        // ③ 选择文件夹
        root->AddChild<dui::Button>(L"选择文件夹…", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                auto r = OpenFolder(dui::OpenFolderOptions{ .title = L"选择目录" });
                if (r.IsOk())               result_->SetText(L"目录：" + r.Value());
                else if (!r.IsCancelled())  result_->SetText(r.Error());
            });

        // ④ 警告 / 确认
        root->AddChild<dui::Button>(L"警告", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                auto r = MessageBox(dui::MessageOptions{
                    .text = L"确定要删除所选内容吗？", .title = L"警告",
                    .icon = dui::MessageIcon::Warning,
                    .buttons = dui::MessageButtons::YesNo,
                    .defaultButton = 2 });           // 默认「否」，防误触
                result_->SetText(r == dui::MessageResult::Yes ? L"选择了：是" : L"选择了：否");
            });

        // 便捷糖：一行警告 / 一行确认
        root->AddChild<dui::Button>(L"简单警告", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                Warn(L"操作已完成。");
            });
        root->AddChild<dui::Button>(L"简单确认", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                result_->SetText(Confirm(L"要继续吗？") ? L"继续" : L"取消");
            });
    }

private:
    dui::Text* result_ = nullptr;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    DialogWindow window;
    return app.Run(window);
}
```

调用点是 `OnClick` 的 lambda，捕获 `this` 即可拿到所在窗口（owner 由框架内部提供，无需传 HWND）；这与 `Samples/Async` 捕获 `status` 的写法一致，指针生命周期由业务保证。

---

## 3. API 速查

| 方法 | 参数 | 返回 | 说明 |
| --- | --- | --- | --- |
| `OpenFile(const OpenFileOptions& = {})` | 见下 | `DialogResult<std::vector<String>>` | 多选时按系统返回顺序；单选也可用，取 `Value()[0]` |
| `SaveFile(const SaveFileOptions& = {})` | 见下 | `DialogResult<String>` | 单个目标路径；**不创建、不写文件**；覆盖确认由系统负责 |
| `OpenFolder(const OpenFolderOptions& = {})` | 见下 | `DialogResult<String>` | 一期单选目录 |
| `MessageBox(const MessageOptions&)` | 见下 | `MessageResult` | 用户按下的按钮；调用失败返回 `Failed` |
| `Warn(text, title = nullptr)` | `const wchar_t*` | `MessageResult` | 等价 `Warning` 图标 + `Ok` 按钮 |
| `Confirm(text, title = nullptr)` | `const wchar_t*` | `bool` | 等价 `Question` 图标 + `YesNo`，`Yes` → `true` |

> `Window::MessageBoxW(...)` 是框架为 Win32 同名宏准备的兼容别名，**业务不要主动调用**，见 §4 第 8 条。

### 3.1 选项字段

| 结构 | 字段 | 默认 | 语义 |
| --- | --- | --- | --- |
| `OpenFileOptions` | `title` | `nullptr` | 对话框标题；空 = 系统默认「打开」 |
| | `initialPath` | `nullptr` | 初始目录，或含文件名的完整路径（预填文件名）；无效路径**被忽略**，不报错 |
| | `filters` | `{}` | `std::span<const FileTypeFilter>`，数组 / `std::array` / `std::vector` 直传；空 = `所有文件 *.*` |
| | `filterIndex` | `0` | **0-based** 默认选中项；越界钳到 0 |
| | `multiSelect` | `false` | 允许 Ctrl/Shift 多选 |
| `SaveFileOptions` | `title` | `nullptr` | 空 = 系统默认「另存为」 |
| | `initialPath` | `nullptr` | 初始目录或建议的完整文件名 |
| | `filters` / `filterIndex` | `{}` / `0` | 同 `OpenFileOptions` |
| | `defaultExtension` | `nullptr` | `L"txt"`（**不带点**）；用户没填扩展名时系统补 |
| `OpenFolderOptions` | `title` / `initialPath` | `nullptr` | 同上 |
| `MessageOptions` | `text` | `L""` | 正文，支持 `L"\n"` 换行 |
| | `title` | `nullptr` | 空 = 窗口标题；窗口标题也为空时 `L"提示"` |
| | `icon` | `Warning` | `None / Information / Warning / Error / Question` |
| | `buttons` | `Ok` | `Ok / OkCancel / YesNo / YesNoCancel / RetryCancel / AbortRetryIgnore / CancelRetryContinue` |
| | `defaultButton` | `1` | 1..4，从左往右；越界钳到 1 |

```cpp
struct FileTypeFilter { const wchar_t* name; const wchar_t* pattern; };
// 例：{ L"文本文件", L"*.txt;*.md" }   —— 多个扩展名用 ';' 分隔

static constexpr dui::FileTypeFilter kFilters[] = { /* ... */ };
auto r = OpenFile(dui::OpenFileOptions{ .filters = kFilters });  // 无需 filterCount
```

**不要**写 `.filters = { {L"文本文件", L"*.txt"} }`：`std::span` 没有从花括号列表直接构造的推导路径，且临时量生命周期易踩坑；先声明具名数组 / `std::array`。

### 3.2 结果与错误（三态，务必分别处理）

```cpp
auto r = OpenFile(opt);
if (r.IsCancelled()) { /* 用户点了取消/关闭：不是错误，通常直接 return */ }
if (r.IsError())    { result_->SetText(r.Error()); /* UTF-16 原因，含 HRESULT */ }
if (r.IsOk())       { /* 用 r.Value() */ }

// 也可用 Status() 做 switch：DialogStatus::Ok / Cancelled / Error
```

| 结果 | `IsOk` | `IsCancelled` | `IsError` | `Value()` | `Error()` |
| --- | --- | --- | --- | --- | --- |
| 确认选择 | ✔ | | | 路径列表 / 路径 | 空串 |
| 取消 / 关闭 | | ✔ | | 空 | 空串 |
| 系统错误 | | | ✔ | 空 | `L"… (HRESULT 0x80040154)"` 等 |

`MessageResult` 直接比较即可：`Ok / Cancel / Yes / No / Retry / Abort / Ignore / TryAgain / Continue / Failed`。注意 `Cancel` 是「用户按了取消按钮」，不是「调用失败」；保存对话框里「覆盖确认选否」由系统内部消化，不会变成框架的 `Cancelled`。

---

## 4. 使用约束（重要）

1. **只在 UI 线程调用**：`OnCreate()`、`OnClick` 等事件回调里；协程里须先 `co_await dui::Foreground()` 切回 UI 线程。后台线程直接调用不在支持范围。
2. **必须在 `Application::Run` 建立 HWND 之后**：构造 `Window` 但还没 `Run` 时调用 → `OpenFile` / `SaveFile` / `OpenFolder` 返回 `Error`，`MessageBox` 返回 `Failed`。`OnCreate()` 里 HWND 已建但窗口尚未显示，此时弹窗会挡住首帧显示，**允许但不推荐**。
3. **同步阻塞**：函数返回 = 对话框已关闭，无需回调 / `Then()`。期间框架主消息循环挂起：界面不刷新、动画/GIF/光标闪烁停摆、后台任务的 `PostUi` 回投延后到关闭后执行（**不会丢**）。不要在此期间依赖持续 UI 更新。
4. **字符串**：一律 UTF-16（`String` / `const wchar_t*` / `L"..."`），中文路径直接可用；返回的也是绝对路径。
5. **指针 / span 生命周期**：`title` / `initialPath` / `defaultExtension` / `filters[].name|pattern` 只需在**调用期间**有效，框架不长期持有；`span` 的底层数组必须活到调用返回，用字面量 + `static`/具名数组，别把即将销毁的 `String::c_str()` 传进去。
6. **DPI**：系统对话框由 Per-Monitor V2 自动缩放，框架不传坐标、不做换算；无需为高 DPI 写任何额外代码。
7. **所有权**：owner 窗口由框架取，系统会自动禁用宿主窗口并在关闭后恢复；不传、也不能传自定义 HWND。
8. **`MessageBox` 与 Win32 宏**：`winuser.h` 把 `MessageBox` 定义成 `MessageBoxW` 宏，与成员方法同名。框架在公共头里 `#undef MessageBox` 并额外提供兼容别名，因此：
   - **先 `#include <windows.h>`，再 `#include "DUI/DUI.h"`**（与现有全部示例一致）；
   - 包含 `DUI.h` 之后若要调 Win32 原生弹窗，用显式 `::MessageBoxW(...)`，别写 `MessageBox(...)`；
   - 万一 windows.h 在 DUI.h 之后才包含导致宏重新生效，调用会被替换成 `MessageBoxW(...)`，由兼容别名接住，行为不变。

---

## 5. 常见情形对照

| 需求 | 写法 |
| --- | --- |
| 只选一个图片 | `static constexpr dui::FileTypeFilter kImg[] = {{L"图片", L"*.png;*.jpg"}};` → `OpenFile({.filters = kImg})` → `r.Value()[0]` |
| 记住上次目录 | 业务自己保存上次返回的目录，下次填 `initialPath` |
| 另存为并补扩展名 | `SaveFile({.filters = kFilters, .defaultExtension = L"txt"})`；写文件由业务自己做 |
| 让用户确认危险操作 | `MessageIcon::Warning` + `MessageButtons::YesNo` + `defaultButton = 2` |
| 仅通知，无需选择 | `Warn(L"操作已完成。")` |
| 失败提示给用户 | `if (r.IsError()) Warn(r.Error().c_str());` |
| 初始路径可能不存在 | 直接传；框架忽略无效的 `initialPath`，退回系统默认位置 |

---

## 6. 构建与排错

- 无需改业务工程配置、无需手动加 lib：`ole32` / `shell32` 等系统库由库内 `#pragma comment(lib)` 携带（与 WIC 的 `windowscodecs.lib` 同一做法）。
- 只支持 Windows 10 1703+ / x64、C++20（过滤器用 `std::span`）；**业务不需要初始化 COM**：`Application::Run` 已用 `OleInitialize` 把 UI 线程建成 **STA**（ADR-027）。若不用 `Run` 而自建宿主，必须自己初始化 STA，否则文件类弹窗返回 `Error`。
- 本次已在 Debug/Release x64 全解决方案编译验证（含 `MacroCompat.cpp` 两种包含顺序守卫），`/W4` 无新增警告；四类弹窗的弹出 / 关闭已自动化点击复测，其余人工实测项见 `Arch/17-Dialogs.md §7` D4。

| 现象 | 原因 / 处理 |
| --- | --- |
| 对话框不出现、返回 `Error` / `Failed` | 在 `Run` 之前调用，或在后台线程调用 |
| 点击文件类按钮后界面无响应、也没有对话框窗口 | UI 线程 COM 不是 STA（MTA 会让 `IFileDialog::Show` 死锁；框架已修好）。自定义宿主请自行 `OleInitialize`，见 `Pitfalls §4.11` / ADR-027 |
| `Error()` 里出现 `0x800704C7` | 不该出现：该 HRESULT 已映射为 `Cancelled`；若出现说明实现有回归 |
| `0x80040154`（REGDB_E_CLASSNOTREG）等 | 系统组件异常 / COM 初始化失败；把 `Error()` 文本显示给用户 |
| `MessageBox(...)` 编译/链接报找不到 `MessageBoxW` 成员 | include 顺序问题或框架宏处理回归；按 §4 第 8 条检查 |
| `.filters = { {L"文本文件", L"*.txt"} }` 编译不过 | `std::span` 不能从花括号列表直接推导；改成具名数组 / `std::array` |
| 选中了虚拟位置（如「此电脑」里的库）却没路径 | 一期只返回文件系统路径（见设计 §8） |
| 保存对话框返回后文件并不存在 | 预期行为：框架只给路径，不创建文件 |
| 弹窗期间进度条不动 / 动画停 | 预期行为，系统模态泵接管了主循环（设计 §6.1） |
| 取消后文本框光标不见了 | 属已知风险，实现会做焦点抑制并在验收中实测（设计 §6.2） |

---

## 7. 相关文档

- [Arch/17-Dialogs.md](Arch/17-Dialogs.md)：分层、实现规格、里程碑与验收、决策记录。
- [Pitfalls.md](Pitfalls.md)：§1.7 系统模态循环、§1.8 失活与焦点、§4 链接库惯例。
- [Arch/07-Threading.md](Arch/07-Threading.md)：UI 单线程与 `PostUi` 回投。
