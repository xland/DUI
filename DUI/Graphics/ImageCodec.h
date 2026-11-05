// ============================================================================
//  DUI/Graphics/ImageCodec.h — 图像解码（Windows 内置解码器 WIC）
//
//  支持"Windows 默认能解码"的格式：PNG / JPEG / BMP / DIB / GIF（首帧）/
//  TIFF / ICO 等（取决于系统上安装的 WIC 解码器）；Windows 解不了的格式
//  （WebP / AVIF 等）不支持。
//  不做多帧动画（GIF / TIFF 只取第一帧）。
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

}  // namespace dui::graphics
