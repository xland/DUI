// ============================================================================
//  CustomFrame/main.cpp — 自定义窗口边框与标题栏（无边框窗口 + DWM 阴影）
//
//  演示：
//    · frameless = true：无系统标题栏 / 边框，标题栏与边框全部自绘；
//    · 窗口阴影：DWM（Platform 层 DwmExtendFrameIntoClientArea + Win11 圆角）；
//    · 拖拽标题栏移动窗口：OnNcHitTest 命中标题栏 → HTCAPTION，由系统接管移动；
//    · 拖拽边框缩放窗口：OnNcHitTest 命中四边 / 四角 → HTTOP / HTLEFT /
//      HTRIGHT / HTBOTTOM / HTTOPLEFT / HTTOPRIGHT / HTBOTTOMLEFT /
//      HTBOTTOMRIGHT，由系统接管缩放；
//    · 标题栏按钮：框架内置矢量 Icon（最小化 / 最大化或还原 / 关闭）。
//
//  业务只报"命中区"，移动与缩放全由系统完成 —— 不必自己处理鼠标按下、移动、
//  抬起，也不必自己做窗口几何计算（12-Windowing §6）。
//
//  关键点：
//    · 缩放热区必须先于标题栏判定，否则整条上边（含左上 / 右上角）都被
//      HTCAPTION 吃掉，只能移动窗口、不能缩放。
//    · 标题栏右侧的**按钮区必须排除**在 HTCAPTION 热区之外 —— 命中标题栏
//      的点击会被系统当成"拖标题栏"，按钮收不到鼠标消息，永远点不动。
//      按钮区让出最上面 kResizeBorder 像素给上边 / 右上角缩放（原生窗口同理）。
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

// ---- 标题栏几何（DIP）----
constexpr float kTitleBarHeight = 36.f;
constexpr float kResizeBorder   = 6.f;   // 边框缩放热区厚度（四边通用，顶边压在标题栏上）
constexpr float kButtonWidth    = 46.f;  // 标题栏按钮宽度
constexpr float kIconSize       = 12.f;  // 矢量图标尺寸（Style().Width/Height）

// ---- 配色（0xAARRGGBB）----
constexpr dui::Color kTitleBarBg   = 0xFF2B2B33;
constexpr dui::Color kTitleText    = 0xFFE6E6EA;
constexpr dui::Color kIconColor    = 0xFFC9C9D1;
constexpr dui::Color kButtonHover  = 0x26FFFFFF;   // 15% 白
constexpr dui::Color kCloseHover   = 0xFFE81123;   // Windows 关闭按钮红
constexpr dui::Color kContentBg    = 0xFFF6F7F9;
constexpr dui::Color kTextPrimary  = 0xFF1F2937;
constexpr dui::Color kTextSecond   = 0xFF6B7280;

class CustomFrameWindow final : public dui::Window {
public:
    CustomFrameWindow()
        : dui::Window(dui::WindowOptions{
              .title     = L"DUI CustomFrame",
              .width     = 900.f,
              .height    = 560.f,
              .frameless = true,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Background(0xFFFFFFFF));

        // ---- 标题栏（自绘）----
        dui::Panel* bar = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .AlignItems(dui::Align::Center)      // 子项垂直居中（图标 / 标题）
            .Height(kTitleBarHeight)
            .FlexShrink(0.f)
            .Background(kTitleBarBg));

        bar->AddChild<dui::Text>(L"DUI · 自定义窗口边框与标题栏", dui::Style()
            .FontSize(13.f)
            .Color(kTitleText)
            .FlexShrink(0.f)
            .Padding(dui::Thickness(0.f, 14.f)));   // 左右留白（CSS 顺序：先上下后左右）

        bar->AddChild<dui::Panel>(dui::Style().FlexGrow(1.f));   // 弹性占位：按钮靠右

        // ---- 标题栏按钮（框架内置矢量 Icon）----
        dui::Panel* minBtn = MakeIconButton(bar, dui::IconName::Minimize, kButtonHover, nullptr);
        minBtn->OnClick(this, &CustomFrameWindow::OnMinimizeClick);

        dui::Panel* maxBtn = MakeIconButton(bar, dui::IconName::Maximize, kButtonHover, &maximizeIcon_);
        maxBtn->OnClick(this, &CustomFrameWindow::OnMaximizeClick);

        dui::Panel* closeBtn = MakeIconButton(bar, dui::IconName::Close, kCloseHover, nullptr);
        closeBtn->OnClick(this, &CustomFrameWindow::OnCloseClick);

