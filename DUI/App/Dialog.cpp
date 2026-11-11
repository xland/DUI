// ============================================================================
//  DUI/App/Dialog.cpp — Window 的对话框 / 菜单入口（17-Dialogs）
//
//  UI 层薄封装：owner HWND（私有通道）+ 对话框深度计数（dialogDepth_：宿主
//  失活不清编辑控件焦点，§6.2）。实现在 Core/Dialog.{h,cpp}、Core/Menu.cpp。
// ============================================================================

#include "App/App.h"

#include <utility>

#include "Core/Dialog.h"
#include "Core/Menu.h"

// winuser.h 的 #define MessageBox MessageBoxW 会在包含 Core/Dialog.h 后重新
// 出现；本 TU 必须看到 Window::MessageBox 真名（另见 17-Dialogs §4.4）。
#ifdef MessageBox
#undef MessageBox
#endif

namespace dui {

namespace {

const String kNoWindowError = L"窗口尚未创建（需在 Application::Run 之后调用）";

}  // namespace

DialogResult<std::vector<String>> Window::OpenFile(const OpenFileOptions& options) {
    void* owner = NativeHandle();
    if (owner == nullptr) return DialogResult<std::vector<String>>::Failed(kNoWindowError);

    ++dialogDepth_;
    DialogResult<std::vector<String>> r = PickFiles(owner, options);
    --dialogDepth_;
    return r;
}

DialogResult<String> Window::SaveFile(const SaveFileOptions& options) {
    void* owner = NativeHandle();
    if (owner == nullptr) return DialogResult<String>::Failed(kNoWindowError);

    ++dialogDepth_;
    const DialogResult<String> r = dui::SaveFile(owner, options);   // 成员名遮蔽，显式限定
    --dialogDepth_;
    return r;
}

DialogResult<String> Window::OpenFolder(const OpenFolderOptions& options) {
    void* owner = NativeHandle();
    if (owner == nullptr) return DialogResult<String>::Failed(kNoWindowError);

    ++dialogDepth_;
    const DialogResult<String> r = PickFolder(owner, options);
    --dialogDepth_;
    return r;
}

MessageResult Window::MessageBox(const MessageOptions& options) {
    void* owner = NativeHandle();
    if (owner == nullptr) return MessageResult::Failed;

    MessageOptions o = options;
    if (o.title == nullptr || *o.title == L'\0') {
        o.title = (options_.title != nullptr && *options_.title != L'\0')
                      ? options_.title : L"提示";
    }

    ++dialogDepth_;
    const MessageResult r = ShowMessageBox(owner, o);
    --dialogDepth_;
    return r;
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

// ---------------------------------------------------------------------------
// 系统原生弹出菜单（实现在 Core/Menu.cpp；这里的两件事属于 Window：
// owner 句柄 + 与对话框相同的 dialogDepth_ 失活抑制）
// ---------------------------------------------------------------------------

void Window::ShowMenu(const Menu& menu) {
    // 默认弹在当前鼠标位置；与客户区 DIP 重载走同一条路径
    POINT pt{};
    ::GetCursorPos(&pt);
    ::ScreenToClient(NativeHandle(), &pt);
    ShowMenu(menu, Vec2{pt.x / Scale(), pt.y / Scale()});
}

void Window::ShowMenu(const Menu& menu, Vec2 posDip) {
    const float s = Scale();
    // 菜单弹出期间宿主会收到 WM_ACTIVATE(WA_INACTIVE)，与系统对话框一样不算
    // 「切到别的应用」，抑制失活清焦点（17-Dialogs §6.2）
    ++dialogDepth_;
    ShowPopupMenu(menu, NativeHandle(), Vec2{posDip.x * s + 0.5f, posDip.y * s + 0.5f});
    --dialogDepth_;
}

}  // namespace dui
