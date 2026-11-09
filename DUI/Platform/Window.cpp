// ============================================================================
//  DUI/Platform/Window.cpp — RegisterClass + CreateWindowExW + 窗口过程
//
//  M2 范围（Todo）：建窗/销毁/OnClosing；WM_DPICHANGED 记录 scale +
//  应用系统建议矩形，完整七步流程（ResizeBuffers/字形重光栅）M5 落地。
// ============================================================================

#include "Platform/Window.h"

#include <cstring>
#include <dwmapi.h>

// DWM 是系统库（不在 MSVC 默认链接列表里）→ 随本编译单元携带链接指令，
// 不必改根目录 DUI.props 的公共编译基线。
#pragma comment(lib, "dwmapi.lib")

namespace dui::platform {

namespace {

constexpr wchar_t kClassName[] = L"DUI_Window_v1";

// frameless = false：系统标题栏 + 边框
constexpr DWORD kStyleSystem = WS_OVERLAPPEDWINDOW;
// frameless = true：无系统边框（标题栏/边框自绘）。
//   · WS_THICKFRAME 保留 → 配合 WM_NCHITTEST 返回 HT* 可由系统接管缩放；
//   · WS_POPUP 无标题栏 → 无系统标题栏可盖，客户区由 WM_NCCALCSIZE 扩到整窗；
//   · 保留 SYSMENU/MINIMIZEBOX/MAXIMIZEBOX → 任务栏按钮与 Alt+F4 正常。
constexpr DWORD kStyleBorderless = WS_POPUP | WS_THICKFRAME | WS_SYSMENU |
                                    WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
// WS_POPUP 默认不进任务栏 → 补 WS_EX_APPWINDOW
constexpr DWORD kExStyleBorderless = WS_EX_APPWINDOW;

// 无边框窗口的 DWM 阴影 / 圆角 / 去系统边框色
void ApplyDwmFrame(HWND hwnd) {
    // 把"边框"扩展给 DWM：负边距 = 四周各扩展 1px → DWM 在窗口外侧画投影。
    // （WS_POPUP 窗口本身没有系统阴影，靠这个扩展边框拿到。）
    MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    // Win11：圆角 + 去掉系统 1px 边框色（属性值在 Win10 SDK 无常量，手写）
    const DWORD kCornerPreference = 33;   // DWMWA_WINDOW_CORNER_PREFERENCE
    const int kRound = 2;                 // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, kCornerPreference, &kRound, sizeof(kRound));
    const DWORD kBorderColor = 34;        // DWMWA_BORDER_COLOR
    const COLORREF kColorNone = 0xFFFFFFFE;   // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(hwnd, kBorderColor, &kColorNone, sizeof(kColorNone));
}

// 客户区 DIP → 窗口物理矩形
//   系统边框窗口：AdjustWindowRectExForDpi 把系统标题栏/边框补进来，
//   客户区尺寸仍等于用户请求的 DIP 尺寸。
//   无边框窗口：客户区覆盖整窗（WM_NCCALCSIZE 处理），故**不做**边框补偿 ——
//   补了会让整窗比请求尺寸大出一圈（左右各 8px），业务按 DIP 布局就会错位。
RECT OuterRectForDip(float widthDip, float heightDip, UINT dpi, DWORD style, bool borderless) {
    RECT rc{
        0, 0,
        static_cast<LONG>(widthDip * (static_cast<float>(dpi) / 96.0f) + 0.5f),
        static_cast<LONG>(heightDip * (static_cast<float>(dpi) / 96.0f) + 0.5f),
    };
    if (borderless) return rc;
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);   // Win10 1607+，基线 1703（ADR-009）
    return rc;
}

Rect ToDip(const RECT& px, float scale) {
    return Rect{px.left / scale, px.top / scale,
                (px.right - px.left) / scale, (px.bottom - px.top) / scale};
}

}  // namespace

NativeWindow::~NativeWindow() {
    RemoveImeCaret();
    if (hwnd_) DestroyWindow(hwnd_);
}

