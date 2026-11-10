// ============================================================================
//  DUI/UI/RichEdit.cpp — 富文本控件的排版、绘制与编辑（RichEditor.md §3.1 / §4）
//
//  排版两块状态：
//    · Build：文档结构 → Item 列表（字体、run 表、标记文字、高亮区间），
//      与宽度无关；可编辑文本块同时在 editables 里登记（光标序号 = 数组下标）
//    · Place：按内容宽给每个 Item 排盒并调 TextEngine 排版（引擎按
//      text/family/size/lineHeight/maxWidth/runVersion 缓存）
//  编辑后重建时段落按文本从池中复用，未改动的块不重建 layout。
//
//  编辑（P3）：光标/选区是两个 {可编辑块序号, 段内偏移}；段内文本改动走
//  richtext::RichDocEdit 的纯函数，结构改动（分裂/合并/入列表）在这里做。
// ============================================================================

#include "UI/RichEdit.h"

#include <algorithm>
#include <utility>

#include "Graphics/Graphics.h"
#include "RichText/RichDocEdit.h"
#include "Text/TextEngine.h"
#include "UI/Scroller.h"
#include "UI/UI.h"

namespace dui {
namespace {

// ---------------------------------------------------------------------------
// 默认度量：文档没写 styles 时的兜底。集中一处，改视觉只改这里。
// ---------------------------------------------------------------------------
constexpr float kParagraphGap = 10.f;                                   // 段间距（上）
constexpr float kHeadingScale[6] = {1.8f, 1.5f, 1.25f, 1.1f, 1.f, 0.9f};  // H1..H6 相对基准字号
constexpr float kHeadingGapAbove[6] = {18.f, 16.f, 14.f, 12.f, 10.f, 8.f};
constexpr float kHeadingGapBelow = 4.f;
constexpr float kListIndent = 24.f;      // 每层列表缩进
constexpr float kMarkerWidth = 22.f;     // 列表标记列宽（含与正文的间隙）
constexpr float kMarkerGapRight = 6.f;   // 标记右边缘到正文的间隙
constexpr float kQuoteBorderGap = 10.f;  // 引用左边框到正文
constexpr float kRuleHeight = 1.f;
constexpr float kRuleGap = 14.f;
constexpr float kCodeScale = 0.9f;
constexpr float kCodePadding = 10.f;
constexpr float kLineNumberWidth = 30.f;
constexpr float kImageRatio = 0.5625f;   // 未给尺寸时的占位宽高比（16:9）
constexpr float kNoWrapWidth = 1e6f;     // 不折行（DWrite 需要有限宽）

constexpr Color kCodeBackground = 0xFFF6F7F8;
constexpr Color kCodeNumberColor = 0xFF9AA0A6;
constexpr Color kRuleColor = 0xFFD8DCE2;
constexpr Color kImageFill = 0xFFF2F3F5;
constexpr Color kImageStroke = 0xFFC9CED6;
constexpr Color kImageTextColor = 0xFF8A9099;
constexpr Color kQuoteBorderDefault = 0xFF007BFF;

constexpr size_t kNoEditable = static_cast<size_t>(-1);
constexpr size_t kMaxUndo = 100;

const wchar_t* const kCodeFamily = L"Consolas";

using richtext::Block;
using richtext::BlockKind;
using richtext::BlockStyle;
using richtext::RichDoc;
using richtext::Run;
using richtext::RunStyle;

bool HasAnyMargin(const Thickness& m) noexcept {
    return m.left != 0.f || m.top != 0.f || m.right != 0.f || m.bottom != 0.f;
}

// 行高：倍数优先；绝对 DIP 折算成倍数（引擎只有 PROPORTIONAL，混合字号时是近似）
float ResolveLineHeight(const BlockStyle& style, float fontSize, float base) {
    if (style.lineHeight > 0.f) return style.lineHeight;
    if (style.lineHeightDip > 0.f && fontSize > 0.f) return style.lineHeightDip / fontSize;
    return base > 0.f ? base : 1.2f;
}

int HeadingIndex(const Block& block) noexcept {
    const int level = (block.kind == BlockKind::Heading) ? block.level : 1;
    return level < 1 ? 0 : (level > 6 ? 5 : level - 1);
}

float DefaultGapAbove(const Block& block) {
    switch (block.kind) {
    case BlockKind::Heading: return kHeadingGapAbove[HeadingIndex(block)];
    case BlockKind::Rule:    return kRuleGap;
    default:                 return kParagraphGap;
    }
}

float DefaultGapBelow(BlockKind kind) noexcept {
    return kind == BlockKind::Heading ? kHeadingGapBelow : 0.f;
}

Rect OffsetRect(const Rect& r, Vec2 offset) noexcept {
    return Rect{r.x + offset.x, r.y + offset.y, r.w, r.h};
}

// 文本块（可编辑）：段落 / 标题 / 引用 / 列表项；代码块只读
bool IsEditableKind(BlockKind kind) noexcept {
    return kind == BlockKind::Paragraph || kind == BlockKind::Heading ||
           kind == BlockKind::Quote || kind == BlockKind::ListItem;
}

bool IsHighSurrogate(wchar_t c) noexcept { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(wchar_t c) noexcept { return c >= 0xDC00 && c <= 0xDFFF; }

// 按码点移动（跳过代理对）
uint32_t NextIndex(const String& s, uint32_t i, int dir) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    if (dir < 0) {
        if (i == 0 || n == 0) return 0;
        const uint32_t j = i - 1;
        if (j > 0 && IsLowSurrogate(s[j]) && IsHighSurrogate(s[j - 1])) return j - 1;
        return j;
    }
    if (i >= n) return n;
    if (IsHighSurrogate(s[i]) && i + 1 < n && IsLowSurrogate(s[i + 1])) return i + 2;
    return i + 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl：文档副本 + 排版结果 + 编辑状态
// ---------------------------------------------------------------------------
struct RichEdit::Impl {
    enum class KeyResult { None, Handled, Content };

    struct Cursor {
        size_t   block = 0;
        uint32_t offset = 0;
    };

    // run 高亮盒（RunStyle.background 非透明时记一条）
    struct Highlight {
        uint32_t  start = 0;
        uint32_t  length = 0;
        Color     color = 0;
        Thickness padding{};
        float     radius = 0.f;
    };

    // 一个可视块（列表项 / 段落 / 代码块 / 图片 / 分割线各一条；嵌套列表按序摊平）
    // 用 unique_ptr 持有：地址稳定，runs 数组指针可以安全地交给 TextEngine 长期引用
    struct Item {
        const Block* block = nullptr;
        BlockStyle   style;
        int          depth = 0;
        const Block* parentList = nullptr;   // 所属 list；nullptr = 顶层 doc.blocks
        size_t       indexInContainer = 0;
        size_t       editableIndex = kNoEditable;   // editables 下标；非文本块为 kNoEditable

        // 文字与字体
        String   text;
        String   family;
        float    fontSize = 14.f;
        float    lineHeight = 1.2f;
        uint32_t weight = 400;
        std::vector<text::TextRun> runs;   // 空 = 单样式快速路径
        std::vector<Highlight>     highlights;

        // 代码块
        bool   code = false;
        bool   lineNumbers = false;
        String numberText;

        // 列表标记
        String marker;
        float  markerX = 0.f;

        // 排版结果（相对文档原点 = 内容盒左上）
        Rect blockBox{};
        Rect textBox{};

        text::Paragraph* para = nullptr;
        text::Paragraph* auxPara = nullptr;     // 代码行号、图片 altText
        text::Paragraph* markerPara = nullptr;  // 列表标记
    };

    // 可编辑文本块（下标即光标 / 选区的 block 序号）
    struct Editable {
        Block* block = nullptr;
        Item*  item = nullptr;
    };

    struct VisLine {   // 视觉行（折行后的一行）
        size_t   editable = 0;
        uint32_t start = 0;
        uint32_t end = 0;
        float    top = 0.f;
        float    bottom = 0.f;
    };

    struct Snapshot {   // 撤销快照（整文档 + 光标）
        RichDoc doc;
        Cursor  caret;
        Cursor  anchor;
    };

    struct Spot {   // 块在文档树中的位置
        std::vector<Block>* container = nullptr;
        size_t              index = 0;
        Block*              parentList = nullptr;
    };

    struct SelectionInfo {
        bool     active = false;
        size_t   first = 0;
        size_t   last = 0;
        uint32_t firstOffset = 0;
        uint32_t lastOffset = 0;
    };

    RichDoc doc;
    std::vector<std::unique_ptr<Item>> items;
    std::vector<Editable> editables;
    Vec2  contentSize{};
    float baseSize = 14.f;
    bool  overflowX = false;

    // ---- 编辑状态 ----
    Cursor caret{};
    Cursor anchor{};
    bool   dragging = false;
    bool   caretVisible = true;
    float  goalX = -1.f;   // 上下移动的目标列（-1 = 未记录）

    std::vector<Snapshot> undo;
    std::vector<Snapshot> redo;
    bool lastEditTyping = false;

    // 视觉行缓存（按键时才重建）
    mutable bool     linesDirty = true;
    mutable std::vector<VisLine> lines;
    mutable std::vector<text::LineInfo> lineScratch;   // LineInfos 的复用缓冲
    mutable std::vector<Rect> scratch;                 // 选区/高亮逐行矩形

    // 排版缓存键 + 上下文（编辑后强制重排要复用同一 ctx）
    uint64_t     version = 0;
    bool         laidOut = false;
    float        layoutWidth = -1.f;
    float        layoutBase = 0.f;
    float        layoutLine = 0.f;
    String       layoutFamily;
    StyleContext lastCtx{};

    // 段落池：编辑后重建时按文本复用（引擎自己判定 layout 是否真的重建）
    std::vector<text::Paragraph*> poolMain;
    std::vector<text::Paragraph*> poolAux;
    std::vector<text::Paragraph*> poolMarker;

    void Discard(bool recycle);
    void FlushPool();
    text::Paragraph* Take(std::vector<text::Paragraph*>& pool, const wchar_t* text);

    void EnsureLayout(const StyleContext& ctx, float width);
    void Build(const StyleContext& ctx, float base, float line, const wchar_t* family);
    void BuildList(const Block& list, int depth, float base, uint32_t weight, float line,
                   const wchar_t* family);
    // 块的子级：List → 下一层列表；其余（列表项降级后的段落 / 占位项）→ 与父项同级缩进
    void BuildChildBlocks(const Block& parent, int depth, float base, uint32_t weight, float line,
                          const wchar_t* family);
    void BuildItem(const Block& block, const String& marker, int depth, float fontSize,
                   uint32_t weight, float line, const wchar_t* family, bool code,
                   const Block* parentList, size_t indexInContainer);
    void Place(float width);
    void PlaceItem(Item& it, float width, float& y);

    text::Paragraph* MakePara(text::Paragraph*& slot, const wchar_t* text, float size,
                              uint32_t weight, float line, const wchar_t* family,
                              float maxWidth, const std::vector<text::TextRun>* runs);

    void Paint(graphics::Painter& p, const StyleContext& ctx, float scale, Vec2 origin,
               bool showCaret, Color caretColor, Color selectionColor) const;
    void PaintItem(const Item& it, graphics::Painter& p, const StyleContext& ctx, float scale,
                   Vec2 origin, const SelectionInfo& sel, Color selectionColor) const;

    // ---- 几何 ----
    Vec2  TextOriginDoc(const Item& it) const;
    float AlignDx(const Item& it) const;
    bool  CaretRectOf(Cursor c, Rect& docRect) const;
    void  EnsureLines() const;
    int   LineIndexOf(Cursor c) const;
    float CaretXDoc() const;
    Cursor HitTestDoc(Vec2 docPt) const;
    bool  SelectionRange(const Item& it, const SelectionInfo& sel, uint32_t& s, uint32_t& e) const;

    // ---- 光标 / 选区 ----
    bool   HasSelection() const noexcept {
        return caret.block != anchor.block || caret.offset != anchor.offset;
    }
    static bool BlockLess(Cursor a, Cursor b) noexcept {
        return a.block != b.block ? a.block < b.block : a.offset < b.offset;
    }
    Cursor   ClampToDoc(Cursor c) const;
    uint32_t TextLenOf(size_t block) const;
    Cursor   EndCursor(size_t block) const;
    size_t   IndexOf(const Block* block) const;
    void     SetCaret(Cursor c, bool extend);
    void     MoveHorz(int dir, bool extend);
    void     MoveVertical(int dir, bool extend);
    void     MoveLineEdge(bool toEnd, bool extend, bool whole);
    void     SelectAll();

    // ---- 编辑 ----
    KeyResult HandleKey(int key, bool shift, bool ctrl);
    bool InsertTyped(const String& typed);
    bool DeleteBackward();
    bool DeleteForward();
    bool InsertNewline();
    bool DeleteSelectionNoUndo();
    String SelectedText() const;
    void CopySelection() const;
    bool CutSelection();
    bool PasteClipboard();
    void PushUndo(bool typing);
    bool Undo();
    bool Redo();
    void Restore(const Snapshot& s);
    void Rebuild();
    void RebuildAt(float width);
    bool FindSpot(Block* target, Spot& out);
    static bool FindSpotRec(std::vector<Block>& vec, Block* target, Block* ownerList, Spot& out);
    // children 恰好是 target 的块（嵌套 List 的父 ListItem）；顶层容器返回 nullptr
    static Block* OwnerOfChildren(std::vector<Block>& vec, const std::vector<Block>* target) {
        for (Block& block : vec) {
            if (&block.children == target) return &block;
            if (block.children.empty()) continue;
            if (Block* owner = OwnerOfChildren(block.children, target)) return owner;
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// 段落池
// ---------------------------------------------------------------------------

void RichEdit::Impl::Discard(bool recycle) {
    if (recycle) {
        poolMain.clear();
        poolAux.clear();
        poolMarker.clear();
    }
    for (const std::unique_ptr<Item>& up : items) {
        if (recycle) {
            if (up->para != nullptr) poolMain.push_back(up->para);
            if (up->auxPara != nullptr) poolAux.push_back(up->auxPara);
            if (up->markerPara != nullptr) poolMarker.push_back(up->markerPara);
        } else {
            text::Engine::Get().Discard(up->para);
            text::Engine::Get().Discard(up->auxPara);
            text::Engine::Get().Discard(up->markerPara);
        }
    }
    if (!recycle) {
        poolMain.clear();
        poolAux.clear();
        poolMarker.clear();
    }
    items.clear();
    editables.clear();
    contentSize = {};
    overflowX = false;
    laidOut = false;
    layoutWidth = -1.f;
}

void RichEdit::Impl::FlushPool() {
    for (text::Paragraph* p : poolMain) text::Engine::Get().Discard(p);
    for (text::Paragraph* p : poolAux) text::Engine::Get().Discard(p);
    for (text::Paragraph* p : poolMarker) text::Engine::Get().Discard(p);
    poolMain.clear();
    poolAux.clear();
    poolMarker.clear();
}

text::Paragraph* RichEdit::Impl::Take(std::vector<text::Paragraph*>& pool, const wchar_t* text) {
    if (text == nullptr) return nullptr;
    for (size_t i = 0; i < pool.size(); ++i) {
        text::Paragraph* p = pool[i];
        if (p != nullptr && p->text == text) {
            pool.erase(pool.begin() + static_cast<ptrdiff_t>(i));
            return p;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Build：结构 → Item（与宽度无关）
// ---------------------------------------------------------------------------

void RichEdit::Impl::Build(const StyleContext& ctx, float base, float line, const wchar_t* family) {
    baseSize = base;
    const uint32_t baseWeight = dui::DWriteWeight(ctx.fontWeight);

    for (size_t i = 0; i < doc.blocks.size(); ++i) {
        const Block& block = doc.blocks[i];
        switch (block.kind) {
        case BlockKind::Heading:
            BuildItem(block, String(), 0, base * kHeadingScale[HeadingIndex(block)], 700u,
                      line, family, false, nullptr, i);
            break;
        case BlockKind::List:
            BuildList(block, 0, base, baseWeight, line, family);
            break;
        case BlockKind::Code:
            BuildItem(block, String(), 0, base * kCodeScale, 400u, line, kCodeFamily, true,
                      nullptr, i);
            break;
        default:   // Paragraph / Quote / Image / Rule（后两者不带文字）
            BuildItem(block, String(), 0, base, baseWeight, line, family, false, nullptr, i);
            break;
        }
    }
}

void RichEdit::Impl::BuildList(const Block& list, int depth, float base, uint32_t weight,
                               float line, const wchar_t* family) {
    for (size_t i = 0; i < list.children.size(); ++i) {
        const Block& item = list.children[i];
        if (item.kind != BlockKind::ListItem) continue;

        String marker;
        if (list.listType == richtext::ListType::Ordered) {
            marker = std::to_wstring(i + 1);
            marker += L".";
        } else {
            marker = L"\u2022";
        }

        BuildItem(item, marker, depth, base, weight, line, family, false, &list, i);
        BuildChildBlocks(item, depth, base, weight, line, family);
    }
}

void RichEdit::Impl::BuildChildBlocks(const Block& parent, int depth, float base, uint32_t weight,
                                      float line, const wchar_t* family) {
    for (size_t i = 0; i < parent.children.size(); ++i) {
        const Block& child = parent.children[i];
        switch (child.kind) {
        case BlockKind::List:   // 嵌套列表：比父项再缩进一层
            BuildList(child, depth + 1, base, weight, line, family);
            break;
        case BlockKind::Code:
            BuildItem(child, String(), depth, base * kCodeScale, 400u, line, kCodeFamily, true,
                      nullptr, i);
            break;
        case BlockKind::Heading:
            BuildItem(child, String(), depth, base * kHeadingScale[HeadingIndex(child)], 700u, line,
                      family, false, nullptr, i);
            break;
        case BlockKind::ListItem:   // 父项内降级出来的占位项：一级项带点标记，顶级项无标记
            BuildItem(child, child.level > 0 ? String(L"\u2022") : String(), depth, base, weight,
                      line, family, false, nullptr, i);
            break;
        default:   // 段落 / 引用等：无标记，与父项同级缩进
            BuildItem(child, String(), depth, base, weight, line, family, false, nullptr, i);
            break;
        }
    }
}

void RichEdit::Impl::BuildItem(const Block& block, const String& marker, int depth, float fontSize,
                               uint32_t weight, float line, const wchar_t* family, bool code,
                               const Block* parentList, size_t indexInContainer) {
    auto owned = std::make_unique<Item>();
    Item& it = *owned;
    it.block = &block;
    it.style = block.style;
    it.depth = depth;
    it.marker = marker;
    it.fontSize = fontSize;
    it.weight = weight;
    it.family = family != nullptr ? family : L"";
    it.lineHeight = ResolveLineHeight(block.style, fontSize, line);
    it.code = code;
    it.parentList = parentList;
    it.indexInContainer = indexInContainer;

    if (code) {
        it.text = block.code;
        it.lineNumbers = block.showLineNumbers;
        if (it.lineNumbers) {
            uint32_t count = 1;
            for (const wchar_t ch : block.code) {
                if (ch == L'\n') ++count;
            }
            for (uint32_t i = 1; i <= count; ++i) {
                if (i > 1) it.numberText += L'\n';
                it.numberText += std::to_wstring(i);
            }
        }
    } else {
        uint32_t start = 0;
        bool overrides = false;
        for (const Run& run : block.runs) {
            const uint32_t length = static_cast<uint32_t>(run.text.size());
            if (length == 0) continue;
            it.text += run.text;

            const RunStyle& s = run.style;
            text::TextRun tr;
            tr.start = start;
            tr.length = length;
            tr.fontSize = s.fontSize;
            tr.weight = s.weight;
            tr.family = s.family.empty() ? nullptr : s.family.c_str();
            tr.color = s.color;
            tr.italic = s.italic;
            tr.underline = s.underline;
            tr.strike = s.strike;
            it.runs.push_back(tr);

            if (s.fontSize > 0.f || s.weight != 0 || !s.family.empty() ||
                (s.color >> 24) != 0u || s.italic || s.underline || s.strike) {
                overrides = true;
            }
            if ((s.background >> 24) != 0u) {
                it.highlights.push_back(Highlight{start, length, s.background, s.padding, s.radius});
            }
            start += length;
        }
        if (!overrides) it.runs.clear();   // 全默认样式 → 走引擎单样式路径
    }

    it.para = Take(poolMain, it.text.c_str());

    if (!code && IsEditableKind(block.kind)) {
        it.editableIndex = editables.size();
        editables.push_back(Editable{const_cast<Block*>(&block), owned.get()});
    }

    items.push_back(std::move(owned));
}

// ---------------------------------------------------------------------------
// Place：给每个 Item 排盒 + 调引擎排版
// ---------------------------------------------------------------------------

text::Paragraph* RichEdit::Impl::MakePara(text::Paragraph*& slot, const wchar_t* text, float size,
                                          uint32_t weight, float line, const wchar_t* family,
                                          float maxWidth, const std::vector<text::TextRun>* runs) {
    text::ShapeInput in;
    in.text = text != nullptr ? text : L"";
    in.fontSize = size;
    in.weight = weight != 0 ? weight : 400u;
    in.lineHeight = line > 0.f ? line : 1.2f;
    in.maxWidth = maxWidth;
    in.family = (family != nullptr && family[0] != L'\0') ? family : nullptr;
    if (runs != nullptr && !runs->empty()) {
        in.runs = runs->data();
        in.runCount = static_cast<uint32_t>(runs->size());
        in.runVersion = version;
    }
    slot = text::Engine::Get().Shape(slot, in);
    return slot;
}

void RichEdit::Impl::Place(float width) {
    float y = 0.f;
    float maxRight = 0.f;
    for (const std::unique_ptr<Item>& up : items) {
        PlaceItem(*up, width, y);
        if (up->blockBox.Right() > maxRight) maxRight = up->blockBox.Right();
    }
    contentSize = {width > 0.f ? width : maxRight, y};
    overflowX = width > 0.f && maxRight > width + 0.5f;
    linesDirty = true;
}

void RichEdit::Impl::PlaceItem(Item& it, float width, float& y) {
    const Block& block = *it.block;
    const BlockStyle& st = it.style;
    const bool hasMargin = HasAnyMargin(st.margin);

    if (y > 0.f) y += hasMargin ? st.margin.top : DefaultGapAbove(block);

    const bool wrap = width > 0.f;
    const float left = st.margin.left;
    const float availW = wrap ? std::max(0.f, width - st.margin.left - st.margin.right) : 0.f;

    // ---- 图片：占位框 + altText（P2 不解码）----
    if (block.kind == BlockKind::Image) {
        float imageW = block.width > 0.f ? block.width : (wrap ? availW : 200.f);
        if (wrap && imageW > availW) imageW = availW;
        const float imageH = block.height > 0.f ? block.height : imageW * kImageRatio;

        float imageX = left;
        if (block.imageAlign == richtext::Align::Center) {
            imageX = left + (availW - imageW) * 0.5f;
        } else if (block.imageAlign == richtext::Align::End) {
            imageX = left + availW - imageW;
        }

        it.blockBox = {imageX, y, imageW, imageH};
        it.textBox = it.blockBox;
        if (!block.altText.empty()) {
            if (it.auxPara == nullptr) it.auxPara = Take(poolAux, block.altText.c_str());
            it.auxPara = MakePara(it.auxPara, block.altText.c_str(), baseSize * 0.85f, 400u,
                                  it.lineHeight, it.family.c_str(), kNoWrapWidth, nullptr);
        }
        y += imageH + (hasMargin ? st.margin.bottom : 0.f);
        return;
    }

    // ---- 分割线 ----
    if (block.kind == BlockKind::Rule) {
        it.blockBox = {left, y, wrap ? availW : 200.f, kRuleHeight};
        it.textBox = it.blockBox;
        y += kRuleHeight + (hasMargin ? st.margin.bottom : 0.f);
        return;
    }

    // ---- 代码块：不折行，等宽，可选行号列 ----
    if (it.code) {
        const float pad = kCodePadding;
        const float numberW = it.lineNumbers ? kLineNumberWidth : 0.f;

        if (it.lineNumbers && it.auxPara == nullptr) {
            it.auxPara = Take(poolAux, it.numberText.c_str());
        }
        it.para = MakePara(it.para, it.text.c_str(), it.fontSize, it.weight, it.lineHeight,
                           it.family.c_str(), kNoWrapWidth, nullptr);
        if (it.lineNumbers) {
            it.auxPara = MakePara(it.auxPara, it.numberText.c_str(), it.fontSize, it.weight,
                                  it.lineHeight, it.family.c_str(), kNoWrapWidth, nullptr);
        }

        const float textH = it.para != nullptr ? it.para->size.y : it.fontSize * it.lineHeight;
        const float textW = it.para != nullptr ? it.para->size.x : 0.f;
        const float boxW = std::max(wrap ? availW : 0.f, textW + numberW + 2.f * pad);
        const float boxH = textH + 2.f * pad;

        it.blockBox = {left, y, boxW, boxH};
        it.textBox = {left + pad + numberW, y + pad, boxW - numberW - 2.f * pad, textH};
        y += boxH + (hasMargin ? st.margin.bottom : 0.f);
        return;
    }

    // ---- 文本块（段落 / 标题 / 引用 / 列表项）----
    float gutter = st.indent + static_cast<float>(it.depth) * kListIndent;
    if (block.kind == BlockKind::Quote) gutter += st.borderLeftWidth + kQuoteBorderGap;
    if (!it.marker.empty()) gutter += kMarkerWidth;

    const float maxW = wrap ? std::max(0.f, availW - gutter) : kNoWrapWidth;
    it.para = MakePara(it.para, it.text.c_str(), it.fontSize, it.weight, it.lineHeight,
                       it.family.c_str(), maxW, it.runs.empty() ? nullptr : &it.runs);

    const float textH = it.para != nullptr ? it.para->size.y : it.fontSize * it.lineHeight;
    const float textW = it.para != nullptr ? it.para->size.x : 0.f;
    const float boxW = wrap ? availW : std::max(textW + gutter, 1.f);

    it.blockBox = {left, y, boxW, textH};
    it.textBox = {left + gutter, y, std::max(0.f, boxW - gutter), textH};

    if (!it.marker.empty()) {
        if (it.markerPara == nullptr) it.markerPara = Take(poolMarker, it.marker.c_str());
        it.markerPara = MakePara(it.markerPara, it.marker.c_str(), it.fontSize, it.weight,
                                 it.lineHeight, it.family.c_str(), kNoWrapWidth, nullptr);
        const float markerW = it.markerPara != nullptr ? it.markerPara->size.x : 0.f;
        it.markerX = it.textBox.x - kMarkerGapRight - markerW;
    }

    y += textH + (hasMargin ? st.margin.bottom : DefaultGapBelow(block.kind));
}

// ---------------------------------------------------------------------------
// 几何
// ---------------------------------------------------------------------------

float RichEdit::Impl::AlignDx(const Item& it) const {
    const float natural = it.para != nullptr ? it.para->size.x : 0.f;
    if (it.textBox.w <= natural) return 0.f;
    if (it.style.align == richtext::Align::Center) return (it.textBox.w - natural) * 0.5f;
    if (it.style.align == richtext::Align::End) return it.textBox.w - natural;
    return 0.f;
}

Vec2 RichEdit::Impl::TextOriginDoc(const Item& it) const {
    return Vec2{it.textBox.x + AlignDx(it), it.textBox.y};
}

bool RichEdit::Impl::CaretRectOf(Cursor c, Rect& out) const {
    if (c.block >= editables.size()) return false;
    const Item& it = *editables[c.block].item;
    if (it.para == nullptr) return false;
    Rect r;
    if (!text::Engine::Get().CaretRect(it.para, c.offset, r)) return false;
    const Vec2 o = TextOriginDoc(it);
    out = Rect{o.x + r.x, o.y + r.y, r.w > 0.f ? r.w : 1.f, r.h};
    return true;
}

void RichEdit::Impl::EnsureLines() const {
    if (!linesDirty) return;
    lines.clear();
    for (size_t i = 0; i < editables.size(); ++i) {
        const Item& it = *editables[i].item;
        if (it.para == nullptr) continue;
        text::Engine::Get().LineInfos(it.para, lineScratch);
        const Vec2 o = TextOriginDoc(it);
        for (const text::LineInfo& li : lineScratch) {
            VisLine line;
            line.editable = i;
            line.start = li.start;
            line.end = li.end;
            line.top = o.y + li.top;
            line.bottom = o.y + li.top + li.height;
            lines.push_back(line);
        }
    }
    linesDirty = false;
}

int RichEdit::Impl::LineIndexOf(Cursor c) const {
    int found = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].editable != c.block) continue;
        found = static_cast<int>(i);
        if (c.offset <= lines[i].end) return found;   // 折行边界归前一行
    }
    return found;
}

float RichEdit::Impl::CaretXDoc() const {
    Rect r;
    if (!CaretRectOf(caret, r)) return 0.f;
    return r.x;
}

uint32_t RichEdit::Impl::TextLenOf(size_t block) const {
    if (block >= editables.size()) return 0;
    return static_cast<uint32_t>(editables[block].item->text.size());
}

RichEdit::Impl::Cursor RichEdit::Impl::EndCursor(size_t block) const {
    return Cursor{block, TextLenOf(block)};
}

RichEdit::Impl::Cursor RichEdit::Impl::ClampToDoc(Cursor c) const {
    if (editables.empty()) return Cursor{};
    if (c.block >= editables.size()) c.block = editables.size() - 1;
    const uint32_t length = TextLenOf(c.block);
    if (c.offset > length) c.offset = length;
    return c;
}

size_t RichEdit::Impl::IndexOf(const Block* block) const {
    if (block == nullptr) return kNoEditable;
    for (size_t i = 0; i < editables.size(); ++i) {
        if (editables[i].block == block) return i;
    }
    return kNoEditable;
}

RichEdit::Impl::Cursor RichEdit::Impl::HitTestDoc(Vec2 pt) const {
    if (editables.empty()) return Cursor{};
    for (size_t i = 0; i < editables.size(); ++i) {
        const Item& it = *editables[i].item;
        const Rect& box = it.blockBox;
        if (pt.y < box.y) {
            if (i == 0) return Cursor{0, 0};
            const Rect& prev = editables[i - 1].item->blockBox;
            // 两点之间按距离取近的一侧（图片/代码块区点击会落到相邻文本块）
            return (pt.y - prev.Bottom() < box.y - pt.y) ? EndCursor(i - 1) : Cursor{i, 0};
        }
        if (pt.y <= box.Bottom()) {
            const Vec2 o = TextOriginDoc(it);
            uint32_t index = 0;
            if (it.para != nullptr) {
                index = text::Engine::Get().HitTestPoint(it.para, Vec2{pt.x - o.x, pt.y - o.y});
            }
            index = richtext::SnapBoundary(it.text, index);
            return Cursor{i, index};
        }
    }
    return EndCursor(editables.size() - 1);
}

bool RichEdit::Impl::SelectionRange(const Item& it, const SelectionInfo& sel,
                                   uint32_t& s, uint32_t& e) const {
    if (!sel.active || it.editableIndex == kNoEditable) return false;
    const size_t b = it.editableIndex;
    if (b < sel.first || b > sel.last) return false;
    const uint32_t length = static_cast<uint32_t>(it.text.size());
    s = (b == sel.first) ? std::min(sel.firstOffset, length) : 0u;
    e = (b == sel.last) ? std::min(sel.lastOffset, length) : length;
    return s < e;
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void RichEdit::Impl::Paint(graphics::Painter& p, const StyleContext& ctx, float scale, Vec2 origin,
                          bool showCaret, Color caretColor, Color selectionColor) const {
    SelectionInfo sel;
    sel.active = HasSelection();
    if (sel.active) {
        Cursor a = ClampToDoc(caret);
        Cursor b = ClampToDoc(anchor);
        if (BlockLess(b, a)) std::swap(a, b);
        sel.first = a.block;
        sel.last = b.block;
        sel.firstOffset = a.offset;
        sel.lastOffset = b.offset;
    }

    for (const std::unique_ptr<Item>& up : items) {
        PaintItem(*up, p, ctx, scale, origin, sel, selectionColor);
    }

    if (showCaret) {
        Rect r;
        if (CaretRectOf(caret, r)) {
            p.FillRoundedRect(Rect{origin.x + r.x, origin.y + r.y, r.w, r.h}, 0.f, caretColor, 1.f, scale);
        }
    }
}

void RichEdit::Impl::PaintItem(const Item& it, graphics::Painter& p, const StyleContext& ctx,
                              float scale, Vec2 origin, const SelectionInfo& sel,
                              Color selectionColor) const {
    const Block& block = *it.block;
    const BlockStyle& st = it.style;

    Color background = st.background;
    if (background == 0 && it.code) background = kCodeBackground;
    if (background != 0) {
        p.FillRoundedRect(OffsetRect(it.blockBox, origin), 0.f, background, 1.f, scale);
    }

    if (st.borderLeftWidth > 0.f) {
        const Rect border{origin.x + it.blockBox.x, origin.y + it.blockBox.y,
                          st.borderLeftWidth, it.blockBox.h};
        const Color color = st.borderLeftColor != 0 ? st.borderLeftColor : kQuoteBorderDefault;
        p.FillRoundedRect(border, 0.f, color, 1.f, scale);
    }

    if (block.kind == BlockKind::Image) {
        const Rect box = OffsetRect(it.blockBox, origin);
        p.FillRoundedRect(box, 4.f, kImageFill, 1.f, scale);
        p.StrokeRoundRect(box, 4.f, 1.f, kImageStroke, scale);
        if (it.auxPara != nullptr && !it.auxPara->text.empty()) {
            const Vec2 altOrigin{box.x + (box.w - it.auxPara->size.x) * 0.5f,
                                 box.y + (box.h - it.auxPara->size.y) * 0.5f};
            text::Engine::Get().Draw(it.auxPara, p, scale, altOrigin, kImageTextColor);
        }
        return;
    }

    if (block.kind == BlockKind::Rule) {
        p.FillRoundedRect(OffsetRect(it.blockBox, origin), 0.f,
                          st.background != 0 ? st.background : kRuleColor, 1.f, scale);
        return;
    }

    const Vec2 textOrigin{origin.x + it.textBox.x + AlignDx(it), origin.y + it.textBox.y};

    if (it.para != nullptr) {
        // run 高亮盒
        for (const Highlight& hl : it.highlights) {
            if (hl.color == 0) continue;
            text::Engine::Get().SelectionRects(it.para, hl.start, hl.start + hl.length, scratch);
            for (const Rect& r : scratch) {
                const Rect box{textOrigin.x + r.x - hl.padding.left,
                               textOrigin.y + r.y - hl.padding.top,
                               r.w + hl.padding.left + hl.padding.right,
                               r.h + hl.padding.top + hl.padding.bottom};
                p.FillRoundedRect(box, hl.radius, hl.color, 1.f, scale);
            }
        }

        // 选区（压在 run 高亮之上、文字之下）
        uint32_t selStart = 0;
        uint32_t selEnd = 0;
        if (SelectionRange(it, sel, selStart, selEnd)) {
            text::Engine::Get().SelectionRects(it.para, selStart, selEnd, scratch);
            for (const Rect& r : scratch) {
                p.FillRoundedRect(Rect{textOrigin.x + r.x, textOrigin.y + r.y, r.w, r.h}, 0.f,
                                  selectionColor, 1.f, scale);
            }
        }

        text::Engine::Get().Draw(it.para, p, scale, textOrigin, ctx.color);
    }

    if (it.lineNumbers && it.auxPara != nullptr) {
        const Vec2 numberOrigin{origin.x + it.textBox.x - kLineNumberWidth, origin.y + it.textBox.y};
        text::Engine::Get().Draw(it.auxPara, p, scale, numberOrigin, kCodeNumberColor);
    }

    if (it.markerPara != nullptr) {
        const Vec2 markerOrigin{origin.x + it.markerX, origin.y + it.textBox.y};
        text::Engine::Get().Draw(it.markerPara, p, scale, markerOrigin, ctx.color);
    }
}

// ---------------------------------------------------------------------------
// 缓存调度
// ---------------------------------------------------------------------------

void RichEdit::Impl::EnsureLayout(const StyleContext& ctx, float width) {
    lastCtx = ctx;

    const float base = doc.defaultFontSize > 0.f ? doc.defaultFontSize : ctx.fontSize;
    const float line = ctx.lineHeight > 0.f ? ctx.lineHeight : 1.2f;
    const wchar_t* family = !doc.defaultFont.empty() ? doc.defaultFont.c_str() : ctx.family;
    const wchar_t* fam = (family != nullptr) ? family : L"";

    if (laidOut && width == layoutWidth && base == layoutBase && line == layoutLine &&
        layoutFamily == fam) {
        return;
    }

    Discard(true);
    Build(ctx, base, line, fam);
    Place(width);
    FlushPool();

    layoutWidth = width;
    layoutBase = base;
    layoutLine = line;
    layoutFamily = fam;
    laidOut = true;
}

void RichEdit::Impl::Rebuild() {
    RebuildAt(layoutWidth);
}

void RichEdit::Impl::RebuildAt(float width) {
    laidOut = false;
    linesDirty = true;
    EnsureLayout(lastCtx, width);
    caret = ClampToDoc(caret);
    anchor = ClampToDoc(anchor);
}

// ---------------------------------------------------------------------------
// 光标 / 选区
// ---------------------------------------------------------------------------

void RichEdit::Impl::SetCaret(Cursor c, bool extend) {
    caret = ClampToDoc(c);
    if (!extend) anchor = caret;
    caretVisible = true;
    goalX = -1.f;
    lastEditTyping = false;
}

void RichEdit::Impl::MoveHorz(int dir, bool extend) {
    if (editables.empty()) return;
    Cursor c = ClampToDoc(caret);
    const String& text = editables[c.block].item->text;
    if (dir < 0) {
        if (c.offset == 0) {
            if (c.block == 0) return;
            c = EndCursor(c.block - 1);
        } else {
            c.offset = NextIndex(text, c.offset, -1);
        }
    } else {
        if (c.offset >= text.size()) {
            if (c.block + 1 >= editables.size()) return;
            c.block += 1;
            c.offset = 0;
        } else {
            c.offset = NextIndex(text, c.offset, 1);
        }
    }
    SetCaret(c, extend);
}

void RichEdit::Impl::MoveVertical(int dir, bool extend) {
    EnsureLines();
    if (lines.empty()) return;

    if (goalX < 0.f) goalX = CaretXDoc();
    const int current = LineIndexOf(ClampToDoc(caret));
    int target = current + dir;
    if (current < 0) target = 0;

    if (target < 0) {
        caret = Cursor{0, 0};
        if (!extend) anchor = caret;
        caretVisible = true;
        lastEditTyping = false;
        return;
    }
    if (target >= static_cast<int>(lines.size())) {
        caret = EndCursor(editables.size() - 1);
        if (!extend) anchor = caret;
        caretVisible = true;
        lastEditTyping = false;
        return;
    }

    const VisLine& line = lines[static_cast<size_t>(target)];
    const Item& it = *editables[line.editable].item;
    const Vec2 o = TextOriginDoc(it);
    const float localY = (line.top + line.bottom) * 0.5f - o.y;
    uint32_t index = 0;
    if (it.para != nullptr) {
        index = text::Engine::Get().HitTestPoint(it.para, Vec2{goalX - o.x, localY});
    }
    index = richtext::SnapBoundary(it.text, index);

    caret = ClampToDoc(Cursor{line.editable, index});
    if (!extend) anchor = caret;
    caretVisible = true;
    lastEditTyping = false;
}

void RichEdit::Impl::MoveLineEdge(bool toEnd, bool extend, bool whole) {
    Cursor c;
    if (editables.empty()) {
        c = Cursor{};
    } else if (whole) {
        c = toEnd ? EndCursor(editables.size() - 1) : Cursor{0, 0};
    } else {
        EnsureLines();
        const int index = LineIndexOf(ClampToDoc(caret));
        if (index >= 0) {
            const VisLine& line = lines[static_cast<size_t>(index)];
            c = Cursor{line.editable, toEnd ? line.end : line.start};
        } else {
            const size_t block = ClampToDoc(caret).block;
            c = toEnd ? EndCursor(block) : Cursor{block, 0};
        }
    }
    SetCaret(c, extend);
}

void RichEdit::Impl::SelectAll() {
    if (editables.empty()) {
        caret = Cursor{};
        anchor = caret;
        return;
    }
    anchor = Cursor{0, 0};
    caret = EndCursor(editables.size() - 1);
    caretVisible = true;
    lastEditTyping = false;
}

// ---------------------------------------------------------------------------
// 编辑：文本增删
// ---------------------------------------------------------------------------

void RichEdit::Impl::PushUndo(bool typing) {
    // 连续输入合并成一步：上一条也是输入且栈非空时不重复快照
    if (typing && lastEditTyping && !undo.empty()) return;

    Snapshot snapshot;
    snapshot.doc = doc;
    snapshot.caret = caret;
    snapshot.anchor = anchor;
    if (undo.size() >= kMaxUndo) undo.erase(undo.begin());
    undo.push_back(std::move(snapshot));
    redo.clear();
    lastEditTyping = typing;
}

bool RichEdit::Impl::Undo() {
    if (undo.empty()) return false;
    Snapshot current;
    current.doc = doc;
    current.caret = caret;
    current.anchor = anchor;
    redo.push_back(std::move(current));

    Snapshot previous = std::move(undo.back());
    undo.pop_back();
    Restore(previous);
    lastEditTyping = false;
    return true;
}

bool RichEdit::Impl::Redo() {
    if (redo.empty()) return false;
    Snapshot current;
    current.doc = doc;
    current.caret = caret;
    current.anchor = anchor;
    undo.push_back(std::move(current));

    Snapshot next = std::move(redo.back());
    redo.pop_back();
    Restore(next);
    lastEditTyping = false;
    return true;
}

void RichEdit::Impl::Restore(const Snapshot& s) {
    const float width = layoutWidth;
    Discard(false);           // 文本全变，段落不可复用
    doc = s.doc;
    ++version;
    RebuildAt(width);
    caret = ClampToDoc(s.caret);
    anchor = ClampToDoc(s.anchor);
    caretVisible = true;
}

bool RichEdit::Impl::InsertTyped(const String& typed) {
    if (typed.empty()) return false;

    if (editables.empty()) {   // 空文档：先造一个空段落
        PushUndo(true);
        Block paragraph;
        paragraph.kind = BlockKind::Paragraph;
        doc.blocks.push_back(std::move(paragraph));
        ++version;
        Rebuild();
        caret = anchor = Cursor{0, 0};
    }

    PushUndo(true);
    if (HasSelection()) DeleteSelectionNoUndo();
    if (editables.empty()) return false;

    const Cursor c = ClampToDoc(caret);
    Block& block = *editables[c.block].block;
    const String text = richtext::BlockText(block);
    const uint32_t offset = richtext::SnapBoundary(text, c.offset);
    const RunStyle style = richtext::StyleAt(block, offset);

    richtext::InsertText(block, offset, typed, style);
    ++version;
    Rebuild();
    caret = anchor = ClampToDoc(Cursor{c.block, offset + static_cast<uint32_t>(typed.size())});
    caretVisible = true;
    return true;
}

bool RichEdit::Impl::DeleteBackward() {
    if (editables.empty()) return false;

    if (HasSelection()) {
        PushUndo(false);
        DeleteSelectionNoUndo();
        return true;
    }

    Cursor c = ClampToDoc(caret);
    if (c.offset > 0) {
        const String text = editables[c.block].item->text;
        const uint32_t from = NextIndex(text, c.offset, -1);
        PushUndo(false);
        richtext::DeleteRange(*editables[c.block].block, from, c.offset);
        ++version;
        Rebuild();
        caret = anchor = ClampToDoc(Cursor{c.block, from});
        caretVisible = true;
        return true;
    }

    if (c.block == 0) return false;

    // 段首：与上一块合并（要求同容器且相邻）
    Block* current = editables[c.block].block;
    Block* previous = editables[c.block - 1].block;
    Spot currentSpot{};
    Spot previousSpot{};
    if (!FindSpot(current, currentSpot) || !FindSpot(previous, previousSpot)) return false;
    if (currentSpot.container != previousSpot.container) return false;
    if (currentSpot.index != previousSpot.index + 1) return false;

    const uint32_t previousLen = static_cast<uint32_t>(richtext::BlockText(*previous).size());
    PushUndo(false);
    if (!richtext::BlockText(*current).empty()) richtext::AppendRuns(*previous, *current);
    currentSpot.container->erase(currentSpot.container->begin() +
                                 static_cast<ptrdiff_t>(currentSpot.index));
    ++version;
    Rebuild();
    caret = anchor = ClampToDoc(Cursor{c.block - 1, previousLen});
    caretVisible = true;
    return true;
}

bool RichEdit::Impl::DeleteForward() {
    if (editables.empty()) return false;

    if (HasSelection()) {
        PushUndo(false);
        DeleteSelectionNoUndo();
        return true;
    }

    const Cursor c = ClampToDoc(caret);
    const String text = editables[c.block].item->text;
    if (c.offset < text.size()) {
        const uint32_t to = NextIndex(text, c.offset, 1);
        PushUndo(false);
        richtext::DeleteRange(*editables[c.block].block, c.offset, to);
        ++version;
        Rebuild();
        caret = anchor = ClampToDoc(c);
        caretVisible = true;
        return true;
    }

    if (c.block + 1 >= editables.size()) return false;

    Block* current = editables[c.block].block;
    Block* next = editables[c.block + 1].block;
    Spot currentSpot{};
    Spot nextSpot{};
    if (!FindSpot(current, currentSpot) || !FindSpot(next, nextSpot)) return false;
    if (currentSpot.container != nextSpot.container) return false;
    if (nextSpot.index != currentSpot.index + 1) return false;

    PushUndo(false);
    richtext::AppendRuns(*current, *next);
    currentSpot.container->erase(currentSpot.container->begin() +
                                 static_cast<ptrdiff_t>(nextSpot.index));
    ++version;
    Rebuild();
    caret = anchor = ClampToDoc(c);
    caretVisible = true;
    return true;
}

bool RichEdit::Impl::InsertNewline() {
    if (editables.empty()) {
        PushUndo(false);
        Block paragraph;
        paragraph.kind = BlockKind::Paragraph;
        doc.blocks.push_back(std::move(paragraph));
        ++version;
        Rebuild();
        caret = anchor = Cursor{0, 0};
        return true;
    }

    PushUndo(false);
    if (HasSelection()) DeleteSelectionNoUndo();
    if (editables.empty()) return false;

    const Cursor c = ClampToDoc(caret);
    Block* block = editables[c.block].block;
    Spot spot{};
    if (!FindSpot(block, spot)) return false;

    // 空列表项上回车：退出列表 —— 空项原地换成普通段落，列表在空项处断成上下两个
    // 独立列表：[A, _, B] → List[A] + 段落(光标) + List[B]。空项在首/尾时对应只剩
    // 一侧列表；列表只有这一空项时整段换成空段落。
    // 关键是段落必须插在 list 所在的容器里（而不是 ListItem.children —— 那里只认
    // List，段落不会被 Build 登记，IndexOf 失败会让光标跳回文档开头）。
    if (block->kind == BlockKind::ListItem && richtext::BlockText(*block).empty()) {
        Block* list = spot.parentList;
        const BlockStyle itemStyle = block->style;

        // 占位项（落在父项 children 里的孤立列表项，没有所属 list）。它用 level 表示
        // "还能提升几级"：1 = 一级项（带点标记），0 = 顶级项（无标记，缩进不变）。
        // 每次回车提升一级，已在顶级 → 变空段落。
        // 缩进基准取父项自己的 style.indent，标记列 / 无标记列都要加在它上面，
        // 否则占位项会比父项少一段缩进（文档给了 item 缩进时尤其明显）。
        if (list == nullptr) {
            const Block* owner = OwnerOfChildren(doc.blocks, spot.container);
            const float ownerIndent = (owner != nullptr) ? owner->style.indent : 0.f;
            if (block->level > 0) {
                block->level = 0;                                // 一级项 → 顶级项
                block->style.indent = ownerIndent + kMarkerWidth;  // 去标记，缩进保持一级
            } else {
                block->kind = BlockKind::Paragraph;              // 顶级项 → 空段落
                block->style.indent = 0.f;
            }
            ++version;
            Rebuild();
            const size_t index = IndexOf(block);
            caret = anchor = (index == kNoEditable ? ClampToDoc(caret) : Cursor{index, 0});
            caretVisible = true;
            return true;
        }

        Spot listSpot{};
        if (!FindSpot(list, listSpot)) return false;

        const size_t k = spot.index;   // 空项在 list.children 里的下标
        Block* owner = OwnerOfChildren(doc.blocks, listSpot.container);
        const bool nested = owner != nullptr && owner->kind == BlockKind::ListItem;

        const BlockStyle listStyle = list->style;
        const int listLevel = list->level;
        const richtext::ListType listType = list->listType;

        std::vector<Block> kids = std::move(list->children);

        Block head;   // 空项之前的部分
        Block tail;   // 空项之后的部分
        head.kind = tail.kind = BlockKind::List;
        head.style = tail.style = listStyle;
        head.level = tail.level = listLevel;
        head.listType = tail.listType = listType;
        for (size_t i = 0; i < kids.size(); ++i) {
            if (i < k) head.children.push_back(std::move(kids[i]));
            else if (i > k) tail.children.push_back(std::move(kids[i]));
        }

        const bool hasHead = !head.children.empty();
        const bool hasTail = !tail.children.empty();

        std::vector<Block>* container = listSpot.container;
        const size_t at = listSpot.index;
        container->erase(container->begin() + static_cast<ptrdiff_t>(at));

        size_t at2 = at;
        if (hasHead) {
            container->insert(container->begin() + static_cast<ptrdiff_t>(at2), std::move(head));
            ++at2;
        }

        // 中间块：顶层列表 → 段落（退出列表）；嵌套列表 → 一级占位项（父项内，标记列
        // 给出与一级列表项一致的缩进）。占位项留在原位，再回车逐级提升
        // （一级 → 顶级 → 空段落），位置始终停在上下两段之间。
        Block placeholder;
        placeholder.kind = nested ? BlockKind::ListItem : BlockKind::Paragraph;
        placeholder.style = itemStyle;
        placeholder.style.indent = nested && owner != nullptr ? owner->style.indent : 0.f;
        placeholder.level = 1;
        container->insert(container->begin() + static_cast<ptrdiff_t>(at2), std::move(placeholder));

        const size_t placeholderAt = at2;
        if (hasTail) {
            container->insert(container->begin() + static_cast<ptrdiff_t>(placeholderAt + 1),
                              std::move(tail));
        }

        Block* focus = &(*container)[placeholderAt];
        ++version;
        Rebuild();
        const size_t index = IndexOf(focus);
        caret = anchor = (index == kNoEditable ? ClampToDoc(Cursor{c.block, 0})
                                               : Cursor{index, 0});
        caretVisible = true;
        return true;
    }

    // 其余：分裂（列表项 → 同级新项；段落/标题/引用 → 同类型新块）

    Block tail = richtext::SplitBlock(*block, c.offset);
    std::vector<Block>* container = spot.container;
    const size_t insertAt = spot.index + 1;
    container->insert(container->begin() + static_cast<ptrdiff_t>(insertAt), std::move(tail));
    Block* inserted = &container->at(insertAt);
    ++version;
    Rebuild();
    const size_t index = IndexOf(inserted);
    caret = anchor = (index == kNoEditable ? ClampToDoc(Cursor{c.block, 0})
                                          : Cursor{index, 0});
    caretVisible = true;
    return true;
}

bool RichEdit::Impl::DeleteSelectionNoUndo() {
    if (!HasSelection() || editables.empty()) return false;

    Cursor a = ClampToDoc(caret);
    Cursor b = ClampToDoc(anchor);
    if (BlockLess(b, a)) std::swap(a, b);

    const bool sameBlock = (a.block == b.block);

    // 1) 首块：删 [a.offset, 末)（同块时删到 b.offset）
    {
        Block& first = *editables[a.block].block;
        const uint32_t length = static_cast<uint32_t>(richtext::BlockText(first).size());
        richtext::DeleteRange(first, a.offset, sameBlock ? b.offset : length);
    }
    // 2) 末块：删 [0, b.offset)
    if (!sameBlock && b.offset > 0) {
        richtext::DeleteRange(*editables[b.block].block, 0, b.offset);
    }
    ++version;

    // 3) 中间块：清空文本，空的就整块删掉（从后往前，前面的下标不受影响）
    if (!sameBlock && b.block > a.block + 1) {
        Rebuild();
        for (size_t i = b.block - 1; i > a.block; --i) {
            if (i >= editables.size()) continue;
            Block* middle = editables[i].block;
            richtext::DeleteRange(*middle, 0, static_cast<uint32_t>(richtext::BlockText(*middle).size()));
            if (!richtext::BlockText(*middle).empty() || !middle->children.empty()) continue;
            Spot spot{};
            if (FindSpot(middle, spot)) {
                spot.container->erase(spot.container->begin() + static_cast<ptrdiff_t>(spot.index));
                Rebuild();
            }
        }
    }

    // 4) 相邻两块同容器 → 合并（跨块删除的常见形态）
    if (!sameBlock && b.block == a.block + 1) {
        Block* first = editables[a.block].block;
        Block* second = editables[b.block].block;
        Spot firstSpot{};
        Spot secondSpot{};
        if (FindSpot(first, firstSpot) && FindSpot(second, secondSpot) &&
            firstSpot.container == secondSpot.container &&
            secondSpot.index == firstSpot.index + 1) {
            richtext::AppendRuns(*first, *second);
            firstSpot.container->erase(firstSpot.container->begin() +
                                       static_cast<ptrdiff_t>(secondSpot.index));
        }
    }

    Rebuild();
    caret = anchor = ClampToDoc(Cursor{a.block, a.offset});
    caretVisible = true;
    linesDirty = true;
    return true;
}

// ---------------------------------------------------------------------------
// 编辑：剪贴板
// ---------------------------------------------------------------------------

String RichEdit::Impl::SelectedText() const {
    if (!HasSelection() || editables.empty()) return String();

    Cursor a = ClampToDoc(caret);
    Cursor b = ClampToDoc(anchor);
    if (BlockLess(b, a)) std::swap(a, b);

    String out;
    for (size_t i = a.block; i <= b.block && i < editables.size(); ++i) {
        const String& text = editables[i].item->text;
        const uint32_t start = (i == a.block) ? std::min(a.offset, static_cast<uint32_t>(text.size())) : 0u;
        const uint32_t end = (i == b.block) ? std::min(b.offset, static_cast<uint32_t>(text.size()))
                                            : static_cast<uint32_t>(text.size());
        if (i != a.block) out += L'\n';
        if (end > start) out.append(text, start, end - start);
    }
    return out;
}

void RichEdit::Impl::CopySelection() const {
    if (!HasSelection()) return;
    WriteClipboardText(SelectedText());
}

bool RichEdit::Impl::CutSelection() {
    if (!HasSelection()) return false;
    WriteClipboardText(SelectedText());
    PushUndo(false);
    DeleteSelectionNoUndo();
    return true;
}

bool RichEdit::Impl::PasteClipboard() {
    String raw;
    if (!ReadClipboardText(raw) || raw.empty()) return false;

    // 换行归一：\r\n / \r → \n（块内不留 \n，换行即块边界）
    String text;
    text.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        const wchar_t ch = raw[i];
        if (ch == L'\r') {
            text += L'\n';
            if (i + 1 < raw.size() && raw[i + 1] == L'\n') ++i;
        } else {
            text += ch;
        }
    }

    PushUndo(false);
    if (HasSelection()) DeleteSelectionNoUndo();

    if (editables.empty()) {
        Block paragraph;
        paragraph.kind = BlockKind::Paragraph;
        doc.blocks.push_back(std::move(paragraph));
        ++version;
        Rebuild();
        caret = anchor = Cursor{0, 0};
    }
    if (editables.empty()) return false;

    const Cursor c = ClampToDoc(caret);
    const RunStyle style = richtext::StyleAt(*editables[c.block].block, c.offset);

    std::vector<String> segments;
    {
        String line;
        for (const wchar_t ch : text) {
            if (ch == L'\n') {
                segments.push_back(line);
                line.clear();
            } else {
                line += ch;
            }
        }
        segments.push_back(line);
    }

    Block* block = editables[c.block].block;
    Spot spot{};
    if (!FindSpot(block, spot)) return false;
    const BlockStyle paragraphStyle = block->style;

    richtext::InsertText(*block, c.offset, segments[0], style);
    const uint32_t firstEnd = c.offset + static_cast<uint32_t>(segments[0].size());

    for (size_t i = 1; i < segments.size(); ++i) {
        Block paragraph;
        paragraph.kind = BlockKind::Paragraph;
        paragraph.style = paragraphStyle;
        paragraph.style.indent = 0.f;
        if (!segments[i].empty()) paragraph.runs.push_back(Run{segments[i], style});
        spot.container->insert(spot.container->begin() + static_cast<ptrdiff_t>(spot.index + i),
                               std::move(paragraph));
    }

    ++version;
    Rebuild();

    if (segments.size() == 1) {
        caret = anchor = ClampToDoc(Cursor{c.block, firstEnd});
    } else {
        const uint32_t lastOffset = static_cast<uint32_t>(segments.back().size());
        caret = anchor = ClampToDoc(Cursor{c.block + segments.size() - 1, lastOffset});
    }
    caretVisible = true;
    return true;
}

// ---------------------------------------------------------------------------
// 文档树定位
// ---------------------------------------------------------------------------

bool RichEdit::Impl::FindSpotRec(std::vector<Block>& vec, Block* target, Block* ownerList,
                                 Spot& out) {
    for (size_t i = 0; i < vec.size(); ++i) {
        Block& block = vec[i];
        if (&block == target) {
            out.container = &vec;
            out.index = i;
            out.parentList = ownerList;
            return true;
        }
        if (block.children.empty()) continue;
        // List 的 children 是 ListItem（ownerList = 该 list）；
        // ListItem 的 children 是嵌套 List（继续下潜，ownerList 由内层 list 决定）
        Block* childOwner = (block.kind == BlockKind::List) ? &block : nullptr;
        if (FindSpotRec(block.children, target, childOwner, out)) return true;
    }
    return false;
}

bool RichEdit::Impl::FindSpot(Block* target, Spot& out) {
    out = Spot{};
    return target != nullptr && FindSpotRec(doc.blocks, target, nullptr, out);
}

// ---------------------------------------------------------------------------
// 按键分派
// ---------------------------------------------------------------------------

RichEdit::Impl::KeyResult RichEdit::Impl::HandleKey(int key, bool shift, bool ctrl) {
    switch (key) {
    case VK_LEFT:   MoveHorz(-1, shift);       return KeyResult::Handled;
    case VK_RIGHT:  MoveHorz(1, shift);        return KeyResult::Handled;
    case VK_UP:     MoveVertical(-1, shift);   return KeyResult::Handled;
    case VK_DOWN:   MoveVertical(1, shift);    return KeyResult::Handled;
    case VK_HOME:   MoveLineEdge(false, shift, ctrl); return KeyResult::Handled;
    case VK_END:    MoveLineEdge(true, shift, ctrl);  return KeyResult::Handled;
    case VK_BACK:   return DeleteBackward() ? KeyResult::Content : KeyResult::None;
    case VK_DELETE: return DeleteForward() ? KeyResult::Content : KeyResult::None;
    case VK_RETURN: return InsertNewline() ? KeyResult::Content : KeyResult::None;
    default: break;
    }

    if (!ctrl) return KeyResult::None;

    switch (key) {
    case 'A': SelectAll(); return KeyResult::Handled;
    case 'C': CopySelection(); return KeyResult::Handled;
    case 'X': return CutSelection() ? KeyResult::Content : KeyResult::None;
    case 'V': return PasteClipboard() ? KeyResult::Content : KeyResult::None;
    case 'Z': return Undo() ? KeyResult::Content : KeyResult::None;
    case 'Y': return Redo() ? KeyResult::Content : KeyResult::None;
    default: return KeyResult::None;
    }
}

// ---------------------------------------------------------------------------
// RichEdit
// ---------------------------------------------------------------------------

RichEdit::RichEdit() : impl_(std::make_unique<Impl>()) {
    SetFocusable(true);
}

RichEdit::~RichEdit() {
    if (impl_) impl_->Discard(false);
}

void RichEdit::SetDoc(const richtext::RichDoc& doc) {
    impl_->Discard(false);     // 旧 layout 先丢：Item 里的 Block* 随即失效
    impl_->doc = doc;
    impl_->undo.clear();
    impl_->redo.clear();
    impl_->lastEditTyping = false;
    impl_->caret = Impl::Cursor{};
    impl_->anchor = Impl::Cursor{};
    ++impl_->version;
    MarkDirty(kFlagLayout | kFlagPaint);
}

const richtext::RichDoc& RichEdit::Doc() const noexcept {
    return impl_->doc;
}

float RichEdit::ContentHeight() const noexcept {
    return impl_->contentSize.y;
}

void RichEdit::SetEditable(bool on) {
    editable_ = on;
    SetFocusable(on);
    if (!on) {
        impl_->dragging = false;
        impl_->caret = Impl::Cursor{};
        impl_->anchor = Impl::Cursor{};
    }
    MarkDirty(kFlagPaint);
}

bool RichEdit::HasSelection() const noexcept {
    return impl_->HasSelection();
}

Vec2 RichEdit::ContentOrigin() const {
    const Thickness pad = ValueAsThickness(ResolveProperty(this, Prop::Padding));
    const Rect box = Bounds();
    return Vec2{box.x + pad.left, box.y + pad.top};
}

Vec2 RichEdit::MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const {
    float width = contentAvail.x;
    if (width <= 0.f) {
        // 无宽度约束（Row 里的自适应）：沿用上次排好的宽度，其次用实际盒宽
        width = impl_->laidOut && impl_->layoutWidth > 0.f ? impl_->layoutWidth
                                                          : std::max(0.f, Bounds().w);
    }
    impl_->EnsureLayout(ctx, width);
    return impl_->contentSize;
}

void RichEdit::OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) {
    const Thickness pad = ValueAsThickness(ResolveProperty(this, Prop::Padding, ctx));
    const Rect box = Bounds();
    const float width = box.w - pad.left - pad.right;
    impl_->EnsureLayout(ctx, width > 0.f ? width : 0.f);

    const Color selectionColor = ResolveProperty(this, Prop::SelectionColor, ctx).u;
    Color caretColor = ResolveProperty(this, Prop::Color, ctx).u;
    StyleValue caretValue;
    if (TryResolveElement(this, Prop::CaretColor, caretValue)) caretColor = caretValue.u;

    const bool showCaret = editable_ && HasFocus() && impl_->caretVisible;
    impl_->Paint(p, ctx, scale, Vec2{box.x + pad.left, box.y + pad.top}, showCaret, caretColor,
                 selectionColor);
}

bool RichEdit::ContentClipRect(Rect& out) const {
    if (!impl_->overflowX) return false;   // 常规情况不加裁剪单元（省一次绘制切换）
    const Thickness pad = ValueAsThickness(ResolveProperty(this, Prop::Padding));
    out = Rect{ContentOrigin().x, ContentOrigin().y,
               std::max(0.f, Bounds().w - pad.left - pad.right),
               std::max(0.f, Bounds().h - pad.top - pad.bottom)};
    return true;
}

void RichEdit::OnFocusChanged(bool focused) {
    (void)focused;
    impl_->caretVisible = true;
    if (!focused) impl_->dragging = false;
    MarkDirty(kFlagPaint);
}

bool RichEdit::OnBlink() {
    if (!HasFocus()) {
        impl_->caretVisible = true;
        return false;
    }
    impl_->caretVisible = !impl_->caretVisible;
    MarkDirty(kFlagPaint);
    return true;
}

bool RichEdit::InsertionPoint(Rect& outRect) const {
    Rect doc;
    if (!impl_->CaretRectOf(impl_->caret, doc)) return false;

    impl_->EnsureLines();
    float top = doc.y;
    float height = doc.h;
    const int line = impl_->LineIndexOf(impl_->caret);
    if (line >= 0) {
        top = impl_->lines[static_cast<size_t>(line)].top;
        height = impl_->lines[static_cast<size_t>(line)].bottom - top;
    }
    if (!(height > 0.f)) height = doc.h;

    const Vec2 origin = ContentOrigin();
    outRect = Rect{origin.x + doc.x, origin.y + top, 1.f, height};
    return true;
}

bool RichEdit::HandlePointerDown(PointerEvent& ev) {
    if (!editable_) return false;
    Focus();
    const Vec2 origin = ContentOrigin();
    impl_->dragging = true;
    impl_->SetCaret(impl_->HitTestDoc(Vec2{ev.pos.x - origin.x, ev.pos.y - origin.y}), false);
    SetCapture();
    MarkDirty(kFlagPaint);
    return true;
}

bool RichEdit::HandlePointerMove(PointerEvent& ev) {
    if (!editable_ || !impl_->dragging) return false;
    const Vec2 origin = ContentOrigin();
    impl_->SetCaret(impl_->HitTestDoc(Vec2{ev.pos.x - origin.x, ev.pos.y - origin.y}), true);
    MarkDirty(kFlagPaint);
    return true;
}

bool RichEdit::HandlePointerUp(PointerEvent& ev) {
    (void)ev;
    if (!impl_->dragging) return false;
    impl_->dragging = false;
    ReleaseCapture();
    return false;
}

bool RichEdit::HandleKeyDown(KeyEvent& ev) {
    if (!editable_) return false;

    const Impl::KeyResult result = impl_->HandleKey(ev.key, ev.shift, ev.ctrl);
    if (result == Impl::KeyResult::None) return false;

    const bool changed = (result == Impl::KeyResult::Content);
    MarkDirty(changed ? (kFlagLayout | kFlagPaint) : kFlagPaint);
    if (changed) RevealCaret();

    ev.handled = true;
    return true;
}

bool RichEdit::HandleChar(KeyEvent& ev) {
    if (!editable_ || ev.ctrl || ev.alt) return false;

    const wchar_t ch = ev.ch;
    if (ch == 0 || ch == L'\t' || ch < 0x20 || ch == 0x7F) return false;

    impl_->InsertTyped(String(1, ch));
    MarkDirty(kFlagLayout | kFlagPaint);
    RevealCaret();

    ev.handled = true;
    return true;
}

void RichEdit::RevealCaret() {
    Scroller* scroller = FindScroller(this);
    if (scroller == nullptr) return;

    Rect doc;
    if (!impl_->CaretRectOf(impl_->caret, doc)) return;

    const Vec2 origin = ContentOrigin();
    const Rect caret{origin.x + doc.x, origin.y + doc.y, doc.w, doc.h};
    const Rect viewport = scroller->Viewport();
    if (viewport.w <= 0.f || viewport.h <= 0.f) return;

    float dy = 0.f;
    if (caret.y < viewport.y) dy = caret.y - viewport.y;
    else if (caret.Bottom() > viewport.Bottom()) dy = caret.Bottom() - viewport.Bottom();
    if (dy != 0.f) scroller->ScrollBy(0.f, dy);

    float dx = 0.f;
    if (caret.x < viewport.x) dx = caret.x - viewport.x;
    else if (caret.Right() > viewport.Right()) dx = caret.Right() - viewport.Right();
    if (dx != 0.f) scroller->ScrollBy(dx, 0.f);
}

}  // namespace dui
