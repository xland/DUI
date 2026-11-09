// ============================================================================
//  Dialog/main.cpp — 系统对话框示例（17-Dialogs / Doc/Dialogs.md）
//
//  四个能力：打开文件（多选）、保存文件、选择文件夹、消息警告；
//  另演示 Warn / Confirm 两个便捷糖。结果统一走三态 DialogResult。
//  六个可点击项统一用 kButton / kButtonHover 做成带背景与 hover 效果的按钮。
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带；
//  <windows.h> 必须先于 DUI/DUI.h（MessageBox 同名宏，见 17-Dialogs §4.4）。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

// C++20：数组直接传成 std::span，不需要 filterCount。
// span 是视图，底层数组必须活到调用返回 —— 用 static/具名数组最稳妥。
static constexpr dui::FileTypeFilter kFilters[] = {
    { L"文本文件", L"*.txt;*.md" },
    { L"图片文件", L"*.png;*.jpg;*.gif" },
    { L"所有文件", L"*.*" },
};

// dui::Button 只是可点击文本、没有内建外观；背景与 hover 全靠 Style + 状态变体。
static const dui::Style kButton = dui::Style()
    .FontSize(13.f)
    .Color(0xFFFFFFFF)
    .Background(0xFF374151)
    .Padding(dui::Thickness(6.f, 12.f))     // 上下 6 / 左右 12
    .CornerRadius(4.f)
    .TextAlign(dui::TextAlign::Center)
    .Cursor(dui::Cursor::Hand)
    .FlexShrink(0.f);

static const dui::Style kButtonHover = dui::Style()
    .Background(0xFF1F6FE5)
    .Color(0xFFFFF2CC);

class DialogWindow final : public dui::Window {
public:
    DialogWindow()
        : dui::Window(dui::WindowOptions{
              .title = L"DUI 对话框示例",
              .width = 560.f,
              .height = 360.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .Gap(10.f).Padding(24.f)
            .Background(0xFFF5F5F5));
        result_ = root->AddChild<dui::Text>(L"（尚未选择）", dui::Style().FontSize(16.f));

        // ① 打开文件（多选 + 过滤器）
        root->AddChild<dui::Button>(L"打开文件…", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                auto r = OpenFile(dui::OpenFileOptions{
                    .title = L"选择文件",
                    .filters = kFilters,
                    .filterIndex = 0,
                    .multiSelect = true,
                });
                if (r.IsCancelled()) { result_->SetText(L"已取消"); return; }
                if (r.IsError())    { result_->SetText(r.Error()); return; }
                dui::String s = L"已选择：";
                for (const dui::String& p : r.Value()) s += L"\n" + p;
                result_->SetText(s);
            });

        // ② 保存文件（只拿路径，不写文件）
        root->AddChild<dui::Button>(L"保存文件…", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                auto r = SaveFile(dui::SaveFileOptions{
                    .title = L"保存为",
                    .filters = kFilters,
                    .defaultExtension = L"txt",
                });
                if (r.IsCancelled()) { result_->SetText(L"已取消"); return; }
                if (r.IsError())    { result_->SetText(r.Error()); return; }
                result_->SetText(L"目标：" + r.Value() + L"（框架未创建文件）");
            });

        // ③ 选择文件夹
        root->AddChild<dui::Button>(L"选择文件夹…", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                auto r = OpenFolder(dui::OpenFolderOptions{ .title = L"选择目录" });
                if (r.IsOk())              result_->SetText(L"目录：" + r.Value());
                else if (!r.IsCancelled()) result_->SetText(r.Error());
            });

        // ④ 警告 / 确认
        root->AddChild<dui::Button>(L"警告", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                auto r = MessageBox(dui::MessageOptions{
                    .text = L"确定要删除所选内容吗？",
                    .title = L"警告",
                    .icon = dui::MessageIcon::Warning,
                    .buttons = dui::MessageButtons::YesNo,
                    .defaultButton = 2,
                });
                result_->SetText(r == dui::MessageResult::Yes ? L"选择了：是" : L"选择了：否");
            });

        // 便捷糖：一行警告 / 一行确认
        root->AddChild<dui::Button>(L"简单警告", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                Warn(L"操作已完成。");
            });
        root->AddChild<dui::Button>(L"简单确认", kButton)
            ->When(dui::State::Hover, kButtonHover)
            .OnClick([this](dui::PointerEvent&) {
                result_->SetText(Confirm(L"要继续吗？") ? L"继续" : L"取消");
            });
    }

private:
    dui::Text* result_ = nullptr;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    DialogWindow window;
    return app.Run(window);
}
