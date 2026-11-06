// ============================================================================
//  Event/main.cpp — 元素级事件与冒泡演示
//
//  演示（06-Events §3 / §5）：
//    · 冒泡顺序：三层嵌套容器逐层绑定 OnPointerDown / Up / Click，日志显示
//      子 → 父 → 根；祖先容器能统一处理深层子元素的事件
//    · handled 终止：父级拦截开关打开时，父处理器置 handled，根收不到 Down
//    · 直达事件：OnPointerEnter / Leave 由悬停链差分逐元素触发，不走冒泡路由、
//      handled 无效 —— 开拦截后三层仍全部收到，与 Down 形成对比
//    · OnWheel 冒泡 + handled 阻止滚动容器消费；OnKeyDown 从焦点元素冒泡到根
//
//  注意：框架目前只有冒泡阶段。06-Events §5 / D2 的「捕获/隧道」阶段与
//  §6 的指针捕获均尚未实现；本示例用「祖先统一处理」演示冒泡侧的拦截效果。
// ============================================================================

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "DUI/DUI.h"

namespace {

static const dui::Style kButton = dui::Style()
    .FontSize(13.f).Color(0xFFFFFFFF)
    .Background(0xFF374151)
    .Padding(dui::Thickness(6.f, 12.f))
    .CornerRadius(4.f)
    .Cursor(dui::Cursor::Hand)
    .FlexShrink(0.f);

static const dui::Style kButtonHover = dui::Style().Background(0xFF1F6FE5);

dui::Panel* MakeRow(dui::Panel* parent) {
    return parent->AddChild<dui::Panel>(dui::Style()
        .FlexDirection(dui::FlexDirection::Row)
        .AlignItems(dui::Align::Center)
        .Gap(8.f)
        .FlexShrink(0.f));
}

template <class F>
dui::Button* AddButton(dui::Panel* parent, const wchar_t* label, F fn) {
    dui::Button* b = parent->AddChild<dui::Button>(label, kButton);
    b->OnClick([fn](dui::ClickEvent&) { fn(); });
    b->When(dui::State::Hover, kButtonHover);
    return b;
}

dui::Panel* MakeTier(dui::Panel* parent, float w, float h, dui::Color bg) {
    return parent->AddChild<dui::Panel>(dui::Style()
        .Width(w).Height(h)
        .Background(bg)
        .CornerRadius(6.f)
        .Padding(dui::Thickness(12.f))
        .AlignItems(dui::Align::Center)
        .JustifyContent(dui::Justify::Center)
        .FlexShrink(0.f));
}

class EventWindow final : public dui::Window {
public:
    EventWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI Event 示例",
              .width  = 760.f,
              .height = 720.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .AlignItems(dui::Align::Start)
            .Gap(10.f)
            .Padding(18.f)
            .Background(0xFFF3F4F6));

        root->AddChild<dui::Text>(L"DUI 事件：冒泡 / 直达 / 祖先处理",
            dui::Style().FontSize(20.f).Color(0xFF1F2933).FlexShrink(0.f));
        root->AddChild<dui::Text>(
            L"点击嵌套方块看 子→父→根 的冒泡顺序；打开「父级拦截」后对比 Down 与 Enter 的差别",
            dui::Style().FontSize(13.f).Color(0xFF4B5563).FlexShrink(0.f));

        dui::Panel* row = MakeRow(root);
        interceptBtn_ = AddButton(row, L"父级拦截：关", [this] {
            intercept_ = !intercept_;
            interceptBtn_->SetText(intercept_ ? L"父级拦截：开" : L"父级拦截：关");
            Append(intercept_ ? L"开关：父级 Down 置 handled，冒泡到父为止"
                              : L"开关：关闭拦截，Down 完整冒泡到根");
        });
        AddButton(row, L"清空日志", [this] {
            entries_.clear();
            log_->SetText(L"（日志已清空）");
        });

        log_ = root->AddChild<dui::Text>(L"（等待事件…）", dui::Style()
            .Width(560.f).Height(130.f)
            .Padding(dui::Thickness(8.f, 10.f))
            .FontSize(13.f).Color(0xFF0F766E)
            .Background(0xFFFFFFFF)
            .CornerRadius(4.f)
            .FlexShrink(0.f));

        dui::Panel* outer  = MakeTier(root,   560.f, 200.f, 0xFFD1D5DB);
        dui::Panel* middle = MakeTier(outer,  360.f, 130.f, 0xFF2563EB);
        dui::Panel* inner  = MakeTier(middle, 180.f,  70.f, 0xFFF59E0B);

        BindTier(outer,  L"根", false);
        BindTier(middle, L"父", true);
        BindTier(inner,  L"子", false);

        inner->OnWheel([this](dui::WheelEvent& e) {
            Append(e.horizontal ? L"Wheel(横向)@子 → handled" : L"Wheel(纵向)@子 → handled");
            e.handled = true;
        });

        root->AddChild<dui::Text>(
            L"直达事件（Enter/Leave）三层各自触发、不受 handled 影响；Down/Up/Click 走冒泡路由",
            dui::Style().FontSize(13.f).Color(0xFF4B5563).FlexShrink(0.f));

        dui::Panel* focus = root->AddChild<dui::Panel>(dui::Style()
            .Width(240.f).Height(34.f)
            .Background(0xFFFFFFFF)
            .CornerRadius(4.f)
            .Padding(dui::Thickness(6.f, 10.f))
            .FlexShrink(0.f));
        focus->SetFocusable(true);
        focus->OnKeyDown([this](dui::KeyEvent&) {
            Append(L"KeyDown@输入框（焦点元素）");
        });
        root->OnKeyDown([this](dui::KeyEvent&) {
            Append(L"KeyDown@窗口根（冒泡）");
        });
        root->AddChild<dui::Text>(
            L"点击上方白色区域取得焦点后按键：观察焦点元素 → 祖先链的键盘冒泡",
            dui::Style().FontSize(13.f).Color(0xFF4B5563).FlexShrink(0.f));
    }

private:
    void BindTier(dui::Panel* p, const wchar_t* name, bool isParent) {
        const dui::String tag(name);
        p->OnPointerDown([this, tag, isParent](dui::PointerEvent& e) {
            Append((tag + L"·Down").c_str());
            if (isParent && intercept_) {
                e.handled = true;
                Append(L"  父置 handled → 根收不到本次 Down");
            }
        });
        p->OnPointerUp([this, tag](dui::PointerEvent&) {
            Append((tag + L"·Up").c_str());
        });
        p->OnClick([this, tag](dui::ClickEvent&) {
            Append((tag + L"·Click").c_str());
        });
        p->OnPointerEnter([this, tag](dui::PointerEvent&) {
            Append((tag + L"·Enter（直达）").c_str());
        });
        p->OnPointerLeave([this, tag](dui::PointerEvent&) {
            Append((tag + L"·Leave（直达）").c_str());
        });
    }

    // 追加一行事件日志（最多保留 6 条）
    void Append(const wchar_t* what) {
        entries_.push_back(dui::String(what));
        if (entries_.size() > 6) entries_.erase(entries_.begin());
        dui::String text;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (i != 0) text += L"\n";
            text += entries_[i];
        }
        log_->SetText(text);
    }

    dui::Text*   log_ = nullptr;
    dui::Button* interceptBtn_ = nullptr;
    std::vector<dui::String> entries_;
    bool intercept_ = false;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    EventWindow window;
    return app.Run(window);
}
