// ============================================================================
//  DUI/UI/Image.cpp — 图像控件实现
//
//  加载路径：WIC 解码（Graphics/ImageCodec）→ 登记到纹理仓库
//  （graphics::TextureStore）→ App 层每帧把脏纹理上传到 GPU。
//  控件本身只持有纹理句柄，不碰 D3D（分层：UI → Graphics）。
//  动画 GIF：帧序列留在 UI 层，节拍由 App 层定时器驱动；切帧只替换同一张
//  纹理的像素并标脏，不重复 Register（避免每帧新建 GPU 纹理）。
// ============================================================================

#include "UI/Image.h"

#include "Graphics/Graphics.h"
#include "Graphics/ImageCodec.h"

#include <utility>

namespace dui {

namespace {

const graphics::ImageTexture* AsTexture(const std::shared_ptr<void>& p) noexcept {
    return static_cast<const graphics::ImageTexture*>(p.get());
}

}  // namespace

// 动画状态：帧序列 + 当前帧 + 下一次切帧的绝对时刻（首次 tick 时初始化）
struct Animation {
    std::vector<graphics::AnimationFrame> frames;
    size_t   index = 0;
    uint64_t nextTickMs = 0;
};

AnimationClock& AnimationClock::Instance() {
    static AnimationClock clock;
    return clock;
}

void AnimationClock::Add(Image* img) {
    images_.push_back(img);
}

void AnimationClock::Remove(Image* img) noexcept {
    for (size_t i = 0; i < images_.size(); ++i) {
        if (images_[i] == img) {
            images_.erase(images_.begin() + i);
            return;
        }
    }
}

void AnimationClock::Tick(uint64_t nowMs) {
    for (Image* img : images_) img->AdvanceAnimation(nowMs);
}

Image::Image() = default;

Image::~Image() { StopAnimation(); }

bool Image::LoadFile(const wchar_t* path) {
    graphics::DecodedImage img;
    String err;
    if (!graphics::DecodeImageFile(path, img, &err)) {
        lastError_ = err;
        return false;
    }
    StopAnimation();
    tex_ = graphics::TextureStore::Instance().Register(img.width, img.height,
                                                       std::move(img.pixels));
    lastError_.clear();
    MarkDirty(kFlagLayout | kFlagPaint);
    return true;
}

bool Image::LoadAnimatedFile(const wchar_t* path) {
    graphics::DecodedAnimation a;
    String err;
    if (!graphics::DecodeAnimatedImageFile(path, a, &err)) {
        lastError_ = err;
        return false;
    }
    StopAnimation();

    auto anim = std::make_unique<Animation>();
    anim->frames = std::move(a.frames);
    // 首帧登记纹理；后续帧在 AdvanceAnimation 里覆盖同一张纹理的像素
    tex_ = graphics::TextureStore::Instance().Register(
        a.width, a.height, std::vector<uint8_t>(anim->frames[0].pixels));
    anim_ = std::move(anim);
    if (anim_->frames.size() > 1) AnimationClock::Instance().Add(this);
    lastError_.clear();
    MarkDirty(kFlagLayout | kFlagPaint);
    return true;
}

bool Image::LoadMemory(const void* data, uint32_t size) {
    graphics::DecodedImage img;
    String err;
    if (!graphics::DecodeImageMemory(data, size, img, &err)) {
        lastError_ = err;
        return false;
    }
    StopAnimation();
    tex_ = graphics::TextureStore::Instance().Register(img.width, img.height,
                                                       std::move(img.pixels));
    lastError_.clear();
    MarkDirty(kFlagLayout | kFlagPaint);
    return true;
}

void Image::Clear() {
    StopAnimation();
    tex_.reset();
    MarkDirty(kFlagLayout | kFlagPaint);
}

void Image::StopAnimation() {
    if (anim_) AnimationClock::Instance().Remove(this);
    anim_.reset();
}

void Image::AdvanceAnimation(uint64_t nowMs) {
    if (anim_ == nullptr || tex_ == nullptr) return;
    Animation& a = *anim_;
    if (a.nextTickMs == 0) {
        a.nextTickMs = nowMs + a.frames[a.index].delayMs;   // 首帧先按自己的延迟显示
        return;
    }
    if (nowMs < a.nextTickMs) return;
    a.index = (a.index + 1) % a.frames.size();
    auto* t = static_cast<graphics::ImageTexture*>(tex_.get());
    t->pixels = a.frames[a.index].pixels;   // 全画布 BGRA8，尺寸恒定
    t->dirty = true;
    MarkDirty(kFlagPaint);
    a.nextTickMs = nowMs + a.frames[a.index].delayMs;
}

uint32_t Image::PixelWidth() const noexcept {
    const graphics::ImageTexture* t = AsTexture(tex_);
    return t != nullptr ? t->width : 0;
}

uint32_t Image::PixelHeight() const noexcept {
    const graphics::ImageTexture* t = AsTexture(tex_);
    return t != nullptr ? t->height : 0;
}

Vec2 Image::MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const {
    (void)contentAvail;   // 图像尺寸由自身像素决定，不受可用空间约束
    const graphics::ImageTexture* t = AsTexture(tex_);
    if (t == nullptr) return Vec2{};
    // 像素 → DIP：除以 DPI 缩放，使图像在 100% 下 1 像素 = 1 物理像素
    const float s = ctx.scale > 0.f ? ctx.scale : 1.f;
    return Vec2{static_cast<float>(t->width) / s, static_cast<float>(t->height) / s};
}

void Image::OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) {
    const graphics::ImageTexture* t = AsTexture(tex_);
    if (t == nullptr) return;
    // 内容盒 = 元素盒去掉 padding（与布局的 content box 一致）
    const StyleValue pv = ResolveProperty(this, Prop::Padding, ctx);
    const Rect b = Bounds();
    const Rect content{b.x + pv.f, b.y + pv.extra[0],
                       b.w - pv.f - pv.extra[1], b.h - pv.extra[0] - pv.extra[2]};
    if (content.w <= 0.f || content.h <= 0.f) return;

    // 等比缩放并居中（object-fit: contain 语义）：盒子与自然比例不同时
    // （显式 Width/Height、空间不足被压缩）图像内容不拉伸变形
    const float natW = static_cast<float>(t->width) / scale;   // 自然尺寸（DIP）
    const float natH = static_cast<float>(t->height) / scale;
    float w = content.w;
    float h = content.h;
    if (natW > 0.f && natH > 0.f) {
        const float kx = content.w / natW;
        const float ky = content.h / natH;
        const float k = kx < ky ? kx : ky;
        w = natW * k;
        h = natH * k;
    }
    const Rect dst{content.x + (content.w - w) * 0.5f,
                   content.y + (content.h - h) * 0.5f, w, h};

    // 透明度：Prop::Opacity（默认 1）
    float opacity = ResolveProperty(this, Prop::Opacity, ctx).f;
    if (opacity < 0.f) opacity = 0.f;
    if (opacity > 1.f) opacity = 1.f;
    const uint32_t a = static_cast<uint32_t>(opacity * 255.f + 0.5f);
    // 颜色约定 0xAARRGGBB（见 AddGlyphQuad 的打包）：RGB 取白，alpha = 不透明度
    const uint32_t tint = (a << 24) | 0x00FFFFFFu;
    p.AddImageQuad(t->id, dst, tint, scale);
}

}  // namespace dui
