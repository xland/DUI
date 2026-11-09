// ============================================================================
//  DUI/Graphics/SvgRaster.cpp — SVG 光栅化（Direct2D，Win10 1703+）
//
//  WIC 没有系统内置 SVG 解码器；Windows 的内置能力是 Direct2D 的
//  ID2D1DeviceContext5::CreateSvgDocument / DrawSvgDocument（d2d1_3.h）。
//  本文件把 SVG 按目标像素尺寸画成 BGRA8 straight alpha 的 DecodedImage，
//  再交给现有 TextureStore / Image 批 —— 渲染核心零改动。
//
//  D2D 位图约定（D2D1_BITMAP_OPTIONS）：CPU_READ 不能与 TARGET 组合，故
//  先画到 TARGET 位图，再 CopyFromBitmap 到 CPU_READ 位图后 Map 读回。
//  D2D 输出为预乘 alpha，须转回 straight（PS 里再预乘，ADR-013）。
// ============================================================================

#include "Graphics/ImageCodec.h"

#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi.h>
#include <objidl.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

// D2D / D3D11 / shlwapi 是系统库，随本编译单元携带链接指令
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "shlwapi.lib")

namespace dui::graphics {

namespace {

void SetErr(String* err, const wchar_t* msg) {
    if (err != nullptr) *err = msg;
}

// D2D 设备 + DeviceContext5（SVG API 只挂在 ID2D1DeviceContext5 上）。
// 进程级单例，与 WicFactory() 同风格：进程退出由系统回收，不配对释放。
struct SvgContext {
    ID3D11Device*          d3d  = nullptr;
    IDXGIDevice*           dxgi = nullptr;
    ID2D1Device*           d2d  = nullptr;
    ID2D1DeviceContext5*   dc   = nullptr;
};

SvgContext& SvgCtx() {
    static SvgContext c = []() {
        SvgContext s;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, &s.d3d, nullptr, nullptr))) return s;
        s.d3d->QueryInterface(IID_PPV_ARGS(&s.dxgi));
        if (s.dxgi == nullptr) return s;
        if (FAILED(D2D1CreateDevice(s.dxgi, nullptr, &s.d2d))) return s;
        ID2D1DeviceContext* ctx = nullptr;
        s.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx);
        if (ctx == nullptr) return s;
        ctx->QueryInterface(IID_PPV_ARGS(&s.dc));
        ctx->Release();
        return s;
    }();
    return c;
}

// 读取根 <svg> 标签里的一个数值属性（"24" / "24px" 都取前导数值）
float ReadNumberAttr(const std::string& tag, const char* name) {
    const size_t p = tag.find(name);
    if (p == std::string::npos) return 0.f;
    const size_t q = tag.find_first_of("\"'", p);
    if (q == std::string::npos) return 0.f;
    float v = 0.f;
    if (::sscanf_s(tag.c_str() + q + 1, "%f", &v) != 1) return 0.f;
    return v;
}

// 预乘 → straight（PS 里再预乘，与 WIC 解码结果一致）
void Unpremultiply(uint8_t* p) {
    const uint8_t a = p[3];
    if (a == 0) {
        p[0] = p[1] = p[2] = 0;
    } else if (a != 255) {
        for (int i = 0; i < 3; ++i) {
            const unsigned c = static_cast<unsigned>(p[i]) * 255u + a / 2u;
            p[i] = static_cast<uint8_t>(std::min(255u, c / a));
        }
    }
}

}  // namespace

bool ReadSvgFile(const wchar_t* path, std::vector<uint8_t>& out, String* err) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SetErr(err, L"无法打开 SVG 文件");
        return false;
    }
    const DWORD n = GetFileSize(h, nullptr);
    if (n == 0 || n == INVALID_FILE_SIZE) {
        CloseHandle(h);
        SetErr(err, L"SVG 文件为空或无法读取");
        return false;
    }
    out.resize(n);
    DWORD read = 0;
    const BOOL ok = ReadFile(h, out.data(), n, &read, nullptr);
    CloseHandle(h);
    if (!ok || read != n) {
        SetErr(err, L"无法读取 SVG 文件");
        return false;
    }
    return true;
}