bool NativeWindow::EnsureClassRegistered(String& error) {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &NativeWindow::WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));   // M2 占位：全黑；M3 换 D3D 清屏
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        error = L"RegisterClassExW failed (code " + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    registered = true;
    return true;
}

bool NativeWindow::Create(const wchar_t* title, float widthDip, float heightDip,
                          NativeWindowHost& host, String& error, bool borderless) {
    if (!EnsureClassRegistered(error)) return false;

    host_ = &host;
    borderless_ = borderless;

    const DWORD style = borderless ? kStyleBorderless : kStyleSystem;
    // 所有窗口统一不建系统重定向表面，内容由 DirectComposition 视觉树交给
    // DWM 合成（ADR-010）；WS_EX_APPWINDOW 仅用于让无边框窗口进任务栏。
    const DWORD exStyle = (borderless ? kExStyleBorderless : 0)
                          | WS_EX_NOREDIRECTIONBITMAP;

    // 先按 96dpi 占位尺寸创建（隐藏），拿到真实显示器 DPI 后再定最终尺寸，
    // 中间过程不可见（App.h OnCreate 契约：构建期间窗口不可见）。
    const RECT initial = OuterRectForDip(widthDip, heightDip, 96, style, borderless);
    hwnd_ = CreateWindowExW(exStyle, kClassName, title, style,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            initial.right - initial.left, initial.bottom - initial.top,
                            nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        error = L"CreateWindowExW failed (code " + std::to_wstring(GetLastError()) + L")";
        return false;
    }

    // PMv2 下即窗口所在显示器的 DPI（窗口级，12-Windowing §6）
    const UINT dpi = GetDpiForWindow(hwnd_);
    scale_ = static_cast<float>(dpi) / 96.0f;

    const RECT rc = OuterRectForDip(widthDip, heightDip, dpi, style, borderless);
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    // 无边框：阴影 / 圆角由 DWM 提供（WS_POPUP 没有系统阴影）
    if (borderless) ApplyDwmFrame(hwnd_);

    host.OnNativeCreated(hwnd_, scale_);
    return true;
}

void NativeWindow::Show() {
    if (hwnd_) ShowWindow(hwnd_, SW_SHOW);
}

