// ============================================================================
//  DUI/Graphics/ImageCodec.h — 图像解码（Windows 内置解码器 WIC）
//
//  支持"Windows 默认能解码"的格式：PNG / JPEG / BMP / DIB / GIF /
//  TIFF / ICO 等（取决于系统上安装的 WIC 解码器）；Windows 解不了的格式
//  （WebP / AVIF 等）不支持。
//  静态接口只取第 0 帧；DecodeAnimatedImageFile 解出 GIF 的全部帧（含
//  disposal 合成与帧延迟），供 UI 层做动画播放。
//
//  解码结果统一为 BGRA8（straight alpha，行序自上而下，每行 width*4 字节），
//  与 GPU 侧的 DXGI_FORMAT_B8G8R8A8_UNORM 纹理一一对应。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "Core/Core.h"

namespace dui::graphics {

// 解码结果：BGRA8（straight alpha），行序自上而下
struct DecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

// 从文件解码；失败返回 false（err 填简短原因，UTF-16）
bool DecodeImageFile(const wchar_t* path, DecodedImage& out, String* err = nullptr);
// 从内存解码（格式由内容嗅探）；失败返回 false
bool DecodeImageMemory(const void* data, uint32_t size, DecodedImage& out, String* err = nullptr);

// ---- SVG（Direct2D 内置解析，Win10 1703+；实现在 Graphics/SvgRaster.cpp）----
// 读文件字节（Win32 文件读取放 Graphics 层，UI 层不直接依赖 Windows API）
bool ReadSvgFile(const wchar_t* path, std::vector<uint8_t>& out, String* err = nullptr);
// 读根 <svg> 的固有尺寸（viewBox，其次 width/height），单位按 DIP
bool ParseSvgIntrinsicSize(const void* data, uint32_t size, Vec2& outDip);
// 按目标像素尺寸光栅成 BGRA8 straight alpha（与 WIC 解码结果同一约定）
bool RasterizeSvg(const void* data, uint32_t size, uint32_t pxW, uint32_t pxH,
                  DecodedImage& out, String* err = nullptr);

// 动画帧：已按 GIF disposal 规则合成到逻辑屏幕的全画布 BGRA8 + 本帧显示时长
struct AnimationFrame {
    std::vector<uint8_t> pixels;
    uint32_t delayMs = 100;
};

struct DecodedAnimation {
    uint32_t width = 0;                 // 逻辑屏幕尺寸（所有帧同尺寸）
    uint32_t height = 0;
    std::vector<AnimationFrame> frames;
};

// 解码动图（当前为 GIF）；失败返回 false（err 填简短原因）
bool DecodeAnimatedImageFile(const wchar_t* path, DecodedAnimation& out,
                             String* err = nullptr);

}  // namespace dui::graphics
