// ============================================================================
//  DUI/UI/TextBox.cpp — 文本编辑控件的实现
//
//  编辑核心：文本 / 光标 / 选区 / 撤销栈 / 剪贴板；渲染：选区 → 文本 → 光标。
//  排版走 text::Engine（段落缓存 + 命中测试几何）；遮罩用 ●（U+25CF）。
// ============================================================================

#include "UI/UI.h"

#include <algorithm>
#include <cmath>

#include "Graphics/Graphics.h"
#include "Text/TextEngine.h"

namespace dui {

namespace {

// 剪贴板服务（平台实现由 App 层注入；UI 层不依赖 Platform，01-Layering §1）
ClipboardReadFn  g_clipRead = nullptr;
ClipboardWriteFn g_clipWrite = nullptr;

constexpr size_t kMaxUndo = 100;
constexpr float kMinBoxWidth = 120.f;      // 单行输入框最小自然宽（DIP）
constexpr float kMinAreaWidth = 200.f;     // 多行输入框最小自然宽（DIP）
constexpr float kMinAreaHeight = 80.f;     // 多行输入框最小自然高（DIP）

text::Paragraph* ParaOf(void* p) { return static_cast<text::Paragraph*>(p); }

bool IsHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// 按字符移动（跳过代理对：增删一个码点而非半个）
uint32_t NextIndex(const String& s, uint32_t i, int dir) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    if (dir < 0) {
        if (i == 0 || n == 0) return 0;
        const uint32_t j = i - 1;
        if (j > 0 && IsLowSurrogate(s[j]) && IsHighSurrogate(s[j - 1])) return j - 1;
        return j;
    }
    if (i >= n) return n;
    const uint32_t j = i + 1;
    if (j < n && IsHighSurrogate(s[i]) && IsLowSurrogate(s[j])) return j + 1;
    return j;
}

// 换行归一：\r\n / \r → \n
String NormalizeNewlines(const String& s) {
    String out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r') {
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;
            out.push_back(L'\n');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// 光标所在行：行首 <= index 的最后一行
int LineOf(const std::vector<text::LineInfo>& lines, uint32_t index) {
    int result = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].start <= index) result = static_cast<int>(i);
        else break;
    }
    return result;
}

}  // namespace

void SetClipboardService(ClipboardReadFn read, ClipboardWriteFn write) {
    g_clipRead = read;
    g_clipWrite = write;
}

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

TextBox::TextBox(bool password) : password_(password) {
    SetFocusable(true);   // Tab 链成员；点击取得焦点（App 层）
}

TextBox::~TextBox() {
    text::Engine::Get().Discard(ParaOf(para_));
}

// ---------------------------------------------------------------------------
// 内容
// ---------------------------------------------------------------------------

void TextBox::SetText(const String& s) {
    text_ = NormalizeNewlines(s);
    RebuildDisplay();
    const uint32_t n = static_cast<uint32_t>(text_.size());
    caret_ = std::min(caret_, n);
    anchor_ = std::min(anchor_, n);
    undo_.clear();
    redo_.clear();
    NotifyChanged();
}

void TextBox::SetText(const wchar_t* s) { SetText(String(s != nullptr ? s : L"")); }

void TextBox::Clear() { SetText(String()); }

void TextBox::SetPassword(bool on) {
    if (on == password_) return;
    password_ = on;
    RebuildDisplay();
    MarkDirty(kFlagLayout | kFlagPaint);
}

void TextBox::SetMaskChar(wchar_t ch) {
    if (ch == maskChar_) return;
    maskChar_ = ch;
    RebuildDisplay();
    MarkDirty(kFlagLayout | kFlagPaint);
}

void TextBox::RebuildDisplay() {
    if (!password_) {
        display_ = text_;
        return;
    }
    display_.assign(text_.size(), maskChar_);
}

