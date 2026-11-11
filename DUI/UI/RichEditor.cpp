// ============================================================================
//  DUI/UI/RichEditor.cpp — 富文本控件的排版、绘制与编辑（RichEditor.md §3.1 / §4）
//
//  排版两块状态：
//    · Build：文档结构 → Item 列表（字体、run 表、标记文字、高亮区间），
//      与宽度无关；可编辑文本块同时在 editables 里登记（光标序号 = 数组下标）
//    · Place：按内容宽给每个 Item 排盒并调 TextEngine 排版（引擎按
//      text/family/size/lineHeight/maxWidth/runVersion 缓存）
//  编辑后重建时段落按文本从池中复用，未改动的块不重建 layout。
//
//  编辑（P3）：光标/选区是两个 {可编辑块序号, 段内偏移}；段内文本改动走
//  richtext::RichDocEditor 的纯函数，结构改动（分裂/合并/入列表）在这里做。
// ============================================================================

#include "UI/RichEditor.h"

#include <algorithm>
#include <utility>

#include "Core/Clipboard.h"
#include "Graphics/Graphics.h"
#include "Graphics/ImageCodec.h"
#include "UI/RichDocEditor.h"
#include "Graphics/TextEngine.h"
#include "UI/Scroller.h"
#include "UI/UI.h"

#include <unordered_map>

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
constexpr float kImageRatio = 0.5625f;   // 图片无尺寸且解码失败时占位框的宽高比（16:9）
constexpr float kNoWrapWidth = 1e6f;     // 不折行（DWrite 需要有限宽）

constexpr Color kCodeBackground = 0xFFF6F7F8;
constexpr Color kCodeNumberColor = 0xFF9AA0A6;
constexpr Color kRuleColor = 0xFFD8DCE2;
constexpr Color kImageFill = 0xFFF2F3F5;
constexpr Color kImageStroke = 0xFFC9CED6;
constexpr Color kImageTextColor = 0xFF8A9099;
constexpr Color kQuoteBorderDefault = 0xFF555555;   // 深灰（原蓝色，引用边框默认色）
constexpr float kQuotePadY = 6.f;                   // 引用块上下内边距：文字不贴盒沿

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

// base64 解码（RFC 4648 标准字母表；'=' 与空白跳过）—— data URI 图片用
size_t DecodeBase64(const wchar_t* s, size_t count, std::vector<uint8_t>& out) {
    auto Digit = [](wchar_t ch) -> int {
        if (ch >= L'A' && ch <= L'Z') return ch - L'A';
        if (ch >= L'a' && ch <= L'z') return ch - L'a' + 26;
        if (ch >= L'0' && ch <= L'9') return ch - L'0' + 52;
        if (ch == L'+') return 62;
        if (ch == L'/') return 63;
        return -1;
    };
    out.reserve(count / 4 * 3 + 3);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < count; ++i) {
        const int v = Digit(s[i]);
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out.size();
}

// 文本块（可编辑）：段落 / 标题 / 引用 / 列表项；代码块单独处理（内容在 block.code）
bool IsEditableKind(BlockKind kind) noexcept {
    return kind == BlockKind::Paragraph || kind == BlockKind::Heading ||
           kind == BlockKind::Quote || kind == BlockKind::ListItem;
}

