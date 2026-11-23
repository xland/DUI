// ============================================================================
//  Animation/main.cpp — 动画示例集
//
//  每个 DemoXxx() 都是一个独立、可单独阅读的小示例：标题 + 卡片 + 按钮，
//  控件和回调都写在函数内部，没有成员变量、没有跨示例状态。想看哪个功能
//  就直接读那个函数，OnCreate 只是把示例名列一遍。
//
//  root 用 Row + FlexWrap(true) 自动排成示例网格；FontSize / Color 是继承
//  属性，由 root 一次设好下传，所以单个示例里不需要重复写外观配置。
//  示例 11 / 12 演示多通道补间：Thickness 四边（每帧重排）与四角圆角（只重绘）。
//  示例 13 演示生命周期控制：Animate 只创建动画，Start 才执行；一组句柄可统一
//  启动 / 暂停 / 继续 / 停止。
//  示例 14 演示循环播放：Loop 让动画反复跑，每轮触发 OnStarted / OnFinished。
//  GIF 帧动画与播放控制见 Samples/Image；三类时间行为共用 App 层一个定时器。
// ============================================================================

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "DUI/DUI.h"

namespace {

// ---- 全文件仅这四处公共外观（其余样式都在各自示例里）----------------------
static const dui::Style kButton = dui::Style()
    .Background(0xFF374151).Color(0xFFFFFFFF)
    .Padding(8.f).CornerRadius(4.f);

template <class F>
void AddButton(dui::Panel* parent, const wchar_t* label, F fn) {
    parent->AddChild<dui::Button>(label, kButton)
        ->OnClick([fn](dui::PointerEvent&) { fn(); });
}

// "开始 第 2 轮" —— 循环示例的状态文本
std::wstring RoundText(const wchar_t* what, int n) {
    return std::wstring(what) + L" 第 " + std::to_wstring(n) + L" 轮";
}

// 一列 = 一个示例；返回的容器用来放卡片与按钮。
// autoWidth=true 时不设 Width，列宽由子项自然尺寸决定（示例 9 用来演示回流）
dui::Panel* Demo(dui::Panel* root, const wchar_t* title, bool autoWidth = false) {
    dui::Style s;
    s.Height(260.f).Gap(8.f).Padding(12.f)
     .Background(0xFFFFFFFF).CornerRadius(6.f);
    if (!autoWidth) s.Width(240.f);
    dui::Panel* col = root->AddChild<dui::Panel>(s);
    col->AddChild<dui::Text>(title);
    return col;
}

dui::Panel* Card(dui::Panel* col) {
    return col->AddChild<dui::Panel>(dui::Style()
        .Width(160.f).Height(56.f)
        .Background(0xFF2D7FF9).CornerRadius(6.f));
}

// ---- 1 最基础：Animate 只创建，Start 才执行 ----------------------------------
void DemoOpacity(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"1 淡入淡出：Opacity");
    dui::Panel* card = Card(col);
    AddButton(col, L"淡出", [card] {
        card->Animate(dui::Opacity, 0.f, { .durationMs = 600 }).Start();
    });
    AddButton(col, L"淡入", [card] {
        card->Animate(dui::Opacity, 1.f, { .durationMs = 600 }).Start();
    });
}

// ---- 2 颜色：ARGB 四通道分别插值 -------------------------------------------
void DemoColor(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"2 换色：BackgroundColor");
    dui::Panel* card = Card(col);
    AddButton(col, L"变橙", [card] {
        card->Animate(dui::BackgroundColor, 0xFFE8590C, { .durationMs = 700 }).Start();
    });
    AddButton(col, L"变蓝", [card] {
        card->Animate(dui::BackgroundColor, 0xFF2D7FF9, { .durationMs = 700 }).Start();
    });
}