// 掩码文本的垂直居中补偿（排版就绪后调用）。
//
// bullet 类掩码字符（'•'/'●'）的字形墨迹紧贴基线（圆心几乎与基线齐平），
// 按基线对齐绘制会明显偏下：圆点不在行高中心，与贯穿整行的光标条错位。
// 做法：量出掩码字形的墨迹中心（相对基线），补偿到**行高中心** —— 与光标条
// （行顶→行底）的中部一致，也与普通输入框里文字的视觉位置一致。
void TextBox::UpdateMaskOffset() const {
    maskDy_ = 0.f;
    if (!password_) return;
    auto* para = ParaOf(para_);
    if (para == nullptr || para->layout == nullptr) return;
    // 缓存：补偿只取决于字体度量与行几何 → 段落 layout / 字号 / 掩码字符不变则跳过
    // （否则每帧都要重跑一次字形测量）
    if (maskLayout_ == para->layout && maskFontSize_ == para->fontSize &&
        maskCharUsed_ == maskChar_) {
        maskDy_ = maskDyCached_;
        return;
    }
    maskDyCached_ = 0.f;
    std::vector<text::LineInfo> lines;
    text::Engine::Get().LineInfos(para, lines);
    if (!lines.empty()) {
        // 目标：行高中心（相对基线，向上为正）
        const float target = lines[0].baseline - lines[0].height * 0.5f;
        float ink = 0.f;
        if (text::Engine::Get().GlyphInkCenterDip(para, maskChar_, ink)) {
            // 圆心相对基线 = ink；整段下移 dy 后 = ink - dy → 令其等于 target
            maskDyCached_ = ink - target;
        }
    }
    maskDy_ = maskDyCached_;
    maskLayout_ = para->layout;
    maskFontSize_ = para->fontSize;
    maskCharUsed_ = maskChar_;
}

void TextBox::NotifyChanged() {
    ResetBlinkPhase();     // 输入后光标立即可见
    MarkDirty(kFlagLayout | kFlagPaint);
    if (onTextChanged_) onTextChanged_(text_);
}

// ---------------------------------------------------------------------------
// 光标与选区
// ---------------------------------------------------------------------------

void TextBox::SetCaret(uint32_t index) {
    const uint32_t n = static_cast<uint32_t>(text_.size());
    caret_ = anchor_ = std::min(index, n);
    goalX_ = -1.f;
    ResetBlinkPhase();
    EnsureCaretVisible();
    MarkDirty(kFlagPaint);
}

String TextBox::SelectedText() const {
    const uint32_t s = SelectionStart();
    const uint32_t e = SelectionEnd();
    if (e <= s) return String();
    return text_.substr(s, e - s);
}

void TextBox::SelectAll() {
    anchor_ = 0;
    caret_ = static_cast<uint32_t>(text_.size());
    MarkDirty(kFlagPaint);
}

void TextBox::Deselect() {
    anchor_ = caret_;
    MarkDirty(kFlagPaint);
}

void TextBox::MoveTo(uint32_t index, bool extend) {
    const uint32_t n = static_cast<uint32_t>(text_.size());
    caret_ = std::min(index, n);
    if (!extend) anchor_ = caret_;
    ResetBlinkPhase();     // 移动光标后立即可见
    MarkDirty(kFlagPaint);
    EnsureCaretVisible();
}

void TextBox::MoveCaret(int delta, bool extend) {
    const uint32_t s = SelectionStart();
    const uint32_t e = SelectionEnd();
    uint32_t target;
    if (extend) {
        target = NextIndex(text_, caret_, delta);
    } else if (s != e) {
        target = (delta < 0) ? s : e;      // 有选区：先跳到选区端点
    } else {
        target = NextIndex(text_, caret_, delta);
    }
    goalX_ = -1.f;
    MoveTo(target, extend);
}

void TextBox::MoveCaretLine(int delta, bool extend) {
    auto* para = ParaOf(para_);
    if (para == nullptr) return;
    std::vector<text::LineInfo> lines;
    text::Engine::Get().LineInfos(para, lines);
    if (lines.empty()) return;
    const int line = LineOf(lines, caret_);
    const int target = line + delta;
    if (target < 0 || target >= static_cast<int>(lines.size())) {
        MoveTo(delta < 0 ? 0u : static_cast<uint32_t>(text_.size()), extend);
        return;
    }
    // 目标列：沿用记录的目标列，否则取当前光标 x
    float x = goalX_;
    if (x < 0.f) {
        Rect cr;
        x = text::Engine::Get().CaretRect(para, caret_, cr) ? cr.x : 0.f;
    }
    goalX_ = x;
    const text::LineInfo& li = lines[static_cast<size_t>(target)];
    const uint32_t idx = text::Engine::Get().HitTestPoint(para, Vec2{x, li.top + li.height * 0.5f});
    MoveTo(idx, extend);
}

