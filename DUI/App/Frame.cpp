// ============================================================================
//  DUI/App/Frame.cpp — 帧路径组装（App 层组合 UI + Text + Graphics，01-Layering §2）
// ============================================================================

#include "App/App.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "Graphics/Graphics.h"
#include "Core/Window.h"
#include "Text/TextEngine.h"
#include "UI/Scroller.h"

namespace dui {

namespace {

void DrawTextContent(Element* e, const StyleContext& ctx, graphics::Painter& p, float scale) {
    auto* para = static_cast<text::Paragraph*>(static_cast<const Text*>(e)->ParaHandle());
    if (para == nullptr) return;
    const StyleValue pv = ResolveProperty(e, Prop::Padding, ctx);
    const StyleValue cv = ResolveProperty(e, Prop::Color, ctx);
    const StyleValue av = ResolveProperty(e, Prop::TextAlign, ctx);
    const Rect b = e->Bounds();
    // textAlign（15-Properties §4.1）：段落按自然宽左排，绘制期在内容盒剩余宽度内
    // 整体平移。不改 Paragraph 排版，故 TextBox 的水平滚动与命中测试不受影响；
    // 多行文本按最长行整体对齐（不逐行居中）。
    const float boxW = b.w - pv.f - pv.extra[1];
    const float textW = para->size.x;
    float dx = 0.f;
    if (av.u == static_cast<uint32_t>(TextAlign::Center)) dx = (boxW - textW) * 0.5f;
    else if (av.u == static_cast<uint32_t>(TextAlign::End)) dx = boxW - textW;
    const Vec2 origin{b.x + pv.f + dx, b.y + pv.extra[0]};   // 文本原点 = 内容盒左上
    text::Engine::Get().Draw(para, p, scale, origin, cv.u);
}

// 绘制单元列表：Painter 池（跨帧复用）+ 单元表（顺序即绘制顺序）。
// 滚动容器的内容单独成单元（原点 = -滚动偏移，scissor = 视口）。
class DrawList {
public:
    // 清空单元表（Painter 池保留 → 稳态零分配）
    void Begin() {
        units_.clear();
        used_ = 0;
    }

    graphics::Painter* Acquire() {
        if (used_ == pool_.size())
            pool_.push_back(std::make_unique<graphics::Painter>());
        graphics::Painter* p = pool_[used_++].get();
        p->Reset();   // 新建与复用两条路径都要有默认状态槽
        return p;
    }

    void Commit(graphics::Painter* p, const Rect& clipPx) {
        units_.push_back(graphics::DrawUnit{p, clipPx});
    }

    const std::vector<graphics::DrawUnit>& Units() const noexcept { return units_; }

private:
    std::vector<std::unique_ptr<graphics::Painter>> pool_;
    std::vector<graphics::DrawUnit> units_;
    size_t used_ = 0;
};

Rect IntersectRect(const Rect& a, const Rect& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.Right(), b.Right());
    const float y1 = std::min(a.Bottom(), b.Bottom());
    return Rect{x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
}

// 收集器：遍历可见树 → 绘制单元。
// 普通元素进当前单元；滚动容器把内容切到独立单元（原点 = -滚动偏移，
// 裁剪 = 视口 ∩ 当前裁剪），滚动条画在内容之上的父级单元。
// 窗口级 Fixed 层的一项：元素 + 记录时的祖先裁剪快照 + 继承态
struct FixedItem {
    Element* e;
    Rect clipPx;
    StyleContext ctx;
};

class Collector {
public:
    Collector(DrawList& list, float scale, const Rect& windowPx)
        : list_(list), scale_(scale) {
        origin_ = {0.f, 0.f};
        clipPx_ = windowPx;
        painter_ = AcquirePainter(origin_);
    }

    void Collect(Element* e, const StyleContext& parentCtx) { Collect(e, parentCtx, clipPx_, 0); }