// ---- 3 缓动：AnimConfig 里指定 easing ---------------------------------------
void DemoEasing(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"3 缓动：EaseInOut");
    dui::Panel* card = Card(col);
    auto angle = std::make_shared<float>(0.f);   // 每点一次多转一圈（360° 与 0° 视觉等价）
    AddButton(col, L"播放一次", [card, angle] {
        *angle += 360.f;
        card->Animate(dui::Rotation, *angle,
            { .durationMs = 900, .easing = dui::Easing::EaseInOut }).Start();
    });
}

// ---- 4 句柄：启动 / 暂停 / 继续 / 停止 / 取消 ---------------------------------
void DemoHandle(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"4 句柄：Start / Pause / Stop");
    dui::Panel* card = Card(col);
    dui::Text* status = col->AddChild<dui::Text>(L"等待启动");
    auto h = std::make_shared<dui::AnimationHandle>();  // 多个按钮共享同一句柄
    AddButton(col, L"启动（1.5 秒）", [card, status, h] {
        // Animate 只创建动画（不执行），Start() 才真正开始推进
        *h = card->Animate(dui::Opacity, 0.f, { .durationMs = 1500 });
        h->OnStarted([status] { status->SetText(L"已开始推进"); })
         .OnPaused([status] { status->SetText(L"已暂停（冻结在当前值）"); })
         .OnStopped([status] { status->SetText(L"被 Stop 打断（值落在终值）"); })
         .OnFinished([status] { status->SetText(L"完成：终值保留"); });
        h->Start();
    });
    AddButton(col, L"暂停", [status, h] {
        h->Pause();          // 冻结在当前值
        status->SetText(L"已暂停");
    });
    AddButton(col, L"继续", [status, h] {
        h->Continue();       // 从当前值接着跑（已是终态则不再执行）
        status->SetText(L"继续…");
    });
    AddButton(col, L"停止", [status, h] {
        h->Stop();           // 立即落到终值
        status->SetText(L"已停止（终值）");
    });
    AddButton(col, L"取消", [status, h] {
        h->Cancel();         // 幂等；清除动画槽并回落基值
        status->SetText(L"已取消，回落到基值");
    });
}

// ---- 5 同属性替换：连点不跳变 ----------------------------------------------
void DemoReplace(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"5 同属性替换：连点不跳变");
    dui::Panel* card = Card(col);
    auto dim = std::make_shared<bool>(false);
    AddButton(col, L"连点这个按钮", [card, dim] {
        *dim = !*dim;
        // 新动画从"当前显示值"起播，旧动画被替换且不触发其回调
        card->Animate(dui::Opacity, *dim ? 0.2f : 1.f, { .durationMs = 1200 }).Start();
    });
}

// ---- 6 位置：Paint 级平移，不改布局盒 --------------------------------------
void DemoMove(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"6 位置：TranslationX");
    dui::Panel* card = Card(col);
    AddButton(col, L"右移并回弹", [card] {
        card->Animate(dui::TranslationX, 40.f, { .durationMs = 600 })
            .OnFinished([card] {
                card->Animate(dui::TranslationX, 0.f, { .durationMs = 600 }).Start();
            })
            .Start();
    });
    col->AddChild<dui::Text>(L"平移后命中测试也跟着走");
}

// ---- 7 旋转：单位度，支点是卡片中心 ----------------------------------------
void DemoRotation(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"7 旋转：Rotation（度）");
    dui::Panel* card = Card(col);
    AddButton(col, L"转 15°", [card] {
        card->Animate(dui::Rotation, 15.f, { .durationMs = 500 }).Start();
    });
    AddButton(col, L"转回 0°", [card] {
        card->Animate(dui::Rotation, 0.f, { .durationMs = 500 }).Start();
    });
}

// ---- 8 尺寸（视觉）：Scale 只改顶点，不重排 --------------------------------
void DemoScale(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"8 尺寸：Scale（不重排）");
    dui::Panel* card = Card(col);
    AddButton(col, L"放大", [card] {
        card->Animate(dui::Scale, 1.3f, { .durationMs = 500 }).Start();
    });
    AddButton(col, L"还原", [card] {
        card->Animate(dui::Scale, 1.f, { .durationMs = 500 }).Start();
    });
}

