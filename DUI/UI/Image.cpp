// ============================================================================
//  DUI/UI/Image.cpp — 图像控件实现
//
//  加载路径：WIC 解码（Graphics/ImageCodec）→ 登记到纹理仓库
//  （graphics::TextureStore）→ App 层每帧把脏纹理上传到 GPU。
//  控件本身只持有纹理句柄，不碰 D3D（分层：UI → Graphics）。
//  动画 GIF：帧序列留在 UI 层，节拍由 UI 层统一 ticker 驱动（与光标闪烁共用）；
//  切帧只替换同一张纹理的像素并标脏，不重复 Register（避免每帧新建 GPU 纹理）。
// ============================================================================

#include "UI/Image.h"

#include "Graphics/Graphics.h"
#include "Graphics/ImageCodec.h"
#include "UI/Animation.h"
#include "UI/Focus.h"

#include <utility>

namespace dui {

namespace {

constexpr uint64_t kFrameTickMs = 20;    // 帧动画节拍；各帧按自身延迟到点才切
constexpr uint64_t kBlinkTickMs = 500;   // 光标闪烁半周期

const graphics::ImageTexture* AsTexture(const std::shared_ptr<void>& p) noexcept {
    return static_cast<const graphics::ImageTexture*>(p.get());
}

}  // namespace

// 动画状态：帧序列 + 当前帧 + 下一次切帧的绝对时刻（首次 tick 时初始化）
struct Animation {
    std::vector<graphics::AnimationFrame> frames;
    size_t   index = 0;
    uint64_t nextTickMs = 0;
    uint32_t loopCount = 0;    // 0 = 无限循环
    uint32_t loopsDone = 0;    // 已完成轮数（index 回绕时递增）
    bool     playing = false;  // 是否已注册到 ticker（暂停/停止/播完为 false）
};

AnimationClock& AnimationClock::Instance() {
    static AnimationClock clock;
    return clock;
}

void AnimationClock::Add(Image* img) {
    for (Image* p : images_) {
        if (p == img) return;   // 注册唯一：Play() 重复调用不会让帧速翻倍
    }
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

bool AnimationClock::NeedsTick() const noexcept {
    return !images_.empty() || NeedsBlinkTick() || detail::HasActiveAnimations();
}

unsigned AnimationClock::TickPeriodMs() const noexcept {
    // 只有闪烁时保持 500ms 唤醒，不因统一 ticker 而退化成 20ms 空转；
    // 帧动画或属性动画期间按 20ms 推进补间
    const bool fast = !images_.empty() || detail::HasActiveAnimations();
    return fast ? static_cast<unsigned>(kFrameTickMs)
                : static_cast<unsigned>(kBlinkTickMs);
}

void AnimationClock::Reset() noexcept {
    nextBlinkMs_ = 0;
    blinkStarted_ = false;
}

void AnimationClock::Tick(uint64_t nowMs) {
    // 快照：AdvanceAnimation 达到循环上限时返回 false 并被 Remove，
    // 不能在遍历 images_ 的同时修改它（与属性动画的快照同理）
    const std::vector<Image*> snapshot = images_;
    for (Image* img : snapshot) {
        if (!img->AdvanceAnimation(nowMs)) Remove(img);
    }

    // 光标闪烁：首拍即切换一次（沿用原 500ms 定时器的相位），之后按绝对时刻
    if (NeedsBlinkTick()) {
        if (!blinkStarted_ || nowMs >= nextBlinkMs_) {
            DispatchBlink();
            blinkStarted_ = true;
            nextBlinkMs_ = nowMs + kBlinkTickMs;
        }
    } else {
        blinkStarted_ = false;
        nextBlinkMs_ = 0;
    }

    // 属性动画放最后：完成回调可能启动新动画、销毁图像控件甚至整个子树
    detail::TickAnimations(nowMs);
}

Image::Image() = default;

Image::~Image() { DisposeAnimation(); }

bool Image::LoadFile(const wchar_t* path) {
    graphics::DecodedImage img;
    String err;
    if (!graphics::DecodeImageFile(path, img, &err)) {
        lastError_ = err;
        return false;
    }
    DisposeAnimation();
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
    DisposeAnimation();

    auto anim = std::make_unique<Animation>();
    anim->frames = std::move(a.frames);
    // 首帧登记纹理；后续帧在 AdvanceAnimation 里覆盖同一张纹理的像素
    tex_ = graphics::TextureStore::Instance().Register(
        a.width, a.height, std::vector<uint8_t>(anim->frames[0].pixels));
    anim_ = std::move(anim);
    if (anim_->frames.size() > 1) {
        anim_->playing = true;
        AnimationClock::Instance().Add(this);
    }
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
    DisposeAnimation();
    tex_ = graphics::TextureStore::Instance().Register(img.width, img.height,
                                                       std::move(img.pixels));
    lastError_.clear();
    MarkDirty(kFlagLayout | kFlagPaint);
    return true;
}

void Image::Clear() {
    DisposeAnimation();
    tex_.reset();
    MarkDirty(kFlagLayout | kFlagPaint);
}

void Image::DisposeAnimation() {
    if (anim_) AnimationClock::Instance().Remove(this);
    anim_.reset();
}

void Image::ApplyFrame(size_t index) {
    if (anim_ == nullptr || tex_ == nullptr) return;
    auto* t = static_cast<graphics::ImageTexture*>(tex_.get());
    t->pixels = anim_->frames[index].pixels;   // 全画布 BGRA8，尺寸恒定
    t->dirty = true;
    MarkDirty(kFlagPaint);
}

void Image::SetPixels(uint32_t w, uint32_t h, std::vector<uint8_t>&& pixels) {
    auto* t = static_cast<graphics::ImageTexture*>(tex_.get());
    if (t != nullptr && t->width == w && t->height == h) {
        t->pixels = std::move(pixels);
        t->dirty = true;
        return;
    }
    tex_ = graphics::TextureStore::Instance().Register(w, h, std::move(pixels));
}

// ---------------------------------------------------------------------------
// 播放控制原语（dui::Gif 的公共 API 转发到这里）
// ---------------------------------------------------------------------------

void Image::PauseAnimation() {
    if (anim_ == nullptr || !anim_->playing) return;
    anim_->playing = false;
    AnimationClock::Instance().Remove(this);   // 停表，暂停期间不空转
}

void Image::PlayAnimation() {
    if (anim_ == nullptr || anim_->playing || anim_->frames.size() <= 1) return;
    if (anim_->loopCount != 0 && anim_->loopsDone >= anim_->loopCount) {
        anim_->loopsDone = 0;      // 已播完设定轮数：Play 从头重播
        anim_->index = 0;
        ApplyFrame(0);
    }
    anim_->playing = true;
    anim_->nextTickMs = 0;         // 恢复时重新起算，跳过暂停时长
    AnimationClock::Instance().Add(this);
}

void Image::StopAnimation() {
    if (anim_ == nullptr) return;
    if (anim_->playing) {
        anim_->playing = false;
        AnimationClock::Instance().Remove(this);
    }
    anim_->index = 0;
    anim_->loopsDone = 0;
    anim_->nextTickMs = 0;
    ApplyFrame(0);                 // 回首帧；帧序列保留，Play() 可重播
}

void Image::SetAnimationLoopCount(uint32_t count) {
    if (anim_ != nullptr) anim_->loopCount = count;
}

bool Image::AdvanceAnimation(uint64_t nowMs) {
    if (anim_ == nullptr || tex_ == nullptr || !anim_->playing) return false;
    Animation& a = *anim_;
    if (a.nextTickMs == 0) {
        a.nextTickMs = nowMs + a.frames[a.index].delayMs;   // 首拍先按当前帧延迟显示
        return true;
    }
    if (nowMs < a.nextTickMs) return true;

    const size_t next = a.index + 1;
    if (next >= a.frames.size()) {
        ++a.loopsDone;
        if (a.loopCount != 0 && a.loopsDone >= a.loopCount) {
            a.playing = false;     // 播完停在最后一帧；返回 false 让 ticker 注销
            return false;
        }
    }
    a.index = next % a.frames.size();
    ApplyFrame(a.index);
    a.nextTickMs = nowMs + a.frames[a.index].delayMs;
    return true;
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
