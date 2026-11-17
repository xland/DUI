// ============================================================================
//  DUI/UI/Text.cpp — Text 的实现（DWrite 排版测量与段落释放）
//
//  M5.63 追加：文本选择（user-select）—— 拖选 / 双击选词 / 三击选段 /
//  Ctrl+C 复制。选区几何全部走引擎（HitTestPoint / SelectionRects /
//  LineInfos），绘制端在 App/Frame.cpp 叠选区底色。
// ============================================================================

#include "UI/Text.h"

#include <algorithm>
#include <cmath>

#include "Core/Clipboard.h"
#include "Graphics/TextEngine.h"
#include "UI/UI.h"

namespace dui {

namespace {

// 连击判定（双击选词 / 三击选段）：窗口类没开 CS_DBLCLKS，收不到
// WM_LBUTTONDBLCLK，故按"时间 + 位置"自行判定（阈值取 Windows 默认双击时间）。
constexpr uint64_t kDoubleClickMs = 500;
constexpr float kClickSlopDip = 4.f;    // 连击的位置容差
constexpr float kDragSlopDip = 4.f;     // 拖动判定容差（与框架的 Click 合成一致）

// 字符类别（分词用）：CJK 逐字成词（浏览器对中文的行为），
// ASCII 字母 / 数字连成一个词，其余按同类段成词。
enum class CharClass : uint8_t { Space, Word, Cjk, Surrogate, Punct };

CharClass ClassOf(wchar_t c) {
    if (c >= 0x4E00 && c <= 0x9FFF) return CharClass::Cjk;   // CJK 统一表意
    if (c >= 0x3400 && c <= 0x4DBF) return CharClass::Cjk;   // 扩展 A
    if (c >= 0x3040 && c <= 0x30FF) return CharClass::Cjk;   // 日文假名 / 注音
    if (c >= 0xAC00 && c <= 0xD7AF) return CharClass::Cjk;   // 韩文音节
    if (c >= 0xD800 && c <= 0xDFFF) return CharClass::Surrogate;
    if (c < 0x80) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_')
            return CharClass::Word;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return CharClass::Space;
        return CharClass::Punct;
    }
    return CharClass::Punct;
}

bool IsHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(wchar_t c)  { return c >= 0xDC00 && c <= 0xDFFF; }

// 索引吸附到码点边界（代理对的后半段不算独立位置）
uint32_t SnapToCodePoint(const String& s, uint32_t i) {
    if (i > 0 && i < s.size() && IsLowSurrogate(s[i]) && IsHighSurrogate(s[i - 1])) return i - 1;
    return i;
}

// 按字符移动（跳过代理对：一次一个码点）
uint32_t NextIndex(const String& s, uint32_t i, int dir) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    if (dir < 0) {
        if (i == 0) return 0;
        const uint32_t j = i - 1;
        if (j > 0 && IsLowSurrogate(s[j]) && IsHighSurrogate(s[j - 1])) return j - 1;
        return j;
    }
    if (i >= n) return n;
    const uint32_t j = i + 1;
    if (j < n && IsHighSurrogate(s[i]) && IsLowSurrogate(s[j])) return j + 1;
    return j;
}

// index 处字符所属的同类段 [a, b)：CJK 逐字、ASCII 词整体、代理对算一个码点
void SameClassSpan(const String& s, uint32_t index, uint32_t& a, uint32_t& b) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    const CharClass c = ClassOf(s[index]);
    if (c == CharClass::Surrogate) {
        // 代理对：整体算一个码点
        if (index + 1 < n && IsHighSurrogate(s[index]) && IsLowSurrogate(s[index + 1])) {
            a = index;
            b = index + 2;
        } else if (index > 0 && IsLowSurrogate(s[index]) && IsHighSurrogate(s[index - 1])) {
            a = index - 1;
            b = index + 1;
        } else {
            a = index;
            b = index + 1;
        }
        return;
    }
    a = index;
    b = index + 1;
    while (a > 0 && ClassOf(s[a - 1]) == c) --a;
    while (b < n && ClassOf(s[b]) == c) ++b;
}

// 词边界（Ctrl+← / Ctrl+→）：向左取所在词的开头，向右取所在词的末尾
uint32_t WordEdge(const String& s, uint32_t i, int dir) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    if (dir < 0) {
        const uint32_t j = std::min(i, n);
        if (j == 0) return 0;
        uint32_t a = 0, b = 0;
        SameClassSpan(s, j - 1, a, b);
        return a;
    }
    uint32_t j = std::min(i, n);
    if (j >= n) return n;
    if (ClassOf(s[j]) == CharClass::Space) {
        while (j < n && ClassOf(s[j]) == CharClass::Space) ++j;
        if (j >= n) return n;
    }
    uint32_t a = 0, b = 0;
    SameClassSpan(s, j, a, b);
    return b;
}

}  // namespace

