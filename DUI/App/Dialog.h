// ============================================================================
//  DUI/App/Dialog.h — 系统对话框公共 API（17-Dialogs）
//
//  owner HWND 以 void* 在层间传递，Win32/COM 细节全在
//  Platform/Dialog.cpp。结果三态（Ok / Cancelled / Error）与现有 Result<T> 分开：
//  Result<T> 无法区分「用户取消」与「真出错」。
//
//  注意：winuser.h 在 UNICODE 下定义 #define MessageBox MessageBoxW，会让
//  Window::MessageBox 的声明/调用在不同 TU 被替换成不同符号。这里主动取消该宏；
//  业务若要调 Win32 原生弹窗请用 ::MessageBoxW(...)。另见 17-Dialogs §4.4。
// ============================================================================

#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "Core/Core.h"

#ifdef MessageBox
#undef MessageBox
#endif

namespace dui {

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
    const wchar_t* title = nullptr;             // nullptr = 窗口标题；为空则 L"提示"
    MessageIcon    icon = MessageIcon::Warning;
    MessageButtons buttons = MessageButtons::Ok;
    int defaultButton = 1;                      // 1..4，越界钳到 1
};

}  // namespace dui
