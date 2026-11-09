// ============================================================================
//  Event/main.cpp — 事件示例集
//
//  每个 DemoXxx() 都是一个独立、可单独阅读的小示例：标题 + 一两个方块 +
//  日志，控件和回调都写在函数内部，没有成员变量、没有跨示例状态。
//  想看哪种事件就直接读那个函数，OnCreate 只是把示例名列一遍。
//
//  日志直接读回 dui::Text 的当前内容再追加一行，不需要 vector / 环形缓冲。
//  root 用 Row + FlexWrap(Wrap) 自动排成示例网格；FontSize / Color 由继承
//  下传，单个示例里不重复写外观配置。
//  可路由事件分两阶段：隧道（根 → 命中）+ 冒泡（命中 → 根）；
//  Enter / Leave 是直达事件，没有阶段。指针捕获（Element::SetCapture）属于
//  控件行为，不在本示例集演示。
// ============================================================================

#include <windows.h>

#include <string>

#include "DUI/DUI.h"

namespace {

// ---- 全文件仅这两处公共外观 ------------------------------------------------
// 一列 = 一个示例；返回的容器同时也是"根"层，可直接在上面绑冒泡处理器
dui::Panel* Demo(dui::Panel* root, const wchar_t* title) {
    dui::Panel* col = root->AddChild<dui::Panel>(dui::Style()
        .Width(240.f).Height(260.f)
        .Gap(8.f).Padding(12.f)
        .Background(0xFFFFFFFF).CornerRadius(6.f));
    col->AddChild<dui::Text>(title);
    return col;
}

dui::Panel* Box(dui::Panel* parent, float w, float h, dui::Color bg) {
    return parent->AddChild<dui::Panel>(dui::Style()
        .Width(w).Height(h)
        .Background(bg).CornerRadius(4.f)
        .Padding(10.f));
}

// ---- 1 Click 冒泡：命中元素 → 祖先链 ---------------------------------------
void DemoClick(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"1 Click 冒泡：子 → 父 → 根");
    dui::Text* log = col->AddChild<dui::Text>(L"", dui::Style().Height(88.f));
    auto say = [log](const wchar_t* s) { log->SetText(log->Text_() + s + L"\n"); };
    dui::Panel* outer = Box(col,   190.f, 84.f, 0xFFD1D5DB);
    dui::Panel* inner = Box(outer, 120.f, 48.f, 0xFF2563EB);

    col->OnClick([say](dui::PointerEvent&)   { say(L"3 根（列）"); });
    outer->OnClick([say](dui::PointerEvent&) { say(L"2 父（灰）"); });
    inner->OnClick([say, log](dui::PointerEvent&) {
        log->SetText(L"");           // 每次点击从空白开始，正好看到三行顺序
        say(L"1 子（蓝）");
    });
}

// ---- 2 handled：父级处理器终止冒泡 -----------------------------------------
void DemoHandled(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"2 handled：父级终止冒泡");
    dui::Text* log = col->AddChild<dui::Text>(L"", dui::Style().Height(88.f));
    auto say = [log](const wchar_t* s) { log->SetText(log->Text_() + s + L"\n"); };
    dui::Panel* outer = Box(col,   190.f, 84.f, 0xFFD1D5DB);
    dui::Panel* inner = Box(outer, 120.f, 48.f, 0xFF2563EB);

    col->OnClick([say](dui::PointerEvent&) { say(L"根（被拦截，不该出现）"); });
    outer->OnClick([say](dui::PointerEvent& e) {
        say(L"父：handled = true");
        e.handled = true;
    });
    inner->OnClick([say, log](dui::PointerEvent&) {
        log->SetText(L"");
        say(L"子");
    });
}

// ---- 3 直达事件：Enter / Leave 不冒泡 --------------------------------------
void DemoEnterLeave(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"3 Enter / Leave：直达事件");
    dui::Text* log = col->AddChild<dui::Text>(L"", dui::Style().Height(88.f));
    auto say = [log](const wchar_t* s) { log->SetText(log->Text_() + s + L"\n"); };
    dui::Panel* outer = Box(col,   190.f, 84.f, 0xFFD1D5DB);
    dui::Panel* inner = Box(outer, 120.f, 48.f, 0xFF2563EB);

    // 移到子元素上不算离开父元素，故 outer 不会再触发 Leave / Enter
    outer->OnPointerEnter([say, log](dui::PointerEvent&) {
        log->SetText(L"");
        say(L"进入父（直达，不冒泡）");
    });
    outer->OnPointerLeave([say](dui::PointerEvent&) { say(L"离开父"); });
    inner->OnPointerEnter([say](dui::PointerEvent&)  { say(L"进入子：父不 Leave"); });
    inner->OnPointerLeave([say](dui::PointerEvent&)  { say(L"离开子：父不 Enter"); });
}

// ---- 4 按下 / 弹起：冒泡 ---------------------------------------------------
void DemoDownUp(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"4 Down / Up：冒泡");
    dui::Text* log = col->AddChild<dui::Text>(L"", dui::Style().Height(88.f));
    auto say = [log](const wchar_t* s) { log->SetText(log->Text_() + s + L"\n"); };
    dui::Panel* outer = Box(col,   190.f, 84.f, 0xFFD1D5DB);
    dui::Panel* inner = Box(outer, 120.f, 48.f, 0xFF2563EB);

    col->OnPointerDown([say](dui::PointerEvent&)   { say(L"Down@根"); });
    outer->OnPointerDown([say](dui::PointerEvent&) { say(L"Down@父"); });
    inner->OnPointerDown([say, log](dui::PointerEvent&) {
        log->SetText(L"");
        say(L"Down@子");
    });
    inner->OnPointerUp([say](dui::PointerEvent&) { say(L"Up@子"); });
    col->OnPointerUp([say](dui::PointerEvent&)   { say(L"Up@根（冒泡）"); });
}

