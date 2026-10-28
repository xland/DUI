// ============================================================================
//  Samples/main.cpp —— 三栏阅读器示例
//
//  用途：本文件同时充当"目标 API 的可执行规格"。DUI 库尚未实现，代码按设计
//        文档给出的 API 书写，**当前无法编译/链接**；它的价值是在实现前校准
//        API 的书写手感与使用成本（ADR-015：开发体验优先）。
//
//  布局：左（菜单，定宽 220） | 中（文章列表，定宽 320） | 右（正文，占满剩余）
//
//  约定：框架符号一律 `dui::` 限定，不使用 `using namespace dui;`
//        —— 调用点一眼可见哪些来自框架、哪些来自标准库或本文件。
//
//  覆盖能力（括号为设计文档章节）：
//    · 派生窗口类 + OnCreate 钩子（无边框/自定义标题栏的同一扩展点） 01-Layering §2
//    · AddChild<T>(..., style)：创建即定型，单次样式可内联          03-Style §6.1
//    · 匿名样式（默认）+ 派生 + 共享，状态变体与过渡动画             03-Style §6、05-Animation §3
//    · Flex 三栏布局：定宽 + 弹性宽 + 可收缩                          04-Layout §4.1
//    · 事件绑定（成员函数 + 上下文）、e.source、handled                06-Events §4、§5
//    · 后台加载 → 回投 UI 线程（协程，体验主路径）                     07-Threading §4
//    · 遍历外读样式 dui::Style::Resolve                              15-Properties §5.2
//    · 无异常错误传递（dui::Result<T>）                               ADR-007
//    · 全 UTF-16 文本（L"..."）                                       ADR-026
//
//  注：列表未用"虚拟化列表"——其元素复用/回收机制尚未设计（ADR-017 暂缓事项），
//      此处用 ScrollView + 固定 4 条数据演示。
// ============================================================================

#include "DUI/DUI.h"