// ---- 9 尺寸（布局）：Width 每帧重排，作为对照 ------------------------------
// 这一列不设 Width（宽度自适应）：卡片变宽 → 列宽随之变化 → 同一行后面的示例
// 右移，直接看到 flex 回流。示例 8 的 Scale 只改绘制顶点，完全不进布局统计。
void DemoSize(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"9 布局尺寸：Width", true);
    dui::Panel* card = Card(col);
    AddButton(col, L"变宽", [card] {
        card->Animate(dui::Width, 240.f, { .durationMs = 500 }).Start();
    });
    AddButton(col, L"还原", [card] {
        card->Animate(dui::Width, 160.f, { .durationMs = 500 }).Start();
    });
}

// ---- 10 组合：不同属性各占一个槽，可同时运行 -------------------------------
void DemoCombo(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"10 组合：三个属性同时补间");
    dui::Panel* card = Card(col);
    AddButton(col, L"同时动", [card] {
        card->Animate(dui::TranslationX, 20.f, { .durationMs = 600 }).Start();
        card->Animate(dui::Rotation,     15.f,  { .durationMs = 600 }).Start();
        card->Animate(dui::Scale,        1.2f,  { .durationMs = 600 }).Start();
    });
    AddButton(col, L"复位", [card] {
        card->Animate(dui::TranslationX, 0.f, { .durationMs = 400 }).Start();
        card->Animate(dui::Rotation,     0.f, { .durationMs = 400 }).Start();
        card->Animate(dui::Scale,        1.f, { .durationMs = 400 }).Start();
    });
}

// ---- 11 间距：Thickness 四边同时补间（每帧重排） -----------------------------
// padding / margin 是 kFlagLayout → 补间期间每帧重排整棵子树：内边距把内容挤动，
// 外边距把兄弟项推开（与示例 9 的 Width 同一代价）；四边共用一条时间轴，一起动。
void DemoSpacing(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"11 间距：Padding / Margin");
    dui::Panel* card = col->AddChild<dui::Panel>(dui::Style()
        .Width(160.f).Height(56.f).Padding(4.f)
        .Background(0xFF2D7FF9).CornerRadius(6.f));
    card->AddChild<dui::Text>(L"内容");
    AddButton(col, L"撑开", [card] {
        card->Animate(dui::Padding, dui::Thickness(20.f), { .durationMs = 600 }).Start();
        card->Animate(dui::Margin,  dui::Thickness(16.f), { .durationMs = 600 }).Start();
    });
    AddButton(col, L"收回", [card] {
        card->Animate(dui::Padding, dui::Thickness(4.f), { .durationMs = 600 }).Start();
        card->Animate(dui::Margin,  dui::Thickness(0.f), { .durationMs = 600 }).Start();
    });
    col->AddChild<dui::Text>(L"四边一起动：每帧重排");
}

// ---- 12 四角：CornerRadii 四通道各自插值（只重绘，不重排） -------------------
// 目标值四角不同（左上/右下大、右上/左下小）→ 能看出四个通道各自插值；
// cornerRadius 是 kFlagPaint → 只重绘，布局完全不动。
void DemoCorner(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"12 四角：CornerRadius");
    dui::Panel* card = col->AddChild<dui::Panel>(dui::Style()
        .Width(160.f).Height(56.f)
        .Background(0xFF2D7FF9).CornerRadius(4.f));
    AddButton(col, L"圆角化", [card] {
        card->Animate(dui::CornerRadius, dui::CornerRadii{28.f, 4.f, 28.f, 4.f},
            { .durationMs = 700 }).Start();
    });
    AddButton(col, L"复位", [card] {
        card->Animate(dui::CornerRadius, dui::CornerRadii(4.f), { .durationMs = 700 }).Start();
    });
    col->AddChild<dui::Text>(L"四角各自插值：只重绘");
}