// ---- 5 移动：读客户区绝对坐标 ----------------------------------------------
void DemoMove(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"5 Move：读指针坐标");
    dui::Text* pos = col->AddChild<dui::Text>(L"把指针移到灰块上");
    dui::Panel* box = Box(col, 190.f, 70.f, 0xFFD1D5DB);
    box->OnPointerMove([pos](dui::PointerEvent& e) {
        pos->SetText(L"x=" + std::to_wstring(static_cast<int>(e.pos.x)) +
                     L"  y=" + std::to_wstring(static_cast<int>(e.pos.y)));
    });
}

// ---- 6 滚轮：置 handled 阻止祖先消费 ---------------------------------------
void DemoWheel(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"6 Wheel：handled");
    dui::Text* log = col->AddChild<dui::Text>(L"在灰块上滚动滚轮");
    dui::Panel* box = Box(col, 190.f, 70.f, 0xFFD1D5DB);
    box->OnWheel([log](dui::WheelEvent& e) {
        log->SetText(e.horizontal ? L"横向 → handled = true"
                                  : L"纵向 → handled = true");
        e.handled = true;   // 无人 handled 时滚动容器才会消费滚轮
    });
}

// ---- 7 键盘：焦点元素 → 祖先链 ---------------------------------------------
void DemoKeyboard(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"7 KeyDown：焦点 → 根");
    dui::Text* log = col->AddChild<dui::Text>(L"", dui::Style().Height(88.f));
    auto say = [log](const wchar_t* s) { log->SetText(log->Text_() + s + L"\n"); };
    dui::Panel* focus = Box(col, 190.f, 56.f, 0xFFDBEAFE);
    focus->SetFocusable(true);
    // 聚焦态换色：既让可点区域看得见，也让"当前有没有焦点"一目了然
    focus->When(dui::State::Focused, dui::Style().Background(0xFF93C5FD));

    focus->OnKeyDown([say, log](dui::KeyEvent&) {
        log->SetText(L"");
        say(L"1 焦点元素");
    });
    col->OnKeyDown([say](dui::KeyEvent&) { say(L"2 根（冒泡）"); });
    col->AddChild<dui::Text>(L"点击浅蓝区域（变深即已聚焦）后按任意键");
}

// ---- 8 状态变体：hover 立即换色，不带过渡 ----------------------------------
void DemoWhen(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"8 When：hover 状态变体");
    dui::Panel* box = Box(col, 190.f, 60.f, 0xFF2563EB);
    box->When(dui::State::Hover, dui::Style().Background(0xFFF59E0B));
    col->AddChild<dui::Text>(L"移到方块上立即换色（无过渡）");
}

// ---- 9 隧道：根先收到，再冒泡回根 ------------------------------------------
void DemoTunnel(dui::Panel* root) {
    dui::Panel* col = Demo(root, L"9 隧道：根 → 父 → 子 → 父 → 根");
    dui::Text* log = col->AddChild<dui::Text>(L"", dui::Style().Height(110.f));
    auto say = [log](const wchar_t* s) { log->SetText(log->Text_() + s + L"\n"); };
    dui::Panel* outer = Box(col,   190.f, 84.f, 0xFFD1D5DB);
    dui::Panel* inner = Box(outer, 120.f, 48.f, 0xFF2563EB);

    // 隧道：绑在上级容器，却比子元素先收到。在这里置 e.handled = true 可拦掉
    // 后面全部阶段（子元素连 Down 都收不到），用于预判/过滤。
    col->OnPointerDown([say, log](dui::PointerEvent&) {
        log->SetText(L"");
        say(L"1 根（隧道）");
    }, dui::Phase::Tunnel);
    outer->OnPointerDown([say](dui::PointerEvent&) {
        say(L"2 父（隧道）");
    }, dui::Phase::Tunnel);

    // 冒泡：默认阶段，命中元素 → 父 → 根；同一元素两个阶段的处理器互不覆盖。
    inner->OnPointerDown([say](dui::PointerEvent&) { say(L"3 子（冒泡）"); });
    outer->OnPointerDown([say](dui::PointerEvent&) { say(L"4 父（冒泡）"); });
    col->OnPointerDown([say](dui::PointerEvent&)   { say(L"5 根（冒泡）"); });
}

class EventWindow final : public dui::Window {
public:
    EventWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI 事件示例集",
              .width  = 1200.f,
              .height = 900.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .FlexWrap(dui::FlexWrap::Wrap)
            .AlignItems(dui::Align::Start)
            .Gap(14.f).Padding(16.f)
            .FontSize(13.f).Color(0xFF1F2933)
            .Background(0xFFF3F4F6));

        DemoClick(root);
        DemoHandled(root);
        DemoEnterLeave(root);
        DemoDownUp(root);
        DemoMove(root);
        DemoWheel(root);
        DemoKeyboard(root);
        DemoWhen(root);
        DemoTunnel(root);
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    EventWindow window;
    return app.Run(window);
}
