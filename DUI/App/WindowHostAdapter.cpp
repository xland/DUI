// ============================================================================
//  DUI/App/WindowHostAdapter.cpp — 平台事件 → dui::Window 事件的桥接实现
// ============================================================================

#include "App/WindowHostAdapter.h"

#include "Graphics/Graphics.h"
#include "Graphics/Device.h"
#include "App/NativeWindow.h"
#include "UI/Scroller.h"

#include <cstdio>

namespace dui {

namespace {

// 光标形态 → Windows 预定义光标资源（映射放在 App 层：Platform 不依赖 UI 层）
LPCWSTR ToWinCursor(dui::Cursor c) {
    switch (c) {
    case dui::Cursor::IBeam:      return IDC_IBEAM;
    case dui::Cursor::Hand:       return IDC_HAND;
    case dui::Cursor::Wait:       return IDC_WAIT;
    case dui::Cursor::Crosshair:  return IDC_CROSS;
    case dui::Cursor::Move:       return IDC_SIZEALL;
    case dui::Cursor::SizeAll:    return IDC_SIZEALL;
    case dui::Cursor::SizeWE:     return IDC_SIZEWE;
    case dui::Cursor::SizeNS:     return IDC_SIZENS;
    case dui::Cursor::NotAllowed: return IDC_NO;
    case dui::Cursor::Help:       return IDC_HELP;
    default:                      return IDC_ARROW;
    }
}

}  // namespace

WindowHostAdapter::WindowHostAdapter(App& app, Window& window, const NativeWindow& native,
                                     graphics::Device& device)
    : app_(app), window_(window), native_(native), device_(device) {}

bool WindowHostAdapter::OnWindowDestroyed() {
    return app_.OnWindowDestroyed(this);
}

void WindowHostAdapter::SetFramePending(bool* flag) noexcept {
    framePending_ = flag;
}

void WindowHostAdapter::SetDeviceReady() noexcept {
    deviceReady_ = true;
}

void WindowHostAdapter::OnNativeCreated(HWND, float initialScale) {
    window_.scale_ = initialScale;
    std::printf("[DUI] window created, scale = %.2f\n", initialScale);
}

bool WindowHostAdapter::OnCloseRequest() {
    return window_.OnClosing();
}

void WindowHostAdapter::OnDpiScaleChanging(float newScale) {
    pendingOldScale_ = window_.scale_;   // 旧值留档（OnDpiChanged 要用）
    window_.scale_ = newScale;
}

void WindowHostAdapter::OnDpiChanged(float newScale, const Rect& suggestedDip,
                                     bool clientDipChanged) {
    const float oldScale = pendingOldScale_ != 0.f ? pendingOldScale_ : window_.scale_;
    pendingOldScale_ = 0.f;
    window_.scale_ = newScale;
    // §4-5 分级失效：DIP 尺寸不变 → 只重绘不重排；字形按新 pixelSize 重光栅
    // （图集按 (字体, 字形, 像素尺寸) 缓存，新档位按需生成，旧档位 LRU 淘汰）
    std::printf("[DUI] DPI changed: %.2f -> %.2f (%s)\n", oldScale, newScale,
                clientDipChanged ? "layout invalidated" : "repaint only");
    if (clientDipChanged) window_.Root()->InvalidateLayout();
    else window_.Root()->InvalidatePaint();
    window_.OnDpiChanged(DpiChanged{oldScale, newScale, suggestedDip});
    // §4-7：重算鼠标当前位置（物理 → DIP），避免 hover 状态错位
    const HWND hwnd = static_cast<HWND>(window_.NativeHandle());
    POINT pt{};
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    Element* hit = HitTest(window_.Root(), Vec2{pt.x / newScale, pt.y / newScale});
    UpdateHoverChain(hover_, hit);
    hover_ = hit;
    RequestFrame();
}

// ---- 指针输入（M3 最小形态；归一化事件与完整路由在 M4）----

void WindowHostAdapter::OnPointerMove(Vec2 pt) {
    if (dragging_ != nullptr) {          // 滑块拖拽中：只更新滑块位置
        dragging_->UpdateBarDrag(pt);
        RequestFrame();
        return;
    }
    if (Element* cap = CapturedElement()) {
        // 捕获期间：不命中测试、不更新悬停链，Move 直达捕获者（仍从其开始冒泡）
        if (hover_ != nullptr) {
            UpdateHoverChain(hover_, nullptr);
            DispatchHoverTransitions(hover_, nullptr, pt);
            hover_ = nullptr;
        }
        lastPt_ = pt;
        DispatchPointerMove(cap, ClientToLocal(cap, pt));
        RequestFrame();
        return;
    }
    // 命中点换算到命中元素所在单元空间（滚动容器内含滚动偏移）——
    // 控件据此把 ev.pos 与自己的 Bounds 相减求局部坐标
    Vec2 local{};
    Element* hit = HitTest(window_.Root(), pt, &local);
    UpdateHoverChain(hover_, hit);
    DispatchHoverTransitions(hover_, hit, local);   // 移入/移出（悬停链差分）
    hover_ = hit;
    lastPt_ = pt;
    DispatchPointerMove(hit, local);                 // 移动（冒泡）
    window_.SyncImeCaret();                       // 拖选移动光标 → 同步 IME 插入点
    RequestFrame();
}

void WindowHostAdapter::OnPointerButton(Vec2 pt, PointerButton button, bool down) {
    if (down) {
        if (button != PointerButton::Left) {
            // 非左键按下（目前只有右键）：**只派发元素级 PointerDown** ——
            // 不动 pressed 链 / 焦点 / 指针捕获 / IME 插入点：
            //   · Pressed 视觉态与焦点是左键语义（右键按下不该让按钮亮起、更不该抢焦点）
            //   · 覆盖 pressed_ 会让左键抬起时 Pressed 卡住、Click 合成失效
            //   · 窗口捕获服务于左键"拖出窗口外仍能收到抬起"，右键不做拖拽
            Vec2 local{};
            Element* hit = HitTest(window_.Root(), pt, &local);
            DispatchPointerDown(hit, local, button);
            RequestFrame();
            return;
        }
        // 命中滚动条 → 进入滑块拖拽（不参与 Click 合成，也不派发元素事件）
        Vec2 local{};
        Element* hit = HitTest(window_.Root(), pt, &local);
        Scroller* sc = FindScroller(hit);
        bool vertical = false;
        if (sc != nullptr && sc->HitScrollBar(pt, vertical)) {
            sc->BeginBarDrag(vertical, pt);
            dragging_ = sc;
            dragVertical_ = vertical;
            RequestFrame();
            return;
        }
        pressPt_ = pt;
        pressed_ = hit;
        SetPressedChain(hit, true);
        // 点击取得焦点（命中可聚焦元素或其可聚焦祖先；06-Events §6）；
        // 没命中可聚焦元素（点窗口空白 / 点非可聚焦元素）→ **清焦点**（浏览器语义）：
        // 编辑控件随之失焦 → 选区消失、光标停闪。否则选区会留在一个"看起来仍聚焦着"
        // 的控件上（用户已不打算继续输入）；而看不见的选区会让后续 Delete/输入
        // 替换掉用户看不见的内容（与 TextBox 失焦清选区同一条理由）。
        if (Element* f = FindFocusable(hit)) SetFocus(f);
        else SetFocus(nullptr);
        DispatchPointerDown(hit, local, button);      // 按下（冒泡）
    } else if (button != PointerButton::Left) {
        // 非左键抬起：只派发元素级弹起，不动 pressed / 焦点 / Click 状态机
        Vec2 local{};
        Element* hit = HitTest(window_.Root(), pt, &local);
        DispatchPointerUp(hit, local, button);
    } else {
        if (dragging_ != nullptr) {
            dragging_->EndBarDrag();
            dragging_ = nullptr;
            RequestFrame();
            return;
        }
        Vec2 local{};
        Element* cap = CapturedElement();
        Element* hit = nullptr;
        if (cap != nullptr) {
            hit = cap;
            local = ClientToLocal(cap, pt);   // 捕获者空间（未必是命中元素）
        } else {
            hit = HitTest(window_.Root(), pt, &local);
        }
        SetPressedChain(pressed_, false);
        DispatchPointerUp(hit, local, button);           // 弹起（冒泡）
        if (cap != nullptr) {
            SetCaptureElement(nullptr);            // 抬起自动释放捕获（拖拽不合成 Click）
            Vec2 curLocal{};
            Element* cur = HitTest(window_.Root(), pt, &curLocal);
            UpdateHoverChain(hover_, cur);         // 恢复悬停链到当前指针位置
            DispatchHoverTransitions(hover_, cur, curLocal);
            hover_ = cur;
        } else {
            // Click 合成：按下抬起同元素且拖出在阈值内（06-Events §3：4 dip）
            constexpr float kDragThreshold = 4.f;
            if (pressed_ != nullptr && hit == pressed_ &&
                pt.x >= pressPt_.x - kDragThreshold && pt.x <= pressPt_.x + kDragThreshold &&
                pt.y >= pressPt_.y - kDragThreshold && pt.y <= pressPt_.y + kDragThreshold) {
                DispatchClick(pressed_, local, PointerButton::Left);
            }
        }
        pressed_ = nullptr;
    }
    window_.SyncImeCaret();   // 点击定位光标 → 同步 IME 插入点
    RequestFrame();
}

// 滚轮：先派发元素级 Wheel（冒泡，handled 可拦截）；无人处理时由滚动容器消费
// （沿祖先链找最近的"该轴还可滚"的滚动容器：内层滚不动 → 冒泡外层；
//   纵向滚轮，Shift + 滚轮横向）
void WindowHostAdapter::OnMouseWheel(Vec2 pt, float delta, bool horizontal) {
    Vec2 local{};
    Element* hit = HitTest(window_.Root(), pt, &local);
    if (DispatchWheel(hit, local, delta, horizontal)) {   // 已被元素处理
        RequestFrame();
        return;
    }
    Scroller* sc = nullptr;
    for (Element* p = hit; p != nullptr; p = p->Parent()) {
        if (p->GetKind() != Kind::Scroller) continue;
        Scroller* s = static_cast<Scroller*>(p);
        const float max = horizontal ? s->MaxScrollX() : s->MaxScrollY();
        if (max > 0.f) { sc = s; break; }
    }
    if (sc == nullptr) return;
    constexpr float kWheelStep = 48.f;   // 每格滚动量（DIP）
    if (horizontal) sc->ScrollBy(-delta * kWheelStep, 0.f);
    else            sc->ScrollBy(0.f, -delta * kWheelStep);
    RequestFrame();
}

// 拖放（M5.85）：转给 Window 的虚钩子（默认实现走元素级路由）
bool WindowHostAdapter::OnDragEnterFiles(const std::vector<String>& paths, Vec2 ptDip) {
    return window_.OnDragEnterFiles(paths, ptDip);
}

bool WindowHostAdapter::OnDragOverFiles(const std::vector<String>& paths, Vec2 ptDip) {
    return window_.OnDragOverFiles(paths, ptDip);
}

void WindowHostAdapter::OnDragLeaveFiles() {
    window_.OnDragLeaveFiles();
}

void WindowHostAdapter::OnDropFiles(const std::vector<String>& paths, Vec2 ptDip) {
    window_.OnDropFiles(paths, ptDip);
}

void WindowHostAdapter::OnPointerLeave() {
    UpdateHoverChain(hover_, nullptr);
    DispatchHoverTransitions(hover_, nullptr, lastPt_);   // 全部移出
    hover_ = nullptr;
    RequestFrame();
}

// ---- 键盘输入（M5.9）：先到焦点元素，再沿祖先链冒泡 ----

void WindowHostAdapter::OnKeyDown(int key, bool ctrl, bool shift, bool alt) {
    Element* target = FocusedElement();
    bool handled = false;
    if (target != nullptr) handled = DispatchKeyDown(target, key, ctrl, shift, alt);
    // 默认行为：Tab / Shift+Tab 切换焦点（控件未消费时）
    if (!handled && key == VK_TAB) {
        FocusNext(window_.Root(), shift);
        RequestFrame();
    }
    window_.SyncImeCaret();   // 光标可能已移动 → 同步 IME 插入点
}

void WindowHostAdapter::OnKeyUp(int key, bool ctrl, bool shift, bool alt) {
    Element* target = FocusedElement();
    if (target != nullptr) DispatchKeyUp(target, key, ctrl, shift, alt);
}

void WindowHostAdapter::OnChar(wchar_t ch) {
    Element* target = FocusedElement();
    if (target != nullptr) DispatchChar(target, ch);
    window_.SyncImeCaret();   // 输入后光标右移 → 同步 IME 插入点
}

// 非客户区命中（WM_NCHITTEST）：框架默认值 → 交给业务覆盖。
//   框架默认（仅无边框窗口）：① 缩放热区（resizable）② app-region 拖动区
//                              ③ 客户区
//   业务返回 true  → 采用 hit（业务可先改写框架默认值再返回）
//   业务返回 false → 采用框架默认值
// 有边框窗口：框架不介入（缩放由系统自带），业务不接管则交回 DefWindowProc。
bool WindowHostAdapter::OnNcHitTest(Vec2 pt, int& hit) {
    HitTestCode code = HitTestCode::Client;
    if (window_.options_.frameless) code = DefaultNcHitTest(pt);
    if (window_.OnNcHitTest(pt, code)) {
        hit = static_cast<int>(code);
        return true;
    }
    if (!window_.options_.frameless) return false;   // 有边框：交回系统默认
    hit = static_cast<int>(code);
    return true;
}

// 指针形态：命中链就近者胜 —— 元素指定了 cursor 就用它；没指定则沿链向上取
// 父级的（与 CSS 的 cursor 继承同效）；都没指定再看是否为编辑控件（I-beam）。
bool WindowHostAdapter::OnSetCursor(Vec2 pt) {
    Element* hit = HitTest(window_.Root(), pt);
    for (Element* e = hit; e != nullptr; e = e->Parent()) {
        StyleValue v;
        if (TryResolveElement(e, Prop::Cursor, v)) {
            native_.SetCursorShape(ToWinCursor(static_cast<dui::Cursor>(v.u)));
            return true;
        }
        if (e->WantsTextCursor()) {
            native_.SetCursorShape(IDC_IBEAM);
            return true;
        }
    }
    return false;   // 默认箭头
}

// 定时器节拍（统一 ticker）：转发到窗口，由 AnimationClock 分发
void WindowHostAdapter::OnTimer(uintptr_t id) {
    window_.OnTimer(id);
}

void WindowHostAdapter::OnResized() {
    window_.Root()->InvalidateLayout();   // 客户区尺寸变化 → 重排
    // 拖拽边框期间 Windows 在模态循环里连续派发 WM_SIZE，主消息循环被挂起，
    // framePending 要等拖拽结束才被消费 —— 帧必须就地渲染，内容才能实时变化。
    // 但建窗时的 SetWindowPos 也同步派发 WM_SIZE，那时 Device 尚未 Init，
    // 就地渲染会崩；置脏交给随后正常帧路径（AddWindow 已设 framePending）。
    if (deviceReady_) {
        App::RenderFrame(window_, native_, device_);
        window_.OnStateChanged();        // 尺寸 / 最大化状态变化（业务自绘按钮用）
    }
}

// 窗口激活状态变化：失活 → 焦点离开窗口（编辑控件失焦，光标停止闪烁）。
// 延迟一轮判定：最小化也会发 WM_ACTIVATE(WA_INACTIVE)，但最小化应保留焦点
// （还原后可直接继续输入）—— 下一轮 WM_SIZE 已到达，IsIconic 才是准的。
void WindowHostAdapter::OnActivated(bool active) {
    if (active) {
        pendingDeactivate_ = false;
        return;
    }
    // 系统对话框弹出期间宿主失活不是「切到别的应用」：不清编辑控件焦点
    // （17-Dialogs §6.2；dialogDepth_ 由 App/Dialog.cpp 在调用前后维护）。
    if (window_.dialogDepth_ > 0) return;
    pendingDeactivate_ = true;
}

bool WindowHostAdapter::TakeDeactivate() {
    if (!pendingDeactivate_) return false;
    pendingDeactivate_ = false;
    return !native_.IsMinimized();
}

void WindowHostAdapter::ClearPointerStateFor(Element* dying) {
    // p 指向 dying 子树中的节点？从 p 沿 Parent 链上溯是否碰到 dying。
    auto inDyingTree = [dying](const Element* p) noexcept {
        for (; p != nullptr; p = p->Parent()) {
            if (p == dying) return true;
        }
        return false;
    };
    if (inDyingTree(hover_))    hover_ = nullptr;
    if (inDyingTree(pressed_))  pressed_ = nullptr;
    if (inDyingTree(dragging_)) {
        dragging_->EndBarDrag();   // 结束滑块拖拽态（此刻元素还活着）
        dragging_ = nullptr;
    }
}

void WindowHostAdapter::RequestFrame() noexcept {
    if (framePending_ != nullptr) *framePending_ = true;
}

// 框架默认的非客户区命中（仅无边框窗口有效）：
//   ① 缩放热区（resizable 且未最大化）→ HT* 缩放码
//   ② app-region 拖动区（元素命中 + 继承链）→ HTCAPTION
//   ③ 其余 → HTCLIENT
// 顺序固定：**缩放热区优先于拖动区** —— 否则窗口上边那条窄带会被拖动区
// 整条吃掉，只剩"移动窗口"、没有"缩放窗口"（12-Windowing §6.2）。
HitTestCode WindowHostAdapter::DefaultNcHitTest(Vec2 pt) const {
    // ① 缩放热区：厚度取系统边框度量，与原生窗口同厚、随 DPI 自适应
    if (window_.options_.resizable && !window_.IsMaximized()) {
        const float b = ResizeBorderDip();
        if (b > 0.f) {
            const Vec2 size = window_.ClientSizeDip();
            const bool left   = pt.x < b;
            const bool right  = pt.x > size.x - b;
            const bool top    = pt.y < b;
            const bool bottom = pt.y > size.y - b;
            if (top && left)     return HitTestCode::TopLeft;
            if (top && right)    return HitTestCode::TopRight;
            if (bottom && left)  return HitTestCode::BottomLeft;
            if (bottom && right) return HitTestCode::BottomRight;
            if (top)    return HitTestCode::Top;
            if (left)   return HitTestCode::Left;
            if (right)  return HitTestCode::Right;
            if (bottom) return HitTestCode::Bottom;
        }
    }
    // ② app-region：命中元素 → 沿父链取最近设置者（继承属性，StyleResolve）
    //    Drag → 拖动窗口；NoDrag / None → 普通客户区
    Element* hit = HitTest(window_.Root(), pt);
    if (hit != nullptr) {
        const StyleValue v = ResolveProperty(hit, Prop::AppRegion);
        if (v.u == static_cast<uint32_t>(AppRegion::Drag)) return HitTestCode::Caption;
    }
    return HitTestCode::Client;
}

// 缩放热区厚度（DIP）：系统边框度量（SM_CXSIZEFRAME + SM_CXPADDEDBORDER，
// 物理像素，按窗口 DPI 查询）÷ 缩放系数。与原生窗口的可缩放边框同厚。
float WindowHostAdapter::ResizeBorderDip() const {
    const float scale = native_.Scale();
    if (scale <= 0.f) return 0.f;
    const UINT dpi = static_cast<UINT>(scale * 96.f + 0.5f);
    const int frame = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi);
    const int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    return static_cast<float>(frame + pad) / scale;
}

}  // namespace dui