namespace {

// ---------------------------------------------------------------------------
// 1. 可复用样式（03-Style：匿名 + 派生 + 共享；一个 Style 被 N 个元素引用只存一份）
//
//  默认匿名：不进注册表、无字符串驻留、不需要起名。
//  只有"要全局覆盖控件默认外观"或"要被主题按名替换"时才具名 —— 名字是注册表
//  键与调试标识，**不参与任何匹配**（03-Style D3：不做选择器）。
//    具名示例：const dui::Style kButton = dui::StyleBuilder("button").Padding({8, 16}).Build();
// ---------------------------------------------------------------------------

// 容器的静态外观（含布局属性）全部在 Style 里描述，元素只 SetStyle —— 
// 避免"背景在样式里、宽高在 Set 里"这种把同一元素外观拆两处的写法。
const dui::Style kAppRoot = dui::StyleBuilder()
    .Display(dui::Display::Flex)
    .FlexDirection(dui::FlexDirection::Row)
    .Background(0xFF1E1E1E)
    .Color(0xFFE6E6E6)
    .FontSize(14.f)
    .Build();

const dui::Style kSidebar = dui::StyleBuilder()
    .Width(220.f)
    .FlexShrink(0.f)                // 定宽不被压缩
    .Background(0xFF252526)
    .Padding({ 12.f, 12.f, 12.f, 12.f })
    .Gap(4.f)
    .Build();

const dui::Style kBrand = dui::StyleBuilder(kSidebar)      // 派生：只写差异
    .FontSize(13.f)
    .Color(0xFF9D9D9D)
    .Build();

// 状态变体 + 过渡：hover / selected 各自换背景，切换时 120ms 补间（05-Animation §3）
const dui::Style kMenuItem = dui::StyleBuilder()
    .Padding({ 8.f, 10.f, 8.f, 10.f })
    .CornerRadius(4.f)
    .Color(0xFFCCCCCC)
    .Transition(dui::Prop::BackgroundColor, 120, dui::Easing::EaseOut)
    .When(dui::State::Hover,    dui::StyleBuilder().Background(0xFF2A2D2E))
    .When(dui::State::Pressed,  dui::StyleBuilder().Background(0xFF3A3D3E))
    .When(dui::State::Selected, dui::StyleBuilder().Background(0xFF094771).Color(0xFFFFFFFF))
    .Build();

const dui::Style kListColumn = dui::StyleBuilder()
    .Width(320.f)
    .FlexShrink(0.f)
    .Background(0xFF1E1E1E)
    .Padding({ 12.f, 12.f, 12.f, 12.f })
    .Gap(8.f)
    .Build();

const dui::Style kCard = dui::StyleBuilder()
    .FlexDirection(dui::FlexDirection::Column)
    .Background(0xFF252526)
    .Padding({ 12.f, 12.f, 12.f, 12.f })
    .CornerRadius(6.f)
    .Gap(4.f)
    .Transition(dui::Prop::BackgroundColor, 120, dui::Easing::EaseOut)
    .When(dui::State::Hover,    dui::StyleBuilder().Background(0xFF2D2D30))
    .When(dui::State::Selected, dui::StyleBuilder().Background(0xFF094771))
    .Build();

const dui::Style kCardTitle = dui::StyleBuilder()
    .FontSize(14.f)
    .FontWeight(dui::FontWeight::SemiBold)
    .Color(0xFFEAEAEA)
    .Build();

const dui::Style kCardSummary = dui::StyleBuilder()
    .FontSize(12.f)
    .Color(0xFFA0A0A0)
    .Build();

const dui::Style kCardTime = dui::StyleBuilder()
    .FontSize(11.f)
    .Color(0xFF707070)
    .Build();

const dui::Style kArticleColumn = dui::StyleBuilder()
    .FlexGrow(1.f)                  // 占满剩余宽度
    .MinWidth(0.f)                  // 允许收缩，避免长文本撑破
    .Background(0xFF1B1B1B)
    .Padding({ 24.f, 20.f, 24.f, 20.f })
    .Gap(12.f)
    .Build();

// 纯布局样式：把"占满剩余空间 + 允许收缩"抽成一份，滚动区共用
const dui::Style kFill = dui::StyleBuilder()
    .FlexGrow(1.f)
    .MinWidth(0.f)
    .Build();

const dui::Style kArticleTitle = dui::StyleBuilder(kAppRoot)   // 派生自根样式
    .FontSize(24.f)
    .FontWeight(dui::FontWeight::SemiBold)
    .Build();

const dui::Style kArticleBody = dui::StyleBuilder()
    .FontSize(15.f)
    .Color(0xFFD0D0D0)
    .LineHeight(1.6f)
    .Build();

// ---------------------------------------------------------------------------
// 2. 数据（文本统一 UTF-16，字面量用 L""，与 DWrite / Win32 零转码 —— ADR-026）
// ---------------------------------------------------------------------------

enum class Category { All, Tech, Essay };

struct Article final {
    const wchar_t* id;
    const wchar_t* title;
    const wchar_t* summary;
    const wchar_t* time;
    Category       category;
};

const Article kArticles[] = {
    { L"a1", L"样式系统的三个层次",   L"共享、派生、继承分别解决什么问题",        L"2026-10-24", Category::Tech  },
    { L"a2", L"HTML 风格布局的取舍",  L"为什么不做 margin 塌陷，以及单遍约束的代价", L"2026-10-25", Category::Tech  },
    { L"a3", L"退出浏览器的那个下午", L"关于工具与手感的一些随笔",                L"2026-10-26", Category::Essay },
    { L"a4", L"动画的尽头是克制",     L"从 120ms 说起",                          L"2026-10-27", Category::Essay },
};

constexpr int kArticleCount = static_cast<int>(sizeof(kArticles) / sizeof(kArticles[0]));

// 业务函数：读磁盘 / 网络，纯数据，不碰 UI（07-Threading §3.1）
// M1 占位：真实实现做文件 IO / 网络请求，这里返回固定文案让协程链路可运行。
dui::String FetchBodyFromDisk(const wchar_t* id) {
    (void)id;
    return dui::String(L"（正文内容：M4 接入 Dispatcher 后由真实数据源填充。）");
}

// ---------------------------------------------------------------------------
// 3. 异步加载（07-Threading §4.2）
//    协程是体验主路径；切换点为命名空间内函数，写法上就是一个"动作"。
// ---------------------------------------------------------------------------

dui::Task<dui::Result<dui::String>> LoadBodyAsync(const wchar_t* id) {
    co_await dui::Background();                    // 切到线程池
    dui::String body = FetchBodyFromDisk(id);      // 子线程：只处理纯数据
    co_await dui::Foreground();                    // 切回 UI 线程
    co_return body;
}

// ---------------------------------------------------------------------------
// 4. 窗口：三栏
//
//  窗口类同时承担"声明窗口形态"（构造）与"搭界面"（OnCreate），界面状态直接
//  是窗口成员 —— 不再需要额外的 Ui 类 + Build(window) 手工连接。
//  无边框 / 自定义标题栏走同一个扩展点（构造里换 Chrome，OnCreate 里加标题栏）。
// ---------------------------------------------------------------------------

class ReaderWindow final : public dui::Window {
public:
    ReaderWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI 示例 · 三栏阅读器",
              .width  = 1200.f,
              .height = 760.f,
          }) {}

