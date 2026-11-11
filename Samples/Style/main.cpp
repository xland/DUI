// ============================================================================
//  Style/main.cpp — 样式能力演示
//
//  覆盖能力（03-Style / 15-Properties）：样式复用与派生（Style::Derive）、
//  继承属性沿树下传、状态变体 When(State::Hover, …)、
//  级联优先级（Style → 本地 Set → 状态变体）、
//  视觉属性集（Background / Color / CornerRadius / Opacity / 字号字重 /
//  LineHeight / Padding / Cursor）、四角独立圆角（CornerRadii）、
//  边框（BorderWidth / BorderColor，实线内描边）、分边边框与虚线（BorderWidth(Thickness)
//  / BorderStyle）、BoxSizing: BorderBox（边框占盒内空间）、Margin（外边距盒，不塌陷）、
//  TextAlign（内容盒内对齐 + 继承）、运行时 Set 覆盖（Thickness / 字体族指针）、
//  Shadow（box-shadow 子集：圆角矩形投影，Shape 批 SDF 衰减带，无纹理/离屏）、
//  FontStyle（斜体，继承属性）、TextDecoration（下划线 / 删除线，位标志，不继承）、
//  Overflow（默认溢出可见，Hidden 显式截断）、WordSpacing（词距，继承）、
//  TextOverflow: Ellipsis（单行省略号截断）。
//
//  布局相关示例见 Samples/Layout；定位相关示例见 Samples/Position。
//
//  构建产物（Build 布局，DUI.slnx 根目录相对）：
//    中间产物  Build\Temp\Style\Debug|Release\
//    最终产物  Build\Style\Debug|Release\Style.exe
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

// 可复用的静态样式：派生样式持它的裸指针（parent_），故必须是静态存储
// （03-Style §2）。样式里也可以写布局属性（FlexGrow 等），与视觉属性同层。
static const dui::Style kCard = dui::Style()
    .Height(52.f)
    .FlexGrow(1.f)
    .Padding(10.f)
    .CornerRadius(6.f)
    .Color(0xFFFFFFFF)
    .FontSize(14.f)
    .JustifyContent(dui::Justify::Center)
    .AlignItems(dui::Align::Center);

// 分区小标题
void NewCaption(dui::Panel* parent, const wchar_t* text) {
    parent->AddChild<dui::Text>(text, dui::Style()
        .FontSize(12.f)
        .Color(0xFF777777));
}

class StyleWindow final : public dui::Window {
public:
    StyleWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"Style · 样式能力演示",
              .width  = 900.f,
              .height = 1100.f,
          }) {}

