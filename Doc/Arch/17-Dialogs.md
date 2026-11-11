# 对话框（系统原生）

> 状态：**v1.1 — 已实现并修复 MTA 死锁**（2026-11-07）。D1–D3 完成，Debug/Release 全解决方案编译通过、无新增警告；
> M7.1：UI 线程 COM 固定 STA（ADR-027），四类弹窗弹出 / 关闭已自动化点击复测通过，**剩余人工验证项见 §7 D4**。
> 实现踩坑见 `../Pitfalls.md §4.7–4.11`。
> 使用说明（调用方式）：[../Dialogs.md](../Dialogs.md)
> 相关：[01-Layering.md](./01-Layering.md)（单向依赖 / Platform 模块表）、[02-Decisions.md](./02-Decisions.md)（ADR-007/009/010/012/022/026/027）、[07-Threading.md](./07-Threading.md)（UI 单线程 / Dispatcher）、[12-Windowing.md](./12-Windowing.md)（PMv2 / 模态循环 / 失活）、[../Pitfalls.md](../Pitfalls.md)（§1.7 模态循环、§1.8 失活与焦点、§4 链接库惯例）。

---

## 0. 决策记录

A–J 按建议定案；K / L / I 为本次反馈的修订。

| # | 决策点 | 结论 |
| --- | --- | --- |
| A | 系统原生 vs 框架自绘 | **系统原生**：`IFileOpenDialog` / `IFileSaveDialog` + `MessageBoxW`；自绘列 P2 |
| B | 同步模态 vs 异步 `Task<T>` | **同步、阻塞调用点**（系统对话框自带模态泵）；异步列 P2 |
| C | API 挂载位置 | **`dui::Window` 的 public 成员方法**，owner 天然确定，不必把私有 `NativeHandle()` 开洞 |
| D | 结果三态 | 新增 `DialogResult<T>`（`Ok / Cancelled / Error`）；现有 `Result<T>` 无法区分「取消」与「出错」，不复用。消息框返回 `MessageResult` 枚举 |
| E | 一期范围 | 文件：标题/初始路径/过滤器/默认项/多选；保存：标题/初始路径/过滤器/默认扩展名；路径：标题/初始路径、单选；消息：4 图标 × 7 按钮组 + 默认按钮 + 2 个便捷糖 |
| F | 消息框实现 | **`MessageBoxW`**（user32，零额外清单依赖）；`TaskDialogIndirect` 需 comctl32 v6 清单，留二期 |
| G | COM apartment | **UI 线程固定 STA**（`Application::Run` 起始 `OleInitialize`，见 ADR-027 / §9）。原方案复用 WIC 的 MTA 实测让 `IFileDialog::Show` 死锁，已改正；`Core/Dialog.cpp` 另做单元校验，非 STA 直接返回 `Error`，不再静默挂死 |
| H | 示例 | 新建 `Samples/Dialog`（四个能力按钮 + 便捷糖 + 结果文本），挂 `DUI.slnx` |
| I | 保存文件对话框 | **做**（本次确认）：`IFileSaveDialog`，返回单个路径，规格见 §4 / §5.2 |
| J | 文档编号 | `Doc/Arch/17-Dialogs.md` + `Doc/Dialogs.md` |
| **K** | 过滤器数组表达（本次反馈） | **C++20 `std::span<const FileTypeFilter>`**，删掉 `filterCount`；数组 / `std::array` / `std::vector` 可直接传入，`span.size()` 就是数量 |
| **L** | 消息框命名（本次反馈） | **`MessageBox`**，删掉 `ShowMessage`；须处理 Win32 同名宏，方案见 §4.4 |

> 术语区分：ADR-010 窗口形态清单里的「模态对话框」指**框架自绘的窗口形态**（Popup/模态）；本文件说的是**操作系统提供的通用对话框**。二者不冲突，本期不实现前者。

---

## 1. 目标与边界

提供四个开箱即用的系统对话框能力：

| 能力 | 系统实现 | 返回 |
| --- | --- | --- |
| 打开文件（可多选） | `IFileOpenDialog`（Vista+，COM） | 文件系统绝对路径列表 `std::vector<String>` |
| 保存文件 | `IFileSaveDialog`（Vista+，COM） | 单个目标路径 `String` |
| 打开路径（选文件夹） | `IFileOpenDialog` + `FOS_PICKFOLDERS` | 单个目录绝对路径 `String` |
| 消息 / 警告 | `MessageBoxW` | 用户按下的按钮 `MessageResult` |

