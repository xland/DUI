// ============================================================================
//  DUI/App/Window.h — 应用窗口基类（公共 API；01-Layering §2 App；12-Windowing）
//
//  使用者继承 dui::Window，构造期只声明窗口形态（标题/尺寸/是否无边框），
//  不创建原生窗口；App::Run(window) 接管创建 → OnCreate → Show
//  → 消息循环 → 退出码。
//    · OnCreate：原生窗口已建、尚未显示；Root() 可用，构建过程不可见。
//    · 无边框/自定义标题栏 = 构造里设 frameless + OnCreate 里加标题栏
//      （12-Windowing §6.1）。
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <windows.h>      // 公共 API 直接使用 HWND 等 Win32 类型

// winuser.h 在 UNICODE 下把 MessageBox 定义成 MessageBoxW；公共头引入 Win32 头后
// 必须清掉该宏，否则 Window::MessageBox 声明会被替换成 MessageBoxW，与下面的
// 兼容别名重复。App/Dialog.h 末尾另有一处同样的兜底（应对业务先包含 windows.h）。
#ifdef MessageBox
#undef MessageBox
#endif

#include "App/Dialog.h"
#include "App/Menu.h"
#include "UI/UI.h"

namespace dui {

class App;

// 非客户区命中码（对应 Win32 的 HT*；公共头不含 windows.h，故在此自定义）
enum class HitTestCode : int {
    Client = 1,        // HTCLIENT：普通客户区
    Caption = 2,       // HTCAPTION：拖动可移动窗口
    Left = 10,         // HTLEFT：左边缘可缩放
    Right = 11,        // HTRIGHT
    Top = 12,          // HTTOP
    TopLeft = 13,      // HTTOPLEFT
    TopRight = 14,     // HTTOPRIGHT
    Bottom = 15,       // HTBOTTOM
    BottomLeft = 16,   // HTBOTTOMLEFT
    BottomRight = 17,  // HTBOTTOMRIGHT
};

struct DpiChanged {
    float oldScale = 1.f;
    float newScale = 1.f;
    Rect  suggestedDip;    // 系统建议的新窗口矩形（已换算 DIP，12-Windowing §4）
};

struct WindowOptions {
    const wchar_t* title = L"";
    float width = 800.f;         // DIP
    float height = 600.f;        // DIP
    // true → 无系统标题栏 / 边框，业务自绘标题栏（DWM 阴影 + 拖动移动 +
    // 拖边框缩放，12-Windowing §6.1）；默认 false：系统标题栏 + 边框。
    bool frameless = false;
    // true → 交换链按 PREMULTIPLIED 合成、清屏全透明，窗口背景可透视桌面
    // （DirectComposition 承载，ADR-010）。默认 false：IGNORE + 不透明清屏。
    bool transparent = false;
    // 初始显示时置于窗口所在显示器**工作区**正中（不含任务栏）。
    // 默认 true；设 false 沿用系统在 CW_USEDEFAULT 处的层叠摆放。
    bool center = true;
    // true → 无边框窗口可拖边框缩放：框架自动生成四边 / 四角缩放热区（等价
    // HTLEFT / HTTOP / 四角命中码，拖动由系统接管）。热区厚度取系统边框度量
    // （SM_CXSIZEFRAME + SM_CXPADDEDBORDER，按窗口 DPI 查询），与原生窗口同厚、
    // 随 DPI 自适应；最大化状态下自动失效。
    // 仅 frameless 有意义 —— 有边框窗口用系统自带缩放，框架不插手。
    // 默认 false（不生成热区）：无边框窗口的边缘像素仍归业务。
    bool resizable = false;
    // 无边框窗口双击拖动区（标题栏）→ 最大化 / 还原。
    // true  → 系统原生行为（DefWindowProc 见 WS_MAXIMIZEBOX 发 SC_MAXIMIZE）；
    // false → 框架吃掉 WM_NCLBUTTONDBLCLK，双击无反应。
    // 仅 frameless 有效（有边框窗口的系统标题栏本来就是这个行为，框架不碰）；
    // 仅对 app-region: drag 的区域有意义 —— 整窗 drag 的窗口（如圆形小挂件）
    // 双击任意位置都最大化往往不是想要的。默认 true（与原生窗口一致）。
    bool doubleClickMaximize = true;

