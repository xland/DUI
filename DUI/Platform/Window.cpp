// ============================================================================
//  DUI/Platform/Window.cpp — RegisterClass + CreateWindowExW + 窗口过程
//
//  M2 范围（Todo）：建窗/销毁/OnClosing；WM_DPICHANGED 记录 scale +
//  应用系统建议矩形，完整七步流程（ResizeBuffers/字形重光栅）M5 落地。
// ============================================================================

#include "Platform/Window.h"

namespace dui::platform {

namespace {

constexpr wchar_t kClassName[] = L"DUI_Window_v1";

constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;   // Chrome::System；Borderless 为 M5

// 客户区 DIP → 含非客户区的整窗物理矩形
RECT OuterRectForDip(float widthDip, float heightDip, UINT dpi) {
    const float s = static_cast<float>(dpi) / 96.0f;
    RECT rc{
        0, 0,
        static_cast<LONG>(widthDip * s + 0.5f),
        static_cast<LONG>(heightDip * s + 0.5f),
    };
    AdjustWindowRectExForDpi(&rc, kStyle, FALSE, 0, dpi);   // Win10 1607+，基线 1703（ADR-009）
    return rc;
}

Rect ToDip(const RECT& px, float scale) {
    return Rect{px.left / scale, px.top / scale,
                (px.right - px.left) / scale, (px.bottom - px.top) / scale};
}

}  // namespace

NativeWindow::~NativeWindow() {
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
                          NativeWindowHost& host, String& error) {
    if (!EnsureClassRegistered(error)) return false;

    host_ = &host;

    // 先按 96dpi 占位尺寸创建（隐藏），拿到真实显示器 DPI 后再定最终尺寸，
    // 中间过程不可见（App.h OnCreate 契约：构建期间窗口不可见）。
    const RECT initial = OuterRectForDip(widthDip, heightDip, 96);
    hwnd_ = CreateWindowExW(0, kClassName, title, kStyle,
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

    const RECT rc = OuterRectForDip(widthDip, heightDip, dpi);
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    host.OnNativeCreated(hwnd_, scale_);
    return true;
}

void NativeWindow::Show() {
    if (hwnd_) ShowWindow(hwnd_, SW_SHOW);
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

Vec2 NativeWindow::DipFromLParam(LPARAM lp) const noexcept {
    const float x = static_cast<short>(LOWORD(lp));
    const float y = static_cast<short>(HIWORD(lp));
    return {x / scale_, y / scale_};
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
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            self->host_->OnPointerMove(self->DipFromLParam(lp));
            return 0;
        }
        case WM_MOUSELEAVE:
            self->host_->OnPointerLeave();
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);               // 窗口外抬起也能收到（06-Events R6 的最小形态）
            self->host_->OnPointerButton(self->DipFromLParam(lp), true);
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            self->host_->OnPointerButton(self->DipFromLParam(lp), false);
            return 0;
        case WM_APP:
            return 0;   // Dispatcher 唤醒消息：只需唤醒泵，排空在主循环 [调度] 步
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

}  // namespace dui::platform
