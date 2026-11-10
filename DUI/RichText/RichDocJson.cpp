// ============================================================================
//  DUI/RichText/RichDocJson.cpp — JSON 文档 → RichDoc（RichEditor.md §2 形态）
//
//  形态不一致（§2.2）都在这里抹平：
//    · content 是 string 或 Inline[]  → 都落成 run 序列
//    · listItem.content 混入 list 节点 → 拆成「自身 runs + 子 List」
//    · 数值与 CSS 字符串混用         → 数值直接取，字符串走 CssValue
//  未知键 / 坏值只计数，文档继续解析（Report）。
// ============================================================================

#include "RichText/RichDocJson.h"

#include "Core/Json.h"
#include "RichText/CssValue.h"

namespace dui::richtext {
namespace {

using json::Value;

// ---------------------------------------------------------------------------
// 取值的容错包装：CSS 键既可能是字符串也可能是数值
// ---------------------------------------------------------------------------

bool ReadLength(const Value* v, float& out, float emBase) {
    if (v == nullptr) return false;
    if (v->IsNumber()) {
        out = v->AsFloat();
        return true;
    }
    return v->IsString() && ParseLength(v->AsString(), out, emBase);
}

bool ReadThickness(const Value* v, Thickness& out, float emBase) {
    if (v == nullptr) return false;
    if (v->IsNumber()) {
        out = Thickness(v->AsFloat());
        return true;
    }
    return v->IsString() && ParseThickness(v->AsString(), out, emBase);
}

bool ReadLineHeight(const Value* v, float& out, bool& isMultiple) {
    if (v == nullptr) return false;
    if (v->IsNumber()) {
        out = v->AsFloat();
        isMultiple = true;
        return true;
    }
    return v->IsString() && ParseLineHeight(v->AsString(), out, isMultiple);
}

bool ReadColor(const Value* v, Color& out) {
    return v != nullptr && v->IsString() && ParseColor(v->AsString(), out);
}

bool ReadWeight(const Value* v, uint32_t& out) {
    if (v == nullptr) return false;
    if (v->IsNumber()) {
        const int64_t weight = v->AsInt();
        if (weight < 1 || weight > 1000) return false;
        out = static_cast<uint32_t>(weight);
        return true;
    }
    return v->IsString() && ParseWeight(v->AsString(), out);
}

bool ReadAlign(const Value* v, Align& out) {
    return v != nullptr && v->IsString() && ParseAlign(v->AsString(), out);
}

bool ReadSize(const Value* v, float& outPx, bool& isAuto, float& outRatio) {
    if (v == nullptr) return false;
    if (v->IsNumber()) {
        const float px = v->AsFloat();
        if (!(px > 0.f)) return false;
        outPx = px;
        isAuto = false;
        outRatio = 0.f;
        return true;
    }
    return v->IsString() && ParseSize(v->AsString(), outPx, isAuto, outRatio);
}

int ClampInt(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// 样式白名单
// ---------------------------------------------------------------------------

// 行内样式：out 起点是继承来的样式，只覆盖出现的键（空值即继承）
void ParseRunStyle(const Value* styles, RunStyle& out, float emBase, Report& report) {
    if (styles == nullptr || !styles->IsObject()) return;

    for (size_t i = 0; i < styles->Size(); ++i) {
        const String& key = styles->KeyAt(i);
        const Value* v = styles->At(i);
        bool ok = true;

        if (key == L"fontFamily") {
            if (v->IsString()) {
                String family;
                // 列表全不可用时保持继承，不算坏数据（族名回退是既定行为）
                if (ParseFontFamily(v->AsString(), family)) out.family = family;
            } else {
                ok = false;
            }
        } else if (key == L"fontSize") {
            float size = 0.f;
            ok = ReadLength(v, size, emBase) && size > 0.f;
            if (ok) out.fontSize = size;
        } else if (key == L"fontWeight") {
            ok = ReadWeight(v, out.weight);
        } else if (key == L"fontStyle") {
            ok = v->IsString() && ParseFontStyle(v->AsString(), out.italic);
        } else if (key == L"textDecoration") {
            ok = v->IsString() && ParseDecoration(v->AsString(), out.underline, out.strike);
        } else if (key == L"color") {
            ok = ReadColor(v, out.color);
        } else if (key == L"backgroundColor") {
            ok = ReadColor(v, out.background);
        } else if (key == L"padding") {
            ok = ReadThickness(v, out.padding, emBase);
        } else if (key == L"borderRadius") {
            float radius = 0.f;
            ok = ReadLength(v, radius, emBase) && radius >= 0.f;
            if (ok) out.radius = radius;
        } else {
            ++report.unknownStyles;
            report.Note(L"未知行内样式键：" + key);
            continue;
        }

        if (!ok) {
            ++report.invalidValues;
            report.Note(L"非法行内样式值：" + key);
        }
    }
}

// 块样式：同样以继承值起步（列表项的样式默认取外层 list 的样式）
// allowSizeKeys：image 的 width / height 允许写在 styles 里（§2.2）
void ParseBlockStyle(const Value* styles, BlockStyle& out, float emBase,
                     bool allowSizeKeys, Report& report) {
    if (styles == nullptr || !styles->IsObject()) return;

    for (size_t i = 0; i < styles->Size(); ++i) {
        const String& key = styles->KeyAt(i);
        const Value* v = styles->At(i);
        bool ok = true;

        if (key == L"textAlign") {
            ok = ReadAlign(v, out.align);
        } else if (key == L"lineHeight") {
            float value = 0.f;
            bool isMultiple = true;
            ok = ReadLineHeight(v, value, isMultiple) && value >= 0.f;
            if (ok) {
                if (isMultiple) out.lineHeight = value;
                else            out.lineHeightDip = value;
            }
        } else if (key == L"margin") {
            ok = ReadThickness(v, out.margin, emBase);
        } else if (key == L"indent") {
            float indent = 0.f;
            ok = ReadLength(v, indent, emBase) && indent >= 0.f;
            if (ok) out.indent = indent;
        } else if (key == L"backgroundColor") {
            ok = ReadColor(v, out.background);
        } else if (key == L"borderLeft") {
            BorderSpec border;
            ok = v->IsString() && ParseBorder(v->AsString(), border);
            if (ok && border.solid && border.width > 0.f) {
                out.borderLeftWidth = border.width;
                out.borderLeftColor = border.color;
            }
        } else if (allowSizeKeys && (key == L"width" || key == L"height")) {
            continue;                          // image 分支自己取
        } else {
            ++report.unknownStyles;
            report.Note(L"未知块样式键：" + key);
            continue;
        }

        if (!ok) {
            ++report.invalidValues;
            report.Note(L"非法块样式值：" + key);
        }
    }
}

// ---------------------------------------------------------------------------
// 行内树 → run 序列（容器只贡献样式，自身不产生 run）
// ---------------------------------------------------------------------------

void ParseListBlock(const Value& listValue, const BlockStyle& parentStyle,
                    Block& out, float emBase, Report& report);

// styleSize = 当前继承链的实际字号（em 换算基准，0 视为未定）
void FlattenContent(const Value* content, const RunStyle& style, float styleSize,
                    Block& owner, Report& report, bool allowNestedList) {
    if (content == nullptr) return;

    if (content->IsString()) {                 // content 直接是字符串
        const String& text = content->AsString();
        if (!text.empty()) owner.runs.push_back(Run{text, style});
        return;
    }
    if (!content->IsArray()) {
        ++report.invalidValues;
        report.Note(L"content 既不是字符串也不是数组");
        return;
    }

    for (size_t i = 0; i < content->Size(); ++i) {
        const Value* item = content->At(i);
        if (item == nullptr) {
            ++report.invalidValues;
            continue;
        }
        if (item->IsString()) {                // 数组里直接写字符串
            const String& text = item->AsString();
            if (!text.empty()) owner.runs.push_back(Run{text, style});
            continue;
        }
        if (!item->IsObject()) {
            ++report.invalidValues;
            continue;
        }

        const String type = item->GetString(L"type");

        if (type.empty() || type == L"text") {
            const Value* textValue = item->Find(L"text");
            String text = (textValue != nullptr && textValue->IsString())
                              ? textValue->AsString()
                              : item->GetString(L"content");
            RunStyle runStyle = style;
            ParseRunStyle(item->Find(L"styles"), runStyle, styleSize, report);
            if (!text.empty()) owner.runs.push_back(Run{std::move(text), runStyle});
            continue;
        }

        if (type == L"list") {                 // 只在 listItem 的内容里合法
            if (!allowNestedList) {
                ++report.unknownInlines;
                report.Note(L"段落里出现了 list 节点");
                continue;
            }
            Block nested;
            ParseListBlock(*item, owner.style, nested, styleSize, report);
            owner.children.push_back(std::move(nested));
            continue;
        }

        RunStyle child = style;
        if (type == L"bold") {
            child.weight = 700;
        } else if (type == L"italic") {
            child.italic = true;
        } else if (type == L"underline") {
            child.underline = true;
        } else if (type == L"strikethrough" || type == L"strike") {
            child.strike = true;
        } else if (type == L"hyperlink") {
            child.href = item->GetString(L"href");
        } else {
            // 未知容器：样式整个丢掉，但里面的文字继续收集（不丢内容）
            ++report.unknownInlines;
            report.Note(L"未知行内类型：" + type);
            FlattenContent(item->Find(L"content"), style, styleSize, owner, report, allowNestedList);
            continue;
        }

        ParseRunStyle(item->Find(L"styles"), child, styleSize, report);
        const float childSize = child.fontSize > 0.f ? child.fontSize : styleSize;
        FlattenContent(item->Find(L"content"), child, childSize, owner, report, allowNestedList);
    }
}

// ---------------------------------------------------------------------------
// 块
// ---------------------------------------------------------------------------

void ParseListBlock(const Value& listValue, const BlockStyle& parentStyle,
                    Block& out, float emBase, Report& report) {
    out.kind = BlockKind::List;
    out.style = parentStyle;
    ParseBlockStyle(listValue.Find(L"styles"), out.style, emBase, false, report);
    out.listType = (listValue.GetString(L"listType") == L"ordered") ? ListType::Ordered
                                                                   : ListType::Unordered;

    const Value* items = listValue.Find(L"items");
    if (items == nullptr) return;
    if (!items->IsArray()) {
        ++report.invalidValues;
        report.Note(L"list.items 不是数组");
        return;
    }

    for (size_t i = 0; i < items->Size(); ++i) {
        const Value* item = items->At(i);
        if (item == nullptr || !item->IsObject()) {
            ++report.invalidValues;
            report.Note(L"listItem 不是对象");
            continue;
        }
        Block child;
        child.kind = BlockKind::ListItem;
        child.style = out.style;               // 列表样式向下继承
        ParseBlockStyle(item->Find(L"styles"), child.style, emBase, false, report);
        FlattenContent(item->Find(L"content"), RunStyle{}, emBase, child, report, true);
        out.children.push_back(std::move(child));
    }
}

// 返回 false 表示未知块类型（调用方计数并丢弃）
bool ParseOneBlock(const Value& v, Block& out, float emBase, Report& report) {
    const String type = v.GetString(L"type");

    if (type == L"list") {                     // 列表自己带 styles，避免重复解析
        ParseListBlock(v, BlockStyle{}, out, emBase, report);
        return true;
    }

    if (type != L"paragraph" && type != L"heading" && type != L"quote" &&
        type != L"codeBlock" && type != L"image" && type != L"horizontalRule") {
        return false;
    }

    const bool isImage = (type == L"image");
    ParseBlockStyle(v.Find(L"styles"), out.style, emBase, isImage, report);

    if (type == L"paragraph") {
        out.kind = BlockKind::Paragraph;
        FlattenContent(v.Find(L"content"), RunStyle{}, emBase, out, report, false);
        return true;
    }
    if (type == L"heading") {
        out.kind = BlockKind::Heading;
        out.level = ClampInt(v.GetInt(L"level", 1), 1, 6);
        FlattenContent(v.Find(L"content"), RunStyle{}, emBase, out, report, false);
        return true;
    }
    if (type == L"quote") {
        out.kind = BlockKind::Quote;
        out.attribution = v.GetString(L"attribution");
        FlattenContent(v.Find(L"content"), RunStyle{}, emBase, out, report, false);
        return true;
    }
    if (type == L"codeBlock") {
        out.kind = BlockKind::Code;
        out.code = v.GetString(L"code");
        out.showLineNumbers = v.GetBool(L"showLineNumbers", false);
        return true;                           // language 本期丢弃（§1.2 不做语法高亮）
    }
    if (type == L"image") {
        out.kind = BlockKind::Image;
        out.src = v.GetString(L"src");
        out.altText = v.GetString(L"altText");

        // 尺寸：顶层字段优先，其次 styles.width / styles.height（§2.2 两种形态）
        const Value* styles = v.Find(L"styles");
        const Value* widthValue = v.Find(L"width");
        if (widthValue == nullptr && styles != nullptr) widthValue = styles->Find(L"width");
        const Value* heightValue = v.Find(L"height");
        if (heightValue == nullptr && styles != nullptr) heightValue = styles->Find(L"height");

        float px = 0.f;
        float ratio = 0.f;
        bool isAuto = false;

        if (widthValue != nullptr) {
            if (!ReadSize(widthValue, px, isAuto, ratio)) {
                ++report.invalidValues;
                report.Note(L"非法 image.width");
            } else if (isAuto || ratio > 0.f) {
                ++report.invalidValues;
                report.Note(L"image.width 暂不支持 auto / 百分比");
            } else {
                out.width = px;
            }
        }
        if (heightValue != nullptr) {
            if (!ReadSize(heightValue, px, isAuto, ratio)) {
                ++report.invalidValues;
                report.Note(L"非法 image.height");
            } else if (isAuto) {
                out.heightAuto = true;         // 按宽高比
            } else if (ratio > 0.f) {
                ++report.invalidValues;
                report.Note(L"image.height 暂不支持百分比");
            } else {
                out.height = px;
            }
        }

        out.imageAlign = out.style.align;      // 缺 alignment 时沿用块对齐
        ReadAlign(v.Find(L"alignment"), out.imageAlign);
        return true;
    }

    out.kind = BlockKind::Rule;                // horizontalRule
    return true;
}

// ---------------------------------------------------------------------------
// 文档
// ---------------------------------------------------------------------------

void ParseSettings(const Value* settings, RichDoc& doc) {
    if (settings == nullptr || !settings->IsObject()) return;
    Report& report = doc.report;

    if (const Value* font = settings->Find(L"defaultFont"); font != nullptr && font->IsString()) {
        String family;
        // 全不可用 → 保持空（引擎默认字体），不算坏数据
        if (ParseFontFamily(font->AsString(), family)) doc.defaultFont = family;
    }

    if (const Value* size = settings->Find(L"defaultFontSize")) {
        float dip = 0.f;
        bool ok = false;
        if (size->IsNumber()) {
            dip = size->AsFloat();
            ok = dip > 0.f;
        } else if (size->IsString()) {
            ok = ParseLength(size->AsString(), dip) && dip > 0.f;
        }
        if (ok) doc.defaultFontSize = dip;
        else {
            ++report.invalidValues;
            report.Note(L"非法 settings.defaultFontSize");
        }
    }
    // theme / locale / spellCheck / autoSave*：本期不解析，直接丢弃（§1.2）
}

void ParseBlocks(const Value* blocks, RichDoc& doc) {
    if (blocks == nullptr) return;
    if (!blocks->IsArray()) {
        ++doc.report.invalidValues;
        doc.report.Note(L"blocks 不是数组");
        return;
    }

    doc.blocks.reserve(blocks->Size());
    for (size_t i = 0; i < blocks->Size(); ++i) {
        const Value* v = blocks->At(i);
        if (v == nullptr || !v->IsObject()) {
            ++doc.report.invalidValues;
            doc.report.Note(L"block 不是对象");
            continue;
        }
        Block block;
        if (!ParseOneBlock(*v, block, doc.defaultFontSize, doc.report)) {
            ++doc.report.unknownBlocks;
            doc.report.Note(L"未知块类型：" + v->GetString(L"type"));
            continue;
        }
        doc.blocks.push_back(std::move(block));
    }
}

}  // namespace

bool ParseRichDoc(const String& jsonText, RichDoc& out) {
    out = RichDoc{};

    json::Error error;
    const json::Value root = json::Parse(jsonText, error);
    if (error.Failed()) {
        out.report.Note(L"JSON 语法错误：" + error.ToString());
        return false;
    }
    if (!root.IsObject()) {
        out.report.Note(L"根节点不是对象");
        return false;
    }

    ParseSettings(root.Find(L"settings"), out);
    ParseBlocks(root.Find(L"blocks"), out);
    return true;
}

}  // namespace dui::richtext