Vec2 Text::MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const {
    text::Engine& en = text::Engine::Get();
    if (!en.Ready()) {
        // DWrite 不可用：M3 占位估算兜底（CJK 1em、其余 0.55em）
        float units = 0.f;
        for (const wchar_t ch : text_) units += (ch >= 0x2E80) ? 1.0f : 0.55f;
        singleLineHeight_ = ctx.fontSize * ctx.lineHeight;
        inkValid_ = false;
        return {units * ctx.fontSize, ctx.fontSize * ctx.lineHeight};
    }
    const float maxW = contentAvail.x > 0.f ? contentAvail.x : 1e6f;   // 不限宽
    const bool ellipsis =
        ctx.textOverflow == static_cast<uint32_t>(dui::TextOverflow::Ellipsis);

    // 换行宽：显式 Width 优先（元素盒宽 − 内边距 − border-box 的边框），
    // 否则用父给的内容可用宽。与 TextBox 多行的既有做法一致（TextBox.cpp:488）。
    // 少了这一步，设了 Width 的 Text 仍按父可用宽排版 → 单行不折行 → 溢出元素盒。
    float wrapW = maxW;
    StyleValue wv;
    if (TryResolve(this, Prop::Width, ctx, wv)) {
        const Thickness pad = ValueAsThickness(ResolveProperty(this, Prop::Padding, ctx));
        float bw = 0.f;
        if (ResolveProperty(this, Prop::BoxSizing, ctx).u ==
            static_cast<uint32_t>(dui::BoxSizing::BorderBox)) {
            const Thickness bwv = ValueAsThickness(ResolveProperty(this, Prop::BorderWidth, ctx));
            bw = bwv.left + bwv.right;
        }
        const float own = wv.f - pad.left - pad.right - bw;
        // 父可用宽更小时以它为准（近似 flex-shrink 后的盒宽，避免超出父容器）
        if (own > 0.f && own < wrapW) wrapW = own;
    }

    text::ShapeInput in;
    in.text = text_.c_str();
    in.fontSize = ctx.fontSize;
    in.weight = dui::DWriteWeight(ResolveProperty(this, Prop::FontWeight, ctx).u);
    in.lineHeight = ctx.lineHeight;
    in.maxWidth = wrapW;
    in.family = ctx.family;
    in.letterSpacing = ctx.letterSpacing;   // 字距（继承属性）
    in.wordSpacing = ctx.wordSpacing;       // 词距（继承属性）
    // 斜体（继承属性，走 ctx）/ 文本装饰（不继承，走元素自身链）
    in.italic = ResolveProperty(this, Prop::Italic, ctx).u != 0;
    const uint32_t deco = ResolveProperty(this, Prop::TextDecoration, ctx).u;
    in.underline = dui::HasDecoration(deco, dui::TextDecoration::Underline);
    in.strike = dui::HasDecoration(deco, dui::TextDecoration::LineThrough);

    if (!ellipsis) {
        truncated_ = false;
        fullPara_ = en.Shape(static_cast<text::Paragraph*>(fullPara_), in);
        const text::Paragraph* para = static_cast<text::Paragraph*>(fullPara_);
        baseline_ = para != nullptr ? para->baseline : -1.f;
        singleLineHeight_ = para != nullptr ? para->firstLineHeight : 0.f;
        inkCenter_ = para != nullptr ? para->inkCenter : 0.f;
        inkValid_ = para != nullptr ? para->inkValid : false;
        return para != nullptr ? para->size : Vec2{};
    }

    // ---- text-overflow: ellipsis（单行；M5.56）--------------------------------
    // ① 单行排版（不折行）→ 得全文自然宽。省略号语义要求"一行放不下"，
    //    若照常折行就会变成多行，永远走不到省略号分支。
    in.noWrap = true;
    fullPara_ = en.Shape(static_cast<text::Paragraph*>(fullPara_), in);
    const text::Paragraph* full = static_cast<text::Paragraph*>(fullPara_);
    if (full == nullptr) { truncated_ = false; singleLineHeight_ = 0.f; inkValid_ = false; return Vec2{}; }
    baseline_ = full->baseline;
    singleLineHeight_ = full->firstLineHeight;
    inkCenter_ = full->inkCenter;
    inkValid_ = full->inkValid;
    if (full->size.x <= wrapW) { truncated_ = false; return full->size; }   // 放得下

    // ② 省略号宽度 + "能放下的最长前缀"（簇边界安全）→ 截断文本再排一次
    const float ellipsisW =
        en.MeasureEllipsis(static_cast<text::Paragraph*>(ellipsisPara_), in);
    const uint32_t fit = en.FitPrefixChars(full, wrapW - ellipsisW);
    String clipped = text_.substr(0, fit);
    clipped += L"…";
    in.text = clipped.c_str();
    truncPara_ = en.Shape(static_cast<text::Paragraph*>(truncPara_), in);
    truncated_ = true;
    const text::Paragraph* tp = static_cast<text::Paragraph*>(truncPara_);
    singleLineHeight_ = tp != nullptr ? tp->firstLineHeight : full->firstLineHeight;
    inkCenter_ = tp != nullptr ? tp->inkCenter : full->inkCenter;
    inkValid_ = tp != nullptr ? tp->inkValid : full->inkValid;
    return tp != nullptr ? tp->size : full->size;
}