框架**只负责选出路径 / 收集用户选择，不读文件内容、不写文件、不创建 DUI 窗口、不打开流**；保存对话框的「同名文件已存在」覆盖确认由系统提供，框架不创建文件。字符串统一 UTF-16（`String = std::wstring`，ADR-026）。最低系统 Windows 10 1703（ADR-009），上述 API 均在其下可用，无新增第三方依赖。

---

## 2. 为什么用系统原生

- **无依赖 / 体积小**：只用 OS 自带的 `user32 / ole32 / shell32`；不引入 WinRT（ADR-022 已否定），也不引入自绘所需的 Shell 图标提取、文件系统枚举、列表虚拟化。
- **DPI 免费**：进程清单已是 Per-Monitor V2，系统对话框由系统自动缩放（12-Windowing §2），框架不做任何 DIP ↔ 物理像素换算。
- **生态完整**：最近访问、库、OneDrive、搜索、第三方 Shell 扩展、快捷键、键盘导航、覆盖确认全部由系统提供。
- **与既有「系统模态循环」经验一致**：12-Windowing §6.1 已承认拖边框缩放时主循环被系统接管；本期只是把同一情形扩展到通用对话框（§6）。

---

## 3. 分层与数据流

```
业务代码（Window 派生类，OnCreate / OnClick）
   │  OpenFile / SaveFile / OpenFolder / MessageBox
   ▼
业务代码（Window 派生类，OnCreate / OnClick）
   │  OpenFile / SaveFile / OpenFolder / MessageBox
   ▼
App/Dialog.cpp      Window 成员方法薄封装：取 owner HWND、对话框深度计数（焦点抑制）
   ▼
App/Dialog.h        公共头：公共类型（选项 / DialogResult / 枚举）+ 服务函数声明
App/Dialog.cpp      COM / Win32 实现、HRESULT → DialogResult、资源释放 + Window 薄封装
   ▼
IFileOpenDialog / IFileSaveDialog + IShellItem(IShellItemArray)   /   MessageBoxW
```

约束（逐条对照既有约定）：

1. **公共头无 `windows.h` 类型泄漏**：`App/Dialog.h` 含 `windows.h`（宏防护见 §4.4）但公共类型只用 `void*` owner；UI 层以 `NativeHandle()` 私有通道传递（2026-11-11 起「公共头无 windows.h」约束已取消，见 `Pitfalls §4.5`）。
2. **服务函数不接触 `dui::Window`**：公共服务函数收 `void* owner`，只依赖窗口句柄，不反向依赖 UI（01-Layering §1）。原「App 层转 POD / 枚举映射」转换层已随公共类型与实现合并到同一文件而移除（2026-11-11）。
3. **Window 成员方法是薄封装**：`App/Dialog.cpp` 只做 owner 取值 + `dialogDepth_` 失活抑制（§6.2），业务也可直接调自由函数（`PickFiles` 等）。
4. **无异常 / 无 RTTI**（ADR-007，`/EHs-c- /GR-`）：错误走 `DialogResult`，不抛；`std::vector` / `std::span` 均为头文件模板，分配失败按项目既有惯例（进程 terminate），不额外造异常路径。
5. **链接库**：`ole32.lib` / `shell32.lib`（以及按需 `uuid.lib`）用 `#pragma comment(lib, ...)` 写在 `App/Dialog.cpp` 内，**不改根目录 `DUI.props`**（照 `windowscodecs.lib` 惯例，16-Image §2.1）。
6. **UI 单线程**（ADR-012 / 07-Threading §3.1）：只允许在 UI 线程、且 `Application::Run` 建好 HWND 之后调用；禁止在后台线程直接调用（不提供内部跨线程转发）。

工程落点：`DUI/App/Dialog.h/.cpp`（原 `DUI/Platform/Dialog.*` → Core 内部头 → 2026-11-11 二次整合为公共类型 + 实现合并到 `App/Dialog.cpp`）；`DUI/DUI.h` `#include "App/Dialog.h"`；`DUI/DUI.vcxproj` 与 `.filters` 登记。

