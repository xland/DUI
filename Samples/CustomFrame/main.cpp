// ============================================================================
//  CustomFrame/main.cpp — 自定义窗口边框与标题栏（无边框窗口 + DWM 阴影）
//
//  演示：
//    · frameless = true：无系统标题栏 / 边框，标题栏与边框全部自绘；
//    · 窗口阴影：DWM（Platform 层 DwmExtendFrameIntoClientArea + Win11 圆角）；
//    · 拖拽标题栏移动窗口：标题栏设 app-region: drag → 命中报 HTCAPTION，
//      由系统接管移动；
//    · 拖拽边框缩放窗口：resizable = true → 框架自动生成四边 / 四角热区
//      （HTTOP / HTLEFT / HTRIGHT / HTBOTTOM / 四角命中码），由系统接管缩放；
//    · 标题栏按钮：框架内置矢量 Icon（最小化 / 最大化或还原 / 关闭），
//      按钮设 app-region: no-drag 从拖动区里"挖洞"，才能正常点击。
//
//  业务只声明"哪块是拖动区"，移动与缩放全由系统完成 —— 不必自己处理鼠标
//  按下、移动、抬起，也不必自己做窗口几何计算（12-Windowing §6）。
//
//  关键点：
//    · app-region 是**继承属性**：标题栏设 drag 后，其中的标题文字也算拖动区
//      （点文字也能拖窗）；子元素设 no-drag 即可挖洞恢复交互。
//    · 缩放热区**优先于**拖动区（框架固定此顺序），否则窗口上边那条窄带会被
//      拖动区整条吃掉，只剩"移动窗口"、没有"缩放窗口"。
//    · 缩放热区会盖住窗口最外圈几像素 —— 贴边的控件需自行内缩。
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

// ---- 标题栏几何（DIP）----
constexpr float kTitleBarHeight = 36.f;
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
              .resizable = true,   // 框架自动生成四边 / 四角缩放热区
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Background(0xFFFFFFFF));

        // ---- 标题栏（自绘）----
        // app-region: drag → 这块区域可拖动窗口；标题文字等子元素自动继承
        dui::Panel* bar = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .AlignItems(dui::Align::Center)      // 子项垂直居中（图标 / 标题）
            .Height(kTitleBarHeight)
            .FlexShrink(0.f)
            .Background(kTitleBarBg)
            .AppRegion(dui::AppRegion::Drag));

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
            L"· 拖拽标题栏空白处 → 移动窗口（标题栏 app-region: drag，系统接管移动）",
            dui::Style().FontSize(14.f).Color(kTextSecond).FlexShrink(0.f));
        content->AddChild<dui::Text>(
            L"· 拖拽窗口四边 / 四角 → 缩放窗口（resizable = true，框架自动生成缩放热区）",
            dui::Style().FontSize(14.f).Color(kTextSecond).FlexShrink(0.f));
        content->AddChild<dui::Text>(
            L"· 标题栏按钮设 app-region: no-drag，从拖动区挖洞 → 按钮可正常点击",
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

    // 注意：这里**不需要**再写 OnNcHitTest —— 标题栏的 app-region: drag 与
    // 窗口的 resizable = true 已经让框架接管了拖动与缩放（12-Windowing §6.2）。

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
            .Cursor(dui::Cursor::Hand)
            // no-drag：从标题栏的拖动区里挖洞 —— 否则点击会被系统当成
            // "拖标题栏"，按钮收不到鼠标消息，永远点不动
            .AppRegion(dui::AppRegion::NoDrag));
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

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::App app;
    CustomFrameWindow window;
    return app.Run(window);
}
