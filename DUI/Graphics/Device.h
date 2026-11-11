// ============================================================================
//  DUI/Graphics/Device.h — D3D11 设备与交换链（内部头，不进 DUI.h）
//
//  为什么单独成头：Device 的成员全是 D3D11 / DXGI / DirectComposition 类型，
//  必须包含 <d3d11.h>。而 d3d11.h 会经 rpc.h → rpcndr.h 引入
//  `#define small char` 这类 MIDL 遗留宏 —— 公共头一旦带上它，业务代码里
//  任何叫 small 的变量都会变成 char（Samples/Image 就踩过这个坑）。
//  故公共的 Graphics.h 只留 Painter / 顶点指令 / TextureStore（纯 C++），
//  D3D 设备留在本内部头，由 App 层使用。
//
//  对齐已定决策：ADR-001（D3D11 + DXGI）、ADR-010（DirectComposition）、
//  ADR-013（BGRA8、预乘 Alpha、立即 Present）。
// ============================================================================
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

#include <vector>

#include "Core/Core.h"
#include "Graphics/Graphics.h"

namespace dui::graphics {

class Device {
public:
    Device() = default;
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // transparent = true → PREMULTIPLIED + 全透明清屏，背景可透视桌面（ADR-010）；
    // 默认 false → IGNORE + 不透明清屏，视觉与普通窗口一致。
    bool Init(HWND hwnd, bool transparent, String& error);

    // 幂等：尺寸未变直接返回。DPI 变化与窗口缩放共用此入口（12-Windowing §5）
    void Resize(const Vec2& clientPx);

    Painter& GetPainter() noexcept { return painter_; }
    // 清屏 + 按指令顺序发射顶点并提交（scale 用于 DIP → 物理像素换算）
    void Render(const DrawUnit* units, uint32_t unitCount, float scale);
    // 全时立即提交（ADR-013，2026-11 修订）：不等 vblank，主循环不阻塞在 VSync 上。
    void Present();

    // 字形图集页（R8）：创建或全量更新；页被 LRU 整页释放后调 Release
    void UploadGlyphPage(uint32_t id, uint32_t w, uint32_t h, const uint8_t* bits);
    void ReleaseGlyphPage(uint32_t id);

    // 图像纹理（BGRA8）：创建或全量更新；控件释放纹理后由 TextureStore 调 Release
    void UploadImage(uint32_t id, uint32_t w, uint32_t h, const uint8_t* bgra);
    void ReleaseImage(uint32_t id);

private:
    bool CreateSizeDependent(const Vec2& clientPx, String& error);
    void EnsureVertexBuffer(uint32_t vertexCount);
    ID3D11ShaderResourceView* FindGlyphSrv(uint32_t id);
    ID3D11ShaderResourceView* FindImageSrv(uint32_t id);

    ID3D11Device*           dev_ = nullptr;
    ID3D11DeviceContext*    ctx_ = nullptr;
    IDXGISwapChain1*        swap_ = nullptr;
    // DirectComposition 视觉树：所有窗口内容都经它交给 DWM 合成（ADR-010）
    IDCompositionDevice*    dcompDevice_ = nullptr;
    IDCompositionTarget*    dcompTarget_ = nullptr;
    IDCompositionVisual*    dcompVisual_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11VertexShader*     vs_ = nullptr;
    ID3D11PixelShader*      ps_ = nullptr;
    ID3D11InputLayout*      layout_ = nullptr;
    ID3D11Buffer*           vb_ = nullptr;
    ID3D11Buffer*           ib_ = nullptr;
    ID3D11Buffer*           cb_ = nullptr;
    ID3D11BlendState*       blend_ = nullptr;
    ID3D11RasterizerState*  raster_ = nullptr;
    // 字形管线（图集采样批）
    ID3D11VertexShader*     vsG_ = nullptr;
    ID3D11PixelShader*      psG_ = nullptr;
    ID3D11InputLayout*      layoutG_ = nullptr;
    ID3D11SamplerState*     sampler_ = nullptr;
    ID3D11BlendState*       blendGlyph_ = nullptr;   // 字形专用：dst 因子取 1 - 覆盖率（逐通道）
    // 形状管线（SDF 图元批）
    ID3D11VertexShader*     vsS_ = nullptr;
    ID3D11PixelShader*      psS_ = nullptr;
    ID3D11InputLayout*      layoutS_ = nullptr;
    // 图像管线（彩色纹理采样批；顶点格式同字形批，复用 vsG_/layoutG_）
    ID3D11PixelShader*      psI_ = nullptr;
    ID3D11SamplerState*     samplerI_ = nullptr;   // 线性采样（图像缩放需平滑）
    struct GlyphPageTex {
        uint32_t id = 0;
        ID3D11Texture2D* tex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };
    struct ImageTex {
        uint32_t id = 0;
        ID3D11Texture2D* tex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };
    std::vector<GlyphPageTex> pages_;
    std::vector<ImageTex> images_;
    // 发射期缓冲：段描述符 + 单元段范围前缀（单元 i 的段为 [first[i], first[i+1])）
    std::vector<EmittedSegment> segments_;
    std::vector<uint32_t> unitFirstSeg_;
    uint32_t vbCapacity_ = 0;
    bool     transparent_ = false;
    float    width_ = 0.f;
    float    height_ = 0.f;
    Painter  painter_;
};

}  // namespace dui::graphics