---

## 4. 公共 API 规格（冻结候选）

```cpp
// DUI/App/Dialog.h —— 公共头（含 windows.h；公共类型不含 Win32 类型）
namespace dui {

enum class DialogStatus { Ok, Cancelled, Error };

template <class T>
class [[nodiscard]] DialogResult {
public:
    static DialogResult Succeeded(T value);      // Status = Ok
    static DialogResult Cancelled();             // Status = Cancelled
    static DialogResult Failed(String error);    // Status = Error
    DialogStatus Status()       const noexcept;
    bool IsOk()                 const noexcept;
    bool IsCancelled()          const noexcept;
    bool IsError()              const noexcept;
    const T&      Value()       const noexcept;  // 非 Ok 时返回默认构造的 T
    const String& Error()       const noexcept;  // 非 Error 时返回空串
};

struct FileTypeFilter {
    const wchar_t* name;      // 显示名，如 L"文本文件"
    const wchar_t* pattern;   // 如 L"*.txt;*.md"；多项用 ';' 分隔
};

struct OpenFileOptions {
    const wchar_t* title = nullptr;                  // nullptr = 系统默认「打开」
    const wchar_t* initialPath = nullptr;            // 目录或完整文件路径；nullptr = 系统默认
    std::span<const FileTypeFilter> filters = {};    // 空 = 所有文件 *.*
    size_t filterIndex = 0;                          // 0-based（Win32 为 1-based，转换在 Platform）
    bool multiSelect = false;
};

struct SaveFileOptions {
    const wchar_t* title = nullptr;                  // nullptr = 系统默认「另存为」
    const wchar_t* initialPath = nullptr;            // 目录或建议的完整文件名
    std::span<const FileTypeFilter> filters = {};
    size_t filterIndex = 0;
    const wchar_t* defaultExtension = nullptr;       // L"txt"（不带点）；用户不填扩展名时系统补
};

struct OpenFolderOptions {
    const wchar_t* title = nullptr;
    const wchar_t* initialPath = nullptr;
};

enum class MessageIcon    { None, Information, Warning, Error, Question };
enum class MessageButtons { Ok, OkCancel, YesNo, YesNoCancel, RetryCancel,
                            AbortRetryIgnore, CancelRetryContinue };
enum class MessageResult  { Failed, Ok, Cancel, Yes, No, Retry, Abort, Ignore,
                            TryAgain, Continue };

struct MessageOptions {
    const wchar_t* text = L"";                  // 正文，支持 L"\n"
    const wchar_t* title = nullptr;             // nullptr = 窗口标题；窗口标题为空则 L"提示"
    MessageIcon    icon = MessageIcon::Warning; // 「消息警告」默认就是警告图标
    MessageButtons buttons = MessageButtons::Ok;
    int defaultButton = 1;                      // 1..4，越界钳到 1
};

// dui::Window 新增 public 成员（定义在 App/Dialog.cpp）
DialogResult<std::vector<String>> OpenFile  (const OpenFileOptions&   options = {});
DialogResult<String>             SaveFile  (const SaveFileOptions&   options = {});
DialogResult<String>             OpenFolder(const OpenFolderOptions& options = {});
MessageResult                    MessageBox(const MessageOptions&    options);
MessageResult                    MessageBoxW(const MessageOptions&);  // 宏兼容别名，见 §4.4；业务不要主动调用

// 便捷糖
MessageResult Warn   (const wchar_t* text, const wchar_t* title = nullptr); // Warning + Ok
bool          Confirm(const wchar_t* text, const wchar_t* title = nullptr); // Question + YesNo → Yes

}  // namespace dui
```

### 4.1 过滤器：`std::span`（C++20）

- 数组、`std::array`、`std::vector` 都能直接赋给 `.filters`，**不需要再传数量**：
  ```cpp
  static constexpr dui::FileTypeFilter kFilters[] = {
      { L"文本文件", L"*.txt;*.md" }, { L"所有文件", L"*.*" } };
  auto r = OpenFile(dui::OpenFileOptions{ .filters = kFilters, .multiSelect = true });
  ```