// 光标可落的块 = 可编辑文本块 + 代码块 + 图片块。
// 代码块：内容在 block.code，可进入、可编辑（回车 = 块内换行，不拆块）。
// 图片块：注册成定位点，有两个光标位：
// offset 0 = 图前（贴图片左缘）、offset 1 = 图后（贴图片右缘）；
// Backspace / Delete 删除图片本身
bool IsCaretKind(BlockKind kind) noexcept {
    return IsEditableKind(kind) || kind == BlockKind::Code || kind == BlockKind::Image;
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
struct RichEditor::Impl {
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
        float     fontSize = 0.f;   // run 有效字号（DIP）：文字带高度按它缩放
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

    // 图片纹理缓存：src → 纹理（File 路径 / data URI）；空句柄 = 解码失败的
    // 负缓存，避免每个布局/绘制帧重试解码。绘制是 const 路径，惰性解码走 mutable
    mutable std::unordered_map<String, std::shared_ptr<graphics::ImageTexture>> images;

    void Discard(bool recycle);
    void FlushPool();
    text::Paragraph* Take(std::vector<text::Paragraph*>& pool, const wchar_t* text);
    const graphics::ImageTexture* ImageOf(const Block& block) const;

    bool IsImageAt(size_t index) const;
    bool EraseImageAt(size_t index);
    bool LeaveImageToText();
    // 文档顺序里 block 之后的第一个块（跨容器 / 跨层级）；没有则 nullptr
    Block* NextBlockAfter(Block* block);
    static Block* NextBlockRec(std::vector<Block>& vec, Block* block, bool& found);
    // 摘掉一个不可编辑的装饰块（分割线）：光标落到 caretAfter
    bool EraseDecorBlock(Block* target, Cursor caretAfter);
    // 图后按 Del：处理图片右边的块（分割线删掉 / 后邻图片删掉 / 文本块则光标移进去）
    bool DeleteAfterImage(size_t index);

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
    // 光标位（文本位置 / 图片的图前图后）都是"贴在某处的一小条矩形"，
    // 点到它的距离取平方比较（点落在矩形内为 0）
    static float DistToRect(Vec2 p, const Rect& r) {
        const float dx = (p.x < r.x) ? (r.x - p.x) : ((p.x > r.Right()) ? p.x - r.Right() : 0.f);
        const float dy = (p.y < r.y) ? (r.y - p.y) : ((p.y > r.Bottom()) ? p.y - r.Bottom() : 0.f);
        return dx * dx + dy * dy;
    }
    // 矩形向上延伸到 top（图片的竖线要盖住"图片这一行"上方的相邻块内容）
    static Rect ExtendUp(Rect r, float top) {
        if (top < r.y) {
            r.h += r.y - top;
            r.y = top;
        }
        return r;
    }
    void  EnsureLines() const;
    int   LineIndexOf(Cursor c) const;
    float CaretXDoc() const;
    Cursor HitTestDoc(Vec2 docPt) const;
    bool  SelectionRange(const Item& it, const SelectionInfo& sel, uint32_t& s, uint32_t& e) const;

    // ---- 光标 / 选区 ----
    bool   HasSelection() const noexcept {
        if (caret.block != anchor.block) return true;
        if (caret.offset == anchor.offset) return false;
        // 图片块内（图前 ↔ 图后）之间没有可选的文字
        return !IsImageAt(caret.block);
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

void RichEditor::Impl::Discard(bool recycle) {
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

void RichEditor::Impl::FlushPool() {
    for (text::Paragraph* p : poolMain) text::Engine::Get().Discard(p);
    for (text::Paragraph* p : poolAux) text::Engine::Get().Discard(p);
    for (text::Paragraph* p : poolMarker) text::Engine::Get().Discard(p);
    poolMain.clear();
    poolAux.clear();
    poolMarker.clear();
}

text::Paragraph* RichEditor::Impl::Take(std::vector<text::Paragraph*>& pool, const wchar_t* text) {
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

void RichEditor::Impl::Build(const StyleContext& ctx, float base, float line, const wchar_t* family) {
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

void RichEditor::Impl::BuildList(const Block& list, int depth, float base, uint32_t weight,
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

void RichEditor::Impl::BuildChildBlocks(const Block& parent, int depth, float base, uint32_t weight,
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

void RichEditor::Impl::BuildItem(const Block& block, const String& marker, int depth, float fontSize,
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
                it.highlights.push_back(Highlight{start, length, s.background, s.padding,
                                                  s.radius,
                                                  s.fontSize > 0.f ? s.fontSize : fontSize});
            }
            start += length;
        }
        if (!overrides) it.runs.clear();   // 全默认样式 → 走引擎单样式路径
    }

    it.para = Take(poolMain, it.text.c_str());

    if (IsCaretKind(block.kind)) {
        it.editableIndex = editables.size();
        editables.push_back(Editable{const_cast<Block*>(&block), owned.get()});
    }

    items.push_back(std::move(owned));
}

// ---------------------------------------------------------------------------
// Place：给每个 Item 排盒 + 调引擎排版
// ---------------------------------------------------------------------------

text::Paragraph* RichEditor::Impl::MakePara(text::Paragraph*& slot, const wchar_t* text, float size,
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

void RichEditor::Impl::Place(float width) {
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

void RichEditor::Impl::PlaceItem(Item& it, float width, float& y) {
    const Block& block = *it.block;
    const BlockStyle& st = it.style;
    const bool hasMargin = HasAnyMargin(st.margin);

    if (y > 0.f) y += hasMargin ? st.margin.top : DefaultGapAbove(block);

    const bool wrap = width > 0.f;
    const float left = st.margin.left;
    const float availW = wrap ? std::max(0.f, width - st.margin.left - st.margin.right) : 0.f;

    // ---- 图片：解码纹理按自然尺寸定盒；无纹理（无 src / 解码失败）退回占位 ----
    if (block.kind == BlockKind::Image) {
        const graphics::ImageTexture* tex = ImageOf(block);
        const float dpiScale = lastCtx.scale > 0.f ? lastCtx.scale : 1.f;

        float imageW;
        if (block.width > 0.f) {
            imageW = block.width;
        } else if (tex != nullptr) {
            imageW = static_cast<float>(tex->width) / dpiScale;   // 像素 → DIP
        } else {
            imageW = wrap ? availW : 200.f;
        }
        if (wrap && imageW > availW) imageW = availW;

        const float imageH = block.height > 0.f ? block.height
            : imageW * (tex != nullptr && tex->width > 0
                            ? static_cast<float>(tex->height) / static_cast<float>(tex->width)
                            : kImageRatio);

        float imageX = left;
        if (block.imageAlign == richtext::Align::Center) {
            imageX = left + (availW - imageW) * 0.5f;
        } else if (block.imageAlign == richtext::Align::End) {
            imageX = left + availW - imageW;
        }

        it.blockBox = {imageX, y, imageW, imageH};
        it.textBox = it.blockBox;
        if (tex == nullptr && !block.altText.empty()) {
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
    // 引用块上下留内边距，文字不贴盒沿（盒随排版增高，文字垂直居中）
    const float padY = (block.kind == BlockKind::Quote) ? kQuotePadY : 0.f;

    it.blockBox = {left, y, boxW, textH + 2.f * padY};
    it.textBox = {left + gutter, y + padY, std::max(0.f, boxW - gutter), textH};

    if (!it.marker.empty()) {
        if (it.markerPara == nullptr) it.markerPara = Take(poolMarker, it.marker.c_str());
        it.markerPara = MakePara(it.markerPara, it.marker.c_str(), it.fontSize, it.weight,
                                 it.lineHeight, it.family.c_str(), kNoWrapWidth, nullptr);
        const float markerW = it.markerPara != nullptr ? it.markerPara->size.x : 0.f;
        it.markerX = it.textBox.x - kMarkerGapRight - markerW;
    }

    y += textH + 2.f * padY + (hasMargin ? st.margin.bottom : DefaultGapBelow(block.kind));
}

// ---------------------------------------------------------------------------
// 几何
// ---------------------------------------------------------------------------

float RichEditor::Impl::AlignDx(const Item& it) const {
    const float natural = it.para != nullptr ? it.para->size.x : 0.f;
    if (it.textBox.w <= natural) return 0.f;
    if (it.style.align == richtext::Align::Center) return (it.textBox.w - natural) * 0.5f;
    if (it.style.align == richtext::Align::End) return it.textBox.w - natural;
    return 0.f;
}

Vec2 RichEditor::Impl::TextOriginDoc(const Item& it) const {
    return Vec2{it.textBox.x + AlignDx(it), it.textBox.y};
}

bool RichEditor::Impl::CaretRectOf(Cursor c, Rect& out) const {
    if (c.block >= editables.size()) return false;
    const Item& it = *editables[c.block].item;
    if (it.block->kind == BlockKind::Image) {
        // 图片两个光标位：0 = 图前（贴左缘）、1 = 图后（贴右缘）；都与图片等高
        const float x = (c.offset > 0) ? it.blockBox.Right() : it.blockBox.x;
        out = Rect{x, it.blockBox.y, 1.f, it.blockBox.h};
        return true;
    }
    if (it.para == nullptr) return false;
    Rect r;
    if (!text::Engine::Get().CaretRect(it.para, c.offset, r)) return false;
    const Vec2 o = TextOriginDoc(it);
    out = Rect{o.x + r.x, o.y + r.y, r.w > 0.f ? r.w : 1.f, r.h};
    return true;
}

void RichEditor::Impl::EnsureLines() const {
    if (!linesDirty) return;
    lines.clear();
    for (size_t i = 0; i < editables.size(); ++i) {
        const Item& it = *editables[i].item;
        if (it.block->kind == BlockKind::Image) {
            // 图片块补一条伪视觉行（占满图片盒）：上下移动 / 行首行尾能穿过图片。
            // end = 1：End 键落在图后（图前 = 0）
            VisLine line;
            line.editable = i;
            line.start = 0;
            line.end = 1;
            line.top = it.blockBox.y;
            line.bottom = it.blockBox.Bottom();
            lines.push_back(line);
            continue;
        }
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

int RichEditor::Impl::LineIndexOf(Cursor c) const {
    int found = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].editable != c.block) continue;
        found = static_cast<int>(i);
        if (c.offset <= lines[i].end) return found;   // 折行边界归前一行
    }
    return found;
}

float RichEditor::Impl::CaretXDoc() const {
    Rect r;
    if (!CaretRectOf(caret, r)) return 0.f;
    return r.x;
}

uint32_t RichEditor::Impl::TextLenOf(size_t block) const {
    if (block >= editables.size()) return 0;
    const Item& it = *editables[block].item;
    // 图片占一个光标位宽：offset 0 = 图前、offset 1 = 图后（ClampToDoc 据此放行）
    if (it.block->kind == BlockKind::Image) return 1;
    return static_cast<uint32_t>(it.text.size());
}

RichEditor::Impl::Cursor RichEditor::Impl::EndCursor(size_t block) const {
    return Cursor{block, TextLenOf(block)};
}

RichEditor::Impl::Cursor RichEditor::Impl::ClampToDoc(Cursor c) const {
    if (editables.empty()) return Cursor{};
    if (c.block >= editables.size()) c.block = editables.size() - 1;
    const uint32_t length = TextLenOf(c.block);
    if (c.offset > length) c.offset = length;
    return c;
}

size_t RichEditor::Impl::IndexOf(const Block* block) const {
    if (block == nullptr) return kNoEditable;
    for (size_t i = 0; i < editables.size(); ++i) {
        if (editables[i].block == block) return i;
    }
    return kNoEditable;
}

RichEditor::Impl::Cursor RichEditor::Impl::HitTestDoc(Vec2 pt) const {
    if (editables.empty()) return Cursor{};
    // 命中带（竖直方向）：
    //   · 文本块 = 文字区（不含内边距）—— 点在内边距里不算落在这个块
    //   · 图片块 = 图片盒 + 上一块内容底到图片盒之间那一条（上一块的下内边距 /
    //     间隙算图片这一行）。否则点图片右上方的空白会落进上一块：代码块盒底
    //     紧贴图片顶边（图片 margin.top = 0），中间那条淡灰内边距会被代码块抢走，
    //     光标跳进代码块而不是落到图后。
    float prevBottom = 0.f;   // 上一块的命中带底
    bool hasPrev = false;

    for (size_t i = 0; i < editables.size(); ++i) {
        const Item& it = *editables[i].item;
        const bool image = (it.block->kind == BlockKind::Image);
        Rect band = image ? it.blockBox : it.textBox;
        if (image && hasPrev) band.y = std::min(band.y, prevBottom);

        if (pt.y < band.y) {
            if (i == 0) return Cursor{0, 0};
            // 两点之间按距离取近的一侧（间隙）
            return (pt.y - prevBottom < band.y - pt.y) ? EndCursor(i - 1) : Cursor{i, 0};
        }
        if (pt.y <= band.Bottom()) {
            if (image) {
                // 点在图片右半（含图片右侧的空白）→ 光标落图后；左半 → 图前。
                // 这样"点击图像后面"能把光标放到图像右侧，Backspace 即删该图。
                const float mid = band.x + band.w * 0.5f;
                return Cursor{i, pt.x >= mid ? 1u : 0u};
            }
            const Vec2 o = TextOriginDoc(it);
            uint32_t index = 0;
            if (it.para != nullptr) {
                index = text::Engine::Get().HitTestPoint(it.para, Vec2{pt.x - o.x, pt.y - o.y});
            }
            index = richtext::SnapBoundary(it.text, index);
            Cursor best{i, index};

            // 与**相邻图片块**的光标位按"就近"竞争：图前 / 图后 是贴在图片左右缘的
            // 两条竖线。文字窄、图片又大又居中时，点文字右侧的空白往往离图片更近 ——
            // 示例里代码块只有几行短字、图片居中占满大半，点代码块右侧空白若仍吸到
            // 代码行尾，光标会莫名跳进代码块；按就近取才落在图片右缘（图后）。
            Rect bestRect{};
            if (CaretRectOf(best, bestRect)) {
                float bestDist = DistToRect(pt, bestRect);
                for (int step = -1; step <= 1; step += 2) {
                    if (step < 0 && i == 0) continue;
                    const size_t j = (step < 0) ? i - 1 : i + 1;
                    if (j >= editables.size()) continue;
                    if (!IsImageAt(j)) continue;
                    for (uint32_t off = 0; off < 2; ++off) {
                        Rect bar{};
                        if (!CaretRectOf(Cursor{j, off}, bar)) continue;
                        // 相邻图片的竖线向本块方向延伸：下方的图片向上盖到本块内容底，
                        // 上方的图片向下盖到本块内容顶 —— 图片"这一行"含相邻块那一条
                        if (step > 0) {
                            bar = ExtendUp(bar, band.Bottom());
                        } else {
                            const float bottom = band.y;
                            if (bottom > bar.Bottom()) {
                                bar.h += bottom - bar.Bottom();
                            }
                        }
                        const float d = DistToRect(pt, bar);
                        if (d < bestDist) {
                            bestDist = d;
                            best = Cursor{j, off};
                        }
                    }
                }
            }
            return best;
        }
        prevBottom = band.Bottom();
        hasPrev = true;
    }
    return EndCursor(editables.size() - 1);
}

bool RichEditor::Impl::SelectionRange(const Item& it, const SelectionInfo& sel,
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

void RichEditor::Impl::Paint(graphics::Painter& p, const StyleContext& ctx, float scale, Vec2 origin,
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

void RichEditor::Impl::PaintItem(const Item& it, graphics::Painter& p, const StyleContext& ctx,
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
        const graphics::ImageTexture* tex = ImageOf(block);
        if (tex != nullptr) {
            // 颜色约定与 Image::OnPaint 一致：RGB 取白，alpha = 不透明度
            p.AddImageQuad(tex->id, OffsetRect(it.blockBox, origin), 0xFFFFFFFF, scale);
            return;
        }
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
        // run 高亮盒：垂直收紧到基线附近的**文字带**（上 0.94em / 下 0.20em，含拉丁
        // 降部余量，总高 1.14em），不再用整行高 —— 行高含行距 leading，盒子会远
        // 高于文字且文字不居中。宽度仍用逐行 hit-test 矩形 + padding。
        std::vector<text::LineInfo> lineInfos;
        if (!it.highlights.empty()) {
            text::Engine::Get().LineInfos(it.para, lineInfos);
        }
        for (const Highlight& hl : it.highlights) {
            if (hl.color == 0) continue;
            text::Engine::Get().SelectionRects(it.para, hl.start, hl.start + hl.length, scratch);
            for (const Rect& r : scratch) {
                // 该矩形所在行的基线（HitTestTextRange 的 top 即行顶，与 LineInfos 同源；
                // 找不到行时按行内 80% 处兜底）
                float baseline = r.y + r.h * 0.8f;
                for (const text::LineInfo& li : lineInfos) {
                    if (r.y >= li.top && r.y < li.top + li.height) {
                        baseline = li.top + li.baseline;
                        break;
                    }
                }
                const float bandTop = baseline - hl.fontSize * 0.94f;
                const float bandBottom = baseline + hl.fontSize * 0.20f;
                const Rect box{textOrigin.x + r.x - hl.padding.left,
                               textOrigin.y + bandTop - hl.padding.top,
                               r.w + hl.padding.left + hl.padding.right,
                               (bandBottom - bandTop) + hl.padding.top + hl.padding.bottom};
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

void RichEditor::Impl::EnsureLayout(const StyleContext& ctx, float width) {
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

void RichEditor::Impl::Rebuild() {
    RebuildAt(layoutWidth);
}

void RichEditor::Impl::RebuildAt(float width) {
    laidOut = false;
    linesDirty = true;
    EnsureLayout(lastCtx, width);
    caret = ClampToDoc(caret);
    anchor = ClampToDoc(anchor);
}

// ---------------------------------------------------------------------------
// 图片：src → 纹理（RichEditor.md §3.5 D5）
//
//  File 路径走 WIC 文件解码；`data:image/...;base64,` 走 base64 → 内存解码。
//  结果按 src 缓存（同一张图多次出现只解码一次，纹理进 TextureStore 由
//  App 层每帧同步上传）。失败记空句柄负缓存，绘制退回占位框。
// ---------------------------------------------------------------------------

const graphics::ImageTexture* RichEditor::Impl::ImageOf(const Block& block) const {
    const String& src = block.src;
    if (src.empty()) return nullptr;

    auto [entry, inserted] = images.try_emplace(src);
    if (inserted) {
        graphics::DecodedImage img;
        String err;
        constexpr const wchar_t* kDataUri = L"data:image/";
        constexpr size_t kDataUriLen = 11;
        bool ok = false;
        if (src.compare(0, kDataUriLen, kDataUri) == 0) {
            const size_t sep = src.find(L";base64,");
            if (sep != String::npos) {
                std::vector<uint8_t> bytes;
                DecodeBase64(src.c_str() + sep + 8, src.size() - sep - 8, bytes);
                ok = graphics::DecodeImageMemory(bytes.data(),
                                                 static_cast<uint32_t>(bytes.size()), img, &err);
            } else {
                err = L"data URI 只支持 base64 编码";
            }
        } else {
            ok = graphics::DecodeImageFile(src.c_str(), img, &err);
        }
        if (ok) {
            entry->second = graphics::TextureStore::Instance().Register(
                img.width, img.height, std::move(img.pixels));
        }
    }
    return entry->second.get();
}

// ---------------------------------------------------------------------------
// 图片定位点：光标可停在图片上，Del/Backspace 删除图片
// ---------------------------------------------------------------------------

bool RichEditor::Impl::IsImageAt(size_t index) const {
    return index < editables.size() && editables[index].block->kind == BlockKind::Image;
}

// 删除指定下标的图片块。光标落在图片原来的位置：
//   · 后面还有可编辑块 → 该块开头（图片就在它前面）
//   · 后面没有（图片是最后一个可编辑块）→ 前一个可编辑块的**末尾**
//     不能落前一块开头：那会把光标从图片处拽回上一块行首，多行代码块尤其突兀
//     （示例文档里图片紧跟在代码块之后，删图后光标应停在代码最后一行末）
bool RichEditor::Impl::EraseImageAt(size_t index) {
    Block* image = editables[index].block;
    Spot spot{};
    if (!FindSpot(image, spot)) return false;
    PushUndo(false);
    spot.container->erase(spot.container->begin() + static_cast<ptrdiff_t>(spot.index));
    ++version;
    Rebuild();
    if (index < editables.size()) {
        caret = anchor = ClampToDoc(Cursor{index, 0});
    } else if (index > 0) {
        caret = anchor = EndCursor(index - 1);
    } else {
        caret = anchor = ClampToDoc(Cursor{0, 0});
    }
    caretVisible = true;
    return true;
}

// 文档顺序里 block 之后的第一个块（跨容器 / 跨层级）：没有则 nullptr。
// 用于块边界上的操作（Del 删右邻的分割线等）—— editables 只含可编辑块，
// 分割线这种装饰块不在其中，得按文档树走。
Block* RichEditor::Impl::NextBlockAfter(Block* block) {
    bool found = false;
    return NextBlockRec(doc.blocks, block, found);
}

Block* RichEditor::Impl::NextBlockRec(std::vector<Block>& vec, Block* block, bool& found) {
    for (Block& b : vec) {
        if (found) return &b;                       // 已经越过目标 → 这就是下一个
        if (&b == block) { found = true; continue; }
        if (!b.children.empty()) {
            // 目标可能在自己的子树里：递归找，找到后 found 置位，外层继续往下走
            Block* next = NextBlockRec(b.children, block, found);
            if (next != nullptr) return next;
        }
    }
    return nullptr;
}

// 摘掉一个不可编辑的装饰块（分割线）：它不在 editables 里，光标位置不受影响
bool RichEditor::Impl::EraseDecorBlock(Block* target, Cursor caretAfter) {
    Spot spot{};
    if (!FindSpot(target, spot)) return false;
    PushUndo(false);
    spot.container->erase(spot.container->begin() + static_cast<ptrdiff_t>(spot.index));
    ++version;
    Rebuild();
    caret = anchor = ClampToDoc(caretAfter);
    caretVisible = true;
    return true;
}

// 图后按 Del：光标在图片的右边界，Del 删掉图片**右边**的块
//   · 分割线（不可编辑的装饰块）→ 删掉它，光标留在图后
//   · 图片 → 删掉它（同「段尾 Del 删后邻图片」）
//   · 文本块 → 图片没法与正文合并（会把图片变成文本块），光标移进该块开头
bool RichEditor::Impl::DeleteAfterImage(size_t index) {
    Block* next = NextBlockAfter(editables[index].block);
    if (next == nullptr) return false;

    if (next->kind == BlockKind::Rule) return EraseDecorBlock(next, Cursor{index, 1});

    if (next->kind == BlockKind::Image) {
        const size_t nextIndex = IndexOf(next);
        if (nextIndex == kNoEditable) return false;
        return EraseImageAt(nextIndex);
    }

    // 文本块 / 列表：光标移进右邻第一个可编辑文本块的开头（不吞内容）
    for (size_t i = index + 1; i < editables.size(); ++i) {
        if (IsImageAt(i)) continue;
        caret = anchor = Cursor{i, 0};
        caretVisible = true;
        lastEditTyping = false;
        return true;
    }
    return false;
}

// 光标在图片上而后续操作需要文本块（输入 / 回车 / 粘贴）时，把光标挪到文本块：
// 图后 → 后面最近的文本块开头（没有则退到前面文本块末尾）；
// 图前 → 前面最近的文本块末尾（没有则落到后面文本块开头）。
bool RichEditor::Impl::LeaveImageToText() {
    if (!IsImageAt(caret.block)) return true;
    if (caret.offset > 0) {
        for (size_t i = caret.block + 1; i < editables.size(); ++i) {
            if (!IsImageAt(i)) { caret = anchor = Cursor{i, 0}; return true; }
        }
        for (size_t i = caret.block; i-- > 0;) {
            if (!IsImageAt(i)) { caret = anchor = EndCursor(i); return true; }
        }
        return false;
    }
    for (size_t i = caret.block; i-- > 0;) {
        if (!IsImageAt(i)) { caret = anchor = EndCursor(i); return true; }
    }
    for (size_t i = caret.block + 1; i < editables.size(); ++i) {
        if (!IsImageAt(i)) { caret = anchor = Cursor{i, 0}; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 光标 / 选区
// ---------------------------------------------------------------------------

void RichEditor::Impl::SetCaret(Cursor c, bool extend) {
    caret = ClampToDoc(c);
    if (!extend) anchor = caret;
    caretVisible = true;
    goalX = -1.f;
    lastEditTyping = false;
}

void RichEditor::Impl::MoveHorz(int dir, bool extend) {
    if (editables.empty()) return;
    Cursor c = ClampToDoc(caret);
    const bool image = IsImageAt(c.block);
    const String& text = editables[c.block].item->text;
    if (dir < 0) {
        if (image) {
            // 图片两个光标位：图后 → 图前；图前 → 上一块末尾（上一块是图片则落在图后）
            if (c.offset > 0) {
                c.offset = 0;
            } else {
                if (c.block == 0) return;
                c = EndCursor(c.block - 1);
            }
        } else if (c.offset == 0) {
            if (c.block == 0) return;
            c = EndCursor(c.block - 1);
        } else {
            c.offset = NextIndex(text, c.offset, -1);
        }
    } else {
        if (image) {
            // 图前 → 图后；图后 → 下一块开头
            if (c.offset == 0) {
                c.offset = 1;
            } else {
                if (c.block + 1 >= editables.size()) return;
                c.block += 1;
                c.offset = 0;
            }
        } else if (c.offset >= text.size()) {
            if (c.block + 1 >= editables.size()) return;
            c.block += 1;
            c.offset = 0;
        } else {
            c.offset = NextIndex(text, c.offset, 1);
        }
    }
    SetCaret(c, extend);
}

void RichEditor::Impl::MoveVertical(int dir, bool extend) {
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

void RichEditor::Impl::MoveLineEdge(bool toEnd, bool extend, bool whole) {
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

void RichEditor::Impl::SelectAll() {
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

void RichEditor::Impl::PushUndo(bool typing) {
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

bool RichEditor::Impl::Undo() {
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

bool RichEditor::Impl::Redo() {
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

void RichEditor::Impl::Restore(const Snapshot& s) {
    const float width = layoutWidth;
    Discard(false);           // 文本全变，段落不可复用
    doc = s.doc;
    ++version;
    RebuildAt(width);
    caret = ClampToDoc(s.caret);
    anchor = ClampToDoc(s.anchor);
    caretVisible = true;
}

bool RichEditor::Impl::InsertTyped(const String& typed) {
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
    if (!LeaveImageToText()) return false;

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

bool RichEditor::Impl::DeleteBackward() {
    if (editables.empty()) return false;

    if (HasSelection()) {
        PushUndo(false);
        DeleteSelectionNoUndo();
        return true;
    }

    Cursor c = ClampToDoc(caret);

    // 图片的两个光标位。退格只删光标**左边紧邻**的东西：
    //   · 图后（offset 1）左边紧邻的就是图片 → 删图
    //   · 图前（offset 0）左边紧邻的是前一个块 → 删前块的内容
    if (IsImageAt(c.block)) {
        if (c.offset > 0) return EraseImageAt(c.block);

        // 图前：紧邻的前一块是分割线（不可编辑的装饰块）→ 删掉它
        // （与「图后 Del 删后邻装饰块」对称）
        {
            Spot spot{};
            if (FindSpot(editables[c.block].block, spot) && spot.index > 0) {
                Block* prev = &spot.container->at(spot.index - 1);
                if (prev->kind == BlockKind::Rule) return EraseDecorBlock(prev, c);
            }
        }
        if (c.block == 0) return false;
        // 前一块是图片 → 删掉它
        if (IsImageAt(c.block - 1)) return EraseImageAt(c.block - 1);
        // 前一块是文本 / 代码块 → 删掉它的最后一个字符：光标挪到前一块末尾再退格
        caret = anchor = EndCursor(c.block - 1);
        return DeleteBackward();
    }
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
    if (IsImageAt(c.block - 1)) return EraseImageAt(c.block - 1);   // 段首退格删前邻图片

    // 代码块：块边界不与邻块合并（代码不与正文混排）
    if (editables[c.block].block->kind == BlockKind::Code) return false;
    if (editables[c.block - 1].block->kind == BlockKind::Code) return false;

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

bool RichEditor::Impl::DeleteForward() {
    if (editables.empty()) return false;

    if (HasSelection()) {
        PushUndo(false);
        DeleteSelectionNoUndo();
        return true;
    }

    const Cursor c = ClampToDoc(caret);
    // 图片的两个光标位：图前（offset 0）Del 删掉身后的图片；图后（offset 1）
    // 是图片的**右边界**，Del 删掉图片后面的块（分割线这类装饰块）
    if (IsImageAt(c.block)) {
        if (c.offset == 0) return EraseImageAt(c.block);
        return DeleteAfterImage(c.block);
    }
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

    // 段尾：右邻块（文档顺序）。右邻若是分割线（不可编辑的装饰块）→ 删掉它，
    // 光标留在段尾；它不在 editables 里，只能按文档树找
    Block* current = editables[c.block].block;
    if (Block* next = NextBlockAfter(current)) {
        if (next->kind == BlockKind::Rule) return EraseDecorBlock(next, c);
    }

    if (c.block + 1 >= editables.size()) return false;
    if (IsImageAt(c.block + 1)) return EraseImageAt(c.block + 1);   // 段尾 Del 删后邻图片

    // 代码块：块边界不与邻块合并（代码不与正文混排）
    if (editables[c.block].block->kind == BlockKind::Code) return false;
    if (editables[c.block + 1].block->kind == BlockKind::Code) return false;

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

bool RichEditor::Impl::InsertNewline() {
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
    if (!LeaveImageToText()) return false;

    const Cursor c = ClampToDoc(caret);
    Block* block = editables[c.block].block;

    // 代码块：回车 = 块内换行（代码保留原始换行，不拆块）
    if (block->kind == BlockKind::Code) {
        richtext::InsertText(*block, c.offset, L"\n", RunStyle{});
        ++version;
        Rebuild();
        caret = anchor = ClampToDoc(Cursor{c.block, c.offset + 1});
        caretVisible = true;
        return true;
    }

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

    // 引用块：回车 = 块内换行（在光标处插入 '\n'，quote 是多行块）；只有光标处在
    // **空行**上时才跳出引用 —— 空行合拢后按光标位置分裂，空行处变成普通段落
    // （光标落这里），前后内容仍各为引用；整块为空的引用原地变普通段落。
    if (block->kind == BlockKind::Quote) {
        const String text = richtext::BlockText(*block);
        const size_t off = c.offset;
        const size_t prevNl = (off > 0) ? text.rfind(L'\n', off - 1) : String::npos;
        const size_t lineStart = (prevNl == String::npos) ? 0 : prevNl + 1;
        const size_t nextNl = text.find(L'\n', off);
        const bool emptyLine = (lineStart == off) && (nextNl == off || nextNl == String::npos);

        if (!emptyLine) {
            richtext::InsertText(*block, static_cast<uint32_t>(off), L"\n",
                                 richtext::StyleAt(*block, static_cast<uint32_t>(off)));
            ++version;
            Rebuild();
            const size_t index = IndexOf(block);
            caret = anchor =
                (index == kNoEditable ? ClampToDoc(Cursor{c.block, static_cast<uint32_t>(off) + 1})
                                      : Cursor{index, static_cast<uint32_t>(off) + 1});
            caretVisible = true;
            return true;
        }

        // 空行 → 跳出：合拢空行（删掉一侧的 '\n'），按光标位置分裂
        if (nextNl != String::npos) {
            richtext::DeleteRange(*block, static_cast<uint32_t>(off),
                                  static_cast<uint32_t>(off) + 1);
        } else if (off > 0) {
            richtext::DeleteRange(*block, static_cast<uint32_t>(off) - 1,
                                  static_cast<uint32_t>(off));
        }

        Block tail = richtext::SplitBlock(*block, static_cast<uint32_t>(off));
        const bool headEmpty = richtext::BlockText(*block).empty();
        const bool tailEmpty = richtext::BlockText(tail).empty();

        std::vector<Block>* container = spot.container;
        const size_t at = spot.index;
        Block para;
        para.kind = BlockKind::Paragraph;

        if (headEmpty) {
            // 空行在引用开头：原引用位让给段落（光标），其余内容仍为引用
            block->kind = BlockKind::Paragraph;
            block->style = BlockStyle{};
            if (!tailEmpty) {
                container->insert(container->begin() + static_cast<ptrdiff_t>(at + 1),
                                  std::move(tail));
            }
        } else {
            if (!tailEmpty) {
                container->insert(container->begin() + static_cast<ptrdiff_t>(at + 2),
                                  std::move(tail));
            }
            container->insert(container->begin() + static_cast<ptrdiff_t>(at + 1),
                              std::move(para));
        }
        ++version;
        Rebuild();
        const size_t paraAt = headEmpty ? at : at + 1;
        Block* focus = &(*container)[paraAt];
        const size_t index = IndexOf(focus);
        caret = anchor = (index == kNoEditable ? ClampToDoc(Cursor{c.block, 0}) : Cursor{index, 0});
        caretVisible = true;
        return true;
    }

    // 其余：分裂（列表项 → 同级新项；段落/标题 → 同类型新块）

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

bool RichEditor::Impl::DeleteSelectionNoUndo() {
    if (!HasSelection() || editables.empty()) return false;

    Cursor a = ClampToDoc(caret);
    Cursor b = ClampToDoc(anchor);
    if (BlockLess(b, a)) std::swap(a, b);

    // 选区起点落在图片块上：图片只有零长度定位点，从下一块开始算（起点图片保留）
    if (a.block < b.block && IsImageAt(a.block)) a = Cursor{a.block + 1, 0};

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

    // 4) 相邻两块同容器 → 合并（跨块删除的常见形态）。图片不参与合并：
    //    首块是图片（被选区完整覆盖）→ 直接删除；末块是图片（选区终点在其前）→ 保留
    //    代码块也不参与合并（代码不与正文混排，各块内容各自清空）
    if (!sameBlock && b.block == a.block + 1) {
        Block* first = editables[a.block].block;
        Block* second = editables[b.block].block;
        Spot firstSpot{};
        Spot secondSpot{};
        if (first->kind == BlockKind::Image) {
            if (FindSpot(first, firstSpot)) {
                firstSpot.container->erase(firstSpot.container->begin() +
                                           static_cast<ptrdiff_t>(firstSpot.index));
            }
        } else if (second->kind != BlockKind::Image &&
                   first->kind != BlockKind::Code && second->kind != BlockKind::Code &&
                   FindSpot(first, firstSpot) && FindSpot(second, secondSpot) &&
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

String RichEditor::Impl::SelectedText() const {
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

void RichEditor::Impl::CopySelection() const {
    if (!HasSelection()) return;
    Clipboard::WriteText(SelectedText());
}

bool RichEditor::Impl::CutSelection() {
    if (!HasSelection()) return false;
    Clipboard::WriteText(SelectedText());
    PushUndo(false);
    DeleteSelectionNoUndo();
    return true;
}

bool RichEditor::Impl::PasteClipboard() {
    String raw;
    if (!Clipboard::ReadText(raw) || raw.empty()) return false;

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
    if (!LeaveImageToText()) return false;

    const Cursor c = ClampToDoc(caret);

    // 代码块：整段粘贴（保留换行），不拆成段落
    if (editables[c.block].block->kind == BlockKind::Code) {
        richtext::InsertText(*editables[c.block].block, c.offset, text, RunStyle{});
        ++version;
        Rebuild();
        caret = anchor = ClampToDoc(Cursor{c.block, c.offset + static_cast<uint32_t>(text.size())});
        caretVisible = true;
        return true;
    }

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

bool RichEditor::Impl::FindSpotRec(std::vector<Block>& vec, Block* target, Block* ownerList,
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

bool RichEditor::Impl::FindSpot(Block* target, Spot& out) {
    out = Spot{};
    return target != nullptr && FindSpotRec(doc.blocks, target, nullptr, out);
}

// ---------------------------------------------------------------------------
// 按键分派
// ---------------------------------------------------------------------------

RichEditor::Impl::KeyResult RichEditor::Impl::HandleKey(int key, bool shift, bool ctrl) {
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
// RichEditor
// ---------------------------------------------------------------------------

RichEditor::RichEditor() : impl_(std::make_unique<Impl>()) {
    SetFocusable(true);
}

RichEditor::~RichEditor() {
    if (impl_) impl_->Discard(false);
}

void RichEditor::SetDoc(const richtext::RichDoc& doc) {
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

const richtext::RichDoc& RichEditor::Doc() const noexcept {
    return impl_->doc;
}

float RichEditor::ContentHeight() const noexcept {
    return impl_->contentSize.y;
}

void RichEditor::SetEditable(bool on) {
    editable_ = on;
    SetFocusable(on);
    if (!on) {
        impl_->dragging = false;
        impl_->caret = Impl::Cursor{};
        impl_->anchor = Impl::Cursor{};
    }
    MarkDirty(kFlagPaint);
}

bool RichEditor::HasSelection() const noexcept {
    return impl_->HasSelection();
}

Vec2 RichEditor::ContentOrigin() const {
    const Thickness pad = ValueAsThickness(ResolveProperty(this, Prop::Padding));
    const Rect box = Bounds();
    return Vec2{box.x + pad.left, box.y + pad.top};
}

Vec2 RichEditor::MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const {
    float width = contentAvail.x;
    if (width <= 0.f) {
        // 无宽度约束（Row 里的自适应）：沿用上次排好的宽度，其次用实际盒宽
        width = impl_->laidOut && impl_->layoutWidth > 0.f ? impl_->layoutWidth
                                                          : std::max(0.f, Bounds().w);
    }
    impl_->EnsureLayout(ctx, width);
    return impl_->contentSize;
}

void RichEditor::OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) {
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

bool RichEditor::ContentClipRect(Rect& out) const {
    if (!impl_->overflowX) return false;   // 常规情况不加裁剪单元（省一次绘制切换）
    const Thickness pad = ValueAsThickness(ResolveProperty(this, Prop::Padding));
    out = Rect{ContentOrigin().x, ContentOrigin().y,
               std::max(0.f, Bounds().w - pad.left - pad.right),
               std::max(0.f, Bounds().h - pad.top - pad.bottom)};
    return true;
}

void RichEditor::OnFocusChanged(bool focused) {
    (void)focused;
    impl_->caretVisible = true;
    if (!focused) impl_->dragging = false;
    MarkDirty(kFlagPaint);
}

bool RichEditor::OnBlink() {
    if (!HasFocus()) {
        impl_->caretVisible = true;
        return false;
    }
    impl_->caretVisible = !impl_->caretVisible;
    MarkDirty(kFlagPaint);
    return true;
}

bool RichEditor::InsertionPoint(Rect& outRect) const {
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

bool RichEditor::HandlePointerDown(PointerEvent& ev) {
    if (!editable_) return false;
    Focus();
    const Vec2 origin = ContentOrigin();
    impl_->dragging = true;
    impl_->SetCaret(impl_->HitTestDoc(Vec2{ev.pos.x - origin.x, ev.pos.y - origin.y}), false);
    SetCapture();
    MarkDirty(kFlagPaint);
    return true;
}

bool RichEditor::HandlePointerMove(PointerEvent& ev) {
    if (!editable_ || !impl_->dragging) return false;
    const Vec2 origin = ContentOrigin();
    impl_->SetCaret(impl_->HitTestDoc(Vec2{ev.pos.x - origin.x, ev.pos.y - origin.y}), true);
    MarkDirty(kFlagPaint);
    return true;
}

bool RichEditor::HandlePointerUp(PointerEvent& ev) {
    (void)ev;
    if (!impl_->dragging) return false;
    impl_->dragging = false;
    ReleaseCapture();
    return false;
}

bool RichEditor::HandleKeyDown(KeyEvent& ev) {
    if (!editable_) return false;

    const Impl::KeyResult result = impl_->HandleKey(ev.key, ev.shift, ev.ctrl);
    if (result == Impl::KeyResult::None) return false;

    const bool changed = (result == Impl::KeyResult::Content);
    MarkDirty(changed ? (kFlagLayout | kFlagPaint) : kFlagPaint);
    if (changed) RevealCaret();

    ev.handled = true;
    return true;
}

bool RichEditor::HandleChar(KeyEvent& ev) {
    if (!editable_ || ev.ctrl || ev.alt) return false;

    const wchar_t ch = ev.ch;
    if (ch == 0 || ch == L'\t' || ch < 0x20 || ch == 0x7F) return false;

    impl_->InsertTyped(String(1, ch));
    MarkDirty(kFlagLayout | kFlagPaint);
    RevealCaret();

    ev.handled = true;
    return true;
}

void RichEditor::RevealCaret() {
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