protected:
    void OnCreate() override;                      // 原生窗口已建、尚未显示

private:
    void OnMenuClick(dui::ClickEvent& e);
    void OnCardClick(dui::ClickEvent& e);
    void SelectCategory(Category c);
    void ShowArticle(int index);

    dui::Button* menuItems_[3]{};
    Category     menuCategories_[3]{ Category::All, Category::Tech, Category::Essay };
    dui::Panel*  cards_[kArticleCount]{};

    dui::Text*   articleTitle_ = nullptr;
    dui::Text*   articleMeta_  = nullptr;
    dui::Text*   articleBody_  = nullptr;
};

void ReaderWindow::OnCreate() {
    dui::Panel* root = Root();
    root->SetStyle(kAppRoot);                      // 布局也在这里，见 kAppRoot

    // ---- 左：菜单 -----------------------------------------------------------
    dui::Panel* sidebar = root->AddChild<dui::Panel>(kSidebar);   // 宽 220 + shrink 0 在样式里

    sidebar->AddChild<dui::Text>(L"DUI 阅读器", kBrand);

    const wchar_t* menuLabels[3] = { L"全部文章", L"技术", L"随笔" };
    for (int i = 0; i < 3; ++i) {
        dui::Button* item = sidebar->AddChild<dui::Button>(menuLabels[i], kMenuItem);
        item->OnClick(this, &ReaderWindow::OnMenuClick);   // 成员函数 + 上下文（06-Events §4.1）
        menuItems_[i] = item;
    }

    // ---- 中：文章列表 -------------------------------------------------------
    dui::Panel* listColumn = root->AddChild<dui::Panel>(kListColumn);

    // 用一次的样式直接内联，不起名（03-Style §6.1）
    listColumn->AddChild<dui::Text>(L"文章", dui::StyleBuilder()
        .FontSize(12.f).Color(0xFF9D9D9D));

    dui::ScrollView* listScroll = listColumn->AddChild<dui::ScrollView>(kFill);

    for (int i = 0; i < kArticleCount; ++i) {
        dui::Panel* card = listScroll->AddChild<dui::Panel>(kCard);

        card->AddChild<dui::Text>(kArticles[i].title,   kCardTitle);
        card->AddChild<dui::Text>(kArticles[i].summary, kCardSummary);
        card->AddChild<dui::Text>(kArticles[i].time,    kCardTime);

        // 点击冒泡到卡片（行内文字不拦截），父级统一处理（06-Events §5）
        card->OnClick(this, &ReaderWindow::OnCardClick);
        cards_[i] = card;
    }

    // ---- 右：正文 -----------------------------------------------------------
    dui::Panel* articleColumn = root->AddChild<dui::Panel>(kArticleColumn);

    articleTitle_ = articleColumn->AddChild<dui::Text>(L"", kArticleTitle);
    articleMeta_  = articleColumn->AddChild<dui::Text>(L"", dui::StyleBuilder()
        .FontSize(12.f).Color(0xFF8A8A8A));

    dui::ScrollView* bodyScroll = articleColumn->AddChild<dui::ScrollView>(kFill);
    articleBody_ = bodyScroll->AddChild<dui::Text>(L"", kArticleBody);

    SelectCategory(Category::All);
    ShowArticle(0);
}