- `span` 只是视图，**底层数组必须活到调用返回**（同步 API，调用点同作用域即可）；用 `static` / 具名数组最稳妥。
- **不要**直接写 `.filters = { {L"文本文件", L"*.txt"} }`：`std::span` 没有从花括号列表直接构造的推导路径，且临时量生命周期易踩坑；先声明具名数组 / `std::array`。
- `filterIndex >= filters.size()` 时钳到 0；`filters.empty()` 时按 `*.*` 处理。

### 4.2 三态语义（重要）

| 情形 | `OpenFile` / `SaveFile` / `OpenFolder` | `MessageBox` |
| --- | --- | --- |
| 用户确认 | `IsOk() == true`，`Value()` 有值 | 对应按钮的 `MessageResult` |
| 用户取消 / 关闭窗口 | `IsCancelled() == true`，`Value()` 为空 | `Cancel`（`Ok` 按钮组无取消键） |
| 系统 / 参数错误 | `IsError() == true`，`Error()` 为 UTF-16 简短原因 | `Failed` |

- **取消不是错误**：`HRESULT_FROM_WIN32(ERROR_CANCELLED)`（`0x800704C7`）单独映射到 `Cancelled`，业务不必写「空结果就算失败」的猜测逻辑；保存对话框里「覆盖确认时选否」由系统内部消化，不会变成框架的 `Cancelled`。
- `Error()` 格式示例：`L"打开对话框失败 (HRESULT 0x80040154)"`；不泄漏 Win32 类型到公共头。
- `DialogResult<T>` 实现全内联在 `App/Dialog.h`，不新增 .cpp 符号；风格对齐 `Core/Core.h:108-123` 的 `Result<T>`。

### 4.3 参数生命周期与调用时机

- `title` / `initialPath` / `defaultExtension` / `filters[].name|pattern`：仅在**调用期间**需有效（Core 对话框实现在 `Show()` 返回前用完；不持有指针）。字面量或调用点同生命周期的局部数组均可，勿传临时 `String::c_str()` 后立刻销毁。
- 必须在 `Application::Run` 之后（HWND 已建）调用；`OnCreate()` 里 HWND 已建但窗口未显示，此时弹窗会阻断 `Show()`，**允许但不推荐**。
- 调用是**同步阻塞**的：从函数返回时对话框已关闭，结果可立即使用，无需回调 / `Then()`。

### 4.4 `MessageBox` 与 Win32 同名宏（必须处理）

`winuser.h` 在 UNICODE 下定义 `#define MessageBox MessageBoxW`。项目所有示例都是先 `#include <windows.h>` 再 `#include "DUI/DUI.h"`，若直接把成员方法命名为 `MessageBox` 而不处理：

- 含 `windows.h` 的 TU 里 `window.MessageBox(...)` 被预处理成 `window.MessageBoxW(...)`，成员方法**声明 / 定义**同样被替换；
- 不含 `windows.h` 的 TU 里仍是 `MessageBox` → 跨 TU 符号不一致，**LNK2019 或 ODR 违规**。

实现期固定写法：

1. `App.h` **顶部**与 `App/Dialog.h` **末尾**各做一次 `#ifdef MessageBox / #undef MessageBox / #endif`，`App/Dialog.cpp` 在全部 include 后再 `#undef` 一次，保证框架所有 TU 看到真名。副作用：包含 `DUI.h` 的业务 TU 也不再有该宏 —— 业务若要调 Win32 原生弹窗，用显式 `::MessageBoxW(...)`（文档写明）。
2. `Window` 额外提供兼容别名 `Window::MessageBoxW`（声明在 `App.h`、**定义在 `App/Dialog.cpp`**，不能写成类内 inline 转发，否则宏仍生效时 `MessageBox(o)` 会被替换成 `MessageBoxW(o)` 造成递归）；只用于「业务在 `DUI.h` 之后才 include `windows.h`，宏被重新定义」的情形；业务代码不主动调用。
3. 验收 ✅：`Samples/Dialog/main.cpp`（windows.h 先）与 `MacroCompat.cpp`（DUI.h 先、windows.h 后）都调用 `MessageBox(...)`，Debug / Release 均链接通过。

---

## 5. Core 侧实现规格

