// ============================================================================
//  DUI/Core/Clipboard.cpp — 系统剪贴板的 Win32 实现
// ============================================================================

#include "Core/Clipboard.h"

#include <cstring>

#include <windows.h>

namespace dui {

namespace {

// mime → 剪贴板格式 ID。文本不走这里（ReadText/WriteText 用 CF_UNICODETEXT）。
// "image/png" → 注册名 "PNG"（浏览器 / Office 写入剪贴板的既定注册名）；
// "image/dib" → CF_DIB；未知 mime 原样当注册格式名（mime 是 ASCII，逐字符拷贝）
UINT FormatOf(const char* mime) {
    if (mime == nullptr || mime[0] == '\0') return 0;
    if (std::strcmp(mime, "image/png") == 0) return RegisterClipboardFormatW(L"PNG");
    if (std::strcmp(mime, "image/dib") == 0) return CF_DIB;
    wchar_t name[128];
    size_t i = 0;
    for (; mime[i] != '\0' && i < 127; ++i) name[i] = static_cast<wchar_t>(mime[i]);
    name[i] = L'\0';
    return RegisterClipboardFormatW(name);
}

}  // namespace

// ---------------------------------------------------------------------------
// 图像（CF_DIB ↔ BGRA8）
// ---------------------------------------------------------------------------

// CF_DIB → BGRA8（straight alpha、顶向下）。仅支持标准 BGRA 布局：
// 32bpp（BI_RGB / BI_BITFIELDS 标准掩码）与 24bpp（BI_RGB）；
// 系统会从 CF_DIBV5 自动合成 CF_DIB，故 DIBV5 来源同样覆盖
bool DecodeDib(const void* data, SIZE_T size, Clipboard::Image& out) {
    if (data == nullptr || size < sizeof(BITMAPINFOHEADER)) return false;
    const auto* bi = static_cast<const BITMAPINFOHEADER*>(data);
    const int32_t height = bi->biHeight;
    const uint32_t width = static_cast<uint32_t>(bi->biWidth);
    const uint32_t bpp = static_cast<uint32_t>(bi->biBitCount);
    if (width == 0 || height == 0 || width > 0x100000u) return false;
    if (bpp != 32 && bpp != 24) return false;
    if (bi->biCompression != BI_RGB && bi->biCompression != BI_BITFIELDS) return false;

    const bool topDown = height < 0;
    const uint32_t rows = topDown ? static_cast<uint32_t>(-height) : static_cast<uint32_t>(height);
    const SIZE_T stride = (static_cast<SIZE_T>(width) * bpp + 31) / 32 * 4;   // 行 4 字节对齐
    SIZE_T offset = static_cast<SIZE_T>(bi->biSize);
    if (bi->biClrUsed != 0) {
        offset += static_cast<SIZE_T>(bi->biClrUsed) * 4;
    } else if (bpp <= 8) {
        offset += (SIZE_T(1) << bpp) * 4;
    }
    if (bi->biCompression == BI_BITFIELDS && bi->biSize == sizeof(BITMAPINFOHEADER)) {
        offset += 12;   // V1 头的三个掩码 DWORD 跟在头后
    }
    if (offset >= size) return false;

    const uint8_t* base = static_cast<const uint8_t*>(data) + offset;
    if (static_cast<SIZE_T>(rows) * stride > size - offset) return false;

    out.width = width;
    out.height = rows;
    out.pixels.resize(static_cast<size_t>(width) * rows * 4);

    for (uint32_t y = 0; y < rows; ++y) {
        const uint32_t srcRow = topDown ? y : rows - 1 - y;   // 底向上 DIB → 顶向下像素
        const uint8_t* src = base + static_cast<SIZE_T>(srcRow) * stride;
        uint8_t* dst = out.pixels.data() + static_cast<size_t>(y) * width * 4;
        if (bpp == 32) {
            std::memcpy(dst, src, static_cast<size_t>(width) * 4);
        } else {
            for (uint32_t x = 0; x < width; ++x) {
                dst[x * 4 + 0] = src[x * 3 + 0];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        }
    }

    // BI_RGB DIB 的 alpha 常不被写源填写（全 0）→ 全 0 视为不透明
    if (bpp == 32) {
        bool alphaUsed = false;
        for (size_t i = 3; i < out.pixels.size(); i += 4) {
            if (out.pixels[i] != 0) { alphaUsed = true; break; }
        }
        if (!alphaUsed) {
            for (size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = 255;
        }
    }
    return true;
}

bool Clipboard::ReadImage(Image& out) {
    out = Image{};
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_DIB)) {
        if (void* p = GlobalLock(h)) {
            ok = DecodeDib(p, GlobalSize(h), out);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return ok;
}

bool Clipboard::WriteImage(const Image& img) {
    if (img.width == 0 || img.height == 0) return false;
    const SIZE_T pixelBytes = static_cast<SIZE_T>(img.width) * img.height * 4;
    if (img.pixels.size() < pixelBytes) return false;

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = static_cast<LONG>(img.width);
    bi.biHeight = -static_cast<LONG>(img.height);   // 负高 = 顶向下
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    const SIZE_T total = sizeof(bi) + pixelBytes;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, total);
    if (mem == nullptr) return false;
    if (void* p = GlobalLock(mem)) {
        std::memcpy(p, &bi, sizeof(bi));
        std::memcpy(static_cast<uint8_t*>(p) + sizeof(bi), img.pixels.data(), pixelBytes);
        GlobalUnlock(mem);
    } else {
        GlobalFree(mem);
        return false;
    }
    if (!OpenClipboard(nullptr)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    // SetClipboardData 成功后系统接管 mem；失败需自行释放
    const bool ok = SetClipboardData(CF_DIB, mem) != nullptr;
    if (!ok) GlobalFree(mem);
    CloseClipboard();
    return ok;
}

// ---------------------------------------------------------------------------
// 文本（CF_UNICODETEXT）
// ---------------------------------------------------------------------------

bool Clipboard::ReadText(String& out) {
    out.clear();
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

bool Clipboard::WriteText(const String& text) {
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem == nullptr) return false;
    if (void* p = GlobalLock(mem)) {
        std::memcpy(p, text.c_str(), bytes);
        GlobalUnlock(mem);
    } else {
        GlobalFree(mem);
        return false;
    }
    if (!OpenClipboard(nullptr)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    // SetClipboardData 成功后系统接管 mem；失败需自行释放
    const bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    if (!ok) GlobalFree(mem);
    CloseClipboard();
    return ok;
}

// ---------------------------------------------------------------------------
// 任意格式（mime + 字节载荷）
// ---------------------------------------------------------------------------

bool Clipboard::HasFormat(const char* mime) {
    const UINT fmt = FormatOf(mime);
    if (fmt == 0) return false;
    return IsClipboardFormatAvailable(fmt) != FALSE;
}

bool Clipboard::ReadFormat(const char* mime, std::vector<uint8_t>& bytes) {
    bytes.clear();
    const UINT fmt = FormatOf(mime);
    if (fmt == 0) return false;
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(fmt)) {
        if (void* p = GlobalLock(h)) {
            // GlobalSize 是分配粒度（可能略大于逻辑长度），读取按上界安全
            const SIZE_T size = GlobalSize(h);
            bytes.assign(static_cast<const uint8_t*>(p),
                         static_cast<const uint8_t*>(p) + size);
            GlobalUnlock(h);
            ok = !bytes.empty();
        }
    }
    CloseClipboard();
    return ok;
}

bool Clipboard::WriteFormat(const char* mime, const void* data, uint32_t size) {
    if (data == nullptr || size == 0) return false;
    const UINT fmt = FormatOf(mime);
    if (fmt == 0) return false;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (mem == nullptr) return false;
    if (void* p = GlobalLock(mem)) {
        std::memcpy(p, data, size);
        GlobalUnlock(mem);
    } else {
        GlobalFree(mem);
        return false;
    }
    if (!OpenClipboard(nullptr)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(fmt, mem) != nullptr;
    if (!ok) GlobalFree(mem);
    CloseClipboard();
    return ok;
}

}  // namespace dui