void NativeWindow::CenterOnScreen() {
    if (hwnd_ == nullptr) return;
    RECT wr{};
    if (!GetWindowRect(hwnd_, &wr)) return;
    const HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return;
    const LONG w = wr.right - wr.left;
    const LONG h = wr.bottom - wr.top;
    const LONG x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    const LONG y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    SetWindowPos(hwnd_, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void NativeWindow::Minimize() {
    if (hwnd_) ::ShowWindow(hwnd_, SW_MINIMIZE);
}

void NativeWindow::MaximizeOrRestore() {
    if (hwnd_) ::ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
}

void NativeWindow::RequestClose() {
    if (hwnd_) ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

bool NativeWindow::IsMaximized() const {
    return hwnd_ != nullptr && ::IsZoomed(hwnd_) != FALSE;
}

Vec2 NativeWindow::ClientSizePx() const {
    RECT rc{};
    if (hwnd_ != nullptr) GetClientRect(hwnd_, &rc);
    return {static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top)};
}

Vec2 NativeWindow::ClientSizeDip() const {
    const Vec2 px = ClientSizePx();
    return {px.x / scale_, px.y / scale_};
}

bool NativeWindow::IsMinimized() const noexcept {
    return hwnd_ != nullptr && ::IsIconic(hwnd_) != FALSE;
}

Vec2 NativeWindow::DipFromLParam(LPARAM lp) const noexcept {
    const float x = static_cast<short>(LOWORD(lp));
    const float y = static_cast<short>(HIWORD(lp));
    return {x / scale_, y / scale_};
}

void NativeWindow::StartTimer(uintptr_t id, unsigned ms) {
    if (hwnd_ == nullptr) return;
    ::SetTimer(hwnd_, static_cast<UINT_PTR>(id), ms, nullptr);
}

void NativeWindow::StopTimer(uintptr_t id) {
    if (hwnd_ == nullptr) return;
    ::KillTimer(hwnd_, static_cast<UINT_PTR>(id));
}

// ---------------------------------------------------------------------------
// IME 插入点（系统 caret）
//
// IME 依 `GetCaretPos()`（系统 caret 位置）确定候选窗口该出现在哪 —— 这是
// Win32 的标准机制，且对现代 TSF 输入法同样有效（无 TSF 上下文时走 IMM
// 兼容路径，仍读 caret）。
//
// 我们自绘光标，故另建一个**隐形**系统 caret：1×N 单色位图（全 0 = 完全
// 透明），只用于向 IME 报告插入点，不会出现双光标。
//
// 高度 = 光标所在行「行顶 → 基线」：IME 依 caret 的**底边**摆放候选窗口；
// caret 只有 1px 高（零高度插入点）时，输入法会把候选窗整体下移一整行，
// 表现为候选窗口离光标很远、压在下方面板上。
//
// 注意：不要在 IME 消息处理期间调用 ImmGetContext/ImmReleaseContext —— 那会
// 重建输入上下文，打断正在进行的组合（表现为"中文打不出来"）。
// ---------------------------------------------------------------------------

void NativeWindow::EnsureImeCaret() {
    if (caretBmp_ != nullptr) return;
    const int h = caretHeight_ > 0 ? caretHeight_ : 1;
    // 1×h 单色位图：像素 0 = 透明（画背景色），非 0 = 前景色 → 全 0 即不可见
    HDC mem = CreateCompatibleDC(nullptr);
    if (mem == nullptr) return;
    caretBmp_ = CreateBitmap(1, h, 1, 1, nullptr);
    if (caretBmp_ != nullptr) {
        HGDIOBJ old = SelectObject(mem, caretBmp_);
        SetPixel(mem, 0, 0, RGB(0, 0, 0));   // 索引 0 = 透明
        GdiFlush();
        SelectObject(mem, old);
    }
    DeleteDC(mem);
    if (caretBmp_ == nullptr) return;
    ::CreateCaret(hwnd_, caretBmp_, 1, h);
}

void NativeWindow::UpdateImeCaretPos(int pxX, int pxY, int pxHeight) {
    if (pxHeight < 1) pxHeight = 1;
    if (caretBmp_ != nullptr && pxHeight != caretHeight_) {
        // 高度变了 → 位图与 caret 都要重建（位图尺寸决定 caret 高度）
        ::DestroyCaret();
        DeleteObject(caretBmp_);
        caretBmp_ = nullptr;
        caretHeight_ = 0;
    }
    caretHeight_ = pxHeight;
    EnsureImeCaret();
    ::SetCaretPos(pxX, pxY);   // 客户区物理像素；无输入焦点时失败（下次事件会补）
}

void NativeWindow::RemoveImeCaret() {
    if (caretBmp_ == nullptr) return;
    ::DestroyCaret();
    DeleteObject(caretBmp_);
    caretBmp_ = nullptr;
    caretHeight_ = 0;
}

void NativeWindow::SetCursorShape(LPCWSTR idc) const {
    ::SetCursor(::LoadCursorW(nullptr, idc));
}

LRESULT CALLBACK NativeWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // GWLP_USERDATA 存 NativeWindow*（而非 dui::Window*：Platform 不依赖 App/UI）
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    } else if (auto* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        switch (msg) {
        case WM_CLOSE:
            if (!self->host_->OnCloseRequest()) return 0;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            ValidateRect(hwnd, nullptr);   // 绘制由渲染循环按需全量完成（10-Rendering D2）
            return 0;
        case WM_ERASEBKGND:
            return 1;                       // 抑制闪烁：每帧全窗重绘
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) self->host_->OnResized();
            return 0;
        case WM_ACTIVATE:
            // 窗口激活状态：失活（切到别的应用）→ 上层让编辑控件失焦，光标停闪
            self->host_->OnActivated(LOWORD(wp) != WA_INACTIVE);
            return 0;
        case WM_NCCALCSIZE: {
            // 无边框窗口：客户区覆盖整个窗口矩形（不做系统边框扣减）。
            //   WM_NCCALCSIZE(wParam=TRUE) 的 rgrc[0] **入参是系统建议的新窗口
            //   矩形**，出参才是客户区矩形；rgrc[1] 是**上一次**的窗口矩形。
            //   因此"无边框"只需原样保留 rgrc[0] 并返回 0（不调用 DefWindowProc
            //   去扣边框）—— 绝不能写 rgrc[0] = rgrc[1]：那会把客户区设成旧尺寸，
            //   最小化后还原时 rgrc[1] 正是离屏的图标矩形（-32000,…，199×34），
            //   客户区被压成 199×34 → 内容画不进去，窗口一片空白。
            if (wp && self->borderless_) {
                return 0;   // 原样保留 rgrc[0] → 客户区 = 整个窗口矩形
            }
            break;
        }
        case WM_GETMINMAXINFO: {
            // 无边框窗口最大化：系统默认把窗口放到比工作区还大（边框外扩），
            // 底边/右边被屏幕切掉 → 手动约束到工作区（最大化后内容正好铺满）。
            if (self->borderless_) {
                const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{sizeof(mi)};
                if (GetMonitorInfoW(mon, &mi)) {
                    auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                    // ptMaxPosition 相对显示器原点；ptMaxSize = 工作区尺寸
                    mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                    mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                    mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                    mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                }
                return 0;
            }
            break;
        }
        case WM_NCRBUTTONUP: {
            // 无边框窗口：右键标题栏 → 弹系统菜单（还原 / 移动 / 大小 / 最小化 /
            // 最大化 / 关闭）。WS_POPUP 没有系统标题栏，DefWindowProc 不知道该
            // 在哪弹菜单，必须自己来。wParam 就是 WM_NCHITTEST 报的命中码。
            if (wp == HTCAPTION && self->borderless_) {
                const int sx = static_cast<int>(static_cast<short>(LOWORD(lp)));
                const int sy = static_cast<int>(static_cast<short>(HIWORD(lp)));
                if (HMENU menu = GetSystemMenu(hwnd, FALSE)) {
                    // TPM_RETURNCMD：拿菜单项 ID 自己转发成 WM_SYSCOMMAND，
                    // 不依赖系统菜单的自动派发；SC_* 由 DefWindowProc 执行
                    // （移动 / 缩放是系统模态循环，最小化 / 关闭走 WM_CLOSE 等）。
                    const int cmd = static_cast<int>(TrackPopupMenuEx(
                        menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                        sx, sy, hwnd, nullptr));
                    if (cmd != 0) SendMessageW(hwnd, WM_SYSCOMMAND, static_cast<WPARAM>(cmd), 0);
                }
                return 0;
            }
            break;   // 系统边框窗口交回 DefWindowProc（它本来就会弹系统菜单）
        }
        case WM_NCHITTEST: {
            // 非客户区命中：上层把"标题栏 / 边框热区"报给系统，由系统接管
            // 拖标题栏移动窗口、拖边框缩放 —— 业务不必自己处理鼠标与移动逻辑。
            // lParam 是屏幕坐标 → 客户区 DIP。
            const int sx = static_cast<int>(static_cast<short>(LOWORD(lp)));
            const int sy = static_cast<int>(static_cast<short>(HIWORD(lp)));
            POINT sp{sx, sy};
            ScreenToClient(hwnd, &sp);
            const float s = self->scale_;
            int hit = HTCLIENT;
            if (self->host_->OnNcHitTest(Vec2{sp.x / s, sp.y / s}, hit)) return hit;
            break;   // 未命中热区 → 默认命中测试
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            self->host_->OnPointerMove(self->DipFromLParam(lp));
            return 0;
        }
        case WM_SETCURSOR: {
            // 指针形态：系统每次鼠标移动后都会发 WM_SETCURSOR 并把光标重置为
            // 箭头 —— 要显示 I-beam 必须在消息里设置。
            // 注意：lParam 里的坐标不可靠（实测取到的是负值 → 命中测试失败），
            // 一律用 GetCursorPos 取屏幕坐标再换算到客户区。
            POINT sp{};
            ::GetCursorPos(&sp);
            ::ScreenToClient(hwnd, &sp);
            const float s = self->scale_;
            if (self->host_->OnSetCursor(Vec2{sp.x / s, sp.y / s})) return TRUE;
            break;
        }
        case WM_MOUSELEAVE:
            self->host_->OnPointerLeave();
            return 0;
        case WM_MOUSEWHEEL: {
            // 滚轮坐标是屏幕坐标 → 客户区 DIP
            POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
            ScreenToClient(hwnd, &pt);
            const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            const bool horizontal = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            self->host_->OnMouseWheel(Vec2{pt.x / self->scale_, pt.y / self->scale_}, delta, horizontal);
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);               // 窗口外抬起也能收到（06-Events R6 的最小形态）
            self->host_->OnPointerButton(self->DipFromLParam(lp), PointerButton::Left, true);
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            self->host_->OnPointerButton(self->DipFromLParam(lp), PointerButton::Left, false);
            return 0;
        case WM_RBUTTONUP:
            // 右键抬起交给业务决定是否弹菜单（框架不自动挂钩 WM_CONTEXTMENU）
            self->host_->OnPointerButton(self->DipFromLParam(lp), PointerButton::Right, false);
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            // 修饰键状态取自键盘（VK_* 高位 = 按下）
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            self->host_->OnKeyDown(static_cast<int>(wp), ctrl, shift, alt);
            if (msg == WM_SYSKEYDOWN) return DefWindowProcW(hwnd, msg, wp, lp);
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            self->host_->OnKeyUp(static_cast<int>(wp), ctrl, shift, alt);
            if (msg == WM_SYSKEYUP) return DefWindowProcW(hwnd, msg, wp, lp);
            return 0;
        }
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_IME_CHAR: {
            const wchar_t ch = static_cast<wchar_t>(wp);
            if (ch != 0) self->host_->OnChar(ch);
            // WM_IME_CHAR 必须吃掉：否则 DefWindowProc 会再生成一个 WM_CHAR，
            // 同一个字符被插入两次（IME 提交）。
            return 0;
        }
        case WM_APP:
            return 0;   // Dispatcher 唤醒消息：只需唤醒泵，排空在主循环 [调度] 步
        case WM_TIMER:
            self->host_->OnTimer(static_cast<uintptr_t>(wp));
            return 0;
        case WM_DPICHANGED: {
            const float newScale = static_cast<float>(HIWORD(wp)) / 96.0f;
            if (newScale == self->scale_) return 0;      // 幂等（12-Windowing §7.2-2）
            const Vec2 dipBefore = self->ClientSizeDip();
            const auto* suggested = reinterpret_cast<const RECT*>(lp);
            // 系统建议矩形直接采用，不自算尺寸，否则窗口会跳变（§4-2）
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            self->scale_ = newScale;
            const Vec2 dipAfter = self->ClientSizeDip();
            const bool dipChanged = dipBefore.x != dipAfter.x || dipBefore.y != dipAfter.y;
            self->host_->OnDpiChanged(newScale, ToDip(*suggested, newScale), dipChanged);
            return 0;
        }
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 剪贴板（CF_UNICODETEXT）
// ---------------------------------------------------------------------------

bool GetClipboardText(String& out) {
    out.clear();
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

bool SetClipboardText(const String& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem != nullptr) {
        void* p = GlobalLock(mem);
        if (p != nullptr) {
            std::memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);   // 未交给系统 → 自行释放
    }
    CloseClipboard();
    return ok;
}

}  // namespace dui::platform