> 2026-11-11 起本节的内部 POD（`NativeFileFilter` / `MsgIcon` / `MsgButtons` / `NativeMessageResult`）已并入公共类型——公共枚举即实现枚举，`FileTypeFilter` 与 `COMDLG_FILTERSPEC` 同构直接重解释，转换层不复存在。以下保留历史规格供对照。

```cpp
// DUI/App/Dialog.h —— 公共头（含 windows.h）
namespace dui {
struct NativeFileFilter { const wchar_t* name; const wchar_t* pattern; };

// true = 成功（out 有效）；false = 取消或失败，用 cancelled / error 区分
bool PickFiles (void* owner, const wchar_t* title, const wchar_t* initialPath,
                const NativeFileFilter* filters, size_t filterCount, size_t filterIndex,
                bool multiSelect, std::vector<String>& out, bool& cancelled, String& error);
bool SaveFile  (void* owner, const wchar_t* title, const wchar_t* initialPath,
                const NativeFileFilter* filters, size_t filterCount, size_t filterIndex,
                const wchar_t* defaultExtension,
                String& out, bool& cancelled, String& error);
bool PickFolder(void* owner, const wchar_t* title, const wchar_t* initialPath,
                String& out, bool& cancelled, String& error);

enum class MsgIcon { None, Information, Warning, Error, Question };
enum class MsgButtons { Ok, OkCancel, YesNo, YesNoCancel, RetryCancel,
                        AbortRetryIgnore, CancelRetryContinue };
enum class NativeMessageResult { Failed, Ok, Cancel, Yes, No, Retry, Abort, Ignore,
                                 TryAgain, Continue };
NativeMessageResult ShowMessageBox(void* owner, const wchar_t* text, const wchar_t* title,
                                   MsgIcon icon, MsgButtons buttons, int defaultButton);
}
```

### 5.1 文件 / 文件夹对话框通用流程

1. **COM 单元（ADR-027）**：`Application::Run` 起始调用 `OleInitialize(nullptr)` 把 UI 线程建成 STA（进程级持有、不配对 Uninitialize），必须早于 WIC 的 MTA 尝试。本函数内再请求 `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`（已 STA 时返回 `S_FALSE`）；若返回 `RPC_E_CHANGED_MODE`（线程被别人建成 MTA）或 `CoGetApartmentType` 既不是 `APTTYPE_STA` 也不是 `APTTYPE_MAINSTA`，直接填 `error` 返回 —— MTA 下 `Show()` 会死锁，不能继续（`Pitfalls §4.11`）。
2. `CoCreateInstance(__uuidof(FileOpenDialog) / __uuidof(FileSaveDialog), ..., IID_PPV_ARGS(&dlg))`（coclass 带 `DECLSPEC_UUID`，免 `uuid.lib`，见 `Pitfalls §4.10`）；失败 → `error`。打开与保存共用一个内部 helper（按 CLSID 分流），避免复制粘贴。
3. `GetOptions()` → 按位加 `FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST`（文件夹模式加 `FOS_PICKFOLDERS`，去掉 `FOS_FILEMUSTEXIST`），多选加 `FOS_ALLOWMULTISELECT`，再 `SetOptions()`。
4. `title` → `SetTitle()`；`SetFileTypes(count, COMDLG_FILTERSPEC[])`（数组保持存活至 `Show()` 返回）+ `SetFileTypeIndex(filterIndex + 1)`（0-based → 1-based，越界钳到 0）。
5. `initialPath`：目录 → `SHCreateItemFromParsingName` + `SetFolder`；文件路径 → 拆父目录 `SetFolder` + 文件名 `SetFileName`。路径无效**只忽略、不使调用失败**（沿用开发体验优先）。
6. `dlg->Show(static_cast<HWND>(owner))`：`S_OK` 继续；`HRESULT_FROM_WIN32(ERROR_CANCELLED)` → `cancelled = true`；其余 → `error` 填 `HRESULT`。
7. `GetResult()` → `IShellItem::GetDisplayName(SIGDN_FILESYSPATH, &p)`；打开多选走 `GetResults()` → `IShellItemArray::GetCount/GetItemAt` 逐项取路径。`CoTaskMemFree(p)` 释放每个返回串。
8. 非文件系统项（虚拟 Shell 项）`GetDisplayName` 可能为空 / 失败 → 跳过；若结果全空按 `cancelled = false, error = L"未取得文件路径"` 处理。
9. 文件夹模式不设 `SetFileTypes`、不加 `FOS_ALLOWMULTISELECT`。

