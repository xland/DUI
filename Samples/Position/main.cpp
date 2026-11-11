// ============================================================================
//  Position/main.cpp — 定位能力演示（04-Layout §4.3）
//
//  覆盖能力：Position Relative / Absolute / Fixed（可挂任意层级，含 Scroller 内）、
//  Left/Top/Right/Bottom 四边定位、最近定位祖先（Absolute 跨中间容器提升）、
//  zIndex（同一 CB 内的 Absolute 层叠排序）。
//
//  构建产物（Build 布局，DUI.slnx 根目录相对）：
//    中间产物  Build\Temp\Position\Debug|Release\
//    最终产物  Build\Position\Debug|Release\Position.exe
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>
#include <format>
#include "DUI/DUI.h"

namespace {

// 分区小标题
void NewCaption(dui::Panel* parent, const wchar_t* text) {
    parent->AddChild<dui::Text>(text, dui::Style()
        .FontSize(12.f)
        .Color(0xFF777777));
}

// 演示用色块：标签文字在块内双轴居中
dui::Panel* NewBox(dui::Panel* parent, const wchar_t* label, dui::Color bg,
                   float width, float height) {
    dui::Panel* box = parent->AddChild<dui::Panel>(dui::Style()
        .FlexDirection(dui::FlexDirection::Column)
        .JustifyContent(dui::Justify::Center)
        .AlignItems(dui::Align::Center)
        .Background(bg)
        .CornerRadius(4.f)
        .Color(0xFFFFFFFF)
        .Width(width)
        .Height(height));
    box->AddChild<dui::Text>(label);
    return box;
}

class PositionWindow final : public dui::Window {
public:
    PositionWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"Position · 定位能力演示",
              .width  = 900.f,
              .height = 900.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Padding(16.f)
            .Gap(10.f)
            .Background(0xFFF3F3F3)
            .Color(0xFF222222)
            .FontSize(13.f));

        root->AddChild<dui::Text>(L"Position 定位能力演示", dui::Style()
            .FontSize(18.f)
            .FontWeight(dui::FontWeight::SemiBold)
            .Color(0xFF333333));

