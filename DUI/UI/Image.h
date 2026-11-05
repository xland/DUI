// ============================================================================
//  DUI/UI/Image.h — 图像控件（Image）
//
//  显示一张位图。解码走 Windows 内置解码器（WIC）：PNG / JPEG / BMP /
//  GIF / TIFF / ICO 等 —— 即"Windows 默认能解码"的格式；
//  Windows 解不了的格式（WebP / AVIF 等）不支持。
//
//  尺寸：自然尺寸 = 图像像素尺寸 ÷ 当前 DPI 缩放（保证 1 图像像素 =
//  1 物理像素，跨 DPI 不糊）；显式给 Width/Height 时按 contain 等比居中。
//  动画：LoadAnimatedFile 解出 GIF 全部帧，按帧延迟在同一张纹理上换帧播放。
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "UI/Element.h"

namespace dui {

struct Animation;   // UI/Image.cpp 内部：帧序列与播放游标

class Image : public Element {
public:
    Image();              // 定义放 .cpp：unique_ptr<Animation> 需要完整类型
    ~Image() override;

    // 图像元素在收集期单独成绘制单元（图像批在批序末尾，同单元会盖住文字）
    Kind GetKind() const noexcept override { return Kind::Image; }

    // 从文件加载；失败返回 false（原因见 LastError）
    bool LoadFile(const wchar_t* path);
    bool LoadFile(const String& path) { return LoadFile(path.c_str()); }
    // 加载动画 GIF（WIC 多帧 + disposal 合成）；按帧延迟自动播放
    bool LoadAnimatedFile(const wchar_t* path);
    bool LoadAnimatedFile(const String& path) { return LoadAnimatedFile(path.c_str()); }
    // 从内存加载（格式由内容嗅探）；失败返回 false
    bool LoadMemory(const void* data, uint32_t size);
    // 清除图像（GPU 纹理在下一次纹理同步时回收）
    void Clear();

    // 图像像素尺寸（未加载为 0）
    uint32_t PixelWidth() const noexcept;
    uint32_t PixelHeight() const noexcept;
    // 最近一次加载失败的原因（成功时为空）
    const String& LastError() const noexcept { return lastError_; }

protected:
    // 自然尺寸 = 像素尺寸 ÷ DPI 缩放（DIP）
    Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const override;
    // 绘制：把纹理铺满内容盒（元素盒去掉 padding）
    void OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) override;

private:
    friend class AnimationClock;
    void AdvanceAnimation(uint64_t nowMs);
    void StopAnimation();

    // graphics::ImageTexture*（pimpl：公共头不引入 windows.h / d3d11.h）
    std::shared_ptr<void> tex_;
    std::unique_ptr<Animation> anim_;   // 非空 = 持有动画帧序列
    String lastError_;
};

// UI 层统一时间源（内部）：GIF 帧动画、光标闪烁、命令式属性动画共用一个
// ticker，App 层只启停一个窗口定时器；是否需要节拍、周期多少都由本类决定。
// 进程级单例。
class AnimationClock {
public:
    static AnimationClock& Instance();
    void Add(Image* img);
    void Remove(Image* img) noexcept;
    bool NeedsTick() const noexcept;         // 有帧动画 / 属性动画 / 可闪烁的焦点元素
    unsigned TickPeriodMs() const noexcept;  // 有帧动画或属性动画 20ms；只有闪烁 500ms
    void Tick(uint64_t nowMs);
    void Reset() noexcept;                   // 停表时重置闪烁相位

private:
    std::vector<Image*> images_;
    uint64_t nextBlinkMs_ = 0;
    bool blinkStarted_ = false;
};

}  // namespace dui