### 5.2 保存模式的差异（§0-I）

- CLSID 换 `__uuidof(FileSaveDialog)`，查询 `IFileSaveDialog` 设置默认扩展名；不加 `FOS_FILEMUSTEXIST`（目标是尚不存在的文件）。
- `defaultExtension` → `SetDefaultExtension(L"txt")`（不带点）；`initialPath` 含文件名时预填 `SetFileName`。
- 覆盖确认：系统 `IFileSaveDialog` 默认带 `FOS_OVERWRITEPROMPT`，框架**不额外暴露开关、不创建文件**；用户在系统覆盖提示里选「否」后仍停留在对话框，最终只有确认或取消两种出口。
- 返回 `String`（单路径），不是 vector；`SaveFile` 不写任何字节。

### 5.3 消息框

- 公共 `MessageIcon / MessageButtons` 即实现枚举；`App/Dialog.cpp` `switch` 成 `MB_ICON* / MB_* / MB_DEFBUTTON*`，调用 `::MessageBoxW(owner, text, title, flags)`，把 `ID*` 直接映射成公共 `MessageResult`。公共头因此**不隐含任何 Win32 常量值**（枚举值是框架自定义的连续编码）。
- `owner` 为 HWND → 系统自动禁用宿主窗口并把对话框居中于宿主；`owner == nullptr`（未 Run）→ 直接返回 `Failed`，不弹桌面级窗口。
- `MessageBoxW` 返回 0 = 失败 → `MessageResult::Failed`。

### 5.4 资源与错误

- 所有 COM 接口用一个内部 RAII 小工具 `ComReleaser { IUnknown* p; ~ComReleaser(){ if (p) p->Release(); } }` 管理（无异常环境下析构仍生效），避免提前 return 漏 `Release`；不引入 `wil`/`com_ptr` 等第三方。
- `SIGDN_FILESYSPATH` 返回的 `LPWSTR` 一律 `CoTaskMemFree`，先拷进 `String` 再释放。
- 所有 `HRESULT` 失败路径填 `String& error`（UTF-16，含十六进制 HRESULT），不弹二次错误框、不写日志（由业务决定怎么显示）。

---

## 6. 与现有运行时的交互（重点，验收必测）

### 6.1 系统模态泵 vs 框架主循环

`IFileDialog::Show` / `MessageBoxW` 内部跑自己的消息泵，`Application::Run`（`App.cpp:426-454`）的 `for(;;)` 挂起：

- `Dispatcher::Drain()` 不执行 → 后台任务的 `PostUi` 回投**不会丢**（`PostMessageW(WM_APP)` 排队），只是延后到对话框关闭后的第一次 `[调度]`；文档须明说「对话框打开期间界面不刷新、动画/GIF/光标闪烁停摆」。
- `WM_APP` 唤醒消息会被系统模态泵派发到窗口过程，而窗口过程对它无副作用（`App.cpp:411`）；`WM_TIMER` 仍会标脏但不渲染，关闭后由主循环补一帧 —— 列入验收项实测。
- **不新增框架自己的模态循环**，也不改 `Run` 的消息泵结构。

### 6.2 失活 / 焦点（现有链路的已知风险）

弹窗会让宿主收到 `WM_ACTIVATE(WA_INACTIVE)` → `WindowHostAdapter::OnActivated(false)` 置 `pendingDeactivate_`（`App.cpp:258-264`）→ 主循环恢复时 `TakeDeactivate()` 可能清掉焦点（`App.cpp:439-442`），表现为「文本框聚焦时打开文件对话框，取消后光标/选区不见了」。

**推荐方案（本期一并做）**：`Window` 加私有计数 `int dialogDepth_ = 0`，`App/Dialog.cpp` 在调用 Core 对话框服务前后 `++ / --`；`OnActivated(false)` 里若 `window_.dialogDepth_ > 0` 则直接返回、不置 `pendingDeactivate_`（`WindowHostAdapter` 已是 `Window` 的 friend）。语义：系统对话框期间宿主失活属同一应用的模态流程，不按「切到别的应用」处理。备选：调用前后手动存取 `UI/Focus.h` 的焦点元素并恢复（改动更散）。**须实测确认，见 §7 D4。**

