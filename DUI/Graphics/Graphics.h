// ============================================================================
//  DUI/Graphics/Graphics.h — D3D11 渲染设备与绘制指令收集
//
//  M3：矩形 / 圆角矩形（SDF 片元裁形，10-Rendering §6.2）。
//  对齐已定决策：ADR-001（D3D11 + DXGI）、ADR-013（BGRA8、预乘 Alpha、VSync）、
//  10-Rendering D2（按需全量光栅）。
//  批合并 MVP 形态：单着色器单批；字形/图标图集（M5）引入后按 §6.1 分批。
// ============================================================================
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>

#include "Core/Core.h"

namespace dui::graphics {

// 顶点：物理像素空间；矩形参数供片元 SDF 求值（1 像素过渡带 AA，§9-3）
struct Vertex {
    float    x, y;      // 物理像素
    float    lx, ly;    // 相对矩形中心的偏移（像素）
    float    hw, hh;    // 半宽 / 半高（像素）
    float    radius;    // 圆角半径（像素；< 1px 在收集端退化直角）
    uint32_t color;     // R8G8B8A8 内存序（straight alpha；预乘在 PS 完成）
};

// 字形顶点（10-Rendering §6.1：pos + uv + rgba8 ≈ 20B）；图集采样批与矩形批分离
struct GlyphVertex {
    float    x, y;      // 物理像素
    float    u, v;      // 图集页 UV
    uint32_t color;     // straight alpha；PS 内乘图集覆盖率后预乘
};

// 同一图集页的连续字形顶点区间（跨页才分批）
struct GlyphGroup {
    uint32_t page = 0;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
};

// 形状顶点（形状批：SDF 图元，48B）。一个图元 = 一个四边形，
// 片元按 kind 在局部坐标里求有符号距离（像素空间的真实距离场）再做 1 像素过渡带 AA。
//   kind 0 胶囊（线段）：params = 端点 A/B（相对中心），extra = 半径
//   kind 1 椭圆        ：params = 半轴 (rx, ry)
//   kind 2 圆角矩形    ：params = 半 ext，extra = 圆角半径
// halfStroke = 0 表示填充，> 0 表示描边（沿等距线偏移）。
// 注意字段顺序与 Graphics.cpp 的输入布局逐字节对应
// （POSITION x,y | TEXCOORD0 lx,ly | TEXCOORD1 params | TEXCOORD2 extra,kind
//   | TEXCOORD3 halfStroke | COLOR）。
struct ShapeVertex {
    float    x, y;        // 物理像素：图元中心
    float    lx, ly;      // 局部坐标（相对中心，物理像素）
    float    p0x, p0y;    // 形状参数 0
    float    p1x, p1y;    // 形状参数 1
    float    extra;       // 胶囊半径 / 圆角半径（物理像素）
    float    kind;        // 0 胶囊 / 1 椭圆 / 2 圆角矩形
    float    halfStroke;  // 0 = 填充；> 0 = 描边半宽（物理像素）
    uint32_t color;       // R8G8B8A8 内存序（straight alpha；预乘在 PS 完成）
};

// CPU 端指令收集 → 顶点。M3 用跨帧复用的 vector 承载（容量稳定后零分配）；
// 换窗口 Arena 列 M6（00-Overview §3 热路径零分配）。
class Painter {
public:
    void Reset();
    // 原点偏移（滚动内容整体平移；DIP）。设置后本 Painter 收集的所有图元
    // 都会加上该偏移 —— 滚动不改布局结果，只改绘制坐标。
    void SetOrigin(float dipX, float dipY, float scale) noexcept {
        originDipX_ = dipX;
        originDipY_ = dipY;
        originPxX_ = dipX * scale;
        originPxY_ = dipY * scale;
    }
    void FillRoundedRect(const Rect& dipRect, float radiusDip, Color color, float opacity, float scale);
    void AddGlyphQuad(uint32_t page, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1, Color color);

