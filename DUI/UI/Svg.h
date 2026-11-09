// ============================================================================
//  DUI/UI/Svg.h — SVG 控件（矢量图标）
//
//  继承 dui::Image：复用纹理句柄、contain 等比绘制、Opacity / Padding / 样式盒，
//  只负责按"显示尺寸 × DPI 缩放"重新光栅（矢量在任何尺寸下都清晰）。
//  解析与光栅走 Direct2D 内置 SVG（Win10 1703+），结果经 TextureStore 上传。
//
//  D2D 只支持图标级 SVG 子集（path/形状/渐变/transform 等）：不支持
//  <text>/<filter>/<mask>/<pattern>/CSS/外部资源，遇到不支持的元素静默忽略；
//  1703 上还不支持带 px 等绝对单位的长度值（1709 起才支持）。
//  详见 Doc/Arch/16-Image.md。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "UI/Image.h"

namespace dui {

class Svg : public Image {
public:
    // 从文件 / 内存加载；失败返回 false（原因走已有的 LastError()）。
    // 固有尺寸取根 <svg> 的 viewBox（其次 width/height）；没有时默认 24×24 DIP，
    // 也可以用 Style().Width()/Height() 显式指定。
    bool LoadSvgFile(const wchar_t* path);
    bool LoadSvgFile(const String& path) { return LoadSvgFile(path.c_str()); }
    bool LoadSvgMemory(const void* data, uint32_t size);

    // 固有尺寸（DIP，viewBox）；未加载时为 0
    Vec2 IntrinsicSize() const noexcept { return intrinsic_; }

private:
    Vec2 MeasureContent(const StyleContext& ctx, const Vec2& avail) const override;
    void OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) override;
    void EnsureRaster(const StyleContext& ctx, float scale);

    std::vector<uint8_t> data_;          // 原始 SVG 文档（重光栅用）
    Vec2     intrinsic_{};               // viewBox 固有尺寸（DIP）
    uint32_t rasterW_ = 0;               // 上次光栅的像素尺寸
    uint32_t rasterH_ = 0;
};

}  // namespace dui
