// ============================================================================
//  Samples/ListSelect.cpp —— 单选列表示例
//
//  用途：与 main.cpp 一样充当"目标 API 的可执行规格"。DUI 库尚未实现，本文件
//        按设计文档给出的 API 书写，**当前无法编译/链接**。
//
//  场景：窗口里只有一个列表（设置项），任意时刻至多一项处于选中态。
//
//  约定：
//    · 框架符号一律 `dui::` 限定，不使用 `using namespace dui;`
//    · 窗口 = 一个继承 dui::Window 的类；界面在 OnCreate() 里搭
//    · 静态外观（含布局属性）全部写在 Style 里；用一次的样式直接内联
//    · `Set(Prop::X, v)` 只用于运行时动态值
//
//  覆盖能力（括号为设计文档章节）：
//    · 派生窗口类 + OnCreate 钩子（无边框/自定义标题栏的同一扩展点）  01-Layering §2
//    · AddChild<T>(..., style)：创建即定型，样式可内联              03-Style §6.1
//    · 单选：程序化状态位 SetState(Selected)，不变量"至多一项"       03-Style §6.2、06-Events D6
//    · 状态变体 + 过渡：hover / pressed / selected 背景与文字补间    05-Animation §3
//    · 继承属性跟随：子文本不写 color，随列表项的 color 一起变       15-Properties §4.1、03-Style §5
//    · Flex 布局：Row 项 + Column 文本块 + 尾部占满                  04-Layout §4.1
// ============================================================================

#include "DUI/DUI.h"

namespace {

// ---------------------------------------------------------------------------
// 1. 样式
//
//  规则：用一次的样式内联；"被多个元素共享"或"带状态变体/过渡"的才起名。
//
//  kItem 的 .Color(...) 是继承属性：列表项选中变白时，没写过 color 的子文本
//  （序号、标题、描述）自动跟随 —— 不需要逐个给子元素 SetState，也不需要为
//  选中态准备第二套文字样式。
// ---------------------------------------------------------------------------

const dui::Style kItem = dui::StyleBuilder()
    .FlexDirection(dui::FlexDirection::Row)
    .Gap(12.f)
    .Padding({ 12.f, 10.f, 12.f, 10.f })
    .CornerRadius(6.f)
    .Color(0xFFCCCCCC)
    .Transition(dui::Prop::BackgroundColor, 120, dui::Easing::EaseOut)
    .Transition(dui::Prop::Color, 120, dui::Easing::EaseOut)
    .When(dui::State::Hover,    dui::StyleBuilder().Background(0xFF2A2D2E))
    .When(dui::State::Pressed,  dui::StyleBuilder().Background(0xFF3A3D3E))
    .When(dui::State::Selected, dui::StyleBuilder().Background(0xFF094771).Color(0xFFFFFFFF))
    .Build();

// ---------------------------------------------------------------------------
// 2. 数据
// ---------------------------------------------------------------------------

struct Setting final {
    const wchar_t* title;
    const wchar_t* desc;
};

const Setting kSettings[] = {
    { L"外观",   L"主题、字号与界面密度" },
    { L"编辑器", L"缩进宽度、自动换行、保存时格式化" },
    { L"快捷键", L"查看与修改按键绑定" },
    { L"同步",   L"把设置同步到其它设备" },
    { L"关于",   L"版本信息与更新检查" },
};

constexpr int kSettingCount = static_cast<int>(sizeof(kSettings) / sizeof(kSettings[0]));

// 序号列：与 kSettings 一一对应（元素少，直接写死，不做格式化）
const wchar_t* const kIndexText[kSettingCount] = { L"1", L"2", L"3", L"4", L"5" };

// ---------------------------------------------------------------------------
// 3. 窗口
//
//  窗口类承担三件事：声明窗口形态（构造）、搭界面（OnCreate）、持有交互状态。
//  界面状态直接是窗口成员 —— 不再需要一个额外的 Ui 类 + Build(window) 手工连接。
// ---------------------------------------------------------------------------

class ListWindow final : public dui::Window {
public:
    ListWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI 示例 · 单选列表",
              .width  = 560.f,
              .height = 520.f,
              // 无边框 + 自定义标题栏（将来）只需在这里换形态：
              // .chrome = dui::Chrome::Borderless,
          }) {}

protected:
    void OnCreate() override;

