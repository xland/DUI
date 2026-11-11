// ============================================================================
//  DUI/UI/RichDoc.h — 富文本文档模型（纯数据，无排版 / UI 依赖）
//
//  设计（Doc/Arch/RichEditor.md §3.2 / §3.6 / §4）：
//    · 行内 = 展平后的 run 序列；bold / italic / hyperlink 等嵌套容器在解析期
//      消解成样式合并，模型里不再有树 —— 渲染、光标、选区、序列化只认 run
//    · 段落是纯单行文本，换行即块边界（D6：回车分裂新段落）
//    · 样式"空值即继承"：family 空串 / fontSize 0 / weight 0 / color 的 A==0
//      表示沿用文档默认或父级；富文本属性不进 Style 系统（11-Text §4.1）
//    · 本层只依赖 Core；JSON 读写见 RichDocJson.h
// ============================================================================

#pragma once

#include "Core/Core.h"

#include <vector>

namespace dui {
namespace richtext {

enum class Align { Start, Center, End };
enum class ListType { Unordered, Ordered };

enum class BlockKind {
    Paragraph,
    Heading,
    List,        // children = ListItem
    ListItem,    // children = 嵌套 List（可多个）
    Quote,
    Code,
    Image,
    Rule,
};

// 行内样式：全零 / 空串 = 继承（文档默认或父级）
struct RunStyle {
    String    family;                 // 空 = 继承
    float     fontSize = 0.f;         // 0 = 继承（DIP）
    uint32_t  weight = 0;             // 0 = 继承（DWRITE_FONT_WEIGHT）
    bool      italic = false;
    bool      underline = false;
    bool      strike = false;
    Color     color = 0;              // A == 0 = 继承
    Color     background = 0;         // A == 0 = 无高亮
    Thickness padding{};              // 高亮盒内边距
    float     radius = 0.f;           // 高亮盒圆角（DIP）
    String    href;                   // 非空 = 链接目标
};

struct Run {
    String   text;
    RunStyle style;
};

struct BlockStyle {
    Align     align = Align::Start;
    float     lineHeight = 0.f;       // 倍数；0 = 默认
    float     lineHeightDip = 0.f;    // 绝对行高（"24px"）；0 = 未设置；优先于 lineHeight
    Thickness margin{};               // 主要用 bottom / left
    float     indent = 0.f;           // 列表 / 引用的额外缩进（DIP）
    Color     background = 0;         // 块背景（A == 0 = 无）
    float     borderLeftWidth = 0.f;  // 引用块左边框
    Color     borderLeftColor = 0;
};

struct Block {
    BlockKind kind = BlockKind::Paragraph;
    BlockStyle style;
    std::vector<Run> runs;
    std::vector<Block> children;      // List → ListItem；ListItem → 嵌套 List

    // Heading
    int level = 1;                    // 1..6

    // List
    ListType listType = ListType::Unordered;

    // Code：代码块保留原始换行（不拆段），行号由渲染层按 '\n' 生成
    String code;
    bool   showLineNumbers = false;

    // Image
    String src;
    String altText;
    float  width = 0.f;
    float  height = 0.f;
    bool   heightAuto = false;
    Align  imageAlign = Align::Center;

    // Quote
    String attribution;
};

// 解析诊断：坏数据不中断，只计数（宿主据此决定是否提示）
struct Report {
    uint32_t unknownBlocks = 0;
    uint32_t unknownInlines = 0;
    uint32_t unknownStyles = 0;
    uint32_t invalidValues = 0;
    String   firstIssue;              // 第一条问题的可读描述

    bool Clean() const noexcept {
        return unknownBlocks == 0 && unknownInlines == 0 &&
               unknownStyles == 0 && invalidValues == 0;
    }
    void Note(const wchar_t* issue) {
        if (firstIssue.empty() && issue != nullptr) firstIssue = issue;
    }
    void Note(const String& issue) {
        if (firstIssue.empty()) firstIssue = issue;
    }
};

struct RichDoc {
    String defaultFont;               // settings.defaultFont 解析结果（空 = 引擎默认）
    float  defaultFontSize = 14.f;    // settings.defaultFontSize
    std::vector<Block> blocks;
    Report report;

    const Report& Diagnostics() const noexcept { return report; }
};

// runs 的 UTF-16 总长（编辑层计算段内偏移用）
inline size_t TextLength(const std::vector<Run>& runs) noexcept {
    size_t length = 0;
    for (const Run& run : runs) length += run.text.size();
    return length;
}

// 块内文字总长（递归含 children；列表编号另算）
inline size_t BlockTextLength(const Block& block) noexcept {
    size_t length = TextLength(block.runs);
    for (const Block& child : block.children) length += BlockTextLength(child);
    return length;
}

}  // namespace richtext
}  // namespace dui
