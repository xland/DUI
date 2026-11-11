// ============================================================================
//  DUI/UI/RichDocEditor.cpp — 文档模型编辑原语
// ============================================================================

#include "UI/RichDocEditor.h"

#include <utility>

namespace dui::richtext {
namespace {

bool IsLowSurrogate(wchar_t c) noexcept { return c >= 0xDC00 && c <= 0xDFFF; }
bool IsHighSurrogate(wchar_t c) noexcept { return c >= 0xD800 && c <= 0xDBFF; }

bool SameThickness(const Thickness& a, const Thickness& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

}  // namespace

String BlockText(const Block& block) {
    // 代码块：内容在 block.code（与 runs 分离）
    if (block.kind == BlockKind::Code) return block.code;

    String text;
    text.reserve(TextLength(block.runs));
    for (const Run& run : block.runs) text += run.text;
    return text;
}

uint32_t SnapBoundary(const String& text, uint32_t offset) {
    const uint32_t length = static_cast<uint32_t>(text.size());
    if (offset > length) return length;
    if (offset == 0) return 0;
    // 落在低代理上 → 退到高代理（保持码点完整）
    if (IsLowSurrogate(text[offset]) && IsHighSurrogate(text[offset - 1])) return offset - 1;
    return offset;
}

RunStyle StyleAt(const Block& block, uint32_t offset) {
    // 代码块无行内样式（整块等宽）
    if (block.kind == BlockKind::Code) return RunStyle{};
    if (block.runs.empty()) return RunStyle{};
    uint32_t pos = 0;
    for (size_t i = 0; i < block.runs.size(); ++i) {
        const uint32_t length = static_cast<uint32_t>(block.runs[i].text.size());
        if (offset < pos + length) return block.runs[i].style;
        // 恰在段尾：没有后续 run 时沿用最后一个 run 的样式
        if (offset == pos + length && i + 1 == block.runs.size()) return block.runs[i].style;
        pos += length;
    }
    return block.runs.back().style;
}

bool SameRunStyle(const RunStyle& a, const RunStyle& b) {
    return a.family == b.family && a.fontSize == b.fontSize && a.weight == b.weight &&
           a.italic == b.italic && a.underline == b.underline && a.strike == b.strike &&
           a.color == b.color && a.background == b.background &&
           SameThickness(a.padding, b.padding) && a.radius == b.radius && a.href == b.href;
}

void MergeRuns(Block& block) {
    std::vector<Run> out;
    out.reserve(block.runs.size());
    for (Run& run : block.runs) {
        if (run.text.empty()) continue;
        if (!out.empty() && SameRunStyle(out.back().style, run.style)) {
            out.back().text += run.text;
            continue;
        }
        out.push_back(std::move(run));
    }
    block.runs = std::move(out);
}

void InsertText(Block& block, uint32_t offset, const String& text, const RunStyle& style) {
    if (text.empty()) return;

    // 代码块：写入 block.code（保留原始换行，不拆段）
    if (block.kind == BlockKind::Code) {
        const uint32_t length = static_cast<uint32_t>(block.code.size());
        if (offset > length) offset = length;
        block.code.insert(offset, text);
        return;
    }

    // 定位插入点：index == runs.size() 表示段尾
    size_t index = block.runs.size();
    uint32_t local = 0;
    uint32_t pos = 0;
    for (size_t i = 0; i < block.runs.size(); ++i) {
        const uint32_t length = static_cast<uint32_t>(block.runs[i].text.size());
        if (offset <= pos + length) {
            index = i;
            local = offset - pos;
            break;
        }
        pos += length;
    }

    std::vector<Run> out;
    out.reserve(block.runs.size() + 3);

    const size_t headEnd = (index < block.runs.size()) ? index : block.runs.size();
    for (size_t i = 0; i < headEnd; ++i) out.push_back(std::move(block.runs[i]));

    Run inserted;
    inserted.text = text;
    inserted.style = style;

    if (index < block.runs.size()) {
        Run& target = block.runs[index];
        if (local > 0) {
            Run head;
            head.style = target.style;
            head.text = target.text.substr(0, local);
            out.push_back(std::move(head));
        }
        out.push_back(std::move(inserted));
        if (local < target.text.size()) {
            Run tail;
            tail.style = target.style;
            tail.text = target.text.substr(local);
            out.push_back(std::move(tail));
        }
        for (size_t i = index + 1; i < block.runs.size(); ++i) out.push_back(std::move(block.runs[i]));
    } else {
        out.push_back(std::move(inserted));
    }

    block.runs = std::move(out);
    MergeRuns(block);
}

void DeleteRange(Block& block, uint32_t start, uint32_t end) {
    if (start >= end) return;

    // 代码块：删 block.code 的区间
    if (block.kind == BlockKind::Code) {
        const uint32_t length = static_cast<uint32_t>(block.code.size());
        if (start > length) start = length;
        if (end > length) end = length;
        if (start >= end) return;
        block.code.erase(start, end - start);
        return;
    }

    std::vector<Run> out;
    out.reserve(block.runs.size());
    uint32_t pos = 0;
    for (Run& run : block.runs) {
        const uint32_t length = static_cast<uint32_t>(run.text.size());
        const uint32_t runEnd = pos + length;

        if (runEnd <= start || pos >= end) {   // 完全在区间外
            out.push_back(std::move(run));
            pos = runEnd;
            continue;
        }

        const uint32_t keepHead = (start > pos) ? (start - pos) : 0;
        const uint32_t keepTailFrom = (end < runEnd) ? (end - pos) : length;

        Run trimmed;
        trimmed.style = run.style;
        if (keepHead > 0) trimmed.text.append(run.text, 0, keepHead);
        if (keepTailFrom < length) trimmed.text.append(run.text, keepTailFrom, length - keepTailFrom);
        if (!trimmed.text.empty()) out.push_back(std::move(trimmed));

        pos = runEnd;
    }

    block.runs = std::move(out);
    MergeRuns(block);
}

Block SplitBlock(Block& src, uint32_t offset) {
    Block tail;
    tail.kind = src.kind;
    tail.style = src.style;
    tail.level = src.level;
    tail.listType = src.listType;

    // 代码块：按 code 切分（保留原始换行的同时把尾部独立成块）
    if (src.kind == BlockKind::Code) {
        const uint32_t length = static_cast<uint32_t>(src.code.size());
        if (offset > length) offset = length;
        tail.code = src.code.substr(offset);
        tail.showLineNumbers = src.showLineNumbers;
        src.code.erase(offset);
        return tail;
    }

    uint32_t pos = 0;
    for (Run& run : src.runs) {
        const uint32_t length = static_cast<uint32_t>(run.text.size());
        const uint32_t runEnd = pos + length;
        if (runEnd <= offset) {
            pos = runEnd;
            continue;
        }
        const uint32_t local = (offset > pos) ? (offset - pos) : 0;
        if (local < length) {
            Run piece;
            piece.style = run.style;
            piece.text = run.text.substr(local);
            tail.runs.push_back(std::move(piece));
        }
        pos = runEnd;
    }
    MergeRuns(tail);

    DeleteRange(src, offset, static_cast<uint32_t>(BlockText(src).size()));
    return tail;
}

void AppendRuns(Block& dst, const Block& src) {
    // 代码块：内容在 block.code，只与代码块合并（代码不并入正文，正文也不并入代码）
    if (dst.kind == BlockKind::Code) {
        if (src.kind == BlockKind::Code) dst.code += src.code;
        return;
    }
    if (src.kind == BlockKind::Code) return;

    for (const Run& run : src.runs) {
        if (run.text.empty()) continue;
        dst.runs.push_back(run);
    }
    MergeRuns(dst);
}

}  // namespace dui::richtext