        // ---- Relative：占着流内原位，只有自己挪（Bounds 真偏移）------------
        NewCaption(root, L"Position::Relative：占着原位，只有自己挪（兄弟不动）");
        dui::Panel* rowR = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f).Height(88.f)
            .Background(0xFFE7E6E6).CornerRadius(4.f));
        NewBox(rowR, L"A", 0xFF4472C4, 120.f, 48.f);
        dui::Panel* rel = rowR->AddChild<dui::Panel>(dui::Style()
            .Position(dui::Position::Relative).Left(24.f).Top(16.f)
            .Width(120.f).Height(48.f)
            .Background(0xFFED7D31).CornerRadius(4.f)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        rel->AddChild<dui::Text>(L"relative", dui::Style().Color(0xFFFFFFFF));
        NewBox(rowR, L"C", 0xFF4472C4, 120.f, 48.f);

        // ---- Absolute：脱离流，相对最近定位祖先（这里容器显式设 Relative）------
        NewCaption(root, L"Position::Absolute：脱离流、不占位，相对最近定位祖先定位");
        dui::Panel* boxA = root->AddChild<dui::Panel>(dui::Style()
            .Position(dui::Position::Relative)
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f).Padding(12.f).Height(110.f)
            .Background(0xFFE7E6E6).CornerRadius(4.f));
        NewBox(boxA, L"流内 1", 0xFF70AD47, 120.f, 48.f);
        NewBox(boxA, L"流内 2", 0xFF70AD47, 120.f, 48.f);
        dui::Panel* abs = boxA->AddChild<dui::Panel>(dui::Style()
            .Position(dui::Position::Absolute).Right(12.f).Bottom(12.f)
            .Width(150.f).Height(36.f)
            .Background(0xFFC00000).CornerRadius(4.f)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        abs->AddChild<dui::Text>(L"absolute 右下", dui::Style().Color(0xFFFFFFFF));

        // ---- Fixed：相对窗口客户区，可挂在任意层级 ---------------------------
        NewCaption(root, L"Position::Fixed：钉在窗口客户区右下角");
        dui::Panel* fx = root->AddChild<dui::Panel>(dui::Style()
            .Position(dui::Position::Fixed).Right(16.f).Bottom(16.f)
            .Width(180.f).Height(44.f)
            .Background(0xFF2563EB).CornerRadius(6.f)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        fx->AddChild<dui::Text>(L"fixed · 窗口右下", dui::Style().Color(0xFFFFFFFF));

        // ---- Fixed 通用：挂在 Scroller 里仍钉在窗口，不随内容滚动 ------------
        NewCaption(root, L"Position::Fixed 通用：放在滚动容器里仍钉在窗口上（拖滚动条试试）");
        dui::Scroller* scr = root->AddChild<dui::Scroller>(dui::Style()
            .Height(120.f)
            .Background(0xFFE7E6E6)
            .CornerRadius(4.f));
        for (int i = 1; i <= 8; ++i) {
            auto iStr = std::format(L"滚动内容：这一行会随滚动条移动_{}", i);
            scr->AddChild<dui::Text>(iStr.data(), dui::Style().Height(24.f));
        }
        dui::Panel* fx2 = scr->AddChild<dui::Panel>(dui::Style()
            .Position(dui::Position::Fixed).Right(16.f).Top(16.f)
            .Width(200.f).Height(36.f)
            .Background(0xFF7030A0).CornerRadius(6.f)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        fx2->AddChild<dui::Text>(L"fixed 在 Scroller 内", dui::Style().Color(0xFFFFFFFF));

        // ---- zIndex：同一 CB 内的 Absolute 按值升序绘制 ------------------------
        NewCaption(root, L"zIndex：同一 CB 内，树序 红→绿→蓝，但红盖最上、蓝沉最下");
        dui::Text* hit = root->AddChild<dui::Text>(L"点击色块验证命中顺序（应与视觉一致）",
            dui::Style().FontSize(12.f).Color(0xFF777777));
        dui::Panel* stack = root->AddChild<dui::Panel>(dui::Style()
            .Position(dui::Position::Relative)
            .Height(100.f).Background(0xFFE7E6E6).CornerRadius(4.f));
        auto NewLayer = [stack, hit](const wchar_t* label, dui::Color bg,
                                     float x, float y, int32_t z) {
            dui::Style s = dui::Style()
                .Position(dui::Position::Absolute).Left(x).Top(y)
                .Width(170.f).Height(44.f)
                .Background(bg).CornerRadius(4.f)
                .JustifyContent(dui::Justify::Center)
                .AlignItems(dui::Align::Center);
            if (z != 0) s.ZIndex(z);
            dui::Panel* p = stack->AddChild<dui::Panel>(s);
            p->AddChild<dui::Text>(label, dui::Style().Color(0xFFFFFFFF));
            p->OnClick([hit, label](dui::PointerEvent&) { hit->SetText(label); });
        };
        NewLayer(L"红 · zIndex 10",  0xFFC00000,  20.f, 16.f,  10);
        NewLayer(L"绿 · 默认 0",     0xFF70AD47,  90.f, 30.f,   0);
        NewLayer(L"蓝 · zIndex -1",  0xFF2563EB, 160.f, 44.f,  -1);

        // ---- 最近定位祖先：跨中间 Static 容器提升，zIndex 在同一 CB 内排序 ----
        NewCaption(root, L"最近定位祖先：中间 Static 容器不改变基准，两个 Absolute 共用外层 Relative 容器为 CB");
        dui::Text* anchorHit = root->AddChild<dui::Text>(L"点击重叠色块验证跨容器命中顺序",
            dui::Style().FontSize(12.f).Color(0xFF777777));
        dui::Panel* cb = root->AddChild<dui::Panel>(dui::Style()
            .Position(dui::Position::Relative)
            .Height(112.f).Background(0xFFE7E6E6).CornerRadius(4.f));
        dui::Panel* mid1 = cb->AddChild<dui::Panel>(dui::Style()
            .Height(56.f).Background(0xFFD9D9D9));
        dui::Panel* mid2 = cb->AddChild<dui::Panel>(dui::Style()
            .Height(56.f).Background(0xFFCFCFCF));
        auto NewAnchored = [anchorHit](dui::Panel* mid, const wchar_t* label,
                                       dui::Color bg, float x, float y, int32_t z) {
            dui::Panel* p = mid->AddChild<dui::Panel>(dui::Style()
                .Position(dui::Position::Absolute).Left(x).Top(y).ZIndex(z)
                .Width(190.f).Height(44.f)
                .Background(bg).CornerRadius(4.f)
                .JustifyContent(dui::Justify::Center)
                .AlignItems(dui::Align::Center));
            p->AddChild<dui::Text>(label, dui::Style().Color(0xFFFFFFFF));
            p->OnClick([anchorHit, label](dui::PointerEvent&) { anchorHit->SetText(label); });
        };
        NewAnchored(mid1, L"mid1 · zIndex 10",  0xFFC00000, 30.f, 24.f,  10);
        NewAnchored(mid2, L"mid2 · zIndex -1",  0xFF2563EB, 120.f, 44.f, -1);
    }
};

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::Application app;
    PositionWindow window;
    return app.Run(window);
}
