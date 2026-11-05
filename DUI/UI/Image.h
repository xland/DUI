// ============================================================================
//  DUI/UI/Image.h — 图像控件（Image）
//
//  显示一张位图。解码走 Windows 内置解码器（WIC）：PNG / JPEG / BMP /
//  GIF（首帧）/ TIFF / ICO 等 —— 即"Windows 默认能解码"的格式；
//  Windows 解不了的格式（WebP / AVIF 等）不支持。
//
//  尺寸：自然尺寸 = 图像像素尺寸 ÷ 当前 DPI 缩放（保证 1 图像像素 =
//  1 物理像素，跨 DPI 不糊）；布局给了更大的盒子则拉伸绘制（线性采样）。
//  不做 object-fit 之类的适配策略（当前只有"铺满内容盒"一种）。
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>

#include "UI/Element.h"

namespace dui {

class Image : public Element {
public:
    Image() = default;
    ~Image() override;

    // 图像元素在收集期单独成绘制单元（图像批在批序末尾，同单元会盖住文字）
    Kind GetKind() const noexcept override { return Kind::Image; }

    // 从文件加载；失败返回 false（原因见 LastError）
    bool LoadFile(const wchar_t* path);
    bool LoadFile(const String& path) { return LoadFile(path.c_str()); }
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
    // graphics::ImageTexture*（pimpl：公共头不引入 windows.h / d3d11.h）
    std::shared_ptr<void> tex_;
    String lastError_;
};

}  // namespace dui
