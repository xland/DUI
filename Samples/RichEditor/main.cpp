// ============================================================================
//  RichEditor/main.cpp — P1 + P2 验收示例：JSON → RichDoc → RichEdit 渲染
//
//  左侧：Scroller > 白纸 Panel > RichEdit（块级排版 + 行内 run；图片为占位框）
//  右侧：解析大纲（块 / run 计数、Report 诊断、逐块预览）—— P1 的验收信息
//  样本 JSON 内嵌在下方，覆盖 RichEditor.md §2 的全部块与行内形态。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

namespace rt = dui::richtext;

// 样本文档（UTF-8）。末尾两处是故意放的坏数据，用来验证 Report 计数：
//   · "table" 块          → unknownBlocks
//   · "letterSpacing"     → unknownStyles，"fontSize": "big" → invalidValues
const char* kSampleDocJson = R"json({
  "settings": {
    "defaultFont": "Microsoft YaHei, sans-serif",
    "defaultFontSize": 15
  },
  "blocks": [
    {
      "type": "heading",
      "level": 1,
      "content": [
        { "type": "text", "text": "DUI 富文本", "styles": {
            "fontSize": "30px", "fontWeight": "bold", "color": "#1a1a1a" } }
      ],
      "styles": { "margin": "0 0 10px", "lineHeight": "1.3" }
    },
    {
      "type": "paragraph",
      "content": [
        { "type": "text", "text": "行内容器只贡献样式：" },
        { "type": "bold", "content": [ { "type": "text", "text": "粗体" } ] },
        { "type": "text", "text": "、" },
        { "type": "italic", "content": [ { "type": "text", "text": "斜体" } ] },
        { "type": "text", "text": "、" },
        { "type": "underline", "content": [ { "type": "text", "text": "下划线" } ] },
        { "type": "text", "text": "、" },
        { "type": "strikethrough", "content": [ { "type": "text", "text": "删除线" } ] },
        { "type": "text", "text": "，以及 " },
        { "type": "hyperlink", "href": "https://example.com",
          "content": [ { "type": "text", "text": "一个链接" } ],
          "styles": { "color": "#007bff", "textDecoration": "underline" } },
        { "type": "text", "text": "。" }
      ],
      "styles": { "lineHeight": "1.6", "margin": "0 0 12px" }
    },
    {
      "type": "paragraph",
      "content": "整段 content 是字符串（§2.2 已识别的不一致）。",
      "styles": { "textAlign": "center" }
    },
    {
      "type": "paragraph",
      "content": [
        { "type": "text", "text": "高亮盒：" },
        { "type": "text", "text": "padding 2px 6px、圆角 4px、字号 1.1em", "styles": {
            "backgroundColor": "#fff3cd", "padding": "2px 6px", "borderRadius": "4px",
            "fontSize": "1.1em", "fontFamily": "Arial, sans-serif" } }
      ],
      "styles": { "lineHeight": "24px", "margin": "0 0 12px" }
    },
    {
      "type": "list",
      "listType": "ordered",
      "items": [
        { "content": [ { "type": "text", "text": "第一项" } ] },
        { "content": [
            { "type": "text", "text": "第二项，带子列表：" },
            { "type": "list", "listType": "unordered", "items": [
                { "content": "子项 A" },
                { "content": "阿斯蒂芬" },
                { "content": "阿斯蒂芬" },
                { "content": "阿斯蒂芬" },
                { "content": "子项 B" }
            ] }
        ] },
        { "content": "第三项（content 为字符串）" }
      ],
      "styles": { "margin": "0 0 12px", "indent": "20px" }
    },
    {
      "type": "quote",
      "content": [ { "type": "text", "text": "左边框与背景都来自块级 styles。" } ],
      "attribution": "— DUI",
      "styles": { "borderLeft": "4px solid #007bff", "backgroundColor": "#f8f9fa",
                  "margin": "0 0 12px", "indent": "12px" }
    },
    {
      "type": "codeBlock",
      "language": "cpp",
      "showLineNumbers": true,
      "code": "int main() {\n    return 0;\n}"
    },
    {
      "type": "image",
      "src": "preview.png",
      "altText": "占位图",
      "width": "320px",
      "height": "auto",
      "alignment": "center",
      "styles": { "margin": "0 0 12px" }
    },
    {
      "type": "horizontalRule",
      "styles": { "margin": "16px 0", "backgroundColor": "#dee2e6" }
    },
    { "type": "table", "rows": [] },
    {
      "type": "paragraph",
      "content": [
        { "type": "text", "text": "坏值示范：fontSize 写成 big、letterSpacing 不在白名单",
          "styles": { "fontSize": "big", "letterSpacing": "1px" } }
      ]
    }
  ]
})json";

const wchar_t* KindName(rt::BlockKind kind) {
    switch (kind) {
    case rt::BlockKind::Paragraph: return L"paragraph";
    case rt::BlockKind::Heading:   return L"heading";
    case rt::BlockKind::List:      return L"list";
    case rt::BlockKind::ListItem:  return L"listItem";
    case rt::BlockKind::Quote:     return L"quote";
    case rt::BlockKind::Code:      return L"codeBlock";
    case rt::BlockKind::Image:     return L"image";
    case rt::BlockKind::Rule:      return L"horizontalRule";
    }
    return L"?";
}

size_t CountRuns(const rt::Block& block) {
    size_t count = block.runs.size();
    for (const rt::Block& child : block.children) count += CountRuns(child);
    return count;
}

