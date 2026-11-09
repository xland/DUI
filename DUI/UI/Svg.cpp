// ============================================================================
//  DUI/UI/Svg.cpp — SVG 控件：加载 + 按显示尺寸/DPI 重光栅
//
//  不在加载时立即光栅（此时布局未跑，还不知道显示尺寸）；推迟到 OnPaint，
//  按内容盒 DIP × scale 生成纹理。尺寸或 DPI 变化才重做，稳态零开销。
//  纹理像素同步由 App/Frame.cpp 在收集之后执行，故新纹理当帧即可上传。
// ============================================================================

#include "UI/Svg.h"

#include "Graphics/Graphics.h"
#include "Graphics/ImageCodec.h"

#include <utility>

namespace dui {

bool Svg::LoadSvgFile(const wchar_t* path) {
    std::vector<uint8_t> bytes;
    String err;
    if (!graphics::ReadSvgFile(path, bytes, &err)) {
        SetLastError(err);
        return false;
    }
    return LoadSvgMemory(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

bool Svg::LoadSvgMemory(const void* data, uint32_t size) {
    if (data == nullptr || size == 0) {
        SetLastError(L"SVG 数据为空");
        return false;
    }
    Vec2 intrinsic;
    if (!graphics::ParseSvgIntrinsicSize(data, size, intrinsic)) intrinsic = Vec2{24.f, 24.f};
    const auto* first = static_cast<const uint8_t*>(data);
    data_.assign(first, first + size);
    intrinsic_ = intrinsic;
    rasterW_ = rasterH_ = 0;                 // 强制下一帧重新光栅
    SetLastError(String{});
    MarkDirty(kFlagLayout | kFlagPaint);
    return true;
}

Vec2 Svg::MeasureContent(const StyleContext&, const Vec2&) const {
    return intrinsic_;                      // 与纹理无关：首次布局就有尺寸
}

void Svg::EnsureRaster(const StyleContext& ctx, float scale) {
    if (data_.empty()) return;
    const StyleValue pv = ResolveProperty(this, Prop::Padding, ctx);
    const Rect b = Bounds();
    const float wDip = b.w - pv.f - pv.extra[1];
    const float hDip = b.h - pv.extra[0] - pv.extra[2];
    if (wDip <= 0.f || hDip <= 0.f) return;
    const uint32_t pxW = static_cast<uint32_t>(wDip * scale + 0.5f);
    const uint32_t pxH = static_cast<uint32_t>(hDip * scale + 0.5f);
    if (pxW == rasterW_ && pxH == rasterH_) return;

    graphics::DecodedImage img;
    String err;
    if (!graphics::RasterizeSvg(data_.data(), static_cast<uint32_t>(data_.size()),
                                pxW, pxH, img, &err)) {
        SetLastError(err);
        return;
    }
    SetLastError(String{});
    SetPixels(img.width, img.height, std::move(img.pixels));
    rasterW_ = pxW;
    rasterH_ = pxH;
}

void Svg::OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) {
    EnsureRaster(ctx, scale);
    Image::OnPaint(p, ctx, scale);
}

}  // namespace dui
