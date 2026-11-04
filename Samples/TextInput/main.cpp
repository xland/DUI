// ============================================================================
//  TextInput/main.cpp — 文本输入控件演示（TextBox / 密码框 / TextArea）
//
//  演示：
//    · 单行 TextBox、密码 TextBox（● 掩码）、多行 TextArea（自动换行 + 滚动）
//    · 焦点与 Tab 链：Tab / Shift+Tab 在可聚焦控件间移动；焦点态由
//      When(State::Focused, ...) 驱动样式（背景变色）
//    · 键盘事件：元素级 OnKeyDown / OnChar（冒泡；控件内部行为与外部绑定并存）
//    · 语义事件：OnTextChanged / OnSubmit
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

// 输入框统一样式（焦点态变体：底色变化）
dui::Style InputStyle(float width, float height) {
    return dui::Style()
        .Width(width)
        .Height(height)
        .Padding(6.f)
        .Background(0xFFFFFFFF)
        .When(dui::State::Focused, dui::Style().Background(0xFFDCEEFF));
}

class TextInputWindow final : public dui::Window {
public:
    TextInputWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI TextInput",
              .width  = 620.f,
              .height = 560.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .Gap(8.f)
            .Padding(20.f)
            .Background(0xFFF3F4F6));

        root->AddChild<dui::Text>(L"文本输入：TextBox / 密码框 / TextArea",
                                  dui::Style().FontSize(15.f).FontWeight(dui::FontWeight::Bold));

        // ---- 单行输入框 ----
        root->AddChild<dui::Text>(L"单行输入框（回车 → OnSubmit；Ctrl+A/C/X/V/Z 生效）",
                                  dui::Style().FontSize(12.f).Color(0xFF6B7280));
        name_ = root->AddChild<dui::TextBox>(InputStyle(360.f, 32.f));
        name_->SetText(L"你好，DUI");
        name_->OnTextChanged([this](const dui::String&) { UpdateStatus(); });
        name_->OnSubmit([this]() {
            lastKey_ = L"提交：" + name_->GetText();
            UpdateStatus();
        });
        // 元素级键盘事件（与控件内部行为并存：内部先消费，绑定仍会收到）
        name_->OnKeyDown([this](dui::KeyEvent& e) {
            dui::String s = L"KeyDown VK=" + std::to_wstring(e.key);
            if (e.ctrl) s += L" Ctrl";
            if (e.shift) s += L" Shift";
            if (e.alt) s += L" Alt";
            lastKey_ = s;
            UpdateStatus();
        });
        name_->OnChar([this](dui::KeyEvent& e) {
            lastChar_ = e.ch;
            UpdateStatus();
        });
        name_->OnPointerDown([this](dui::PointerEvent&) { UpdateStatus(); });

        // ---- 密码框 ----
        root->AddChild<dui::Text>(L"密码框（● 掩码；真实文本不变）",
                                  dui::Style().FontSize(12.f).Color(0xFF6B7280));
        password_ = root->AddChild<dui::TextBox>(InputStyle(360.f, 32.f));
        password_->SetPassword(true);
        password_->OnTextChanged([this](const dui::String&) { UpdateStatus(); });

        // ---- 多行输入框 ----
        root->AddChild<dui::Text>(L"多行输入框（自动换行；回车换行；上下方向键移动光标）",
                                  dui::Style().FontSize(12.f).Color(0xFF6B7280));
        area_ = root->AddChild<dui::TextArea>(InputStyle(360.f, 130.f));
        area_->SetText(L"第一行\n第二行\n第三行");
        area_->OnTextChanged([this](const dui::String&) { UpdateStatus(); });

        // ---- 状态栏 ----
        status_ = root->AddChild<dui::Text>(L"", dui::Style().FontSize(12.f).Color(0xFF374151));
        UpdateStatus();
    }

private:
    void UpdateStatus() {
        if (status_ == nullptr) return;
        dui::String s;
        s += L"焦点：";
        s += FocusName();
        s += L"    单行长度：";
        s += std::to_wstring(name_->GetText().size());
        s += L"    多行长度：";
        s += std::to_wstring(area_->GetText().size());
        s += L"    最后按键：";
        s += lastKey_;
        if (lastChar_ != 0) {
            s += L"    最后字符：";
            s += lastChar_;
        }
        status_->SetText(s);
    }

    const wchar_t* FocusName() const {
        if (dui::FocusedElement() == name_) return L"单行输入框";
        if (dui::FocusedElement() == password_) return L"密码框";
        if (dui::FocusedElement() == area_) return L"多行输入框";
        return L"（无）";
    }

    dui::TextBox* name_ = nullptr;
    dui::TextBox* password_ = nullptr;
    dui::TextArea* area_ = nullptr;
    dui::Text* status_ = nullptr;
    dui::String lastKey_;
    wchar_t lastChar_ = 0;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    TextInputWindow window;
    return app.Run(window);
}
