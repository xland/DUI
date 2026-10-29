// ============================================================================
//  DUI/App/Frame.cpp — 帧路径组装（App 层组合 UI + Text + Graphics，01-Layering §2）
// ============================================================================

#include "App/App.h"

#include "Graphics/Graphics.h"
#include "Platform/Window.h"
#include "Text/TextEngine.h"

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

// 收集：遍历可见树 → 矩形/圆角矩形/字形顶点（10-Rendering §2「收集」）。
void CollectElement(Element* e, const StyleContext& parentCtx, graphics::Painter& p, float scale) {
    if (ResolveProperty(e, Prop::Display, parentCtx).u == static_cast<uint32_t>(Display::None)) return;

    const StyleContext ctx = parentCtx.Child(e);

    const StyleValue bg = ResolveProperty(e, Prop::BackgroundColor, ctx);
    if (bg.u != 0) {
        const float radius = ResolveProperty(e, Prop::CornerRadius, ctx).f;
        float opacity = ResolveProperty(e, Prop::Opacity, ctx).f;
        if (opacity < 0.f) opacity = 0.f;
        if (opacity > 1.f) opacity = 1.f;
        p.FillRoundedRect(e->Bounds(), radius, bg.u, opacity, scale);
    }

    if (e->GetKind() == Kind::Text) DrawTextContent(e, ctx, p, scale);
    e->OnPaint(p, ctx, scale);   // 自定义绘制钩子（背景之后、子元素之前）

    for (Element* c : e->Children()) CollectElement(c, ctx, p, scale);
}

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

    graphics::Painter& p = device.GetPainter();
    p.Reset();
    CollectElement(root, StyleContext{}, p, native.Scale());
    for (text::AtlasPage& page : atlas.Pages()) {
        if (page.dirty) {
            device.UploadGlyphPage(page.id, page.w, page.h, page.bits.data());
            page.dirty = false;
        }
    }

    device.Render();
    device.Present();
}

}  // namespace dui
