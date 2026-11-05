// ============================================================================
//  DUI/Graphics/ImageCodec.cpp — WIC 解码实现
//
//  WIC（Windows Imaging Component）是系统自带的图像解码器，覆盖 Windows
//  能原生打开的格式；无需额外依赖（wincodec.lib 是系统库）。
//
//  注意：
//    · WIC 需要 COM 已初始化 —— 此处做一次进程级 CoInitializeEx（不
//      CoUninitialize：进程级单例语义，进程退出时由系统回收）。
//    · 只取第 0 帧（GIF / TIFF 的多帧与动画不做）。
//    · 统一转 32bppBGRA（**非预乘** straight alpha）—— 预乘在 GPU 片元
//      着色器里做，与矩形/字形/形状批的约定一致（ADR-013）。
// ============================================================================

#include "Graphics/ImageCodec.h"

#include <objidl.h>
#include <wincodec.h>

#include <cstring>

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

}  // namespace dui::graphics