private:
    void OnItemClick(dui::ClickEvent& e);
    void Select(int index);                        // 唯一的选中入口

    dui::Panel* items_[kSettingCount]{};
    dui::Text*  status_ = nullptr;

    int selected_ = -1;                            // -1 = 无选中
};

// 此时原生窗口已创建、尚未显示：可以自由搭界面，用户看不到中间过程。
void ListWindow::OnCreate() {
    dui::Panel* root = Root();
    root->SetStyle(dui::StyleBuilder()
        .Display(dui::Display::Flex)
        .FlexDirection(dui::FlexDirection::Column)     // 交叉轴默认 Stretch → 卡片撑满宽度
        .Padding({ 24.f, 20.f })
        .Gap(4.f)
        .Background(0xFF1E1E1E)
        .Color(0xFFE6E6E6)
        .FontSize(14.f));

    root->AddChild<dui::Text>(L"设置", dui::StyleBuilder()
        .FontSize(17.f).FontWeight(dui::FontWeight::SemiBold));
    root->AddChild<dui::Text>(L"整个列表只能有一项处于选中状态", dui::StyleBuilder()
        .FontSize(12.f).Opacity(0.55f));

    dui::Panel* card = root->AddChild<dui::Panel>(dui::StyleBuilder()
        .FlexDirection(dui::FlexDirection::Column)
        .Padding(8.f)
        .Gap(2.f)
        .Background(0xFF252526)
        .CornerRadius(8.f));

    for (int i = 0; i < kSettingCount; ++i) {
        dui::Panel* item = card->AddChild<dui::Panel>(kItem);   // 同一份 Style，N 项共享

        item->AddChild<dui::Text>(kIndexText[i], dui::StyleBuilder()
            .Width(20.f).FontSize(12.f).Opacity(0.5f));

        dui::Panel* body = item->AddChild<dui::Panel>(dui::StyleBuilder()
            .FlexDirection(dui::FlexDirection::Column)
            .FlexGrow(1.f)
            .MinWidth(0.f)                                  // 允许收缩，长描述不撑破
            .Gap(2.f));

        body->AddChild<dui::Text>(kSettings[i].title);       // 字号/颜色继承自 kItem
        body->AddChild<dui::Text>(kSettings[i].desc, dui::StyleBuilder()
            .FontSize(12.f).Opacity(0.65f));

        // 点击冒泡到列表项，父级统一处理（06-Events §5）
        item->OnClick(this, &ListWindow::OnItemClick);
        items_[i] = item;
    }

    // 底部：显示当前选中项的标题，给"单选"一个可见结果
    dui::Panel* footer = root->AddChild<dui::Panel>(dui::StyleBuilder()
        .FlexDirection(dui::FlexDirection::Row)
        .Gap(4.f));

    footer->AddChild<dui::Text>(L"当前选中：", dui::StyleBuilder()
        .FontSize(12.f).Color(0xFF8A8A8A));
    status_ = footer->AddChild<dui::Text>(L"", dui::StyleBuilder()
        .FontSize(12.f).Color(0xFF7FB4FF));

    Select(0);                                              // 初始选中第一项
}

// ---------------------------------------------------------------------------
// 4. 单选逻辑
// ---------------------------------------------------------------------------

void ListWindow::Select(int index) {
    if (index < 0 || index >= kSettingCount) return;
    if (index == selected_) return;                // 幂等：同样的选中不重复标脏

    // 单选不变量靠"全量写"保证：旧项被显式写成 false，不依赖"记得去摘旧的"。
    // SetState 相同值幂等、标脏在帧末合并，因此同一帧内写完不会出现双高亮的中间态。
    selected_ = index;
    for (int i = 0; i < kSettingCount; ++i) {
        items_[i]->SetState(dui::State::Selected, i == index);
    }

    status_->SetText(kSettings[index].title);
}

void ListWindow::OnItemClick(dui::ClickEvent& e) {
    for (int i = 0; i < kSettingCount; ++i) {
        if (items_[i] == e.source) {                // e.source：命中的目标元素（06-Events §4.3）
            Select(i);
            e.handled = true;                       // 终止后续冒泡
            return;
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 5. 入口
//
//  main 里只剩两件事：运行时对象、窗口对象。创建/显示/消息循环/退出码由
//  Application::Run 负责；窗口创建失败时返回非 0，可用 app.LastError() 查原因。
// ---------------------------------------------------------------------------

int main() {
    dui::Application app;
    ListWindow window;                             // 构造只声明窗口，不创建原生窗口
    return app.Run(window);
}