// 单块一行：缩进 + 类型 + 计数 + 文字预览（换行压成空格）
void AppendOutline(const rt::Block& block, int depth, dui::String& out) {
    out.append(static_cast<size_t>(depth) * 2, L' ');
    out += KindName(block.kind);

    if (block.kind == rt::BlockKind::Heading) {
        out += L" L" + std::to_wstring(block.level);
    } else if (block.kind == rt::BlockKind::List) {
        out += (block.listType == rt::ListType::Ordered) ? L" ordered" : L" unordered";
    } else if (block.kind == rt::BlockKind::Image) {
        out += L" src=" + block.src;
    }

    out += L"  runs=" + std::to_wstring(CountRuns(block));
    if (!block.children.empty()) out += L" children=" + std::to_wstring(block.children.size());

    dui::String preview;
    for (const rt::Run& run : block.runs) preview += run.text;
    for (wchar_t& ch : preview) {
        if (ch == L'\n' || ch == L'\r') ch = L' ';
    }
    if (!preview.empty()) {
        if (preview.size() > 24) preview = preview.substr(0, 24) + L"…";
        out += L"  " + preview;
    }

    out += L"\n";
    for (const rt::Block& child : block.children) AppendOutline(child, depth + 1, out);
}

dui::String BuildSummary(const rt::RichDoc& doc, bool ok) {
    dui::String out = ok ? L"文档：解析成功" : L"文档：解析失败";

    out += L"\n默认字体：";
    out += doc.defaultFont.empty() ? L"（引擎默认）" : doc.defaultFont;
    out += L"    默认字号：";
    out += std::to_wstring(static_cast<int>(doc.defaultFontSize + 0.5f));

    size_t runs = 0;
    for (const rt::Block& block : doc.blocks) runs += CountRuns(block);
    out += L"\n块：" + std::to_wstring(doc.blocks.size()) + L"    runs：" + std::to_wstring(runs);

    const rt::Report& report = doc.report;
    out += L"\n\n诊断：未知块 " + std::to_wstring(report.unknownBlocks);
    out += L"    未知行内 " + std::to_wstring(report.unknownInlines);
    out += L"    未知样式 " + std::to_wstring(report.unknownStyles);
    out += L"    非法值 " + std::to_wstring(report.invalidValues);
    if (report.firstIssue.empty()) out += L"\n（无问题）";
    else out += L"\n首条问题：" + report.firstIssue;

    out += L"\n\n—— 块大纲 ——\n";
    for (const rt::Block& block : doc.blocks) AppendOutline(block, 0, out);
    return out;
}

// 顶栏一行：块 / run 计数 + 四类诊断 + 文档默认字体
dui::String StatusLine(const rt::RichDoc& doc, bool ok) {
    size_t runs = 0;
    for (const rt::Block& block : doc.blocks) runs += CountRuns(block);

    dui::String out = ok ? L"解析成功" : L"解析失败";
    out += L" · 块 " + std::to_wstring(doc.blocks.size());
    out += L" · runs " + std::to_wstring(runs);
    out += L" · 未知块 " + std::to_wstring(doc.report.unknownBlocks);
    out += L" · 未知行内 " + std::to_wstring(doc.report.unknownInlines);
    out += L" · 未知样式 " + std::to_wstring(doc.report.unknownStyles);
    out += L" · 非法值 " + std::to_wstring(doc.report.invalidValues);
    out += L" · 默认 ";
    out += doc.defaultFont.empty() ? dui::String(L"引擎默认") : doc.defaultFont;
    out += L" " + std::to_wstring(static_cast<int>(doc.defaultFontSize + 0.5f));
    return out;
}

class RichEditorWindow final : public dui::Window {
public:
    RichEditorWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"RichEditor — P2 只读渲染",
              .width  = 1120.f,
              .height = 780.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style().Background(0xFFF5F6F8));

        rt::RichDoc doc;
        const bool ok = rt::ParseRichDoc(dui::Utf8ToWide(kSampleDocJson), doc);

        // 顶栏：标题 + 解析诊断一行
        dui::Panel* header = root->AddChild<dui::Panel>(dui::Style()
            .Padding(dui::Thickness(20.f, 18.f, 20.f, 12.f))
            .Gap(4.f)
            .Background(0xFFFFFFFF));
        header->AddChild<dui::Text>(L"RichText P2 · JSON → RichDoc → RichEdit", dui::Style()
            .FontSize(20.f)
            .FontWeight(dui::FontWeight::Bold)
            .Color(0xFF1A1A1A));
        const dui::String status = StatusLine(doc, ok);
        header->AddChild<dui::Text>(status.c_str(), dui::Style()
            .FontSize(12.f)
            .Color(0xFF8A8A8A));

        // 主体：左 = 富文本渲染（Scroller > 白纸 > RichEdit），右 = 解析大纲
        dui::Panel* body = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .FlexGrow(1.f));

        dui::Scroller* page = body->AddChild<dui::Scroller>(dui::Style()
            .FlexGrow(1.f)
            .Background(0xFFF5F6F8));
        dui::Panel* sheet = page->AddChild<dui::Panel>(dui::Style()
            .Padding(dui::Thickness(28.f, 24.f, 28.f, 36.f))
            .Background(0xFFFFFFFF));
        dui::RichEdit* view = sheet->AddChild<dui::RichEdit>();
        view->SetDoc(doc);

        dui::Scroller* side = body->AddChild<dui::Scroller>(dui::Style()
            .Width(360.f)
            .Background(0xFFFFFFFF));
        const dui::String summary = BuildSummary(doc, ok);
        side->AddChild<dui::Text>(summary.c_str(), dui::Style()
            .Padding(dui::Thickness(14.f, 16.f, 14.f, 16.f))
            .FontSize(12.f)
            .LineHeight(1.6f)
            .Color(0xFF333333));
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    RichEditorWindow window;
    return app.Run(window);
}