Text::~Text() {
    text::Engine& en = text::Engine::Get();
    en.Discard(static_cast<text::Paragraph*>(fullPara_));
    en.Discard(static_cast<text::Paragraph*>(truncPara_));
    en.Discard(static_cast<text::Paragraph*>(ellipsisPara_));
}

// ---------------------------------------------------------------------------
// 文本选择（M5.63）
// ---------------------------------------------------------------------------

bool Text::Selectable() const {
    return ResolveProperty(this, Prop::UserSelect).u != 0;
}

const String& Text::CurrentText() const {
    auto* para = static_cast<text::Paragraph*>(ParaHandle());
    if (para != nullptr) return para->text;
    return text_;
}

uint32_t Text::TextLength() const {
    return static_cast<uint32_t>(CurrentText().size());
}

Vec2 Text::TextOriginDip() const {
    return TextOriginWith(ResolveProperty(this, Prop::TextAlign).u);
}

Vec2 Text::TextOriginDip(const StyleContext& ctx) const {
    return TextOriginWith(ResolveProperty(this, Prop::TextAlign, ctx).u);
}

Vec2 Text::TextOriginWith(uint32_t textAlign) const {
    const Rect content = ContentRectOf(this);
    auto* para = static_cast<text::Paragraph*>(ParaHandle());
    const float textW = para != nullptr ? para->size.x : 0.f;
    // textAlign（15-Properties §4.1）：段落按自然宽左排，绘制期在内容盒剩余宽度内
    // 整体平移 —— 与 App/Frame.cpp 的 DrawTextContent 同一口径（共用本函数）。
    float dx = 0.f;
    if (textAlign == static_cast<uint32_t>(dui::TextAlign::Center)) dx = (content.w - textW) * 0.5f;
    else if (textAlign == static_cast<uint32_t>(dui::TextAlign::End)) dx = content.w - textW;
    return Vec2{content.x + dx, content.y};
}

uint32_t Text::HitTestIndex(const Vec2& ptAbs) const {
    auto* para = static_cast<text::Paragraph*>(ParaHandle());
    if (para == nullptr) return 0;
    const Vec2 o = TextOriginDip();
    return text::Engine::Get().HitTestPoint(para, Vec2{ptAbs.x - o.x, ptAbs.y - o.y});
}

String Text::SelectedText() const {
    const uint32_t s = SelectionStart();
    const uint32_t e = SelectionEnd();
    if (e <= s) return String();
    const String& src = CurrentText();
    if (e > src.size()) return String();
    return src.substr(s, e - s);
}

void Text::SelectAll() {
    selAnchor_ = 0;
    selCaret_ = TextLength();
    MarkDirty(kFlagPaint);
}

void Text::Deselect() {
    ClearSelectionState();
    MarkDirty(kFlagPaint);
}

void Text::ClearSelectionState() noexcept {
    selAnchor_ = 0;
    selCaret_ = 0;
    selecting_ = false;
    clickCount_ = 0;
}

void Text::SelectWordAt(uint32_t index) {
    const String& s = CurrentText();
    const uint32_t n = static_cast<uint32_t>(s.size());
    if (n == 0) return;
    index = SnapToCodePoint(s, std::min(index, n - 1));
    uint32_t a = 0, b = 0;
    SameClassSpan(s, index, a, b);
    selAnchor_ = a;
    selCaret_ = b;
}

void Text::SelectLineAt(uint32_t index) {
    auto* para = static_cast<text::Paragraph*>(ParaHandle());
    if (para == nullptr) return;
    std::vector<text::LineInfo> lines;
    text::Engine::Get().LineInfos(para, lines);
    for (const text::LineInfo& li : lines) {
        if (index >= li.start && index <= li.end) {
            selAnchor_ = li.start;
            selCaret_ = li.end;
            return;
        }
    }
    // 没落到任何行（度量异常）→ 退回全选
    selAnchor_ = 0;
    selCaret_ = TextLength();
}

