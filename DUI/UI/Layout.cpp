// ============================================================================
//  DUI/UI/Layout.cpp — Flex 单遍约束布局（04-Layout §4.1/§6，MVP 子集）
//
//  MVP 范围（Todo M3）：Row/Column、gap、padding、Width/Height 固定尺寸、
//  FlexGrow/FlexShrink、MinWidth、交叉轴默认 Stretch。
//  两遍遍历：Measure 自底向上求自然尺寸 → Arrange 自顶向下分配矩形。
//  measure 结果缓存（04-Layout §6.1）列后续优化，M3 每帧全量重算。
// ============================================================================

#include "UI/UI.h"

#include <algorithm>

#include "UI/Scroller.h"

namespace dui {

namespace {

Thickness ElementPadding(const Element* e, const StyleContext& ctx) {
    const StyleValue v = ResolveProperty(e, Prop::Padding, ctx);
    return {v.f, v.extra[0], v.extra[1], v.extra[2]};
}

// 每层递归独立的可见子项缓冲（跨帧复用；单线程 UI，ADR-012。Arena 化列 M6）。
// 两层陷阱：
//   1. 不能全递归共享一份：子项循环内会递归 ArrangeElement，递归 clear/push_back
//      会重分配同一缓冲（曾致 0xFEEEFEEE 读崩溃）；
//   2. 外层 vector 扩容会搬动内层 vector 对象本身 → 持有 itemsByDepth[depth]
//      引用的外层帧悬空（递归更深层触发 resize 即炸，0xFDFDFDFD 读崩溃）。
//      故内层用 unique_ptr 固定对象地址，引用跨 resize 稳定。
std::vector<Element*>& ItemsAt(unsigned depth) {
    static std::vector<std::unique_ptr<std::vector<Element*>>> itemsByDepth;
    if (itemsByDepth.size() <= depth) itemsByDepth.resize(depth + 1);
    if (!itemsByDepth[depth]) itemsByDepth[depth] = std::make_unique<std::vector<Element*>>();
    return *itemsByDepth[depth];
}

}  // namespace

// ---------------------------------------------------------------------------
// Measure：传入选定空间，返回自然（外层盒）尺寸
// ---------------------------------------------------------------------------

Vec2 MeasureElement(Element* e, const Vec2& avail, const StyleContext& parentCtx) {
    if (ResolveProperty(e, Prop::Display, parentCtx).u == static_cast<uint32_t>(Display::None)) {
        e->measured_ = {};
        return {};
    }

    const StyleContext ctx = parentCtx.Child(e);
    const Thickness pad = ElementPadding(e, ctx);
    const Vec2 innerAvail{
        avail.x - pad.left - pad.right > 0.f ? avail.x - pad.left - pad.right : 0.f,
        avail.y - pad.top - pad.bottom > 0.f ? avail.y - pad.top - pad.bottom : 0.f,
    };
    const Vec2 content = e->MeasureContent(ctx, innerAvail);

    Vec2 size{content.x + pad.left + pad.right, content.y + pad.top + pad.bottom};

    StyleValue v;
    if (TryResolve(e, Prop::Width, ctx, v))  size.x = v.f;    // BorderBox（04-Layout §3）
    if (TryResolve(e, Prop::Height, ctx, v)) size.y = v.f;
    if (TryResolve(e, Prop::MinWidth, ctx, v)) if (size.x < v.f) size.x = v.f;
    if (TryResolve(e, Prop::MaxWidth, ctx, v)) if (size.x > v.f) size.x = v.f;

    e->measured_ = size;
    return size;
}

// ---------------------------------------------------------------------------
// Arrange：把确定矩形分给子项，写回 Bounds（相对父）
// ---------------------------------------------------------------------------

void ArrangeElement(Element* e, const Rect& slot, const StyleContext& parentCtx, unsigned depth) {
    if (ResolveProperty(e, Prop::Display, parentCtx).u == static_cast<uint32_t>(Display::None)) {
        e->bounds_ = {};
        return;
    }
    e->bounds_ = slot;

    const StyleContext ctx = parentCtx.Child(e);
    const Thickness pad = ElementPadding(e, ctx);
    Rect content{slot.x + pad.left, slot.y + pad.top,
                  slot.w - pad.left - pad.right, slot.h - pad.top - pad.bottom};
    const bool row = ResolveProperty(e, Prop::FlexDirection, ctx).u ==
                     static_cast<uint32_t>(FlexDirection::Row);
    if (e->GetKind() == Kind::Scroller) {
        // 滚动容器：主轴按内容自然尺寸（可溢出 → 可滚动；子项不参与收缩），
        // 交叉轴保持视口尺寸（子项比视口宽时溢出 → 横向也可滚）
        const Vec2 nat = static_cast<Scroller*>(e)->NaturalSize();
        if (row) content.w = nat.x;
        else     content.h = nat.y;
    }
    const float gap = ResolveProperty(e, Prop::Gap, ctx).f;
    const uint32_t just = ResolveProperty(e, Prop::JustifyContent, ctx).u;
    const uint32_t align = ResolveProperty(e, Prop::AlignItems, ctx).u;

    // 可见子项（缓冲按递归深度分层复用，见 ItemsAt 注释）
    std::vector<Element*>& items = ItemsAt(depth);
    items.clear();
    for (Element* c : e->children_) {
        if (ResolveProperty(c, Prop::Display, ctx).u == static_cast<uint32_t>(Display::None)) continue;
        items.push_back(c);
    }

    // 主轴剩余空间分配：grow 拉伸 / shrink 收缩（04-Layout §4.1）；
    // 换行布局按行装箱，不参与 grow/shrink
    const uint32_t wrapMode = ResolveProperty(e, Prop::FlexWrap, ctx).u;
    const bool wrapRow = row && wrapMode == static_cast<uint32_t>(FlexWrap::Wrap);
    const int n = static_cast<int>(items.size());
    float total = 0.f;
    for (Element* c : items) total += row ? c->measured_.x : c->measured_.y;
    total += gap * (n > 1 ? n - 1 : 0);
    const float mainContent = row ? content.w : content.h;
    const float freeSpace = mainContent - total;

    if (n > 0 && freeSpace > 0.f && !wrapRow) {
        float grow = 0.f;
        for (Element* c : items) grow += ResolveProperty(c, Prop::FlexGrow, ctx).f;
        if (grow > 0.f) {
            for (Element* c : items) {
                const float g = ResolveProperty(c, Prop::FlexGrow, ctx).f;
                float& m = row ? c->measured_.x : c->measured_.y;
                m += freeSpace * (g / grow);
            }
        }
    } else if (n > 0 && freeSpace < 0.f && !wrapRow) {
        // 收缩：按 shrink × 当前尺寸加权（CSS 近似；04-Layout §7-7 单遍近似）
        float weight = 0.f;
        for (Element* c : items) {
            const float m = row ? c->measured_.x : c->measured_.y;
            weight += ResolveProperty(c, Prop::FlexShrink, ctx).f * m;
        }
        if (weight > 0.f) {
            for (Element* c : items) {
                const float s = ResolveProperty(c, Prop::FlexShrink, ctx).f;
                const float m0 = row ? c->measured_.x : c->measured_.y;
                const float take = (-freeSpace) * (s * m0 / weight);
                float& m = row ? c->measured_.x : c->measured_.y;
                m = m0 > take ? m0 - take : 0.f;
                StyleValue v;
                if (row && TryResolve(c, Prop::MinWidth, ctx, v)) {
                    if (m < v.f) m = v.f;   // MinWidth 夹紧在收缩结果上（04-Layout §5）
                }
            }
        }
    }

    // 依序排列（04-Layout §4.1）：
    //   主轴：Start（默认）顺序排列；Center 剩余空间均分前后
    //   交叉轴：Stretch（默认，固定尺寸优先否则拉满）/ Start / Center（固定尺寸否则自然尺寸）
    const float crossLen = row ? content.h : content.w;
    Vec2 extent{};          // 子项外接范围（相对内容盒原点）→ 滚动容器的内容尺寸

    if (wrapRow) {
        // 换行布局（Row + Wrap）：贪心装箱成行（与 MeasureContent 同一规则），
        // 行内顺序排列，行高 = 行内最高子项，行间同 gap；交叉轴对齐沿用 alignItems
        auto PlaceLine = [&](int j0, int j1, float y, float lineH) {
            float x = content.x;
            for (int j = j0; j < j1; ++j) {
                Element* cc = items[j];
                float cross;
                float crossPos = 0.f;
                StyleValue v;
                const bool fixedCrossC = TryResolve(cc, Prop::Height, ctx, v);
                if (align == static_cast<uint32_t>(Align::Center)) {
                    cross = fixedCrossC ? v.f : cc->measured_.y;
                    crossPos = (lineH - cross) * 0.5f;
                } else if (fixedCrossC) {
                    cross = v.f;
                } else {
                    cross = lineH;             // Stretch（默认）：拉到行高
                }
                ArrangeElement(cc, Rect{x, y + crossPos, cc->measured_.x, cross},
                               ctx, depth + 1);
                x += cc->measured_.x + gap;
                extent.x = std::max(extent.x, cc->bounds_.Right() - content.x);
                extent.y = std::max(extent.y, cc->bounds_.Bottom() - content.y);
            }
        };

        float lineY = content.y;
        float lineW = 0.f;
        int start = 0;                          // 当前行首项下标
        for (int i = 0; i < n; ++i) {
            const float w = items[i]->measured_.x;
            const float add = (i > start) ? gap + w : w;
            if (i > start && lineW + add > content.w) {   // 放不下 → 结算当前行
                float lineH = 0.f;
                for (int j = start; j < i; ++j) lineH = std::max(lineH, items[j]->measured_.y);
                PlaceLine(start, i, lineY, lineH);
                lineY += lineH + gap;
                lineW = w;
                start = i;                        // 当前项成为新行首项
            } else {
                lineW += add;
            }
        }
        if (start < n) {                          // 最后一行
            float lineH = 0.f;
            for (int j = start; j < n; ++j) lineH = std::max(lineH, items[j]->measured_.y);
            PlaceLine(start, n, lineY, lineH);
        }
    } else {
    float pos = (row ? content.x : content.y) +
                (just == static_cast<uint32_t>(Justify::Center) && freeSpace > 0.f
                     ? freeSpace * 0.5f : 0.f);
    for (Element* c : items) {
        const float main = row ? c->measured_.x : c->measured_.y;
        float cross;
        float crossPos = 0.f;
        StyleValue v;
        const bool fixedCross = TryResolve(c, row ? Prop::Height : Prop::Width, ctx, v);
        if (align == static_cast<uint32_t>(Align::Center)) {
            cross = fixedCross ? v.f : (row ? c->measured_.y : c->measured_.x);
            crossPos = (crossLen - cross) / 2.f;
        } else if (fixedCross) {          // Start / Stretch 下的固定尺寸
            cross = v.f;
        } else {
            cross = crossLen;             // Stretch（默认）
        }
        const Rect childSlot = row ? Rect{pos, content.y + crossPos, main, cross}
                                   : Rect{content.x + crossPos, pos, cross, main};
        ArrangeElement(c, childSlot, ctx, depth + 1);
        pos += main + gap;
        extent.x = std::max(extent.x, c->bounds_.Right() - content.x);
        extent.y = std::max(extent.y, c->bounds_.Bottom() - content.y);
    }
    }   // else：单行布局
    if (e->GetKind() == Kind::Scroller) {
        static_cast<Scroller*>(e)->SetContentExtent(extent);
    }
}

void RunLayout(Element* root, const Vec2& sizeDip) {
    if (root == nullptr) return;
    const StyleContext ctx{};
    MeasureElement(root, sizeDip, ctx);
    ArrangeElement(root, Rect{0.f, 0.f, sizeDip.x, sizeDip.y}, ctx, 0);
}

bool SubtreeLayoutDirty(const Element* e) {
    if ((e->dirty_ & kFlagLayout) != 0) return true;
    for (const Element* c : e->Children()) {
        if (SubtreeLayoutDirty(c)) return true;
    }
    return false;
}

}  // namespace dui