    // 形状指令（DIP 坐标 + scale，与 FillRoundedRect 同语义；2D 绘图控件用）
    void StrokeLine(Vec2 p0, Vec2 p1, Color color, float widthDip, float scale);
    void FillEllipse(Vec2 center, float rxDip, float ryDip, Color color, float scale);
    void StrokeEllipse(Vec2 center, float rxDip, float ryDip, Color color, float widthDip, float scale);
    void StrokeRoundRect(const Rect& dipRect, float radiusDip, float widthDip, Color color, float scale);

    const Vertex* Vertices() const noexcept { return verts_.data(); }
    uint32_t QuadCount() const noexcept { return static_cast<uint32_t>(verts_.size()) / 4; }
    const GlyphVertex* GlyphVertices() const noexcept { return gverts_.data(); }
    uint32_t GlyphQuadCount() const noexcept { return static_cast<uint32_t>(gverts_.size()) / 4; }
    const std::vector<GlyphGroup>& GlyphGroups() const noexcept { return ggroups_; }
    const ShapeVertex* ShapeVertices() const noexcept { return sverts_.data(); }
    uint32_t ShapeQuadCount() const noexcept { return static_cast<uint32_t>(sverts_.size()) / 4; }

private:
    void PushShapeQuad(float cx, float cy, float hw, float hh,
                       float p0x, float p0y, float p1x, float p1y,
                       float extra, float halfStroke, Color color, uint32_t kind);

    std::vector<Vertex> verts_;
    std::vector<GlyphVertex> gverts_;
    std::vector<GlyphGroup> ggroups_;
    std::vector<ShapeVertex> sverts_;
    float originDipX_ = 0.f, originDipY_ = 0.f;   // 原点偏移（DIP）
    float originPxX_ = 0.f, originPxY_ = 0.f;     // 原点偏移（物理像素）
};

// 一个绘制单元 = 一个 Painter + 裁剪矩形（物理像素）。
// 滚动容器的内容单独成单元，用 scissor 裁到视口内；单元顺序即绘制顺序。
struct DrawUnit {
    const Painter* painter = nullptr;
    Rect clipPx{};        // 物理像素裁剪矩形；w/h <= 0 表示整单元不绘制
};

class Device {
public:
    Device() = default;
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    bool Init(HWND hwnd, String& error);

    // 幂等：尺寸未变直接返回。DPI 变化与窗口缩放共用此入口（12-Windowing §5）
    void Resize(const Vec2& clientPx);

    Painter& GetPainter() noexcept { return painter_; }
    void Render(const DrawUnit* units, uint32_t unitCount);      // 清屏 + 提交 Painter 顶点（矩形批 + 字形图集批）
    void Present();

    // 字形图集页（R8）：创建或全量更新；页被 LRU 整页释放后调 Release
    void UploadGlyphPage(uint32_t id, uint32_t w, uint32_t h, const uint8_t* bits);
    void ReleaseGlyphPage(uint32_t id);

private:
    bool CreateSizeDependent(const Vec2& clientPx, String& error);
    void EnsureVertexBuffer(uint32_t vertexCount);
    ID3D11ShaderResourceView* FindGlyphSrv(uint32_t id);

    ID3D11Device*           dev_ = nullptr;
    ID3D11DeviceContext*    ctx_ = nullptr;
    IDXGISwapChain1*        swap_ = nullptr;
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
    // 形状管线（SDF 图元批）
    ID3D11VertexShader*     vsS_ = nullptr;
    ID3D11PixelShader*      psS_ = nullptr;
    ID3D11InputLayout*      layoutS_ = nullptr;
    struct GlyphPageTex {
        uint32_t id = 0;
        ID3D11Texture2D* tex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };
    std::vector<GlyphPageTex> pages_;
    uint32_t vbCapacity_ = 0;
    float    width_ = 0.f;
    float    height_ = 0.f;
    Painter  painter_;
};

}  // namespace dui::graphics