        // ---- 内容区 ----
        dui::Panel* content = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Gap(12.f)
            .FlexGrow(1.f)
            .Padding(dui::Thickness(28.f, 32.f))
            .Background(kContentBg));

        content->AddChild<dui::Text>(L"自定义窗口边框与标题栏", dui::Style()
            .FontSize(22.f).Color(kTextPrimary).FlexShrink(0.f));
        content->AddChild<dui::Text>(
            L"· 拖拽标题栏空白处 → 移动窗口（OnNcHitTest 返回 HTCAPTION，系统接管移动）",
            dui::Style().FontSize(14.f).Color(kTextSecond).FlexShrink(0.f));
        content->AddChild<dui::Text>(
            L"· 拖拽窗口四边 / 四角 → 缩放窗口（返回 HTTOP / HTLEFT / HTRIGHT / HTBOTTOM 及四角命中码）",
            dui::Style().FontSize(14.f).Color(kTextSecond).FlexShrink(0.f));
        content->AddChild<dui::Text>(
            L"· 阴影由 DWM 提供（DwmExtendFrameIntoClientArea + Win11 圆角）",
            dui::Style().FontSize(14.f).Color(kTextSecond).FlexShrink(0.f));
        content->AddChild<dui::Text>(
            L"· 最大化时缩放热区自动失效；最大化 / 还原按钮图标随窗口状态切换",
            dui::Style().FontSize(14.f).Color(kTextSecond).FlexShrink(0.f));
    }

    // 尺寸 / 最大化状态变化 → 刷新最大化按钮的图标（最大化 ↔ 还原）
    void OnStateChanged() override {
        if (maximizeIcon_ == nullptr) return;
        maximizeIcon_->SetIcon(IsMaximized() ? dui::IconName::Restore : dui::IconName::Maximize);
    }

    // 非客户区命中：把"标题栏 / 边框热区"报给系统，由系统接管移动与缩放
    //   ptDip = 客户区 DIP 坐标；hit = 命中码（12-Windowing §6）
    bool OnNcHitTest(dui::Vec2 pt, dui::HitTestCode& hit) override {
        const dui::Vec2 size = ClientSizeDip();
        const float w = size.x;
        const float h = size.y;

        // ① 四边 / 四角 → HT*：拖动即缩放窗口（最大化时无边框可拖，禁用）。
        //    优先级最高：上边这条窄带压在标题栏上，先判缩放才能让"上边 /
        //    左上 / 右上"生效；按钮区因此要让出最上面 kResizeBorder 像素
        //    （Windows 原生窗口的标题栏顶端同样是一条 resize 带）。
        if (!IsMaximized()) {
            const bool left   = pt.x < kResizeBorder;
            const bool right  = pt.x > w - kResizeBorder;
            const bool top    = pt.y < kResizeBorder;
            const bool bottom = pt.y > h - kResizeBorder;
            if (top && left)     { hit = dui::HitTestCode::TopLeft;     return true; }
            if (top && right)    { hit = dui::HitTestCode::TopRight;    return true; }
            if (bottom && left)  { hit = dui::HitTestCode::BottomLeft;  return true; }
            if (bottom && right) { hit = dui::HitTestCode::BottomRight; return true; }
            if (top)    { hit = dui::HitTestCode::Top;    return true; }
            if (left)   { hit = dui::HitTestCode::Left;   return true; }
            if (right)  { hit = dui::HitTestCode::Right;  return true; }
            if (bottom) { hit = dui::HitTestCode::Bottom; return true; }
        }

        // ② 标题栏右侧按钮区 → 留作客户区：否则点击被系统当成"拖标题栏"，
        //    按钮收不到鼠标消息，永远点不动。
        if (pt.y < kTitleBarHeight && pt.x >= w - 3.f * kButtonWidth) {
            hit = dui::HitTestCode::Client;
            return true;
        }

        // ③ 标题栏其余 → HTCAPTION：拖动即移动窗口
        if (pt.y < kTitleBarHeight) {
            hit = dui::HitTestCode::Caption;
            return true;
        }

        // ④ 内容区一律 HTCLIENT：不让 DefWindowProc 因 WS_THICKFRAME 在窗口
        //    最外圈又报出一套 HT*（最大化时会误触缩放）。
        hit = dui::HitTestCode::Client;
        return true;
    }

private:
    // 标题栏图标按钮：Panel 底板（承载 hover 底色）+ 内部 dui::Icon（矢量自绘）
    // iconOut 非空时回传图标控件（最大化按钮要按窗口状态换图样）
    dui::Panel* MakeIconButton(dui::Panel* bar, dui::IconName name,
                                dui::Color hoverBg, dui::Icon** iconOut) {
        dui::Panel* btn = bar->AddChild<dui::Panel>(dui::Style()
            .Width(kButtonWidth)
            .Height(kTitleBarHeight)
            .FlexShrink(0.f)
            .AlignItems(dui::Align::Center)
            .JustifyContent(dui::Justify::Center)
            .Cursor(dui::Cursor::Hand));
        btn->When(dui::State::Hover, dui::Style().Background(hoverBg));
        dui::Icon* icon = btn->AddChild<dui::Icon>(dui::Style()
            .Width(kIconSize).Height(kIconSize)
            .Color(kIconColor));
        icon->SetIcon(name);
        if (iconOut != nullptr) *iconOut = icon;
        return btn;
    }

    // 点击沿祖先链冒泡（06-Events §5）：点在图标上（命中 Icon）也能触发按钮
    void OnMinimizeClick(dui::PointerEvent&)   { Minimize(); }
    void OnMaximizeClick(dui::PointerEvent&)   { MaximizeOrRestore(); }
    void OnCloseClick(dui::PointerEvent&)      { Close(); }

    dui::Icon* maximizeIcon_ = nullptr;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    CustomFrameWindow window;
    return app.Run(window);
}