    // ---- 窗口层级 / 任务栏 / 输入 / 截屏（创建期声明，业务不接触 Win32 细节）----
    // true → 置于所有非置顶窗口之上（HWND_TOPMOST）。运行期可用 SetTopmost 切换。
    bool topmost = false;
    // true → 不进任务栏 / Alt-Tab（WS_EX_TOOLWINDOW）。与 frameless 的
    // WS_EX_APPWINDOW 互斥：设此项时框架不再补 APPWINDOW。仅创建期有效。
    bool toolWindow = false;
    // true → 窗口不抢输入焦点（WS_EX_NOACTIVATE）。适合悬浮挂件 / 通知类窗口。
    // 注意：此类窗口不接受键盘输入，除非业务主动 SetForegroundWindow。仅创建期有效。
    bool noActivate = false;
    // true → 鼠标事件穿透到下层窗口（WS_EX_TRANSPARENT）。
    // 运行期可用 SetMouseThrough 切换。
    bool mouseThrough = false;
    // true → 阻止屏幕捕获 / 录制窗口内容（WDA_EXCLUDEFROMCAPTURE，Win10 2004+）。
    // 旧系统 API 返回失败，框架静默忽略，窗口其余行为不受影响。
    // 运行期可用 SetExcludeFromCapture 切换。
    bool excludeFromCapture = false;
};

class Window {
public:
    explicit Window(const WindowOptions& options = {});
    virtual ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 12-Windowing §9-3：scale = dpi / 96；窗口级（多窗口可各在不同 DPI 显示器）
    float Scale()   const noexcept { return scale_; }
    float DipToPx(float dip) const noexcept { return dip * scale_; }
    float PxToDip(float px)  const noexcept { return px / scale_; }

    // 窗口操作（无边框窗口的自绘标题栏按钮需要）
    void Minimize();              // 最小化
    void MaximizeOrRestore();     // 最大化 / 从最大化还原
    void Close();                 // 请求关闭（走 WM_CLOSE → OnClosing 可拦截）
    bool IsMaximized() const;     // 当前是否最大化

    // ---- 可见性与几何 ----
    void Show();
    void Hide();                  // 隐藏：框架停止为该窗口渲染（省 CPU）
    bool IsVisible() const noexcept;
    void Activate();              // 请求前台焦点（受 Windows 前台锁限制，可能失败）

    // 窗口矩形：整窗、**物理像素**、虚拟桌面全局坐标（与 Screen 的 Px 口径一致；
    // 多屏不同 DPI 时 DIP 全局坐标无统一坐标系，故这里不用 DIP）。
    Rect  BoundsPx() const;
    void  SetBoundsPx(Rect r);    // 移动 + 改尺寸（一次 SetWindowPos）
    void  MoveToPx(float xPx, float yPx);  // 只移动，尺寸不变
    void  Resize(float wDip, float hDip);  // 只改尺寸，位置不变（尺寸 DIP，与 Style 一致）
    void  Center();               // 移到所在屏工作区正中（尺寸不变）

    // 运行期切换（对应 WindowOptions 同名配置；native 未建时安全 no-op）
    void SetTopmost(bool on);              // 置顶 / 取消置顶
    void SetMouseThrough(bool on);         // 鼠标穿透 / 恢复接收
    void SetExcludeFromCapture(bool on);   // 防截屏 / 恢复（Win10 2004+ 生效）

    // 闪烁任务栏图标 / 标题栏（后台任务完成、消息到达时唤起注意）。
    // on=true 开始闪：count=0 持续到窗口获得焦点，count>0 闪 N 次；
    // intervalMs=0 用系统默认频率。on=false 立即停止。窗口销毁自动清除状态。
    void FlashFrame(bool on, unsigned count = 0, unsigned intervalMs = 0);

    // ---- 窗口定时器（绑定本窗口，随窗口销毁自动失效）----
    // 底层 ::SetTimer(hwnd, id, ms, ...)：WM_TIMER 进本窗口过程，HWND 销毁时
    // 系统自动撤销，回调不会打到已销毁窗口（App::SetTimer 无此保障）。
    // ms 夹紧到 >=1；返回 id（>1）传给 KillTimer 取消；每次调用分配新 id。
    // 要一次性触发就在 fn 里 KillTimer(自身 id)。回调在 UI 线程执行，勿做重活。
    // native 尚未建好（OnCreate 之前）时返回 0。
    unsigned SetTimer(unsigned ms, std::function<void()> fn);
    void     KillTimer(unsigned id) noexcept;

    // 客户区尺寸（DIP）：自绘边框热区判定用（OnNcHitTest）
    Vec2 ClientSizeDip() const;

    // 原生窗口句柄：OnCreate 起有效（此时 HWND 已建、窗口尚未显示）。
    HWND NativeHandle() const noexcept;

    // 本窗口所属的 App（AddWindow 起有效，堆对象地址稳定）。
    // 用途：OnClosing 里查 WindowCount() 判断是不是最后一个窗口再决定是否弹确认框。
    dui::App* App() noexcept { return app_; }