bool ParseSvgIntrinsicSize(const void* data, uint32_t size, Vec2& outDip) {
    if (data == nullptr || size == 0) return false;
    // 只读开头一小段：根标签必在文件头部，无需拷贝整个文档
    const uint32_t headLen = size < 4096u ? size : 4096u;
    const std::string head(static_cast<const char*>(data), headLen);
    const size_t p = head.find("<svg");
    if (p == std::string::npos) return false;
    const size_t e = head.find('>', p);
    if (e == std::string::npos) return false;
    const std::string tag = head.substr(p, e - p);

    // 优先 viewBox：图标通常只有它（width/height 常省略）
    const size_t v = tag.find("viewBox");
    if (v != std::string::npos) {
        const size_t q = tag.find_first_of("\"'", v);
        if (q != std::string::npos) {
            float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
            if (::sscanf_s(tag.c_str() + q + 1, "%f %f %f %f", &x, &y, &w, &h) == 4 &&
                w > 0.f && h > 0.f) {
                outDip = Vec2{w, h};
                return true;
            }
        }
    }
    const float w = ReadNumberAttr(tag, "width=");
    const float h = ReadNumberAttr(tag, "height=");
    if (w > 0.f && h > 0.f) {
        outDip = Vec2{w, h};
        return true;
    }
    return false;
}

bool RasterizeSvg(const void* data, uint32_t size, uint32_t pxW, uint32_t pxH,
                  DecodedImage& out, String* err) {
    SvgContext& c = SvgCtx();
    if (c.dc == nullptr) {
        SetErr(err, L"Direct2D SVG 不可用（需要 Windows 10 1703 或更高版本）");
        return false;
    }
    if (pxW == 0 || pxH == 0) {
        SetErr(err, L"SVG 光栅尺寸为 0");
        return false;
    }

    IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(data), size);
    if (stream == nullptr) {
        SetErr(err, L"无法创建 SVG 输入流");
        return false;
    }
    ID2D1SvgDocument* doc = nullptr;
    HRESULT hr = c.dc->CreateSvgDocument(
        stream, D2D1::SizeF(static_cast<float>(pxW), static_cast<float>(pxH)), &doc);
    stream->Release();
    if (FAILED(hr) || doc == nullptr) {
        SetErr(err, L"SVG 解析失败");
        return false;
    }

    D2D1_BITMAP_PROPERTIES1 bp{};
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    ID2D1Bitmap1* target = nullptr;
    hr = c.dc->CreateBitmap(D2D1::SizeU(pxW, pxH), nullptr, 0, &bp, &target);
    if (FAILED(hr)) {
        doc->Release();
        SetErr(err, L"无法创建 SVG 渲染目标");
        return false;
    }

    const D2D1_COLOR_F clear{0.f, 0.f, 0.f, 0.f};
    c.dc->SetTarget(target);
    c.dc->BeginDraw();
    c.dc->Clear(&clear);
    c.dc->DrawSvgDocument(doc);
    hr = c.dc->EndDraw();
    c.dc->SetTarget(nullptr);
    doc->Release();
    if (FAILED(hr)) {
        target->Release();
        SetErr(err, L"SVG 绘制失败");
        return false;
    }

    // CPU_READ 不能与 TARGET 组合：拷到一张只读位图再 Map
    D2D1_BITMAP_PROPERTIES1 rp = bp;
    rp.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ;
    ID2D1Bitmap1* readable = nullptr;
    hr = c.dc->CreateBitmap(D2D1::SizeU(pxW, pxH), nullptr, 0, &rp, &readable);
    if (SUCCEEDED(hr)) hr = readable->CopyFromBitmap(nullptr, target, nullptr);
    target->Release();
    if (FAILED(hr)) {
        if (readable != nullptr) readable->Release();
        SetErr(err, L"无法读回 SVG 像素");
        return false;
    }

    D2D1_MAPPED_RECT mapped{};
    hr = readable->Map(D2D1_MAP_OPTIONS_READ, &mapped);
    if (FAILED(hr)) {
        readable->Release();
        SetErr(err, L"无法映射 SVG 位图");
        return false;
    }
    out.width = pxW;
    out.height = pxH;
    out.pixels.assign(static_cast<size_t>(pxW) * pxH * 4u, 0);
    for (uint32_t y = 0; y < pxH; ++y) {
        uint8_t* row = out.pixels.data() + static_cast<size_t>(y) * pxW * 4u;
        std::memcpy(row, mapped.bits + static_cast<size_t>(y) * mapped.pitch,
                    static_cast<size_t>(pxW) * 4u);
        for (uint32_t x = 0; x < pxW; ++x) Unpremultiply(row + x * 4u);
    }
    readable->Unmap();
    readable->Release();
    return true;
}

}  // namespace dui::graphics
