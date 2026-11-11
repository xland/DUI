// ============================================================================
//  RichEditor/main.cpp — P1 + P2 验收示例：JSON → RichDoc → RichEditor 渲染
//
//  Scroller > 白纸 Panel > RichEditor（块级排版 + 行内 run；图片为占位框）
//  样本 JSON 内嵌在下方，覆盖 RichEditor.md §2 的全部块与行内形态。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

namespace rt = dui::richtext;

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
      "styles": { "borderLeft": "4px solid #555555", "backgroundColor": "#f8f9fa",
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
      "src": "data:image/png;base64,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",
      "altText": "示例图（base64 data URI，解码失败时显示本文字）",
      "width": "320px",
      "height": "auto",
      "alignment": "center",
      "styles": { "margin": "0 0 12px" }
    },
    {
      "type": "horizontalRule",
      "styles": { "margin": "16px 0", "backgroundColor": "#dee2e6" }
    }
  ]
})json";

class RichEditorWindow final : public dui::Window {
public:
    RichEditorWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"RichEditor",
              .width  = 1120.f,
              .height = 780.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style().Background(0xFFF5F6F8));

        rt::RichDoc doc;
        rt::ParseRichDoc(dui::Utf8ToWide(kSampleDocJson), doc);

        dui::Scroller* page = root->AddChild<dui::Scroller>(dui::Style()
            .FlexGrow(1.f)
            .Background(0xFFF5F6F8));
        dui::Panel* sheet = page->AddChild<dui::Panel>(dui::Style()
            .Padding(dui::Thickness(28.f, 24.f, 28.f, 36.f))
            .Background(0xFFFFFFFF));
        dui::RichEditor* view = sheet->AddChild<dui::RichEditor>();
        view->SetDoc(doc);
    }
};

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::Application app;
    RichEditorWindow window;
    return app.Run(window);
}