protected:
    void OnCreate() override {
        // 内容自然高（约 1280 DIP）超过窗口高 → 必须放进 Scroller：
        // 若直接挂在根 Column 容器上，flex-shrink 会把每个块压扁（内容高被压缩，
        // 而文字不裁剪 → 文字溢出块外；CSS 同款行为，但观感不对）。
        dui::Panel* host = Root();
        host->SetStyle(dui::Style().Background(0xFFF3F3F3));
        dui::Scroller* scroll = host->AddChild<dui::Scroller>(dui::Style()
            .FlexGrow(1.f));   // 视口高度由剩余空间决定（内容超出则在视口内滚动）
        dui::Panel* root = scroll->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Padding(16.f)
            .Gap(10.f)
            .Color(0xFF222222)
            .FontSize(13.f));

        root->AddChild<dui::Text>(L"样式能力演示", dui::Style()
            .FontSize(18.f)
            .FontWeight(dui::FontWeight::SemiBold)
            .Color(0xFF333333));

        // ---- 复用与派生：派生只存差异，解析沿 parent_ 链回溯 ------------------
        NewCaption(root, L"复用与派生：一份静态 kCard + Style::Derive 只写差异");
        dui::Panel* cardRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        dui::Panel* cardA = cardRow->AddChild<dui::Panel>(dui::Style::Derive(kCard)
            .Background(0xFF4472C4));
        cardA->AddChild<dui::Text>(L"派生 A：只加蓝底");
        dui::Panel* cardB = cardRow->AddChild<dui::Panel>(dui::Style::Derive(kCard)
            .Background(0xFF70AD47).CornerRadius(18.f));
        cardB->AddChild<dui::Text>(L"派生 B：绿底 + 圆角覆盖");
        dui::Panel* cardC = cardRow->AddChild<dui::Panel>(dui::Style::Derive(kCard)
            .Background(0xFF7030A0).FontSize(18.f));
        cardC->AddChild<dui::Text>(L"派生 C：紫底 + 字号覆盖");

        // ---- 继承属性：父计算值沿树下传，子级可显式覆盖 ----------------------
        NewCaption(root, L"继承属性：FontSize / FontWeight / Color / FontFamily 沿树下传");
        dui::Panel* inheritBox = root->AddChild<dui::Panel>(dui::Style()
            .FontSize(16.f)
            .FontWeight(dui::FontWeight::SemiBold)
            .Color(0xFF2D7FF9)
            .FontFamily(L"SimSun")
            .Height(68.f)
            .Padding(8.f)
            .Gap(4.f)
            .Background(0xFFFFFFFF)
            .CornerRadius(4.f));
        inheritBox->AddChild<dui::Text>(L"子 Text 未设样式 → 继承 16 号半粗 / 蓝色 / 宋体");
        inheritBox->AddChild<dui::Text>(L"子 Text 显式覆盖字号与颜色", dui::Style()
            .FontSize(12.f)
            .Color(0xFF777777));

        // ---- 状态变体 + 光标形态 ---------------------------------------------
        NewCaption(root, L"When(State::Hover)：悬停换背景；Cursor 声明指针形态");
        dui::Panel* hover = root->AddChild<dui::Panel>(dui::Style()
            .Height(48.f)
            .Background(0xFF4472C4)
            .CornerRadius(6.f)
            .Color(0xFFFFFFFF)
            .Cursor(dui::Cursor::Hand)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        hover->When(dui::State::Hover, dui::Style()
            .Background(0xFFED7D31)
            .Color(0xFFFFFFFF));
        hover->AddChild<dui::Text>(L"悬停我：变体换橙底，指针为手型");

        // ---- 级联优先级：层 2 状态变体 > 层 3 本地覆盖 > 层 4 Style ----------
        NewCaption(root, L"级联顺序：静止看 Set（层 3），悬停看 When（层 2，优先级更高）");
        dui::Panel* cascade = root->AddChild<dui::Panel>(dui::Style()
            .Height(48.f)
            .Background(0xFFE7E6E6)
            .CornerRadius(4.f)
            .Color(0xFF222222)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        cascade->When(dui::State::Hover, dui::Style()
            .Background(0xFFED7D31)
            .Color(0xFFFFFFFF));
        cascade->Set(dui::Prop::BackgroundColor, 0xFF4472C4);
        cascade->AddChild<dui::Text>(L"静止：Set 覆盖的蓝底 ／ 悬停：When 变体的橙底");

        // ---- 视觉属性集 -------------------------------------------------------
        NewCaption(root, L"视觉属性：Background / Opacity / CornerRadius / 字号字重 / LineHeight / Padding");
        dui::Panel* visualRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        dui::Panel* visualA = visualRow->AddChild<dui::Panel>(dui::Style()
            .Width(280.f)                              // 高度随内容（不写死 → 18 号 1.6 行高两行也放得下）
            .Background(0xFF70AD47)
            .CornerRadius(14.f)
            .Opacity(0.55f)
            .Padding(10.f)
            .Color(0xFFFFFFFF));
        visualA->AddChild<dui::Text>(L"圆角 14 + 背景透明度 0.55 + Padding 10");
        dui::Panel* visualB = visualRow->AddChild<dui::Panel>(dui::Style()
            .Width(280.f)
            .Background(0xFFFFFFFF)
            .CornerRadius(4.f)
            .Padding(10.f));
        visualB->AddChild<dui::Text>(L"18 号粗体 · 行高 1.6\n第二行仍按 1.6 倍行距", dui::Style()
            .FontSize(18.f)
            .FontWeight(dui::FontWeight::Bold)
            .LineHeight(1.6f));

        // ---- Overflow：默认溢出可见（与 CSS 一致），Hidden 显式截断 -------------
        NewCaption(root, L"Overflow：默认溢出可见（同 CSS）；Hidden 显式把内容裁到元素盒");
        dui::Panel* clipBox = root->AddChild<dui::Panel>(dui::Style()
            .Width(320.f).Height(58.f)
            .Background(0xFFE7E6E6)
            .CornerRadius(4.f)
            .Padding(10.f)
            .Overflow(dui::Overflow::Hidden));
        clipBox->AddChild<dui::Text>(L"Overflow: Hidden —— 这行 18 号字超出了盒子，被裁掉了",
                                     dui::Style().FontSize(18.f));

        // ---- Margin：外边距盒参与父布局占位（不塌陷，与 gap 叠加） ------------
        NewCaption(root, L"Margin：外边距盒参与占位，不塌陷、与 gap 叠加");
        dui::Panel* marginRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f)
            .AlignItems(dui::Align::Center)
            .Height(88.f)
            .Background(0xFFE7E6E6)
            .CornerRadius(4.f));
        const wchar_t* marginLabels[] = {L"无 margin", L"margin 12", L"左右各 24"};
        const dui::Color marginColors[] = {0xFF4472C4, 0xFF70AD47, 0xFFED7D31};
        for (int i = 0; i < 3; ++i) {
            dui::Style box = dui::Style()
                .Width(140.f).Height(48.f)
                .Background(marginColors[i]).CornerRadius(4.f).Color(0xFFFFFFFF)
                .JustifyContent(dui::Justify::Center).AlignItems(dui::Align::Center);
            if (i == 1) box.Margin(12.f);                         // 四边同值
            if (i == 2) box.Margin(dui::Thickness(0.f, 24.f));     // CSS 顺序：上下 0 / 左右 24
            dui::Panel* cell = marginRow->AddChild<dui::Panel>(box);
            cell->AddChild<dui::Text>(marginLabels[i]);
        }

        // ---- TextAlign：内容盒内文本对齐（继承属性） ---------------------------
        NewCaption(root, L"TextAlign：Start / Center / End，以及父级设置后的继承");
        dui::Panel* alignRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        const wchar_t* alignLabels[] = {L"Start", L"Center", L"End", L"End（继承）"};
        const dui::TextAlign aligns[] = {dui::TextAlign::Start, dui::TextAlign::Center,
                                         dui::TextAlign::End};
        for (int i = 0; i < 4; ++i) {
            dui::Style cellStyle = dui::Style()
                .Width(200.f).Height(52.f)
                .Background(0xFFFFFFFF).CornerRadius(4.f)
                .Padding(8.f)
                // 容器默认 Column → 主轴 = 垂直：justify-content: center 把文本块
                // 居中于内容盒。align-items 保持默认 Stretch（子 Text 撑满内容盒
                // 宽度），TextAlign 才能在盒内做水平定位（End/继承两格靠它）。
                .JustifyContent(dui::Justify::Center);
            if (i == 3) cellStyle.TextAlign(dui::TextAlign::End);   // 设在容器 → 子 Text 继承
            dui::Panel* cell = alignRow->AddChild<dui::Panel>(cellStyle);
            if (i < 3) cell->AddChild<dui::Text>(alignLabels[i], dui::Style().TextAlign(aligns[i]));
            else       cell->AddChild<dui::Text>(alignLabels[i]);
        }

        // ---- 阴影：box-shadow 子集（SDF 衰减带，无纹理 / 无离屏 / 无缓存） ----
        NewCaption(root, L"Shadow(blur, color, offsetX, offsetY)：圆角矩形投影，SDF 衰减带近似高斯");
        dui::Panel* shadowRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(16.f));
        const wchar_t* shadowLabels[] = {L"blur 12 · 向下偏移 6", L"blur 12 · 居中", L"blur 0 · 纯偏移硬阴影"};
        const float blurs[] = {12.f, 12.f, 0.f};
        const float offs[] = {6.f, 0.f, 8.f};
        for (int i = 0; i < 3; ++i) {
            dui::Panel* cell = shadowRow->AddChild<dui::Panel>(dui::Style()
                .Width(240.f).Height(72.f)
                .Background(0xFFFFFFFF).CornerRadius(10.f)
                .Shadow(blurs[i], 0x59000000, 0.f, offs[i])
                .JustifyContent(dui::Justify::Center).AlignItems(dui::Align::Center));
            cell->AddChild<dui::Text>(shadowLabels[i]);
        }

        // ---- 四角独立圆角 + 边框 ----------------------------------------------
        NewCaption(root, L"CornerRadii(左上,右上,右下,左下)：四角独立圆角；BorderWidth / BorderColor：实线内描边");
        dui::Panel* cornerRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(12.f));
        dui::Panel* cornerA = cornerRow->AddChild<dui::Panel>(dui::Style()
            .Width(180.f).Height(72.f)
            .Background(0xFF4472C4).Color(0xFFFFFFFF)
            .CornerRadius(dui::CornerRadii(28.f, 0.f, 28.f, 0.f))
            .JustifyContent(dui::Justify::Center).AlignItems(dui::Align::Center));
        cornerA->AddChild<dui::Text>(L"左上 / 右下 = 28");
        dui::Panel* cornerB = cornerRow->AddChild<dui::Panel>(dui::Style()
            .Width(180.f).Height(72.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .CornerRadius(10.f)
            .BorderWidth(2.f).BorderColor(0xFFED7D31)
            .Padding(10.f));
        cornerB->AddChild<dui::Text>(L"边框 2 DIP 内描边\n（不占布局盒）");
        dui::Panel* cornerC = cornerRow->AddChild<dui::Panel>(dui::Style()
            .Width(180.f).Height(72.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .CornerRadius(dui::CornerRadii(0.f, 24.f, 0.f, 24.f))
            .BorderWidth(3.f).BorderColor(0xFF2D7FF9)
            .JustifyContent(dui::Justify::Center).AlignItems(dui::Align::Center));
        cornerC->AddChild<dui::Text>(L"边框 3 + 右上/左下 24");

        // ---- 分边边框 / 虚线 / box-sizing --------------------------------------
        NewCaption(root, L"BorderWidth(Thickness)：分边宽度（左/上/右/下）；BorderStyle：Dashed / Dotted；BoxSizing: BorderBox 让边框占盒内空间");
        dui::Panel* borderRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(12.f));
        // 分边宽度：上/下 8、左/右 2（角部直角对接；分边时圆角不生效）
        dui::Panel* sideBox = borderRow->AddChild<dui::Panel>(dui::Style()
            .Width(150.f).Height(76.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .BorderWidth(dui::Thickness(2.f, 8.f, 2.f, 8.f))   // 左 2 / 上 8 / 右 2 / 下 8
            .BorderColor(0xFFED7D31)
            .Padding(10.f));
        sideBox->AddChild<dui::Text>(L"分边：上/下 8\n左/右 2");
        dui::Panel* dashedBox = borderRow->AddChild<dui::Panel>(dui::Style()
            .Width(150.f).Height(76.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .BorderWidth(3.f).BorderColor(0xFF2D7FF9)
            .BorderStyle(dui::BorderStyle::Dashed)
            .CornerRadius(8.f)
            .Padding(10.f));
        dashedBox->AddChild<dui::Text>(L"虚线 + 圆角 8");
        dui::Panel* dottedBox = borderRow->AddChild<dui::Panel>(dui::Style()
            .Width(150.f).Height(76.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .BorderWidth(3.f).BorderColor(0xFF7030A0)
            .BorderStyle(dui::BorderStyle::Dotted)
            .Padding(10.f));
        dottedBox->AddChild<dui::Text>(L"点线");
        // border-box：边框占盒内空间 → 内容区 = 盒 − 边框 − 内边距
        dui::Panel* boxSizingBox = borderRow->AddChild<dui::Panel>(dui::Style()
            .Width(150.f).Height(76.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .BorderWidth(4.f).BorderColor(0xFF4472C4)
            .BoxSizing(dui::BoxSizing::BorderBox)
            .Padding(10.f));
        boxSizingBox->AddChild<dui::Text>(L"border-box：边框占盒内\n内容区 = 盒 − 边 − 内距");

        // ---- 词距 / 文本省略号 -----------------------------------------------
        NewCaption(root, L"WordSpacing：词距（继承属性，只加宽词间空格）；TextOverflow: Ellipsis：单行超出显示省略号");
        dui::Panel* textRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(12.f));
        dui::Panel* wordSpaceBox = textRow->AddChild<dui::Panel>(dui::Style()
            .Width(280.f).Height(60.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .WordSpacing(4.f)          // 继承属性 → 子 Text 生效
            .Padding(10.f));
        wordSpaceBox->AddChild<dui::Text>(L"词距 4 DIP：词与词之间的空格变宽");
        dui::Panel* ellipsisBox = textRow->AddChild<dui::Panel>(dui::Style()
            .Width(280.f).Height(60.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .Padding(10.f));
        ellipsisBox->AddChild<dui::Text>(
            L"这是一段很长很长很长很长很长很长很长很长很长的文字，用来演示省略号截断",
            dui::Style().TextOverflow(dui::TextOverflow::Ellipsis));

        // ---- 斜体 / 文本装饰：fontStyle 继承，textDecoration 不继承 ----------------
        NewCaption(root, L"FontStyle：斜体（继承属性，父级设置 → 子 Text 生效）；TextDecoration：下划线 / 删除线（位标志，不继承）");
        dui::Panel* decoRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(12.f));
        dui::Panel* italicBox = decoRow->AddChild<dui::Panel>(dui::Style()
            .Width(210.f).Height(76.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .FontStyle(dui::FontStyle::Italic)          // 设在容器上 → 子 Text 继承
            .Padding(10.f));
        italicBox->AddChild<dui::Text>(L"斜体：父级设 fontStyle\n子 Text 继承");
        dui::Panel* decoBox = decoRow->AddChild<dui::Panel>(dui::Style()
            .Width(240.f).Height(76.f)
            .Background(0xFFFFFFFF).Color(0xFF222222)
            .TextDecoration(dui::TextDecoration::Underline)   // 容器设了下划线
            .Padding(10.f));
        decoBox->AddChild<dui::Text>(L"下划线 + 删除线（位标志组合）", dui::Style()
            .TextDecoration(dui::TextDecoration::Underline | dui::TextDecoration::LineThrough));
        decoBox->AddChild<dui::Text>(L"这行没装饰线（textDecoration 不继承）", dui::Style()
            .FontSize(11.f).Color(0xFF888888));

        // ---- 运行时覆盖：Set 可改 Thickness 类属性与字体族 --------------------
        NewCaption(root, L"运行时 Set：点击色块切换 Padding / Margin / CornerRadius / FontFamily");
        dui::Panel* toggle = root->AddChild<dui::Panel>(dui::Style()
            .Height(56.f)
            .Background(0xFF4472C4)
            .CornerRadius(4.f)
            .Color(0xFFFFFFFF)
            .Padding(8.f)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        dui::Text* toggleLabel = toggle->AddChild<dui::Text>(L"点击切换：四边 16 / 宋体");
        toggle->OnClick([toggle, toggleLabel](dui::PointerEvent&) {
            const bool compact = dui::ResolveProperty(toggle, dui::Prop::CornerRadius).f < 10.f;
            if (compact) {
                toggle->Set(dui::Prop::Padding, dui::Thickness(16.f));       // Thickness 值
                toggle->Set(dui::Prop::Margin, dui::Thickness(0.f, 24.f));    // 上下 0 / 左右 24
                toggle->Set(dui::Prop::CornerRadius, 16.f);                   // 标量 → 四边同值
                toggleLabel->Set(dui::Prop::FontFamily, L"SimSun");           // 字面量，长期存活
            } else {
                toggle->Set(dui::Prop::Padding, dui::Thickness(8.f));
                toggle->Set(dui::Prop::Margin, dui::Thickness(0.f));
                toggle->Set(dui::Prop::CornerRadius, 4.f);
                toggleLabel->Set(dui::Prop::FontFamily, L"Microsoft YaHei");
            }
        });
    }
};

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::Application app;
    StyleWindow window;
    return app.Run(window);
}