    void Collect(Element* e, const StyleContext& parentCtx, const Rect& clipForFixed,
                 uint32_t depth) {
        if (ResolveProperty(e, Prop::Display, parentCtx).u == static_cast<uint32_t>(Display::None)) return;
        // 布局期计数为 0 时整段短路：未使用 Fixed 的应用连 IsFixed 都不解析。
        const bool lift = !inFixedLayer_ && FixedCount() != 0;
        // Fixed 整棵子树延后到窗口级 Fixed 层：不受祖先 transform / 滚动原点影响，
        // 但仍沿用记录时的祖先裁剪快照（04-Layout §4.3）。
        if (lift && IsFixed(e)) {
            FixedLayer().push_back(FixedItem{e, clipForFixed, parentCtx});
            return;
        }

        const StyleContext ctx = parentCtx.Child(e);

        // 元素自身变换压栈：影响本元素的绘制与整棵子树（先子后父组合）。
        // 恒等变换不压栈 → 未使用 transform 的元素每帧零额外开销。
        const Transform2D saved = xform_;
        const Transform2D local = ElementTransform(e);
        const bool transformed = !local.IsIdentity();
        if (transformed) {
            xform_ = saved * local;
            painter_->SetTransform(xform_);
        }
        TransformScope scope{*this, saved, transformed};

        if (e->GetKind() == Kind::Scroller) {
            CollectScroller(static_cast<Scroller*>(e), ctx, depth);
            return;
        }

        // 内容裁剪（TextBox/TextArea 的文本溢出）：背景仍画在父级单元，
        // 元素自身内容切到新单元（裁剪 = 内容矩形 ∩ 当前裁剪）。
        Rect clipRect{};
        const bool clipped = e->ContentClipRect(clipRect);

        DrawBackground(e, ctx);

        ClipState st;
        if (clipped) st = BeginClip(clipRect);
        if (e->GetKind() == Kind::Text) DrawTextContent(e, ctx, *painter_, scale_);
        e->OnPaint(*painter_, ctx, scale_);   // 自定义绘制钩子（背景之后、子元素之前）

        // 定位层叠（04-Layout §4.3）：流内子项先画，脱离流的 Absolute 后画（盖在上面）。
        // Fixed 在树序原位记录到窗口级 Fixed 层（不进当前单元），顺序与
        // Event.cpp::CollectFixedLayer 的两趟遍历一致 → 倒序命中即"后画者先命中"。
        for (Element* c : e->Children())
            if (!IsOutOfFlow(c) || (lift && IsFixed(c))) Collect(c, ctx, clipPx_, depth + 1);
        // 脱离流趟：Absolute 提升到所属 CB 处，按 zIndex 升序绘制（同值按树序）。
        // 未使用 Absolute / zIndex 时保持原两趟，零额外遍历。
        if (UseOrderedOutOfFlow()) {
            for (Element* c : ZOrderedOutOfFlow(e, depth))
                if (!(lift && IsFixed(c))) Collect(c, ContextOf(c, e, ctx), clipPx_, depth + 1);
        } else {
            for (Element* c : e->Children())
                if (IsOutOfFlow(c) && !(lift && IsFixed(c))) Collect(c, ctx, clipPx_, depth + 1);
        }

        if (clipped) EndClip(st);
    }

    // 收尾：提交主树最后一个单元，再统一画窗口级 Fixed 层（盖过所有普通内容，
    // 含滚动条）。每项一个单元：原点 = 窗口、变换 = 恒等、裁剪 = 记录时的
    // 祖先裁剪快照（Scroller 视口不在此列，见 CollectScroller）。
    void Finish() {
        Commit();
        if (FixedLayer().empty()) return;
        inFixedLayer_ = true;
        for (const FixedItem& f : FixedLayer()) {
            origin_ = {0.f, 0.f};
            clipPx_ = f.clipPx;
            xform_ = {};
            painter_ = AcquirePainter(origin_);
            Collect(f.e, f.ctx, f.clipPx, 0);
            Commit();
        }
        inFixedLayer_ = false;
        FixedLayer().clear();
    }

private:
    // 窗口级 Fixed 层：跨帧复用（单线程 UI，ADR-012），稳态零分配
    static std::vector<FixedItem>& FixedLayer() {
        static std::vector<FixedItem> v;
        return v;
    }

    void Commit() { list_.Commit(painter_, clipPx_); }

    // 每取一个新 Painter 都要带上当前组合变换（变换烘进顶点，按单元独立）
    graphics::Painter* AcquirePainter(const Vec2& origin) {
        graphics::Painter* p = list_.Acquire();
        p->SetOrigin(origin.x, origin.y, scale_);
        p->SetTransform(xform_);
        return p;
    }

    // 跨中间容器提升到 CB 处绘制的 Absolute，样式继承仍须沿其真实父链重建；
    // 直接子项沿用当前继承态，避免热路径额外遍历。
    StyleContext ContextOf(Element* element, Element* host, const StyleContext& hostCtx) {
        if (element->Parent() == host) return hostCtx;
        static std::vector<Element*> chain;
        chain.clear();
        for (Element* a = element->Parent(); a != nullptr; a = a->Parent()) chain.push_back(a);
        StyleContext c;
        c.scale = scale_;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) c = c.Child(*it);
        return c;
    }