### 6.3 owner / 无边框窗口 / DPI

- owner 直接取 `Window::NativeHandle()`（`App.h:121` 私有成员，成员函数实现可访问），**不新增公共 getter**，公共 API 不出现 `HWND`。
- `frameless = true` 下同样能传 owner：系统对话框独立顶层窗口，不受自绘标题栏 / `WM_NCHITTEST` 影响；系统负责禁用宿主。
- DPI：PMv2 下由系统缩放（12-Windowing §2），框架不换算、不传坐标；跨显示器拖动期间不弹窗（业务侧约束）。

### 6.4 线程

只允许 UI 线程调用（ADR-012、07-Threading §3.1）。一期**不提供**后台线程安全包装；若业务在协程里，须 `co_await dui::Foreground()` 切回 UI 线程后再调用。

---

## 7. 执行步骤与验收（进度）

| 阶段 | 内容 | 验收标准 |
| --- | --- | --- |
| **D1 ✅** | 公共 API 落地：`App/Dialog.h`（`DialogResult` / `std::span` 选项 / 枚举 / `#undef MessageBox`）、`Window` 方法声明（含宏兼容别名）、`App.h` + `DUI.h` 聚合、`DUI.vcxproj` + `.filters` 登记 | Release x64 全解决方案编译通过，`/W4` 无新增警告；`DUI.h` 及 `App/Dialog.h` 不出现 `windows.h` / `HWND` / COM 类型；`DUI.props` 零改动；两种包含顺序的 TU 都能调用 `MessageBox(...)` 且链接通过 |
| **D2 ✅** | Platform 实现 + App 装配：`Platform/Dialog.h/.cpp`（打开 / 保存 / 文件夹 / 消息框 + `#undef`）、`App/Dialog.cpp`、焦点抑制（§6.2） | 取消 / 成功 / 出错三态、过滤器默认项与 0-based 索引、多选、保存默认扩展名与覆盖确认、不创建文件、中文/空格路径、COM 接口与 `CoTaskMemFree` 成对（**编译验证已过，运行时逐项仍待人工实测**） |
| **D3 ✅** | `Samples/Dialog` 示例（照抄 `Samples/HelloWorld/HelloWorld.vcxproj`，OutDir/IntDir 指 `Build\`，挂 `DUI.slnx` + `.filters`） | 四个能力按钮（+ 两个便捷糖按钮）+ 结果文本，演示多选（`std::span` 数组直传）、保存、取消、错误、警告/确认；另有 `MacroCompat.cpp` 宏兼容守卫 |
| **D4 ⏳** | 编译 ✅ + 自动化点击 ✅ + 人工验证待做 | 编译：Debug/Release x64 全解决方案通过、`/W4` 无新增警告 ✅。自动化点击（M7.1）：四类弹窗均出现 `#32770`、关闭后主循环恢复、进程响应正常、15 个示例启动无回归 ✅。人工：① 100% / 150% / 200% DPI；② `frameless = true` 弹窗；③ 文本框聚焦 → 弹窗 → 取消 → 焦点/选区保留；④ 对话框期间后台任务完成，关闭后回投不丢；⑤ 空闲 CPU 归零；⑥ 多选 20+ 文件、长路径、无效初始路径、空 `filters`、越界 `filterIndex` 均不崩；⑦ 实际选文件 / 另存结果文本符合预期 |

实现备注：

- `App/Dialog.cpp` 与 `Core/Dialog.cpp` 同名曾触发 MSB8027 → 两文件已合并为单个 `App/Dialog.cpp`（2026-11-11），特例取消（`Pitfalls §4.8`）。
- `Samples/Dialog/main.cpp`（windows.h 先）与 `MacroCompat.cpp`（DUI.h 先、windows.h 后）覆盖 `MessageBox` 宏的两种 TU 顺序，Debug/Release 均链接通过（`Pitfalls §4.7`）。
- **M7.1**：初版 MTA 让文件类弹窗死锁，已改 STA（ADR-027 / `Pitfalls §4.11`）；`OleInitialize` 是 main STA，单元校验须放行 `APTTYPE_MAINSTA`。
- 已按 `Todo.md §6` 同步 `Todo.md` / `00-Overview §6` / `README.md` / `02-Decisions.md`，新踩坑写入 `Doc/Pitfalls.md §4.7–4.11`。

