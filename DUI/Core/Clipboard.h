// ============================================================================
//  DUI/Core/Clipboard.h — 系统剪贴板（Core 层功能服务）
//
//  功能类能力，与界面无关（2026-11-11 自 UI 层函数指针注入下沉而来）：文本 +
//  任意格式（mime 标识 + 字节载荷），图像 / 文件列表等未来格式走 ReadFormat /
//  WriteFormat，类签名不再扩展性变化。
//  Win32 实现（CF_UNICODETEXT / CF_DIB / RegisterClipboardFormat）直接落在
//  本模块 —— 「Core 不依赖 Win32」约束已取消，与 Dispatcher 的 PostMessage
//  唤醒同一先例；静态库仍按未引用裁剪（ADR-016）。
//
//  线程约定：在 UI 线程（有消息队列的线程）调用。
//  容错：失败返回 false，不报错不中断；OpenClipboard 被其他进程占用时直接
//  失败，需要重试语义由调用方自理。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "Core.h"

namespace dui {

class Clipboard {
public:
    // ---- 图像 ----
    // BGRA8 straight alpha、行序自上而下 —— 与 graphics::DecodedImage 同一约定
    //（消费侧可直接搬进 TextureStore；Core 不引用 Graphics，故自带同构结构）
    struct Image {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;   // width * height * 4 字节
    };

    // 读：取 CF_DIB（系统会从 CF_DIBV5 自动合成 CF_DIB，主流写源均携带）。
    // 仅支持 32bpp / 24bpp 标准 BGRA 布局（BI_RGB / BI_BITFIELDS 标准掩码）；
    // BI_RGB DIB 的 alpha 全 0 视为不透明（大量写源不填 alpha 通道）。
    // 写：32bpp 顶向下 BI_RGB DIB；EmptyClipboard 语义（覆盖全部内容，与
    // WriteText 一致），alpha 按 straight 写入
    static bool ReadImage(Image& out);
    static bool WriteImage(const Image& img);

    // ---- 文本（CF_UNICODETEXT；高频快捷路径）----
    static bool ReadText(String& out);
    static bool WriteText(const String& text);

    // ---- 任意格式（mime 标识 + 字节载荷）----
    // 已知映射："image/png" → 注册格式 PNG；"image/dib" → CF_DIB；
    // 其余 mime 原样作为注册格式名交给 RegisterClipboardFormat
    static bool HasFormat(const char* mime);
    static bool ReadFormat(const char* mime, std::vector<uint8_t>& bytes);
    static bool WriteFormat(const char* mime, const void* data, uint32_t size);
};

}  // namespace dui