    // Collect() 有 Scroller / Image 等提前 return，用作用域守卫统一弹栈。
    // 弹栈时只改当前 Painter 的状态，已发射的顶点不受影响。
    struct TransformScope {
        Collector& c;
        Transform2D saved;
        bool active;
        ~TransformScope() {
            if (!active) return;
            c.xform_ = saved;
            c.painter_->SetTransform(c.xform_);
        }
    };

    // 内容裁剪：切到新绘制单元（原点不变，裁剪 = 内容矩形 ∩ 当前裁剪）。
    // 内容矩形是布局空间绝对 DIP，须叠加父级原点平移到窗口空间（与
    // CollectScroller 的视口同理），否则嵌套时裁剪带留在原位。
    struct ClipState {
        Vec2 origin{};
        Rect clip{};
    };

    ClipState BeginClip(const Rect& dipRect) {
        ClipState st{origin_, clipPx_};
        Commit();
        painter_ = AcquirePainter(st.origin);
        // scissor 只能轴对齐，故取变换后四个角点的 AABB：旋转的裁剪盒不会
        // 留在原位。恒等变换时结果与原矩形完全一致（零回归）。
        const Transform2D eff = painter_->EffectiveTransform(scale_);
        const float x0 = (dipRect.x + st.origin.x) * scale_;
        const float y0 = (dipRect.y + st.origin.y) * scale_;
        const float x1 = (dipRect.Right() + st.origin.x) * scale_;
        const float y1 = (dipRect.Bottom() + st.origin.y) * scale_;
        const Vec2 p0 = eff.Apply({x0, y0});
        const Vec2 p1 = eff.Apply({x1, y0});
        const Vec2 p2 = eff.Apply({x1, y1});
        const Vec2 p3 = eff.Apply({x0, y1});
        const float l = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
        const float t = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
        const float r = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
        const float b = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
        const Rect winPx{l, t, r - l, b - t};
        clipPx_ = IntersectRect(winPx, st.clip);
        return st;
    }

    void EndClip(const ClipState& st) {
        Commit();
        painter_ = AcquirePainter(st.origin);
        origin_ = st.origin;
        clipPx_ = st.clip;
    }

    // 阴影（box-shadow 子集）：画在元素背景之下，且先于背景的透明早退 ——
    // 背景透明的元素同样可以有投影。只投元素边框盒，不影响命中测试。
    void DrawShadow(Element* e, const StyleContext& ctx) {
        const StyleValue sh = ResolveProperty(e, Prop::Shadow, ctx);
        if (sh.u == 0) return;                    // 颜色透明 = 无阴影
        const Rect b = e->Bounds();
        if (b.w <= 0.f || b.h <= 0.f) return;
        const Rect r{b.x + sh.extra[0], b.y + sh.extra[1], b.w, b.h};
        const float radius = ResolveProperty(e, Prop::CornerRadius, ctx).f;
        if (sh.f <= 0.f) {
            painter_->FillRoundedRect(r, radius, sh.u, 1.f, scale_);   // 纯偏移硬阴影
        } else {
            painter_->ShadowRoundRect(r, radius, sh.f, sh.u, scale_);
        }
    }

    void DrawBackground(Element* e, const StyleContext& ctx) {
        DrawShadow(e, ctx);
        const StyleValue bg = ResolveProperty(e, Prop::BackgroundColor, ctx);
        if (bg.u == 0) return;
        const float radius = ResolveProperty(e, Prop::CornerRadius, ctx).f;
        float opacity = ResolveProperty(e, Prop::Opacity, ctx).f;
        if (opacity < 0.f) opacity = 0.f;
        if (opacity > 1.f) opacity = 1.f;
        painter_->FillRoundedRect(e->Bounds(), radius, bg.u, opacity, scale_);
    }

