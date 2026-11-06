// ============================================================================
//  Animation/main.cpp — DUI 动画能力示例
//
//  两类动画共用同一个 AnimationClock（App 层只有一个窗口定时器；全部结束即停表）：
//    1) 命令式属性动画：Element::Animate / AnimConfig / AnimationHandle
//       float 插值（Opacity）、颜色插值（BackgroundColor）、四种缓动、
//       OnFinished、幂等 Cancel、同属性替换（起点取当前显示值）、fill forwards。
//    2) GIF 帧动画播放控制：dui::Gif 的 Pause / Play / Stop / SetLoopCount。
//
//  构建时把 demo.gif 拷到 exe 目录（见 vcxproj PostBuildEvent）。
// ============================================================================

#include <windows.h>

#include <functional>
#include <string>

#include "DUI/DUI.h"

namespace {

constexpr uint32_t kCardA = 0xFF2D7FF9;
constexpr uint32_t kCardB = 0xFFE8590C;

static const dui::Style kButton = dui::Style()
    .FontSize(13.f).Color(0xFFFFFFFF)
    .Background(0xFF374151)
    .Padding(dui::Thickness(6.f, 12.f))
    .CornerRadius(4.f)
    .Cursor(dui::Cursor::Hand)
    .FlexShrink(0.f);

// 元素级状态变体（Element::When）：hover 时立即换背景，不带过渡
static const dui::Style kButtonHover = dui::Style().Background(0xFF1F6FE5);

std::wstring AssetPath(const wchar_t* name) {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    const auto slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) path.resize(slash + 1);
    return path + name;
}

dui::Panel* MakeRow(dui::Panel* parent) {
    return parent->AddChild<dui::Panel>(dui::Style()
        .FlexDirection(dui::FlexDirection::Row)
        .AlignItems(dui::Align::Center)
        .Gap(8.f)
        .FlexShrink(0.f));
}

template <class F>
void AddButton(dui::Panel* parent, const wchar_t* label, F fn) {
    dui::Button* b = parent->AddChild<dui::Button>(label, kButton);
    b->OnClick([fn](dui::ClickEvent&) { fn(); });
    b->When(dui::State::Hover, kButtonHover);
}

