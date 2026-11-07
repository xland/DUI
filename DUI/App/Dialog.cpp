// ============================================================================
//  DUI/App/Dialog.cpp — Window 的四个系统对话框入口（17-Dialogs）
//
//  App 层组装：取 owner HWND（私有通道）、公共 span/枚举 → 平台 POD 转换、
//  三态封装；调用期间维护 dialogDepth_，让宿主失活不清编辑控件焦点（§6.2）。
// ============================================================================

#include "App/App.h"

#include "Platform/Dialog.h"

#include <utility>
#include <vector>

// winuser.h 的 #define MessageBox MessageBoxW 会在包含 Platform/Dialog.h 后
// 重新出现；本 TU 必须看到 Window::MessageBox 真名（另见 17-Dialogs §4.4）。
#ifdef MessageBox
#undef MessageBox
#endif

namespace dui {

namespace {

std::vector<platform::NativeFileFilter> ToNativeFilters(
    std::span<const FileTypeFilter> filters) {
    std::vector<platform::NativeFileFilter> out;
    out.reserve(filters.size());
    for (const FileTypeFilter& f : filters) out.push_back({f.name, f.pattern});
    return out;
}

platform::MsgIcon ToNativeIcon(MessageIcon icon) {
    switch (icon) {
    case MessageIcon::Information: return platform::MsgIcon::Information;
    case MessageIcon::Warning:     return platform::MsgIcon::Warning;
    case MessageIcon::Error:       return platform::MsgIcon::Error;
    case MessageIcon::Question:    return platform::MsgIcon::Question;
    case MessageIcon::None:        return platform::MsgIcon::None;
    default:                       return platform::MsgIcon::None;
    }
}

platform::MsgButtons ToNativeButtons(MessageButtons buttons) {
    switch (buttons) {
    case MessageButtons::Ok:                  return platform::MsgButtons::Ok;
    case MessageButtons::OkCancel:            return platform::MsgButtons::OkCancel;
    case MessageButtons::YesNo:               return platform::MsgButtons::YesNo;
    case MessageButtons::YesNoCancel:         return platform::MsgButtons::YesNoCancel;
    case MessageButtons::RetryCancel:         return platform::MsgButtons::RetryCancel;
    case MessageButtons::AbortRetryIgnore:    return platform::MsgButtons::AbortRetryIgnore;
    case MessageButtons::CancelRetryContinue: return platform::MsgButtons::CancelRetryContinue;
    default:                                  return platform::MsgButtons::Ok;
    }
}

MessageResult FromNative(platform::NativeMessageResult r) {
    switch (r) {
    case platform::NativeMessageResult::Ok:        return MessageResult::Ok;
    case platform::NativeMessageResult::Cancel:    return MessageResult::Cancel;
    case platform::NativeMessageResult::Yes:       return MessageResult::Yes;
    case platform::NativeMessageResult::No:        return MessageResult::No;
    case platform::NativeMessageResult::Retry:     return MessageResult::Retry;
    case platform::NativeMessageResult::Abort:     return MessageResult::Abort;
    case platform::NativeMessageResult::Ignore:    return MessageResult::Ignore;
    case platform::NativeMessageResult::TryAgain:  return MessageResult::TryAgain;
    case platform::NativeMessageResult::Continue:  return MessageResult::Continue;
    case platform::NativeMessageResult::Failed:    return MessageResult::Failed;
    default:                                      return MessageResult::Failed;
    }
}

const String kNoWindowError = L"窗口尚未创建（需在 Application::Run 之后调用）";

}  // namespace

DialogResult<std::vector<String>> Window::OpenFile(const OpenFileOptions& options) {
    void* owner = NativeHandle();
    if (owner == nullptr) return DialogResult<std::vector<String>>::Failed(kNoWindowError);

    const std::vector<platform::NativeFileFilter> filters = ToNativeFilters(options.filters);
    std::vector<String> paths;
    bool cancelled = false;
    String error;

    ++dialogDepth_;
    const bool ok = platform::PickFiles(
        owner, options.title, options.initialPath,
        filters.empty() ? nullptr : filters.data(), filters.size(), options.filterIndex,
        options.multiSelect, paths, cancelled, error);
    --dialogDepth_;

    if (ok) return DialogResult<std::vector<String>>::Succeeded(std::move(paths));
    if (cancelled) return DialogResult<std::vector<String>>::Cancelled();
    return DialogResult<std::vector<String>>::Failed(std::move(error));
}

DialogResult<String> Window::SaveFile(const SaveFileOptions& options) {
    void* owner = NativeHandle();
    if (owner == nullptr) return DialogResult<String>::Failed(kNoWindowError);

    const std::vector<platform::NativeFileFilter> filters = ToNativeFilters(options.filters);
    String path;
    bool cancelled = false;
    String error;

    ++dialogDepth_;
    const bool ok = platform::SaveFile(
        owner, options.title, options.initialPath,
        filters.empty() ? nullptr : filters.data(), filters.size(), options.filterIndex,
        options.defaultExtension, path, cancelled, error);
    --dialogDepth_;

    if (ok) return DialogResult<String>::Succeeded(std::move(path));
    if (cancelled) return DialogResult<String>::Cancelled();
    return DialogResult<String>::Failed(std::move(error));
}

DialogResult<String> Window::OpenFolder(const OpenFolderOptions& options) {
    void* owner = NativeHandle();
    if (owner == nullptr) return DialogResult<String>::Failed(kNoWindowError);

    String path;
    bool cancelled = false;
    String error;

    ++dialogDepth_;
    const bool ok = platform::PickFolder(owner, options.title, options.initialPath,
                                         path, cancelled, error);
    --dialogDepth_;

    if (ok) return DialogResult<String>::Succeeded(std::move(path));
    if (cancelled) return DialogResult<String>::Cancelled();
    return DialogResult<String>::Failed(std::move(error));
}

MessageResult Window::MessageBox(const MessageOptions& options) {
    void* owner = NativeHandle();
    if (owner == nullptr) return MessageResult::Failed;

    const wchar_t* caption = options.title;
    if (caption == nullptr || *caption == L'\0') {
        caption = (options_.title != nullptr && *options_.title != L'\0')
                      ? options_.title : L"提示";
    }

    ++dialogDepth_;
    const platform::NativeMessageResult r = platform::ShowMessageBox(
        owner, options.text != nullptr ? options.text : L"", caption,
        ToNativeIcon(options.icon), ToNativeButtons(options.buttons),
        options.defaultButton);
    --dialogDepth_;
    return FromNative(r);
}

// Win32 同名宏兼容别名（业务不要主动调用）：某些 TU 在 DUI.h 之后才包含
// windows.h，宏重新生效时 window.MessageBox(...) 会被替换成本方法。
MessageResult Window::MessageBoxW(const MessageOptions& options) {
    return this->MessageBox(options);
}

MessageResult Window::Warn(const wchar_t* text, const wchar_t* title) {
    MessageOptions o;
    o.text = text;
    o.title = title;
    o.icon = MessageIcon::Warning;
    o.buttons = MessageButtons::Ok;
    return MessageBox(o);
}

bool Window::Confirm(const wchar_t* text, const wchar_t* title) {
    MessageOptions o;
    o.text = text;
    o.title = title;
    o.icon = MessageIcon::Question;
    o.buttons = MessageButtons::YesNo;
    return MessageBox(o) == MessageResult::Yes;
}

}  // namespace dui