    void CollectScroller(Scroller* sc, const StyleContext& ctx, uint32_t depth) {
        // 容器自身背景（不随内容滚动）
        DrawBackground(sc, ctx);

        // 封存当前单元：父级已画部分到此为止
        Commit();

        sc->ClampScroll();
        const Vec2 off = sc->ScrollOffset();
        const Rect vp = sc->Viewport();

        const Vec2 savedOrigin = origin_;
        const Rect savedClip = clipPx_;

        // 内容单元：原点 = 父级原点 − 滚动偏移；裁剪 = 视口（加父级原点平移到
        // 窗口空间，嵌套时随外层滚动一起移动）∩ 当前裁剪。
        // SetOrigin 与 scissor 都是"绝对"量：嵌套时必须叠加父级偏移，
        // 否则外层滚动对内层内容无效、内层裁剪带留在原位（背景被裁没）。
        const Vec2 contentOrigin{savedOrigin.x - off.x, savedOrigin.y - off.y};
        painter_ = AcquirePainter(contentOrigin);
        origin_ = contentOrigin;
        // 视口是窗口空间的轴对齐 scissor：不随容器 transform 旋转，也不随
        // 内容滚动（用父级 savedOrigin 平移，见上方"绝对量"说明）。
        const Rect vpWinPx{(vp.x + savedOrigin.x) * scale_, (vp.y + savedOrigin.y) * scale_,
                           vp.w * scale_, vp.h * scale_};
        clipPx_ = IntersectRect(vpWinPx, savedClip);

        // 与 Collect() 同规则；Fixed 记录 savedClip（视口求交之前），绕过本层视口 scissor。
        const bool lift = !inFixedLayer_ && FixedCount() != 0;
        for (Element* c : sc->Children())
            if (!IsOutOfFlow(c) || (lift && IsFixed(c))) Collect(c, ctx, savedClip, depth + 1);
        if (UseOrderedOutOfFlow()) {
            for (Element* c : ZOrderedOutOfFlow(sc, depth))
                if (!(lift && IsFixed(c))) Collect(c, ContextOf(c, sc, ctx), savedClip, depth + 1);
        } else {
            for (Element* c : sc->Children())
                if (IsOutOfFlow(c) && !(lift && IsFixed(c))) Collect(c, ctx, savedClip, depth + 1);
        }

        Commit();   // 封存内容单元

        // 回到父级上下文：父级后续兄弟继续画在新单元里
        painter_ = AcquirePainter(savedOrigin);
        origin_ = savedOrigin;
        clipPx_ = savedClip;

        // 滚动条：压在内容之上
        sc->PaintScrollBars(*painter_, scale_);
    }

    DrawList& list_;
    float scale_;
    graphics::Painter* painter_ = nullptr;
    Vec2 origin_{};
    Rect clipPx_{};
    Transform2D xform_{};   // 当前组合变换（布局 DIP 空间；下传时按 scale 共轭）
    bool inFixedLayer_ = false;   // 绘制 Fixed 层时不再二次收集
};

}  // namespace

// 帧路径（10-Rendering §2，按需全量光栅）：
// 尺寸同步 → 失效检查（布局脏才重排；只重绘的帧跳过 Measure/Arrange）
// → 图集页同步 → 收集顶点 → 光栅 → Present。
void Application::RenderFrame(Window& window, const NativeWindow& native,
                              graphics::Device& device) {
    const Vec2 px = native.ClientSizePx();
    if (px.x <= 0.f || px.y <= 0.f) return;   // 最小化：不进帧

    device.Resize(px);

    Element* root = window.Root();
    if (root->SubtreeDirty()) {
        if (SubtreeLayoutDirty(root)) RunLayout(root, native.ClientSizeDip(), native.Scale());
        ClearDirtyTree(root);
    }

    // 字形图集：整页释放先行，收集（可能触发新字形光栅）后上传脏页
    text::Engine& en = text::Engine::Get();
    text::GlyphAtlas& atlas = en.Atlas();
    for (const uint32_t id : atlas.FreedPages()) device.ReleaseGlyphPage(id);
    atlas.ClearFreed();

    // 收集：可见树 → 绘制单元（滚动容器的内容单独成单元，scissor 裁到视口）
    static DrawList list;   // Painter 池跨帧复用（热路径零分配）
    list.Begin();
    Collector collector(list, native.Scale(), Rect{0.f, 0.f, px.x, px.y});
    StyleContext rootCtx;
    rootCtx.scale = native.Scale();   // 遍历期 DPI 缩放（图像类控件用）
    collector.Collect(root, rootCtx);
    collector.Finish();

    for (text::AtlasPage& page : atlas.Pages()) {
        if (page.dirty) {
            device.UploadGlyphPage(page.id, page.w, page.h, page.bits.data());
            page.dirty = false;
        }
    }

    // 图像纹理同步：收集期（Svg 按显示尺寸 / DPI 重光栅）可能产生新纹理或脏像素，
    // 与字形页一样在收集之后、Render 之前上传；控件已释放的纹理在此回收。
    graphics::TextureStore::Instance().Sync(device);

    device.Render(list.Units().data(), static_cast<uint32_t>(list.Units().size()), native.Scale());
    device.Present();
}

}  // namespace dui