---

## 8. 一期不做（明确边界）

- 框架自绘的 DUI 对话框窗口（ADR-010 的「模态对话框」形态）、自定义按钮文案、复选框、超链接、页脚、进度条（TaskDialog 高级特性）。
- 文件夹多选、文件预览、收藏夹/最近访问定制、Shell 虚拟命名空间项（只返回文件系统路径）。
- 自己画「覆盖确认」框、自己创建/写文件、自动补扩展名之外的文件名校验。
- 对话框位置/尺寸自定义、父窗口之外的模态范围、异步 `Task<T>` 版本、编译期裁剪宏 `DUI_ENABLE_DIALOGS`（01-Layering §5 目前也只是文档草案）。
- 文件读写、拖拽、剪贴板联动。

---

## 9. 风险与备选

| 风险 | 影响 | 处理 |
| --- | --- | --- |
| **已修复（M7.1）**：初版沿用 WIC 的 MTA，实测点击文件类按钮后 `IFileDialog::Show` 死锁（进程无响应、无对话框窗口） | 三个文件类对话框全部不可用 | `Application::Run` 起始 `OleInitialize` 建 **STA**（ADR-027），`Platform/Dialog.cpp` 用 `CoGetApartmentType` 校验并放行 `APTTYPE_STA` / `APTTYPE_MAINSTA`；非 STA 返回 `Error` 而非挂界面。自动化点击复测四类弹窗均正常，15 个示例启动无回归（`Pitfalls §4.11`） |
| `OleInitialize` 建的是 main STA | 只判断 `APTTYPE_STA` 会误判 → 弹窗静默不出现 | 单元校验同时放行 `APTTYPE_MAINSTA`（§5.1） |
| `winuser.h` 的 `MessageBox` 宏 | 跨 TU 符号不一致 | §4.4 的 `#undef` + 兼容别名；列入 D1 验收 |
| 当前清单无 `longPathAware` | 超长路径（> MAX_PATH）可能拿不到 `SIGDN_FILESYSPATH` | 列已知限制；确需支持再单独加清单项（属全局 DPI 之外的改动） |
| 系统模态泵与主循环并存 | 动画/异步回投延后、焦点被清 | §6.1 / §6.2 明确约束并列入 D4 验收 |
| `CoCreateInstance` 需要 `ole32.lib` / CLSID 符号 | 链接错误 | `#pragma comment(lib, ...)` 随编译单元携带；优先 `__uuidof(FileOpenDialog)` / `__uuidof(FileSaveDialog)` 以免 `uuid.lib` 依赖，按实际链接结果补 |

---

## 10. 本次实际改动与已同步文件

- 新增：`DUI/App/Dialog.h/.cpp`、`DUI/Platform/Dialog.h/.cpp`、`Samples/Dialog/*`（`main.cpp` / `MacroCompat.cpp` / 工程文件）。
- 修改：`DUI/DUI.h`（聚合）、`DUI/App/App.h`（方法 + `dialogDepth_`）、`DUI/App/App.cpp`（`OnActivated` 抑制 + `OleInitialize` 建 STA）、`DUI/Platform/Dialog.cpp`（STA 请求 + 单元校验）、`DUI/DUI.vcxproj(.filters)`（登记 + `ObjectFileName`）、`DUI.slnx`。
- 文档已同步：`Doc/Dialogs.md`、`Doc/Arch/00-Overview.md` §5/§6、`Doc/Arch/02-Decisions.md`（ADR-027）、`README.md`、`Todo.md` §2.3/§3/§5、`Doc/Pitfalls.md §4.7–4.11`。
- **未改**：`DUI.props`（公共编译基线零改动）、`Graphics/ImageCodec.cpp`（WIC 的 MTA 尝试保留，在已 STA 的 UI 线程上返回 `RPC_E_CHANGED_MODE`，不影响 WIC）。
- D4 的人工验证项未完成时，不要改本文件状态为「全部验收通过」。