void TextBox::MoveToLineEdge(bool toEnd, bool extend, bool whole) {
    if (whole) {
        MoveTo(toEnd ? static_cast<uint32_t>(text_.size()) : 0u, extend);
        goalX_ = -1.f;
        return;
    }
    auto* para = ParaOf(para_);
    if (para == nullptr) return;
    std::vector<text::LineInfo> lines;
    text::Engine::Get().LineInfos(para, lines);
    if (lines.empty()) return;
    const text::LineInfo& li = lines[static_cast<size_t>(LineOf(lines, caret_))];
    MoveTo(toEnd ? li.end : li.start, extend);
    goalX_ = -1.f;
}

// ---------------------------------------------------------------------------
// 编辑操作
// ---------------------------------------------------------------------------

void TextBox::PushUndo() {
    Snapshot s;
    s.text = text_;
    s.caret = caret_;
    s.anchor = anchor_;
    undo_.push_back(std::move(s));
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
}

void TextBox::InsertChar(wchar_t ch) {
    PushUndo();
    const uint32_t s = SelectionStart();
    const uint32_t e = SelectionEnd();
    if (e > s) text_.erase(s, e - s);
    text_.insert(s, 1, ch);
    caret_ = anchor_ = s + 1;
    goalX_ = -1.f;
    RebuildDisplay();
    NotifyChanged();
    EnsureCaretVisible();
}

void TextBox::DeleteBackward() {
    const uint32_t s = SelectionStart();
    const uint32_t e = SelectionEnd();
    if (e > s) {
        PushUndo();
        text_.erase(s, e - s);
        caret_ = anchor_ = s;
    } else {
        if (s == 0) return;
        PushUndo();
        uint32_t del = 1;
        if (s >= 2 && IsLowSurrogate(text_[s - 1]) && IsHighSurrogate(text_[s - 2])) del = 2;
        text_.erase(s - del, del);
        caret_ = anchor_ = s - del;
    }
    goalX_ = -1.f;
    RebuildDisplay();
    NotifyChanged();
    EnsureCaretVisible();
}

void TextBox::DeleteForward() {
    const uint32_t s = SelectionStart();
    const uint32_t e = SelectionEnd();
    const uint32_t n = static_cast<uint32_t>(text_.size());
    if (e > s) {
        PushUndo();
        text_.erase(s, e - s);
        caret_ = anchor_ = s;
    } else {
        if (s >= n) return;
        PushUndo();
        uint32_t del = 1;
        if (s + 1 < n && IsHighSurrogate(text_[s]) && IsLowSurrogate(text_[s + 1])) del = 2;
        text_.erase(s, del);
        caret_ = anchor_ = s;
    }
    goalX_ = -1.f;
    RebuildDisplay();
    NotifyChanged();
}

void TextBox::Copy() const {
    if (!HasSelection()) return;
    const String s = SelectedText();
    if (s.empty() || g_clipWrite == nullptr) return;
    g_clipWrite(s);
}

void TextBox::Cut() {
    if (!HasSelection()) return;
    PushUndo();
    const uint32_t s = SelectionStart();
    const uint32_t e = SelectionEnd();
    const String sel = text_.substr(s, e - s);
    if (g_clipWrite != nullptr) g_clipWrite(sel);
    text_.erase(s, e - s);
    caret_ = anchor_ = s;
    goalX_ = -1.f;
    RebuildDisplay();
    NotifyChanged();
    EnsureCaretVisible();
}

void TextBox::Paste() {
    String clip;
    if (g_clipRead == nullptr || !g_clipRead(clip)) return;
    if (clip.empty()) return;
    if (multiline_) {
        clip = NormalizeNewlines(clip);
    } else {
        for (wchar_t& c : clip) {   // 单行控件：换行折成空格
            if (c == L'\n' || c == L'\r') c = L' ';
        }
    }
    PushUndo();
    const uint32_t s = SelectionStart();
    const uint32_t e = SelectionEnd();
    if (e > s) text_.erase(s, e - s);
    text_.insert(s, clip);
    caret_ = anchor_ = s + static_cast<uint32_t>(clip.size());
    goalX_ = -1.f;
    RebuildDisplay();
    NotifyChanged();
    EnsureCaretVisible();
}