class AnimationWindow final : public dui::Window {
public:
    AnimationWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI 动画示例",
              .width  = 900.f,
              .height = 760.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .AlignItems(dui::Align::Start)
            .Gap(8.f)
            .Padding(18.f)
            .Background(0xFFF3F4F6));

        root->AddChild<dui::Text>(L"DUI 动画能力",
            dui::Style().FontSize(22.f).Color(0xFF1F2933).FlexShrink(0.f));
        root->AddChild<dui::Text>(
            L"属性动画与 GIF 帧动画共用一个 AnimationClock：App 层只启停一个定时器，全部结束即停表",
            dui::Style().FontSize(13.f).Color(0xFF4B5563).FlexShrink(0.f));

        // ---- 1) 命令式属性动画 ----
        root->AddChild<dui::Text>(L"1) 命令式属性动画：Animate / OnFinished / Cancel / 缓动",
            dui::Style().FontSize(15.f).Color(0xFF1F2933).FlexShrink(0.f));

        card_ = root->AddChild<dui::Panel>(dui::Style()
            .Width(240.f).Height(80.f)
            .Background(kCardA)
            .CornerRadius(8.f)
            .Opacity(1.f)
            .FlexShrink(0.f));

        dui::Panel* row1 = MakeRow(root);
        AddButton(row1, L"淡出", [this] { Fade(0.f); });
        AddButton(row1, L"淡入", [this] { Fade(1.f); });
        AddButton(row1, L"换色", [this] { Tint(); });
        AddButton(row1, L"取消", [this] { CancelAnimation(); });

        dui::Panel* row2 = MakeRow(root);
        AddButton(row2, L"Linear",    [this] { Tint(dui::Easing::Linear); });
        AddButton(row2, L"EaseIn",    [this] { Tint(dui::Easing::EaseIn); });
        AddButton(row2, L"EaseOut",   [this] { Tint(dui::Easing::EaseOut); });
        AddButton(row2, L"EaseInOut", [this] { Tint(dui::Easing::EaseInOut); });

        propStatus_ = root->AddChild<dui::Text>(L"就绪：点击上面的按钮启动补间",
            dui::Style().FontSize(13.f).Color(0xFF0F766E).FlexShrink(0.f));

        // ---- 2) GIF 帧动画播放控制 ----
        root->AddChild<dui::Text>(L"2) GIF 帧动画：Pause / Play / Stop / SetLoopCount",
            dui::Style().FontSize(15.f).Color(0xFF1F2933).FlexShrink(0.f));

        gif_ = root->AddChild<dui::Gif>(dui::Style()
            .Width(320.f).Height(134.f)
            .Background(0xFFE5E7EB)
            .FlexShrink(0.f));
        if (!gif_->LoadFile(AssetPath(L"demo.gif"))) {
            root->AddChild<dui::Text>(gif_->LastError().c_str(),
                dui::Style().FontSize(13.f).Color(0xFFB42318));
        }

        dui::Panel* row3 = MakeRow(root);
        AddButton(row3, L"暂停", [this] {
            gif_->Pause();
            SetGifStatus(L"Pause()：停当前帧、保留游标，从 ticker 注销");
        });
        AddButton(row3, L"播放", [this] {
            gif_->Play();
            SetGifStatus(L"Play()：从暂停处继续，跳过暂停时长");
        });
        AddButton(row3, L"停止", [this] {
            gif_->Stop();
            SetGifStatus(L"Stop()：回首帧并保留帧序列，Play() 从头重播");
        });
        AddButton(row3, L"循环 3 次", [this] {
            gif_->SetLoopCount(3);
            gif_->Stop();
            gif_->Play();
            SetGifStatus(L"SetLoopCount(3)：播完 3 轮停在最后一帧并停表");
        });
        AddButton(row3, L"无限循环", [this] {
            gif_->SetLoopCount(0);
            gif_->Stop();
            gif_->Play();
            SetGifStatus(L"SetLoopCount(0)：无限循环（默认）");
        });

        gifStatus_ = root->AddChild<dui::Text>(L"就绪：GIF 加载后自动播放（默认无限循环）",
            dui::Style().FontSize(13.f).Color(0xFF0F766E).FlexShrink(0.f));
    }

private:
    void Fade(float target) {
        handle_ = card_->Animate(dui::Opacity, target,
            { .durationMs = 600, .easing = dui::Easing::EaseOut });
        handle_.OnFinished([this] {
            SetPropStatus(L"完成：终值保留（fill forwards），OnFinished 只触发一次");
        });
        SetPropStatus(L"Opacity 补间中：连点按钮会从当前显示值起播，旧动画被替换且不跳变");
    }

    void Tint(dui::Easing e = dui::Easing::EaseInOut) {
        colorToggle_ = !colorToggle_;
        handle_ = card_->Animate(dui::BackgroundColor,
            colorToggle_ ? kCardB : kCardA,
            { .durationMs = 700, .easing = e });
        handle_.OnFinished([this] {
            SetPropStatus(L"颜色补间完成：0xAARRGGBB 四通道分别线性插值");
        });
        SetPropStatus(L"BackgroundColor 补间中（同属性替换，起点取当前显示值）");
    }

    void CancelAnimation() {
        handle_.Cancel();
        SetPropStatus(L"Cancel()：幂等取消，清除动画槽并回落基值；元素销毁后句柄自动失效");
    }

    void SetPropStatus(const wchar_t* s) { propStatus_->SetText(s); }
    void SetGifStatus(const wchar_t* s)  { gifStatus_->SetText(s); }

    dui::Panel* card_ = nullptr;
    dui::Text*  propStatus_ = nullptr;
    dui::Gif*   gif_ = nullptr;
    dui::Text*  gifStatus_ = nullptr;
    dui::AnimationHandle handle_;
    bool colorToggle_ = false;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    AnimationWindow window;
    return app.Run(window);
}