    // ---- 系统原生弹出菜单（App/Menu.h）------------------------------------
    // 同步模态：菜单弹出期间框架主消息循环挂起，返回后执行选中项回调。
    // 不传位置 → 弹在当前鼠标位置；posDip 为客户区 DIP 坐标。
    // 框架不自动挂钩右键：何时弹出由业务决定（如 OnPointerUp 里判断
    // ev.button == PointerButton::Right）。
    void ShowMenu(const Menu& menu);
    void ShowMenu(const Menu& menu, Vec2 posDip);

    // ---- 系统对话框（17-Dialogs）：同步模态，UI 线程、Run 建好 HWND 后调用 ----
    // 结果三态：IsOk / IsCancelled / IsError；只返回路径，不读写文件。
    // 调用期间系统接管消息泵，框架主循环挂起（Dispatcher 回投延后但不丢）。
    DialogResult<std::vector<String>> OpenFile  (const OpenFileOptions&   options = {});
    DialogResult<String>             SaveFile  (const SaveFileOptions&   options = {});
    DialogResult<String>             OpenFolder(const OpenFolderOptions& options = {});
    MessageResult                    MessageBox(const MessageOptions&    options);

    // Win32 同名宏（#define MessageBox MessageBoxW）兼容别名：仅当业务 TU 在
    // DUI.h 之后才 include windows.h 时被预处理命中；业务不要主动调用。
    MessageResult MessageBoxW(const MessageOptions& options);

    MessageResult Warn(const wchar_t* text, const wchar_t* title = nullptr);
    bool          Confirm(const wchar_t* text, const wchar_t* title = nullptr);

protected:
    // 原生窗口已创建、尚未显示：唯一搭界面的地方（构建期间窗口不可见）
    virtual void OnCreate() {}
    virtual void OnDpiChanged(const DpiChanged&) {}
    virtual bool OnClosing() { return true; }   // 返回 false 阻止关闭（M2）

    // 非客户区命中（仅 frameless 有意义）：ptDip = 客户区 DIP。
    // 返回 true → 采用 hit：Caption = 拖动可移动窗口；Left/Right/Top/Bottom/
    // 四个角 = 拖边框可缩放（由系统接管，业务不必自己处理鼠标与移动逻辑）。
    virtual bool OnNcHitTest(Vec2 ptDip, HitTestCode& hit) {
        (void)ptDip; (void)hit;
        return false;
    }
    // 窗口状态变化（尺寸改变 / 最大化还原）：业务可刷新自绘按钮的图标等
    virtual void OnStateChanged() {}

    // 文件拖放（M5.85）：paths = 完整路径，ptDip = 悬停/释放点客户区 DIP。
    // Enter/Over 返回 true 表示有元素接受（光标 COPY，否则 NONE）；默认走元素级
    // 路由（命中元素隧道+冒泡，控件可绑 OnDragOverFiles / 覆写
    // HandleDragOverFiles 显示插入预览）。重写可自己处理或拦下不派发。
    virtual bool OnDragEnterFiles(const std::vector<String>& paths, Vec2 ptDip);
    virtual bool OnDragOverFiles(const std::vector<String>& paths, Vec2 ptDip);
    virtual void OnDragLeaveFiles();
    virtual void OnDropFiles(const std::vector<String>& paths, Vec2 ptDip);

    Panel* Root() noexcept { return root_.get(); }

private:
    friend class dui::App;
    friend class WindowHostAdapter;    // App.cpp：平台事件 → 本类事件的适配器

    WindowOptions options_;
    float scale_ = 1.f;
    void* native_ = nullptr;           // NativeWindow*（App 层窗口壳对象）
    dui::App* app_ = nullptr;  // 所属 App（AddWindow 设置）
    std::unique_ptr<Panel> root_;
    uintptr_t tickTimer_ = 0;          // UI 统一节拍定时器（0 = 未启动）
    unsigned  tickPeriodMs_ = 0;       // 当前周期（变化时才重设 SetTimer）
    // 业务定时器：id ≥ 2（1 留给 tickTimer_）。HWND 销毁系统自动撤 timer，
    // 但 Window 对象可能先于 HWND 析构，故析构时主动停表并释放闭包。
    unsigned nextTimerId_ = 2;
    std::unordered_map<unsigned, std::function<void()>> timers_;
    // 系统对话框弹出期间 > 0：宿主失活不按「切到别的应用」清焦点（17-Dialogs §6.2）
    int       dialogDepth_ = 0;

    // UI 统一节拍（内部）：App 层只启停一个窗口定时器，需求与周期问 AnimationClock
    void SyncTickTimer();
    void OnTimer(uintptr_t id);
    // IME 插入点：把系统 caret（隐形）移到焦点控件的光标处 —— IME 依
    // GetCaretPos() 把候选窗口定位到光标下方。
    void SyncImeCaret();
};

}  // namespace dui