void TextBox::Undo() {
    if (undo_.empty()) return;
    Snapshot cur;
    cur.text = text_;
    cur.caret = caret_;
    cur.anchor = anchor_;
    redo_.push_back(std::move(cur));
    const Snapshot s = undo_.back();
    undo_.pop_back();
    text_ = s.text;
    const uint32_t n = static_cast<uint32_t>(text_.size());
    caret_ = std::min(s.caret, n);
    anchor_ = std::min(s.anchor, n);
    goalX_ = -1.f;
    RebuildDisplay();
    NotifyChanged();
    EnsureCaretVisible();
}

void TextBox::Redo() {
    if (redo_.empty()) return;
    Snapshot cur;
    cur.text = text_;
    cur.caret = caret_;
    cur.anchor = anchor_;
    undo_.push_back(std::move(cur));
    const Snapshot s = redo_.back();
    redo_.pop_back();
    text_ = s.text;
    const uint32_t n = static_cast<uint32_t>(text_.size());
    caret_ = std::min(s.caret, n);
    anchor_ = std::min(s.anchor, n);
    goalX_ = -1.f;
    RebuildDisplay();
    NotifyChanged();
    EnsureCaretVisible();
}

// ---------------------------------------------------------------------------
// 几何
// ---------------------------------------------------------------------------

Rect TextBox::ContentBox() const {
    const StyleValue pv = ResolveProperty(this, Prop::Padding);
    const Thickness pad{pv.f, pv.extra[0], pv.extra[1], pv.extra[2]};
    const Rect b = Bounds();
    return Rect{b.x + pad.left, b.y + pad.top,
                std::max(0.f, b.w - pad.left - pad.right),
                std::max(0.f, b.h - pad.top - pad.bottom)};
}

Vec2 TextBox::TextOrigin() const {
    const Rect c = ContentBox();
    // 掩码模式：整段显示文本上移 maskDy_（绘制/命中/光标/IME 共用同一原点）
    return Vec2{c.x - scrollX_, c.y - scrollY_ + maskDy_};
}

uint32_t TextBox::HitTestIndex(const Vec2& ptAbs) const {
    auto* para = ParaOf(para_);
    if (para == nullptr) return 0;
    const Vec2 o = TextOrigin();
    return text::Engine::Get().HitTestPoint(para, Vec2{ptAbs.x - o.x, ptAbs.y - o.y});
}

void TextBox::ClampScroll() {
    auto* para = ParaOf(para_);
    const Rect c = ContentBox();
    if (para == nullptr) {
        scrollX_ = 0.f;
        scrollY_ = 0.f;
        return;
    }
    if (multiline_) {
        scrollX_ = 0.f;
        const float maxY = std::max(0.f, para->size.y - c.h);
        scrollY_ = std::clamp(scrollY_, 0.f, maxY);
    } else {
        scrollY_ = 0.f;
        const float maxX = std::max(0.f, para->size.x - c.w);
        scrollX_ = std::clamp(scrollX_, 0.f, maxX);
    }
}

void TextBox::EnsureCaretVisible() {
    auto* para = ParaOf(para_);
    if (para == nullptr) return;
    Rect cr;
    if (!text::Engine::Get().CaretRect(para, caret_, cr)) return;
    const Rect c = ContentBox();
    const float caretX = c.x + cr.x - scrollX_;
    if (caretX < c.x) {
        scrollX_ -= (c.x - caretX);
    } else if (caretX + cr.w > c.Right()) {
        scrollX_ += (caretX + cr.w - c.Right());
    }
    if (multiline_) {
        const float caretY = c.y + cr.y - scrollY_ + maskDy_;
        if (caretY < c.y) {
            scrollY_ -= (c.y - caretY);
        } else if (caretY + cr.h > c.Bottom()) {
            scrollY_ += (caretY + cr.h - c.Bottom());
        }
    }
    ClampScroll();
}

// ---------------------------------------------------------------------------
// 测量与绘制
// ---------------------------------------------------------------------------

