// ============================================================================
//  DUI/Graphics/ImageCodec.cpp — WIC 解码实现
//
//  WIC（Windows Imaging Component）是系统自带的图像解码器，覆盖 Windows
//  能原生打开的格式；无需额外依赖（wincodec.lib 是系统库）。
//
//  注意：
//    · WIC 需要 COM 已初始化 —— 此处做一次进程级 CoInitializeEx（不
//      CoUninitialize：进程级单例语义，进程退出时由系统回收）。
//    · 静态接口只取第 0 帧；动画接口解出 GIF 全部帧并做 disposal 合成。
//    · 统一转 32bppBGRA（**非预乘** straight alpha）—— 预乘在 GPU 片元
//      着色器里做，与矩形/字形/形状批的约定一致（ADR-013）。
// ============================================================================

#include "Graphics/ImageCodec.h"

#include <objidl.h>
#include <propidl.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <utility>

// WIC 是系统库（不在 MSVC 默认链接列表里）。SDK 提供的是 windowscodecs.lib
// （wincodec.lib 不存在）；它同时提供 CLSID_WICImagingFactory / IID_* 符号。
// 用 #pragma comment(lib) 随本编译单元携带链接指令 —— 不必改根目录 DUI.props
// 的公共编译基线。
#pragma comment(lib, "windowscodecs.lib")

namespace dui::graphics {

namespace {

void SetErr(String* err, const wchar_t* msg) {
    if (err != nullptr) *err = msg;
}

// WIC 工厂（进程级；首次调用时创建）
IWICImagingFactory* WicFactory() {
    static IWICImagingFactory* factory = []() -> IWICImagingFactory* {
        // COM 初始化：WIC 依赖。已在 STA 模式下初始化时返回 RPC_E_CHANGED_MODE，
        // 但 COM 本身已可用 → 不影响后续工厂创建。
        static const HRESULT comInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        (void)comInit;
        IWICImagingFactory* f = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) {
            return nullptr;
        }
        return f;
    }();
    return factory;
}

// 帧 → BGRA8 像素
bool DecodeFrame(IWICBitmapFrameDecode* frame, DecodedImage& out, String* err) {
    IWICImagingFactory* factory = WicFactory();
    if (factory == nullptr) {
        SetErr(err, L"WIC factory unavailable");
        return false;
    }
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) {
        SetErr(err, L"invalid image size");
        return false;
    }
    IWICFormatConverter* conv = nullptr;
    if (FAILED(factory->CreateFormatConverter(&conv))) {
        SetErr(err, L"CreateFormatConverter failed");
        return false;
    }
    HRESULT hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        conv->Release();
        SetErr(err, L"format conversion failed");
        return false;
    }
    out.width = w;
    out.height = h;
    out.pixels.assign(static_cast<size_t>(w) * h * 4, 0);
    const UINT stride = w * 4;
    hr = conv->CopyPixels(nullptr, stride, static_cast<UINT>(out.pixels.size()),
                          out.pixels.data());
    conv->Release();
    if (FAILED(hr)) {
        SetErr(err, L"CopyPixels failed");
        return false;
    }
    return true;
}

// 解码器 → 第 0 帧 → 像素
bool DecodeDecoder(IWICBitmapDecoder* decoder, DecodedImage& out, String* err) {
    IWICBitmapFrameDecode* frame = nullptr;
    const HRESULT hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || frame == nullptr) {
        SetErr(err, L"no frame in image");
        return false;
    }
    const bool ok = DecodeFrame(frame, out, err);
    frame->Release();
    return ok;
}

// 读标量元数据。GIF 的 Delay / Disposal / 帧矩形都是标量（VT_UI1/UI2/UI4），
// 不产生分配，故无需 PropVariantClear。
uint32_t MetaUInt(IWICMetadataQueryReader* qr, const wchar_t* path, uint32_t fallback) {
    PROPVARIANT v{};
    if (FAILED(qr->GetMetadataByName(path, &v))) return fallback;
    uint32_t r = fallback;
    switch (v.vt) {
    case VT_UI1: r = v.bVal;   break;
    case VT_UI2: r = v.uiVal;  break;
    case VT_UI4: r = v.ulVal;  break;
    default: break;
    }
    return r;
}

