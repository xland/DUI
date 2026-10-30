// ============================================================================
//  DUI/App/Frame.cpp — 帧路径组装（App 层组合 UI + Text + Graphics，01-Layering §2）
// ============================================================================

#include "App/App.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "Graphics/Graphics.h"
#include "Platform/Window.h"
#include "Text/TextEngine.h"
#include "UI/Scroller.h"

namespace dui {

namespace {

void DrawTextContent(Element* e, const StyleContext& ctx, graphics::Painter& p, float scale) {
    auto* para = static_cast<text::Paragraph*>(static_cast<const Text*>(e)->ParaHandle());
    if (para == nullptr) return;
    const StyleValue pv = ResolveProperty(e, Prop::Padding, ctx);
    const StyleValue cv = ResolveProperty(e, Prop::Color, ctx);
    const Rect b = e->Bounds();
    const Vec2 origin{b.x + pv.f, b.y + pv.extra[0]};   // 文本原点 = 内容盒左上
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
        if (used_ < pool_.size()) {
            graphics::Painter* p = pool_[used_++].get();
            p->Reset();
            return p;
        }
        pool_.push_back(std::make_unique<graphics::Painter>());
        graphics::Painter* p = pool_.back().get();
        ++used_;
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
class Collector {
public:
    Collector(DrawList& list, float scale, const Rect& windowPx)
        : list_(list), scale_(scale) {
        painter_ = list_.Acquire();
        origin_ = {0.f, 0.f};
        clipPx_ = windowPx;
    }

    void Collect(Element* e, const StyleContext& parentCtx) {
        if (ResolveProperty(e, Prop::Display, parentCtx).u == static_cast<uint32_t>(Display::None)) return;

        const StyleContext ctx = parentCtx.Child(e);

        if (e->GetKind() == Kind::Scroller) {
            CollectScroller(static_cast<Scroller*>(e), ctx);
            return;
        }

        DrawBackground(e, ctx);
        if (e->GetKind() == Kind::Text) DrawTextContent(e, ctx, *painter_, scale_);
        e->OnPaint(*painter_, ctx, scale_);   // 自定义绘制钩子（背景之后、子元素之前）

        for (Element* c : e->Children()) Collect(c, ctx);
    }

    // 收尾：提交最后一个单元（否则末尾元素与滚动条会丢失）
    void Finish() { Commit(); }

private:
    void Commit() { list_.Commit(painter_, clipPx_); }

    void DrawBackground(Element* e, const StyleContext& ctx) {
        const StyleValue bg = ResolveProperty(e, Prop::BackgroundColor, ctx);
        if (bg.u == 0) return;
        const float radius = ResolveProperty(e, Prop::CornerRadius, ctx).f;
        float opacity = ResolveProperty(e, Prop::Opacity, ctx).f;
        if (opacity < 0.f) opacity = 0.f;
        if (opacity > 1.f) opacity = 1.f;
        painter_->FillRoundedRect(e->Bounds(), radius, bg.u, opacity, scale_);
    }

    void CollectScroller(Scroller* sc, const StyleContext& ctx) {
        // 容器自身背景（不随内容滚动）
        DrawBackground(sc, ctx);

        // 封存当前单元：父级已画部分到此为止
        Commit();

        sc->ClampScroll();
        const Vec2 off = sc->ScrollOffset();
        const Rect vp = sc->Viewport();
        const Rect vpPx{vp.x * scale_, vp.y * scale_, vp.w * scale_, vp.h * scale_};

        const Vec2 savedOrigin = origin_;
        const Rect savedClip = clipPx_;

        // 内容单元：原点 = -滚动偏移，裁剪 = 视口 ∩ 当前裁剪
        painter_ = list_.Acquire();
        painter_->SetOrigin(-off.x, -off.y, scale_);
        origin_ = {-off.x, -off.y};
        clipPx_ = IntersectRect(vpPx, savedClip);

        for (Element* c : sc->Children()) Collect(c, ctx);

        Commit();   // 封存内容单元

        // 回到父级上下文：父级后续兄弟继续画在新单元里
        painter_ = list_.Acquire();
        painter_->SetOrigin(savedOrigin.x, savedOrigin.y, scale_);
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
};

}  // namespace

// 帧路径（10-Rendering §2，按需全量光栅）：
// 尺寸同步 → 失效检查（布局脏才重排；只重绘的帧跳过 Measure/Arrange）
// → 图集页同步 → 收集顶点 → 光栅 → Present。
void Application::RenderFrame(Window& window, const platform::NativeWindow& native,
                              graphics::Device& device) {
    const Vec2 px = native.ClientSizePx();
    if (px.x <= 0.f || px.y <= 0.f) return;   // 最小化：不进帧

    device.Resize(px);

    Element* root = window.Root();
    if (root->SubtreeDirty()) {
        if (SubtreeLayoutDirty(root)) RunLayout(root, native.ClientSizeDip());
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
    collector.Collect(root, StyleContext{});
    collector.Finish();

    for (text::AtlasPage& page : atlas.Pages()) {
        if (page.dirty) {
            device.UploadGlyphPage(page.id, page.w, page.h, page.bits.data());
            page.dirty = false;
        }
    }

    device.Render(list.Units().data(), static_cast<uint32_t>(list.Units().size()));
    device.Present();
}

}  // namespace dui
