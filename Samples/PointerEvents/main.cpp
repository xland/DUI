// ============================================================================
//  PointerEvents/main.cpp — 元素级指针事件演示
//
//  演示 6 类元素级事件（06-Events §3）：
//    OnPointerEnter / OnPointerLeave —— 直达事件（悬停链差分驱动，不冒泡）
//    OnPointerMove / OnPointerDown / OnPointerUp —— 冒泡（命中元素 → 根）
//    OnWheel —— 冒泡；处理器置 ev.handled = true 可阻止滚动容器消费该次滚轮
//
//  派发只经过"有绑定者"的元素，无绑定直接跳过；不做防抖与节流。
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

class PointerEventsWindow final : public dui::Window {
public:
    PointerEventsWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI PointerEvents",
              .width  = 640.f,
              .height = 420.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .Gap(12.f)
            .Padding(20.f)
            .Background(0xFFF5F5F5));

        log_ = root->AddChild<dui::Text>(L"把指针移入蓝色方块 → 依次出现 Enter / Move；移出 → Leave",
                                         dui::Style().FontSize(13.f).Color(0xFF6B7280));

        dui::Panel* box = root->AddChild<dui::Panel>(dui::Style()
            .Width(280.f)
            .Height(160.f)
            .Background(0xFF2563EB));

        box->OnPointerEnter([this](dui::PointerEvent&) {
            ++enterCount_;
            Append(L"Enter");
        });
        box->OnPointerLeave([this](dui::PointerEvent&) {
            Append(L"Leave");
        });
        box->OnPointerMove([this](dui::PointerEvent&) {
            ++moveCount_;
            Append(L"Move");
        });
        box->OnPointerDown([this](dui::PointerEvent&) {
            Append(L"Down");
        });
        box->OnPointerUp([this](dui::PointerEvent&) {
            Append(L"Up");
        });
        box->OnWheel([this](dui::WheelEvent& e) {
            Append(e.horizontal ? L"Wheel(横向)" : L"Wheel(纵向)");
            e.handled = true;      // 拦截：不再交给滚动容器
        });
    }

private:
    // 追加一行事件日志（最多保留 8 条）
    void Append(const wchar_t* what) {
        entries_.push_back(dui::String(what));
        if (entries_.size() > 8) entries_.erase(entries_.begin());
        dui::String text;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (i != 0) text += L"  ";
            text += entries_[i];
        }
        text += L"\n";
        text += L"Enter ×";
        text += std::to_wstring(enterCount_);
        text += L"    Move ×";
        text += std::to_wstring(moveCount_);
        if (log_ != nullptr) log_->SetText(text);
    }

    dui::Text* log_ = nullptr;
    std::vector<dui::String> entries_;   // 最近事件序列
    int enterCount_ = 0;
    int moveCount_ = 0;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    PointerEventsWindow window;
    return app.Run(window);
}