// GIF 多帧 → 逐帧合成到逻辑屏幕（disposal 0/1 保留、2 清背景、3 恢复前一帧）
bool DecodeAnimation(IWICBitmapDecoder* decoder, DecodedAnimation& out, String* err) {
    UINT count = 0;
    if (FAILED(decoder->GetFrameCount(&count)) || count == 0) {
        SetErr(err, L"no frame in animation");
        return false;
    }

    // 逻辑屏幕尺寸优先取容器元数据（GIF 帧可以只是局部矩形）
    uint32_t sw = 0, sh = 0;
    IWICMetadataQueryReader* global = nullptr;
    if (SUCCEEDED(decoder->GetMetadataQueryReader(&global))) {
        sw = MetaUInt(global, L"/logscrdesc/Width", 0);
        sh = MetaUInt(global, L"/logscrdesc/Height", 0);
        global->Release();
    }
    if (sw == 0 || sh == 0) {
        IWICBitmapFrameDecode* f0 = nullptr;
        if (SUCCEEDED(decoder->GetFrame(0, &f0))) {
            UINT w = 0, h = 0;
            f0->GetSize(&w, &h);
            f0->Release();
            sw = w;
            sh = h;
        }
    }
    if (sw == 0 || sh == 0) {
        SetErr(err, L"invalid logical screen size");
        return false;
    }

    std::vector<uint8_t> canvas(static_cast<size_t>(sw) * sh * 4, 0);
    std::vector<uint8_t> snapshot;
    uint32_t prevDisposal = 0;
    uint32_t prevX = 0, prevY = 0, prevW = 0, prevH = 0;

    for (UINT i = 0; i < count; ++i) {
        IWICBitmapFrameDecode* frame = nullptr;
        if (FAILED(decoder->GetFrame(i, &frame)) || frame == nullptr) {
            SetErr(err, L"cannot read animation frame");
            return false;
        }
        uint32_t delay10 = 10, disposal = 0, left = 0, top = 0;
        IWICMetadataQueryReader* qr = nullptr;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&qr))) {
            delay10 = MetaUInt(qr, L"/grctlext/Delay", 10);   // 单位 10ms
            disposal = MetaUInt(qr, L"/grctlext/Disposal", 0);
            left = MetaUInt(qr, L"/imgdesc/Left", 0);
            top = MetaUInt(qr, L"/imgdesc/Top", 0);
            qr->Release();
        }
        DecodedImage img;
        const bool ok = DecodeFrame(frame, img, err);
        frame->Release();
        if (!ok) return false;

        // 处置上一帧
        if (prevDisposal == 2) {
            for (uint32_t y = 0; y < prevH; ++y) {
                std::memset(&canvas[(static_cast<size_t>(prevY + y) * sw + prevX) * 4],
                           0, static_cast<size_t>(prevW) * 4);
            }
        } else if (prevDisposal == 3) {
            canvas = snapshot;
        }
        snapshot = canvas;   // 叠加本帧前的状态（供下一帧 disposal=3 恢复）

        // WIC 帧已是逻辑屏幕全尺寸时从 (0,0) 覆盖；局部帧按 imgdesc 偏移叠加。
        // GIF 像素只有全透明 / 全不透明：透明处保留画布上的历史内容。
        const bool fullFrame = (img.width == sw && img.height == sh);
        const uint32_t ox = fullFrame ? 0 : left;
        const uint32_t oy = fullFrame ? 0 : top;
        const uint32_t cw = sw > ox ? std::min<uint32_t>(img.width, sw - ox) : 0;
        const uint32_t ch = sh > oy ? std::min<uint32_t>(img.height, sh - oy) : 0;
        for (uint32_t y = 0; y < ch; ++y) {
            for (uint32_t x = 0; x < cw; ++x) {
                const uint8_t* src = &img.pixels[(static_cast<size_t>(y) * img.width + x) * 4];
                if (src[3] == 0) continue;
                std::memcpy(&canvas[(static_cast<size_t>(oy + y) * sw + ox + x) * 4],
                            src, 4);
            }
        }

        AnimationFrame af;
        af.pixels = canvas;
        const uint32_t ms = delay10 * 10;
        af.delayMs = ms < 20 ? 100u : ms;   // Delay 0/1 按浏览器惯例兜到 100ms
        out.frames.push_back(std::move(af));

        prevDisposal = disposal;
        prevX = ox; prevY = oy; prevW = cw; prevH = ch;
    }
    out.width = sw;
    out.height = sh;
    return true;
}

}  // namespace

bool DecodeImageFile(const wchar_t* path, DecodedImage& out, String* err) {
    if (path == nullptr || *path == L'\0') {
        SetErr(err, L"empty path");
        return false;
    }
    IWICImagingFactory* factory = WicFactory();
    if (factory == nullptr) {
        SetErr(err, L"WIC factory unavailable");
        return false;
    }
    IWICBitmapDecoder* decoder = nullptr;
    const HRESULT hr = factory->CreateDecoderFromFilename(
        path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || decoder == nullptr) {
        SetErr(err, L"cannot open or decode image file");
        return false;
    }
    const bool ok = DecodeDecoder(decoder, out, err);
    decoder->Release();
    return ok;
}

bool DecodeImageMemory(const void* data, uint32_t size, DecodedImage& out, String* err) {
    if (data == nullptr || size == 0) {
        SetErr(err, L"empty data");
        return false;
    }
    IWICImagingFactory* factory = WicFactory();
    if (factory == nullptr) {
        SetErr(err, L"WIC factory unavailable");
        return false;
    }
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (mem == nullptr) {
        SetErr(err, L"out of memory");
        return false;
    }
    void* dst = GlobalLock(mem);
    if (dst == nullptr) {
        GlobalFree(mem);
        SetErr(err, L"out of memory");
        return false;
    }
    std::memcpy(dst, data, size);
    GlobalUnlock(mem);

    IStream* stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(mem, TRUE, &stream);   // TRUE：流释放时一并释放 mem
    if (FAILED(hr) || stream == nullptr) {
        GlobalFree(mem);
        SetErr(err, L"CreateStreamOnHGlobal failed");
        return false;
    }
    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand,
                                          &decoder);
    stream->Release();
    if (FAILED(hr) || decoder == nullptr) {
        SetErr(err, L"cannot decode image data");
        return false;
    }
    const bool ok = DecodeDecoder(decoder, out, err);
    decoder->Release();
    return ok;
}

bool DecodeAnimatedImageFile(const wchar_t* path, DecodedAnimation& out, String* err) {
    if (path == nullptr || *path == L'\0') {
        SetErr(err, L"empty path");
        return false;
    }
    IWICImagingFactory* factory = WicFactory();
    if (factory == nullptr) {
        SetErr(err, L"WIC factory unavailable");
        return false;
    }
    IWICBitmapDecoder* decoder = nullptr;
    const HRESULT hr = factory->CreateDecoderFromFilename(
        path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || decoder == nullptr) {
        SetErr(err, L"cannot open or decode animation");
        return false;
    }
    const bool ok = DecodeAnimation(decoder, out, err);
    decoder->Release();
    return ok;
}

}  // namespace dui::graphics