Vec2 TextBox::MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const {
    text::Engine& en = text::Engine::Get();
    const uint32_t weight = ResolveProperty(this, Prop::FontWeight, ctx).u;
    // 换行宽：多行按内容宽（显式 Width 优先），单行不限宽
    float wrapW = 1e6f;
    if (multiline_) {
        StyleValue wv;
        if (TryResolve(this, Prop::Width, ctx, wv)) {
            const StyleValue pv = ResolveProperty(this, Prop::Padding, ctx);
            const Thickness pad{pv.f, pv.extra[0], pv.extra[1], pv.extra[2]};
            wrapW = wv.f - pad.left - pad.right;
        } else {
            wrapW = contentAvail.x;
        }
        if (wrapW <= 0.f) wrapW = 1.f;
    }
    para_ = en.Shape(ParaOf(para_), display_.c_str(), ctx.fontSize, weight,
                     ctx.lineHeight, wrapW, ctx.family);
    UpdateMaskOffset();
    Vec2 size = ParaOf(para_) != nullptr ? ParaOf(para_)->size : Vec2{};
    if (!multiline_) {
        size.x = std::max(size.x, kMinBoxWidth);
        size.y = std::max(size.y, ctx.fontSize * ctx.lineHeight);
    } else {
        size.x = std::max(size.x, kMinAreaWidth);
        size.y = std::max(size.y, kMinAreaHeight);
    }
    return size;
}

bool TextBox::ContentClipRect(Rect& out) const {
    out = ContentBox();
    return out.w > 0.f && out.h > 0.f;
}

void TextBox::OnFocusChanged(bool focused) {
    // 取得焦点（Tab 进入）：光标置于文本末尾（点击进入时随后由指针事件定位）
    if (focused) {
        if (!HasSelection()) SetCaret(static_cast<uint32_t>(text_.size()));
    } else {
        // 失焦 → 清除选区（否则选区底色留在失焦控件上；且不可见的选区会让
        // 再次聚焦后的 Delete/输入替换掉用户看不见的内容）
        Deselect();
    }
    caretVisible_ = true;
    MarkDirty(kFlagPaint);   // 光标的显示/隐藏随焦点变化
}

// 闪烁节拍（框架定时器驱动，500ms 一次）：光标持续闪烁
bool TextBox::OnBlink() {
    if (!HasFocus()) {
        caretVisible_ = true;
        return false;         // 已失焦 → 不再需要节拍
    }
    caretVisible_ = !caretVisible_;
    MarkDirty(kFlagPaint);
    return true;
}

void TextBox::ResetBlinkPhase() {
    caretVisible_ = true;
}

// 插入点矩形（客户区 DIP）：IME 候选窗口跟随光标。
// 矩形覆盖光标所在行的**整行**（行顶 → 行底）：IME 依系统 caret 的**底边**摆放
// 候选窗口，若 caret 只有 1px 高（零高度插入点），输入法会把候选窗整体下移一整行
// （观感：候选窗离光标很远、压在下方面板上）；底边落在行底则候选窗顶边紧贴
// 本行下沿，不会盖住刚输入的文字。
bool TextBox::InsertionPoint(Rect& outRect) const {
    auto* para = ParaOf(para_);
    if (para == nullptr) return false;
    Rect cr;
    if (!text::Engine::Get().CaretRect(para, caret_, cr)) return false;
    const Vec2 o = TextOrigin();

    float top = cr.y;             // 行顶（兜底：取 caret 自身的 y）
    float height = cr.h * 0.8f;   // 兜底：行高的经验比例
    std::vector<text::LineInfo> lines;
    text::Engine::Get().LineInfos(para, lines);
    if (!lines.empty()) {
        const uint32_t idx = std::min(caret_, static_cast<uint32_t>(text_.size()));
        size_t line = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].start <= idx) line = i; else break;
        }
        top = lines[line].top;
        height = lines[line].height;   // 覆盖整行 → caret 底边 = 行底
    }
    if (!(height > 0.f)) height = cr.h * 0.8f;
    outRect = Rect{o.x + cr.x, o.y + top, 1.f, height};
    return true;
}

