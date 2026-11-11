// ============================================================================
//  DUI/App/Dialog.h — 系统对话框（应用层服务，17-Dialogs）
//
//  窗口模态的应用级交互：公共类型（选项 / 结果）与 Win32/COM 实现同在此头
//  与同名 cpp；Window 成员方法（OpenFile / MessageBox …）是薄封装（owner 句柄、
//  对话框深度计数），业务也可直接调用自由函数。
//
//  owner 为 HWND，以 void* 传递（本头不含窗口对象类型）；nullptr 直接返回
//  失败语义的结果。
//
//  注意：winuser.h 在 UNICODE 下定义 #define MessageBox MessageBoxW，本头
//  末尾主动取消该宏；业务若要调 Win32 原生弹窗请用 ::MessageBoxW(...)。
//  另见 17-Dialogs §4.4 与 Pitfalls §4.7。
// ============================================================================
#pragma once

#include <windows.h>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "Core/Core.h"

#ifdef MessageBox
#undef MessageBox
#endif

namespace dui {

// ---------------------------------------------------------------------------
// 公共类型（与 UI 无关；17-Dialogs §4）
// ---------------------------------------------------------------------------

enum class DialogStatus : uint8_t { Ok, Cancelled, Error };

// 默认构造 = Error（无消息），与 Core::Result<T> 的「默认失败」风格一致。
template <class T>
class [[nodiscard]] DialogResult {
public:
    DialogResult() = default;

    static DialogResult Succeeded(T value) {
        DialogResult r;
        r.status_ = DialogStatus::Ok;
        r.value_  = std::move(value);
        return r;
    }
    static DialogResult Cancelled() {
        DialogResult r;
        r.status_ = DialogStatus::Cancelled;
        return r;
    }
    static DialogResult Failed(String error) {
        DialogResult r;
        r.status_ = DialogStatus::Error;
        r.error_  = std::move(error);
        return r;
    }

    DialogStatus Status()      const noexcept { return status_; }
    bool IsOk()                const noexcept { return status_ == DialogStatus::Ok; }
    bool IsCancelled()         const noexcept { return status_ == DialogStatus::Cancelled; }
    bool IsError()             const noexcept { return status_ == DialogStatus::Error; }

    const T&      Value()      const noexcept { return value_; }
    const String& Error()      const noexcept { return error_; }

private:
    DialogStatus status_ = DialogStatus::Error;
    T            value_{};
    String       error_;
};

struct FileTypeFilter {
    const wchar_t* name;      // 显示名，如 L"文本文件"
    const wchar_t* pattern;   // 如 L"*.txt;*.md"；多项用 ';' 分隔
};

// C++20：过滤器用 span 表达，不再需要单独传 filterCount。
// span 是视图，底层数组必须活到调用返回（同步 API，调用点同作用域即可）。
struct OpenFileOptions {
    const wchar_t* title = nullptr;                  // nullptr = 系统默认「打开」
    const wchar_t* initialPath = nullptr;            // 目录或完整文件路径
    std::span<const FileTypeFilter> filters = {};    // 空 = 所有文件 *.*
    size_t filterIndex = 0;                          // 0-based；越界钳到 0
    bool multiSelect = false;
};

struct SaveFileOptions {
    const wchar_t* title = nullptr;                  // nullptr = 系统默认「另存为」
    const wchar_t* initialPath = nullptr;            // 目录或建议的完整文件名
    std::span<const FileTypeFilter> filters = {};
    size_t filterIndex = 0;
    const wchar_t* defaultExtension = nullptr;       // L"txt"（不带点）
};

struct OpenFolderOptions {
    const wchar_t* title = nullptr;
    const wchar_t* initialPath = nullptr;
};

enum class MessageIcon : uint8_t { None, Information, Warning, Error, Question };
enum class MessageButtons : uint8_t { Ok, OkCancel, YesNo, YesNoCancel, RetryCancel,
                                      AbortRetryIgnore, CancelRetryContinue };
enum class MessageResult : uint8_t { Failed, Ok, Cancel, Yes, No, Retry, Abort, Ignore,
                                     TryAgain, Continue };

struct MessageOptions {
    const wchar_t* text = L"";                  // 正文，支持 L"\n"
    const wchar_t* title = nullptr;             // nullptr = L"提示"
    MessageIcon    icon = MessageIcon::Warning;
    MessageButtons buttons = MessageButtons::Ok;
    int defaultButton = 1;                      // 1..4，越界钳到 1
};

// ---------------------------------------------------------------------------
// 服务函数（自由函数；业务可直接调用，Window 成员方法是薄封装）
// ---------------------------------------------------------------------------

DialogResult<std::vector<String>> PickFiles (void* owner, const OpenFileOptions& options);
DialogResult<String>              SaveFile (void* owner, const SaveFileOptions& options);
DialogResult<String>              PickFolder(void* owner, const OpenFolderOptions& options);
MessageResult ShowMessageBox(void* owner, const MessageOptions& options);

}  // namespace dui

// 本头包含 windows.h，宏在末尾重新清理一次（头文件相互包含顺序不定）
#ifdef MessageBox
#undef MessageBox
#endif