void Text::MoveCaret(int dir, bool extend, bool byWord) {
    const String& s = CurrentText();
    // 文本变短后旧索引可能越界 → 先夹到长度内再算下一步
    uint32_t caret = std::min(selCaret_, static_cast<uint32_t>(s.size()));
    caret = byWord ? WordEdge(s, caret, dir) : NextIndex(s, caret, dir);
    selCaret_ = caret;
    if (!extend) selAnchor_ = caret;   // 不按 Shift → 选区收缩到光标
    MarkDirty(kFlagPaint);
}

void Text::MoveToLineEdge(bool toEnd, bool extend) {
    auto* para = static_cast<text::Paragraph*>(ParaHandle());
    if (para == nullptr) return;
    std::vector<text::LineInfo> lines;
    text::Engine::Get().LineInfos(para, lines);
    uint32_t target = std::min(selCaret_, TextLength());
    for (const text::LineInfo& li : lines) {
        if (target >= li.start && target <= li.end) {
            target = toEnd ? li.end : li.start;
            break;
        }
    }
    selCaret_ = target;
    if (!extend) selAnchor_ = target;
    MarkDirty(kFlagPaint);
}

void Text::CopySelection() const {
    if (!HasSelection()) return;
    const String s = SelectedText();
    if (s.empty()) return;
    Clipboard::WriteText(s);
}

void Text::OnFocusChanged(bool focused) {
    if (focused) return;
    // 失焦清选区（浏览器行为：选区随焦点消失）
    if (HasSelection() || selecting_) {
        ClearSelectionState();
        MarkDirty(kFlagPaint);
    }
}

bool Text::HandlePointerDown(PointerEvent& ev) {
    if (!Selectable()) return false;
    if (ev.button != PointerButton::Left) return false;

    const uint32_t idx = HitTestIndex(ev.pos);

    // 连击计数（双击选词 / 三击选段）：窗口类没开 CS_DBLCLKS → 收不到
    // WM_LBUTTONDBLCLK，故按"时间 + 位置"自行判定。
    const uint64_t now = GetTickCount64();
    const bool isRepeat = lastClickMs_ != 0 && (now - lastClickMs_) <= kDoubleClickMs &&
                          std::abs(ev.pos.x - lastClickPos_.x) <= kClickSlopDip &&
                          std::abs(ev.pos.y - lastClickPos_.y) <= kClickSlopDip;
    clickCount_ = isRepeat ? clickCount_ + 1 : 1;
    lastClickMs_ = now;
    lastClickPos_ = ev.pos;
    pressPos_ = ev.pos;

    if (clickCount_ >= 3)      SelectLineAt(idx);   // 三击：选段（整行）
    else if (clickCount_ == 2) SelectWordAt(idx);   // 双击：选词
    else { selAnchor_ = selCaret_ = idx; }          // 单击：落光标，清选区

    selecting_ = true;
    SetCapture();   // 拖出文本框仍能继续扩选（捕获期间 Move/Up 直达本元素）
    MarkDirty(kFlagPaint);
    return false;   // 不消费：事件继续冒泡给祖先
}

bool Text::HandlePointerMove(PointerEvent& ev) {
    if (!selecting_) return false;
    if (!HasState(State::Pressed)) {   // 按钮已抬起（未落在本元素上时收不到 Up）
        selecting_ = false;
        return false;
    }
    const uint32_t idx = HitTestIndex(ev.pos);
    if (idx != selCaret_) {
        selCaret_ = idx;
        MarkDirty(kFlagPaint);
    }
    return false;
}

bool Text::HandlePointerUp(PointerEvent& ev) {
    if (!selecting_) return false;
    selecting_ = false;
    ReleaseCapture();
    // 捕获期间框架不合成 Click（App/App.cpp：抬起自动释放捕获时跳过 Click 合成），
    // 未真正拖动时补一次点击，保持"点文本 = 点元素"的原有语义。
    if (std::abs(ev.pos.x - pressPos_.x) <= kDragSlopDip &&
        std::abs(ev.pos.y - pressPos_.y) <= kDragSlopDip) {
        DispatchClick(this, ev.pos, PointerButton::Left);
    }
    return false;
}

bool Text::HandleKeyDown(KeyEvent& ev) {
    if (!Selectable()) return false;
    const bool ctrl = ev.ctrl;
    const bool shift = ev.shift;
    switch (ev.key) {
    case 'C':
        if (!ctrl) return false;
        CopySelection();
        break;
    case 'A':
        if (!ctrl) return false;
        SelectAll();
        break;
    case VK_LEFT:
        MoveCaret(-1, shift, ctrl);
        break;
    case VK_RIGHT:
        MoveCaret(+1, shift, ctrl);
        break;
    case VK_HOME:
        MoveToLineEdge(false, shift);
        break;
    case VK_END:
        MoveToLineEdge(true, shift);
        break;
    default:
        return false;   // 未消费 → 继续冒泡（Tab 等由框架处理）
    }
    ev.handled = true;
    return true;
}

}  // namespace dui