// ---------------------------------------------------------------------------
// 5. 交互
// ---------------------------------------------------------------------------

void ReaderWindow::OnMenuClick(dui::ClickEvent& e) {
    for (int i = 0; i < 3; ++i) {
        if (menuItems_[i] == e.source) {           // e.source：命中的目标元素（06-Events §4.3）
            SelectCategory(menuCategories_[i]);
            e.handled = true;                      // 终止后续冒泡
            return;
        }
    }
}

void ReaderWindow::OnCardClick(dui::ClickEvent& e) {
    for (int i = 0; i < kArticleCount; ++i) {
        if (cards_[i] == e.source) {
            ShowArticle(i);
            e.handled = true;
            return;
        }
    }
}

void ReaderWindow::SelectCategory(Category c) {
    // 状态位驱动样式变体（03-Style §6.2）：背景过渡由 kMenuItem 的 Transition 完成。
    // 全量写即可 —— SetState 相同值幂等、标脏帧末合并，不必先摘旧项。
    for (int i = 0; i < 3; ++i) {
        menuItems_[i]->SetState(dui::State::Selected, menuCategories_[i] == c);
    }

    // 过滤：Display::None 从布局中移除（15-Properties §4.4）
    // 这里是**运行时动态值**（随筛选变化），不是静态外观，所以走 Set(Prop) 而
    // 不是样式 —— Set 只负责"当下这一帧的值"，静态外观一律留在 Style 里。
    for (int i = 0; i < kArticleCount; ++i) {
        const bool show = (c == Category::All) || (kArticles[i].category == c);
        cards_[i]->Set(dui::Prop::Display, show ? dui::Display::Flex : dui::Display::None);
    }
}

void ReaderWindow::ShowArticle(int index) {
    const Article& a = kArticles[index];

    articleTitle_->SetText(a.title);
    articleMeta_->SetText(a.time);
    articleBody_->SetText(L"加载中…");

    // 安全回投：元素在回调前被销毁则丢弃结果（06-Events D5 / 07-Threading §4.3）
    auto life = articleBody_->Lifetime();
    LoadBodyAsync(a.id).Then([this, life](dui::Result<dui::String> r) {
        if (!life) return;                         // 元素已销毁
        articleBody_->SetText(r ? r.Value() : dui::String(L"加载失败"));
    }).Start();

    // 遍历外读样式（15-Properties §5.2）：非继承属性只查元素自身链
    const dui::Color bg = dui::Style::Resolve(articleBody_, dui::Prop::BackgroundColor);
    (void)bg;

    for (int i = 0; i < kArticleCount; ++i) {
        cards_[i]->SetState(dui::State::Selected, i == index);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 6. 入口
// ---------------------------------------------------------------------------

int main() {
    dui::Application app;
    ReaderWindow window;                           // 构造只声明窗口，不创建原生窗口
    return app.Run(window);                        // 建 HWND → OnCreate → Show → 循环 → 退出码
}