void TextBox::OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) {
    auto* para = ParaOf(para_);
    if (para == nullptr) return;
    const Vec2 origin = TextOrigin();
    const Color textColor = ResolveProperty(this, Prop::Color, ctx).u;

    // 选区
    if (HasSelection()) {
        std::vector<Rect> rects;
        text::Engine::Get().SelectionRects(para, SelectionStart(), SelectionEnd(), rects);
        const Color sel = ResolveProperty(this, Prop::SelectionColor, ctx).u;
        for (const Rect& r : rects) {
            p.FillRoundedRect(Rect{origin.x + r.x, origin.y + r.y, r.w, r.h}, 0.f, sel, 1.f, scale);
        }
    }

    // 文本
    text::Engine::Get().Draw(para, p, scale, origin, textColor);

    // 光标（有焦点且处于可见相位时显示；闪烁由框架节拍驱动）
    if (HasFocus() && caretVisible_) {
        Rect cr;
        if (text::Engine::Get().CaretRect(para, caret_, cr)) {
            Color cc = textColor;
            StyleValue cv;
            if (TryResolveElement(this, Prop::CaretColor, cv)) cc = cv.u;
            p.FillRoundedRect(Rect{origin.x + cr.x, origin.y + cr.y, cr.w, cr.h}, 0.f, cc, 1.f, scale);
        }
    }
}

// ---------------------------------------------------------------------------
// 事件：控件内部行为（Element 的 Handle* 钩子）
// ---------------------------------------------------------------------------

bool TextBox::HandleKeyDown(KeyEvent& ev) {
    const bool ctrl = ev.ctrl;
    const bool shift = ev.shift;
    switch (ev.key) {
    case VK_LEFT:
        MoveCaret(-1, shift);
        break;
    case VK_RIGHT:
        MoveCaret(+1, shift);
        break;
    case VK_UP:
        if (!multiline_) return false;
        MoveCaretLine(-1, shift);
        break;
    case VK_DOWN:
        if (!multiline_) return false;
        MoveCaretLine(+1, shift);
        break;
    case VK_HOME:
        MoveToLineEdge(false, shift, ctrl);
        break;
    case VK_END:
        MoveToLineEdge(true, shift, ctrl);
        break;
    case VK_BACK:
        DeleteBackward();
        break;
    case VK_DELETE:
        DeleteForward();
        break;
    case VK_RETURN:
        if (multiline_) {
            InsertChar(L'\n');
        } else if (onSubmit_) {
            onSubmit_();
        }
        break;
    case 'A':
        if (!ctrl) return false;
        SelectAll();
        break;
    case 'C':
        if (!ctrl) return false;
        Copy();
        break;
    case 'X':
        if (!ctrl) return false;
        Cut();
        break;
    case 'V':
        if (!ctrl) return false;
        Paste();
        break;
    case 'Z':
        if (!ctrl) return false;
        Undo();
        break;
    case 'Y':
        if (!ctrl) return false;
        Redo();
        break;
    default:
        return false;   // 未消费 → 继续冒泡（Tab 等由框架处理）
    }
    ev.handled = true;
    return true;
}

bool TextBox::HandleChar(KeyEvent& ev) {
    const wchar_t ch = ev.ch;
    if (ch == 0) return false;
    if (ev.ctrl || ev.alt) return false;   // 组合键由 KeyDown 处理
    if (ch == L'\t') return false;         // Tab 留给焦点切换
    if (ch < 0x20) return false;           // 控制字符（回车等由 KeyDown 处理）
    InsertChar(ch);
    ev.handled = true;
    return true;
}

bool TextBox::HandlePointerDown(PointerEvent& ev) {
    const uint32_t idx = HitTestIndex(ev.pos);
    caret_ = anchor_ = idx;
    dragAnchor_ = idx;
    dragging_ = true;
    goalX_ = -1.f;
    ResetBlinkPhase();     // 点击后光标立即可见
    MarkDirty(kFlagPaint);
    return false;   // 不消费：事件继续冒泡给祖先
}

bool TextBox::HandlePointerMove(PointerEvent& ev) {
    if (!dragging_) return false;
    if (!HasState(State::Pressed)) {   // 按钮已抬起（未落在本控件上时收不到 Up）
        dragging_ = false;
        return false;
    }
    const uint32_t idx = HitTestIndex(ev.pos);
    anchor_ = dragAnchor_;
    caret_ = idx;
    ResetBlinkPhase();
    MarkDirty(kFlagPaint);
    return false;
}

bool TextBox::HandlePointerUp(PointerEvent&) {
    dragging_ = false;
    return false;
}

// ---------------------------------------------------------------------------
// TextArea：多行编辑
// ---------------------------------------------------------------------------

TextArea::TextArea() : TextBox(false) {
    multiline_ = true;
}

}  // namespace dui