// ---- 13 编排：先全部创建，再统一启动（可整体暂停 / 停止） --------------------
// Animate 只是"创建"动画：可以把一组动画先建好、再一起 Start()，
// 之后还能整体 Pause() / Continue() / Stop()。
void DemoOrchestra(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"13 编排：批量创建 + 统一控制");
    dui::Panel* card = Card(col);
    dui::Text* status = col->AddChild<dui::Text>(L"等待启动");
    auto handles = std::make_shared<std::vector<dui::AnimationHandle>>();

    AddButton(col, L"创建三个动画", [card, handles, status] {
        handles->clear();
        handles->push_back(card->Animate(dui::TranslationX, 30.f, { .durationMs = 900 }));
        handles->push_back(card->Animate(dui::Rotation, 30.f, { .durationMs = 900 }));
        handles->push_back(card->Animate(dui::Scale, 1.2f, { .durationMs = 900 }));
        status->SetText(L"已创建 3 个（尚未执行）");
    });
    AddButton(col, L"统一启动", [handles, status] {
        for (dui::AnimationHandle& h : *handles) h.Start();
        status->SetText(L"已全部启动");
    });
    AddButton(col, L"统一暂停", [handles, status] {
        for (dui::AnimationHandle& h : *handles) h.Pause();
        status->SetText(L"已全部暂停（冻结在当前值）");
    });
    AddButton(col, L"统一继续", [handles, status] {
        for (dui::AnimationHandle& h : *handles) h.Continue();
        status->SetText(L"已全部继续");
    });
    AddButton(col, L"统一停止", [handles, status] {
        for (dui::AnimationHandle& h : *handles) h.Stop();
        status->SetText(L"已全部停止（落在终值）");
    });
}

// ---- 14 循环：Loop 让动画反复播放，每轮触发 OnStarted / OnFinished -------------
void DemoLoop(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"14 循环：Loop");
    dui::Panel* card = Card(col);
    dui::Text* status = col->AddChild<dui::Text>(L"等待开始");
    auto round = std::make_shared<int>(0);      // 已完成的轮数
    auto h = std::make_shared<dui::AnimationHandle>();

    auto arm = [card, status, round, h](uint32_t times) {
        *round = 0;
        *h = card->Animate(dui::TranslationX, 40.f, { .durationMs = 700 });
        h->OnStarted([status, round] {
            status->SetText(RoundText(L"开始", *round + 1).c_str());
        })
         .OnFinished([status, round] {
            status->SetText(RoundText(L"结束", ++(*round)).c_str());
        });
        h->Loop(times);        // times = 0 → 无限循环
    };

    AddButton(col, L"无限循环", [arm] { arm(0); });
    AddButton(col, L"循环 3 轮", [arm] { arm(3); });
    AddButton(col, L"停止", [status, h] {
        h->Stop();             // 立即落到终值，并结束循环
        status->SetText(L"已停止（终值）");
    });
    AddButton(col, L"取消", [status, h] {
        h->Cancel();           // 移除动画槽 → 回落基值
        status->SetText(L"已取消，回落基值");
    });
    col->AddChild<dui::Text>(L"每轮：OnFinished → OnStarted");
}

class AnimationWindow final : public dui::Window {
public:
    AnimationWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI 动画示例集",
              .width  = 1200.f,
              .height = 900.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .FlexWrap(true)
            .AlignItems(dui::Align::Start)
            .Gap(14.f).Padding(16.f)
            .FontSize(13.f).Color(0xFF1F2933)
            .Background(0xFFF3F4F6));

        DemoOpacity(root);
        DemoColor(root);
        DemoEasing(root);
        DemoHandle(root);
        DemoReplace(root);
        DemoMove(root);
        DemoRotation(root);
        DemoScale(root);
        DemoSize(root);
        DemoCombo(root);
        DemoSpacing(root);
        DemoCorner(root);
        DemoOrchestra(root);
        DemoLoop(root);
    }
};

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::App app;
    AnimationWindow window;
    return app.Run(window);
}
