// ============================================================================
//  CustomCtrl/main.cpp — 自定义控件演示（03-Style §6.3、06-Events §3.2）
//
//  一个可拖拽的 ValueSlider，完整走一遍自定义控件的链路：
//    派生 dui::Element → AddChild<T>() 挂入元素树
//    → MeasureContent 自己报自然尺寸（不设 Width/Height 也能参与布局）
//    → OnPaint 用 Painter 的矩形 / 形状指令自绘（DIP 坐标 + scale）
//    → HandlePointerDown/Move/Up + SetCapture 实现拖拽
//    → InvalidatePaint 只标 Paint 脏（不重排）刷新
//    → ResolveProperty 读样式属性，配合 Element::When 状态变体
//    → OnChanged 把值变化交给外部（控件不耦合业务 UI）
//
//  构建产物：Build\CustomCtrl\Debug|Release\CustomCtrl.exe
//  注意：DUI/DUI.h 已聚合 Graphics 层，graphics::Painter 的完整类型随公共头
//        一并给出，本文件不再单独包含框架内部头。
// ============================================================================

#include <windows.h>

#include <functional>
#include <string>
#include <utility>

#include "DUI/DUI.h"

namespace {

// ---- 自定义控件 ------------------------------------------------------------
class ValueSlider final : public dui::Element {
public:
    float Value() const noexcept { return value_; }

    void SetValue(float v) {
        const float nv = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        if (nv == value_) return;
        value_ = nv;
        InvalidatePaint();          // 只标 Paint 脏：不触发 Measure / Arrange
        if (onChanged_) onChanged_(value_);
    }

    void OnChanged(std::function<void(float)> f) { onChanged_ = std::move(f); }

    void OnPaint(dui::graphics::Painter& p, const dui::StyleContext& ctx, float scale) override {
        const dui::Rect b = Bounds();   // 绝对布局坐标（DIP），滚动原点由 Painter 状态槽处理
        const dui::Color accent = dui::ResolveProperty(this, dui::Prop::Color, ctx).u;
        const float x0 = b.x + 16.f;
        const float cy = b.y + b.h * 0.5f;
        const float span = b.w - 32.f;
        const float knobX = x0 + span * value_;

        p.FillRoundedRect(dui::Rect{x0, cy - 4.f, span, 8.f},
                          4.f, 0xFFD4D4D4, 1.f, scale);
        p.FillRoundedRect(dui::Rect{x0, cy - 4.f, span * value_, 8.f},
                          4.f, accent, 1.f, scale);
        for (int i = 0; i <= 4; ++i) {
            const float tx = x0 + span * static_cast<float>(i) / 4.f;
            p.StrokeLine(dui::Vec2{tx, cy + 12.f}, dui::Vec2{tx, cy + 18.f},
                         0xFF999999, 1.f, scale);
        }
        p.FillEllipse(dui::Vec2{knobX, cy}, 11.f, 11.f, accent, scale);
        p.StrokeEllipse(dui::Vec2{knobX, cy}, 11.f, 11.f, 0xFFFFFFFF, 2.f, scale);
    }

    // 元素内部事件钩子：命中链上无条件调用；返回 true 消费并终止冒泡。
    bool HandlePointerDown(dui::PointerEvent& ev) override {
        dragging_ = true;
        SetCapture();               // 拖出控件边界仍能收到 Move / Up
        SetValueAt(ev.pos.x);
        return true;
    }
    bool HandlePointerMove(dui::PointerEvent& ev) override {
        if (!dragging_) return false;
        SetValueAt(ev.pos.x);
        return true;
    }
    bool HandlePointerUp(dui::PointerEvent&) override {
        if (!dragging_) return false;
        dragging_ = false;          // 捕获由框架在抬起时自动释放
        return true;
    }

protected:
    // 自然尺寸（内容盒）：不设 Width/Height 时由它决定最终尺寸，Padding 由布局外加。
    dui::Vec2 MeasureContent(const dui::StyleContext&, const dui::Vec2&) const override {
        return dui::Vec2{280.f, 56.f};
    }

private:
    void SetValueAt(float x) {
        const dui::Rect b = Bounds();
        SetValue((x - (b.x + 16.f)) / (b.w - 32.f));
    }

    float value_ = 0.35f;
    bool dragging_ = false;
    std::function<void(float)> onChanged_;
};

// ---- 窗口 ------------------------------------------------------------------
class CustomCtrlWindow final : public dui::Window {
public:
    CustomCtrlWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"CustomCtrl · 自定义控件演示",
              .width  = 520.f,
              .height = 360.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Padding(20.f)
            .Gap(12.f)
            .Background(0xFFF7F7F7)
            .Color(0xFF222222)
            .FontSize(14.f));

        root->AddChild<dui::Text>(L"自定义控件 · ValueSlider",
            dui::Style().FontSize(18.f));
        root->AddChild<dui::Text>(
            L"派生 Element：自然尺寸 / OnPaint 自绘 / 指针拖拽 / 状态变体 / 变更回调",
            dui::Style().FontSize(12.f).Color(0xFF666666));

        dui::Panel* card = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Gap(10.f).Padding(16.f)
            .Background(0xFFFFFFFF).CornerRadius(6.f));

        ValueSlider* slider = card->AddChild<ValueSlider>(dui::Style()
            .Background(0xFFEFEFEF).CornerRadius(6.f).Padding(4.f)
            .Color(0xFF0064B4));
        slider->When(dui::State::Hover, dui::Style().Color(0xFFD40000));

        dui::Text* readout = card->AddChild<dui::Text>(L"", dui::Style().FontSize(13.f));
        auto Show = [readout](float v) {
            const int pct = static_cast<int>(v * 100.f + 0.5f);
            readout->SetText((L"当前值：" + std::to_wstring(pct)
                              + L"%    （点击轨道或拖动滑块）").c_str());
        };
        slider->OnChanged(Show);
        Show(slider->Value());

        root->AddChild<dui::Text>(
            L"悬停时强调色由 When(State::Hover, …) 切换；滑钮刷新只标 Paint 脏，不重排自身。",
            dui::Style().FontSize(12.f).Color(0xFF666666));
    }
};

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::Application app;
    CustomCtrlWindow window;
    return app.Run(window);
}
