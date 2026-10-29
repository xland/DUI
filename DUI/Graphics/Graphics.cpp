// ============================================================================
//  DUI/Graphics/Graphics.cpp — D3D11 设备/交换链/着色器/批提交（M3）
// ============================================================================

#include "Graphics/Graphics.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <d3d11.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace dui::graphics {

namespace {

// 索引缓冲按此容量一次性分配；超出（>16k quads）在 M5 图集分批时重新设计
constexpr uint32_t kMaxQuads = 16384;

constexpr char kVS[] = R"(
struct VSIn {
    float2 pos    : POSITION;
    float2 local  : TEXCOORD0;
    float2 ext    : TEXCOORD1;
    float  radius : TEXCOORD2;
    float4 color  : COLOR;
};
struct VSOut {
    float4 svpos  : SV_Position;
    float2 local  : TEXCOORD0;
    float2 ext    : TEXCOORD1;
    float  radius : TEXCOORD2;
    float4 color  : COLOR;
};
cbuffer PerFrame : register(b0) {
    float2 g_rtSize;
};
VSOut VSMain(VSIn i) {
    VSOut o;
    o.svpos = float4(i.pos.x / g_rtSize.x * 2.0 - 1.0,
                     1.0 - i.pos.y / g_rtSize.y * 2.0, 0.0, 1.0);
    o.local = i.local;
    o.ext = i.ext;
    o.radius = i.radius;
    o.color = i.color;
    return o;
}
)";

// 圆角矩形 SDF + 1 像素过渡带抗锯齿（10-Rendering §6.2 / §9-3）
constexpr char kPS[] = R"(
struct PSIn {
    float4 svpos  : SV_Position;
    float2 local  : TEXCOORD0;
    float2 ext    : TEXCOORD1;
    float  radius : TEXCOORD2;
    float4 color  : COLOR;
};
float4 PSMain(PSIn i) : SV_Target {
    float2 q = abs(i.local) - (i.ext - i.radius);
    float  d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - i.radius;
    // 过渡带固定 1 像素：d 是像素空间的真实距离（梯度模长=1）。
    // 不能用 fwidth —— 它等于 |ddx|+|ddy|，斜向梯度会算出 1.41 倍宽度，
    // 导致同一形状的不同方向粗细不一（斜边粗、直边细）。
    float  cov = 1.0 - smoothstep(-0.5, 0.5, d);
    float4 c = i.color;
    c.rgb *= c.a;                       // straight → 预乘（ADR-013）
    return float4(c.rgb * cov, c.a * cov);
}
)";

// 字形批（图集采样）：R8 覆盖率 × 文字色；straight → 预乘（ADR-013）
constexpr char kVSG[] = R"(
struct VSInG {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR;
};
struct VSOutG {
    float4 svpos : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR;
};
cbuffer PerFrame : register(b0) {
    float2 g_rtSize;
}
VSOutG VSMain(VSInG i) {
    VSOutG o;
    o.svpos = float4(i.pos.x / g_rtSize.x * 2.0 - 1.0,
                     1.0 - i.pos.y / g_rtSize.y * 2.0, 0.0, 1.0);
    o.uv = i.uv;
    o.color = i.color;
    return o;
}
)";

constexpr char kPSG[] = R"(
Texture2D g_atlas : register(t0);
SamplerState g_sampler : register(s0);
struct PSInG {
    float4 svpos : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR;
};
float4 PSMain(PSInG i) : SV_Target {
    float cov = g_atlas.Sample(g_sampler, i.uv).r;   // 灰度 AA 覆盖率（DWrite 直出，无后处理）
    float a = i.color.a * cov;
    return float4(i.color.rgb * a, a);               // straight → 预乘（ADR-013）
}
)";

// 形状批（SDF 图元）：一个四边形 + 局部空间有符号距离 + 1 像素过渡带 AA。
// kind：0 胶囊（线段）/ 1 椭圆 / 2 圆角矩形；halfStroke = 0 填充，> 0 描边。
constexpr char kVSS[] = R"(
struct VSInS {
    float2 pos       : POSITION;
    float2 local     : TEXCOORD0;
    float4 params    : TEXCOORD1;
    float2 extraKind : TEXCOORD2;
    float  halfStroke: TEXCOORD3;
    float4 color     : COLOR;
};
struct VSOutS {
    float4 svpos     : SV_Position;
    float2 local     : TEXCOORD0;
    float4 params    : TEXCOORD1;
    float2 extraKind : TEXCOORD2;
    float  halfStroke: TEXCOORD3;
    float4 color     : COLOR;
};
cbuffer PerFrame : register(b0) {
    float2 g_rtSize;
};
VSOutS VSMain(VSInS i) {
    VSOutS o;
    o.svpos = float4(i.pos.x / g_rtSize.x * 2.0 - 1.0,
                     1.0 - i.pos.y / g_rtSize.y * 2.0, 0.0, 1.0);
    o.local = i.local;
    o.params = i.params;
    o.extraKind = i.extraKind;
    o.halfStroke = i.halfStroke;
    o.color = i.color;
    return o;
}
)";

constexpr char kPSS[] = R"(
struct PSInS {
    float4 svpos     : SV_Position;
    float2 local     : TEXCOORD0;
    float4 params    : TEXCOORD1;
    float2 extraKind : TEXCOORD2;
    float  halfStroke: TEXCOORD3;
    float4 color     : COLOR;
};
// 线段距离（胶囊本体）
float sdSegment(float2 p, float2 a, float2 b) {
    float2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}
// 椭圆：沿径向的近似真实距离（等值线是椭圆，且梯度模长≈1 → 描边宽度均匀、
// 过渡带宽度正确；中心无奇点）
float sdEllipse(float2 p, float2 r) {
    float2 rr = max(r, float2(1e-4, 1e-4));
    float2 q = p / rr;
    float k = length(q);
    float2 dir = (k > 1e-5) ? q / k : float2(1.0, 0.0);
    return (k - 1.0) * length(rr * dir);
}
// 圆角矩形
float sdRoundBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}
float4 PSMain(PSInS i) : SV_Target {
    float d;
    if (i.extraKind.y < 0.5) {            // 胶囊（线段）
        d = sdSegment(i.local, i.params.xy, i.params.zw) - i.extraKind.x;
    } else if (i.extraKind.y < 1.5) {     // 椭圆
        d = sdEllipse(i.local, i.params.xy);
    } else {                              // 圆角矩形
        d = sdRoundBox(i.local, i.params.xy, i.extraKind.x);
    }
    if (i.halfStroke > 0.0) d = abs(d) - i.halfStroke;
    // 过渡带固定 1 像素：三种 SDF 均为像素空间的真实距离（梯度模长≈1）。
    // 不能用 fwidth —— 它等于 |ddx|+|ddy|，斜向梯度会算出 1.41 倍宽度，
    // 导致同一多边形各边粗细不一（斜边粗、水平/垂直边细）。
    float cov = 1.0 - smoothstep(-0.5, 0.5, d);
    float4 c = i.color;
    c.rgb *= c.a;                        // straight → 预乘（ADR-013）
    return float4(c.rgb * cov, c.a * cov);
}
)";

bool CompileShader(const char* src, const char* entry, const char* target, ID3DBlob** blob, String& error) {
    ID3DBlob* err = nullptr;
    const HRESULT hr = D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr,
                                  entry, target, 0, 0, blob, &err);
    if (FAILED(hr)) {
        error = L"shader compile failed";
        if (err != nullptr) {
            const char* msg = static_cast<const char*>(err->GetBufferPointer());
            error.assign(msg, msg + err->GetBufferSize());   // ASCII 日志可直接读
            err->Release();
        }
        return false;
    }
    if (err != nullptr) err->Release();
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Painter
// ---------------------------------------------------------------------------

void Painter::Reset() {
    verts_.clear();
    gverts_.clear();
    ggroups_.clear();
    sverts_.clear();
}

void Painter::AddGlyphQuad(uint32_t page, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1, Color color) {
    if (w <= 0.f || h <= 0.f) return;
    if (ggroups_.empty() || ggroups_.back().page != page) {
        GlyphGroup g;
        g.page = page;
        g.firstVertex = static_cast<uint32_t>(gverts_.size());
        g.vertexCount = 0;
        ggroups_.push_back(g);
    }
    // 0xAARRGGBB（straight）→ R8G8B8A8 内存序（与 FillRoundedRect 一致）
    const uint32_t packed = ((color & 0xFFu) << 16) | (color & 0xFF00u) |
                            ((color & 0xFF0000u) >> 16) | ((color >> 24) << 24);
    const GlyphVertex quad[4] = {
        {x,     y,     u0, v0, packed},
        {x + w, y,     u1, v0, packed},
        {x + w, y + h, u1, v1, packed},
        {x,     y + h, u0, v1, packed},
    };
    for (const GlyphVertex& v : quad) gverts_.push_back(v);
    ggroups_.back().vertexCount += 4;
}

// 形状图元：生成一个四边形（4 顶点），片元在局部坐标求 SDF。
// 参数均为物理像素（调用方已完成 DIP → 像素换算）。
void Painter::PushShapeQuad(float cx, float cy, float hw, float hh,
                            float p0x, float p0y, float p1x, float p1y,
                            float extra, float halfStroke, Color color, uint32_t kind) {
    if (hw <= 0.f || hh <= 0.f) return;
    // 0xAARRGGBB（straight）→ R8G8B8A8 内存序（与 FillRoundedRect 一致）
    const uint32_t packed = ((color & 0xFFu) << 16) | (color & 0xFF00u) |
                            ((color & 0xFF0000u) >> 16) | ((color >> 24) << 24);
    const float sx[4] = {-1.f, 1.f, 1.f, -1.f};
    const float sy[4] = {-1.f, -1.f, 1.f, 1.f};
    for (int i = 0; i < 4; ++i) {
        ShapeVertex v;
        v.x = cx + sx[i] * hw;
        v.y = cy + sy[i] * hh;
        v.lx = sx[i] * hw;
        v.ly = sy[i] * hh;
        v.p0x = p0x;
        v.p0y = p0y;
        v.p1x = p1x;
        v.p1y = p1y;
        v.extra = extra;
        v.kind = static_cast<float>(kind);
        v.halfStroke = halfStroke;
        v.color = packed;
        sverts_.push_back(v);
    }
}

// 线段：胶囊 SDF（圆头端点），线宽 = widthDip
void Painter::StrokeLine(Vec2 p0, Vec2 p1, Color color, float widthDip, float scale) {
    const float x0 = p0.x * scale;
    const float y0 = p0.y * scale;
    const float x1 = p1.x * scale;
    const float y1 = p1.y * scale;
    const float half = std::max(widthDip * scale, 1.f) * 0.5f;
    const float cx = (x0 + x1) * 0.5f;
    const float cy = (y0 + y1) * 0.5f;
    const float hw = std::abs(x1 - x0) * 0.5f + half;
    const float hh = std::abs(y1 - y0) * 0.5f + half;
    PushShapeQuad(cx, cy, hw, hh, x0 - cx, y0 - cy, x1 - cx, y1 - cy,
                  half, 0.f, color, 0);
}

// 椭圆填充
void Painter::FillEllipse(Vec2 center, float rxDip, float ryDip, Color color, float scale) {
    const float rx = rxDip * scale;
    const float ry = ryDip * scale;
    if (rx <= 0.f || ry <= 0.f) return;
    const float cx = center.x * scale;
    const float cy = center.y * scale;
    PushShapeQuad(cx, cy, rx, ry, rx, ry, 0.f, 0.f, 0.f, 0.f, color, 1);
}

// 椭圆描边：等距线偏移 halfStroke
void Painter::StrokeEllipse(Vec2 center, float rxDip, float ryDip, Color color,
                            float widthDip, float scale) {
    const float rx = rxDip * scale;
    const float ry = ryDip * scale;
    const float half = std::max(widthDip * scale, 1.f) * 0.5f;
    if (rx <= 0.f || ry <= 0.f) return;
    const float cx = center.x * scale;
    const float cy = center.y * scale;
    PushShapeQuad(cx, cy, rx + half, ry + half, rx, ry, 0.f, 0.f, 0.f, half, color, 1);
}

// 圆角矩形描边
void Painter::StrokeRoundRect(const Rect& dipRect, float radiusDip, float widthDip,
                              Color color, float scale) {
    const float w = dipRect.w * scale;
    const float h = dipRect.h * scale;
    if (w <= 0.f || h <= 0.f) return;
    const float half = std::max(widthDip * scale, 1.f) * 0.5f;
    float radius = radiusDip * scale;
    if (radius < 1.f) radius = 0.f;
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;
    const float cx = (dipRect.x + dipRect.w * 0.5f) * scale;
    const float cy = (dipRect.y + dipRect.h * 0.5f) * scale;
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    PushShapeQuad(cx, cy, hw + half, hh + half, hw, hh, 0.f, 0.f, radius, half, color, 2);
}

void Painter::FillRoundedRect(const Rect& dipRect, float radiusDip, Color color, float opacity, float scale) {
    const float x = dipRect.x * scale;
    const float y = dipRect.y * scale;
    const float w = dipRect.w * scale;
    const float h = dipRect.h * scale;
    if (w <= 0.f || h <= 0.f) return;

    float radius = radiusDip * scale;
    if (radius < 1.f) radius = 0.f;                 // §9-3：CPU 收集端退化直角
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;

    // 0xAARRGGBB（straight）→ R8G8B8A8 内存序；alpha 乘 opacity（近似合成，离屏层列 P0 后段）
    const uint32_t a8 = static_cast<uint32_t>(static_cast<float>(color >> 24) * opacity + 0.5f);
    const uint32_t packed = ((color & 0xFFu) << 16) | (color & 0xFF00u) |
                            ((color & 0xFF0000u) >> 16) | (a8 << 24);

    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    const float sx[4] = {-1.f, 1.f, 1.f, -1.f};
    const float sy[4] = {-1.f, -1.f, 1.f, 1.f};
    for (int i = 0; i < 4; ++i) {
        Vertex v;
        v.x = cx + sx[i] * hw;
        v.y = cy + sy[i] * hh;
        v.lx = sx[i] * hw;
        v.ly = sy[i] * hh;
        v.hw = hw;
        v.hh = hh;
        v.radius = radius;
        v.color = packed;
        verts_.push_back(v);
    }
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

Device::~Device() {
    if (ctx_) ctx_->ClearState();
    for (ID3D11DeviceChild* res : {reinterpret_cast<ID3D11DeviceChild*>(rtv_),
                                   reinterpret_cast<ID3D11DeviceChild*>(vs_),
                                   reinterpret_cast<ID3D11DeviceChild*>(ps_),
                                   reinterpret_cast<ID3D11DeviceChild*>(layout_),
                                   reinterpret_cast<ID3D11DeviceChild*>(vb_),
                                   reinterpret_cast<ID3D11DeviceChild*>(ib_),
                                   reinterpret_cast<ID3D11DeviceChild*>(cb_),
                                   reinterpret_cast<ID3D11DeviceChild*>(blend_),
                                   reinterpret_cast<ID3D11DeviceChild*>(raster_),
                                   reinterpret_cast<ID3D11DeviceChild*>(vsG_),
                                   reinterpret_cast<ID3D11DeviceChild*>(psG_),
                                   reinterpret_cast<ID3D11DeviceChild*>(layoutG_),
                                   reinterpret_cast<ID3D11DeviceChild*>(sampler_),
                                   reinterpret_cast<ID3D11DeviceChild*>(vsS_),
                                   reinterpret_cast<ID3D11DeviceChild*>(psS_),
                                   reinterpret_cast<ID3D11DeviceChild*>(layoutS_)}) {
        if (res != nullptr) res->Release();
    }
    for (GlyphPageTex& t : pages_) {
        if (t.tex != nullptr) t.tex->Release();
        if (t.srv != nullptr) t.srv->Release();
    }
    if (swap_ != nullptr) swap_->Release();
    if (ctx_ != nullptr) ctx_->Release();
    if (dev_ != nullptr) dev_->Release();
}

bool Device::Init(HWND hwnd, String& error) {
    constexpr D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels, 3,
                                   D3D11_SDK_VERSION, &dev_, nullptr, &ctx_);
    if (FAILED(hr)) {
        error = L"D3D11CreateDevice failed";
        return false;
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);

    IDXGIFactory2* factory = nullptr;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr)) {
        // Flip Model + BGRA8 + 2 缓冲（10-Rendering §5 已定）
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = static_cast<UINT>(w);
        sd.Height = static_cast<UINT>(h);
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        hr = factory->CreateSwapChainForHwnd(dev_, hwnd, &sd, nullptr, nullptr, &swap_);
        factory->Release();
    }
    if (FAILED(hr)) {
        error = L"CreateSwapChainForHwnd failed";
        return false;
    }

    // 着色器（进程内编译一次；d3dcompiler_47.dll 为系统组件）
    ID3DBlob* vsBlob = nullptr;
    if (!CompileShader(kVS, "VSMain", "vs_5_0", &vsBlob, error)) return false;
    hr = dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
    if (SUCCEEDED(hr)) {
        constexpr D3D11_INPUT_ELEMENT_DESC kLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        hr = dev_->CreateInputLayout(kLayout, 5, vsBlob->GetBufferPointer(),
                                     vsBlob->GetBufferSize(), &layout_);
    }
    vsBlob->Release();
    if (FAILED(hr)) {
        error = L"CreateVertexShader/InputLayout failed";
        return false;
    }

    ID3DBlob* psBlob = nullptr;
    if (!CompileShader(kPS, "PSMain", "ps_5_0", &psBlob, error)) return false;
    hr = dev_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);
    psBlob->Release();
    if (FAILED(hr)) {
        error = L"CreatePixelShader failed";
        return false;
    }

    // 字形管线（图集采样批，10-Rendering §6.1 分批）
    ID3DBlob* vsGBlob = nullptr;
    if (!CompileShader(kVSG, "VSMain", "vs_5_0", &vsGBlob, error)) return false;
    hr = dev_->CreateVertexShader(vsGBlob->GetBufferPointer(), vsGBlob->GetBufferSize(), nullptr, &vsG_);
    if (SUCCEEDED(hr)) {
        constexpr D3D11_INPUT_ELEMENT_DESC kLayoutG[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        hr = dev_->CreateInputLayout(kLayoutG, 3, vsGBlob->GetBufferPointer(),
                                     vsGBlob->GetBufferSize(), &layoutG_);
    }
    vsGBlob->Release();
    if (FAILED(hr)) {
        error = L"CreateVertexShader/InputLayout(glyph) failed";
        return false;
    }

    ID3DBlob* psGBlob = nullptr;
    if (!CompileShader(kPSG, "PSMain", "ps_5_0", &psGBlob, error)) return false;
    hr = dev_->CreatePixelShader(psGBlob->GetBufferPointer(), psGBlob->GetBufferSize(), nullptr, &psG_);
    psGBlob->Release();
    if (FAILED(hr)) {
        error = L"CreatePixelShader(glyph) failed";
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    // POINT：字形 quad 整像素对齐且 1:1 映射，点采样零插值（双线性会糊边）
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = dev_->CreateSamplerState(&sd, &sampler_);
    if (FAILED(hr)) {
        error = L"CreateSamplerState failed";
        return false;
    }

    // 形状管线（SDF 图元批）
    ID3DBlob* vsSBlob = nullptr;
    if (!CompileShader(kVSS, "VSMain", "vs_5_0", &vsSBlob, error)) return false;
    hr = dev_->CreateVertexShader(vsSBlob->GetBufferPointer(), vsSBlob->GetBufferSize(), nullptr, &vsS_);
    if (SUCCEEDED(hr)) {
        constexpr D3D11_INPUT_ELEMENT_DESC kLayoutS[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT,   0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,      0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        hr = dev_->CreateInputLayout(kLayoutS, 6, vsSBlob->GetBufferPointer(),
                                     vsSBlob->GetBufferSize(), &layoutS_);
    }
    vsSBlob->Release();
    if (FAILED(hr)) {
        error = L"CreateVertexShader/InputLayout(shape) failed";
        return false;
    }

    ID3DBlob* psSBlob = nullptr;
    if (!CompileShader(kPSS, "PSMain", "ps_5_0", &psSBlob, error)) return false;
    hr = dev_->CreatePixelShader(psSBlob->GetBufferPointer(), psSBlob->GetBufferSize(), nullptr, &psS_);
    psSBlob->Release();
    if (FAILED(hr)) {
        error = L"CreatePixelShader(shape) failed";
        return false;
    }

    // 常量缓冲：仅 rtSize（正交投影在 VS 内完成）
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 16;
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = dev_->CreateBuffer(&cbd, nullptr, &cb_);
    if (FAILED(hr)) {
        error = L"CreateBuffer(cb) failed";
        return false;
    }

    // 静态索引缓冲：每 quad 6 索引
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(kMaxQuads) * 6);
    for (uint32_t q = 0; q < kMaxQuads; ++q) {
        const uint32_t b = q * 4;
        const uint32_t qidx[6] = {b, b + 1, b + 2, b, b + 2, b + 3};
        for (const uint32_t i : qidx) indices.push_back(i);   // 不用 insert：其实现含 EH 路径（/EHs-c- 下 C4530）
    }
    D3D11_BUFFER_DESC ibd{};
    ibd.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA idata{indices.data(), 0, 0};
    hr = dev_->CreateBuffer(&ibd, &idata, &ib_);
    if (FAILED(hr)) {
        error = L"CreateBuffer(ib) failed";
        return false;
    }

    // 预乘 Alpha 混合（ADR-013）+ CullNone（quad 顶点序不依赖缠绕方向）
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = dev_->CreateBlendState(&bd, &blend_);
    if (FAILED(hr)) {
        error = L"CreateBlendState failed";
        return false;
    }

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    hr = dev_->CreateRasterizerState(&rd, &raster_);
    if (FAILED(hr)) {
        error = L"CreateRasterizerState failed";
        return false;
    }

    return CreateSizeDependent(Vec2{w, h}, error);
}

bool Device::CreateSizeDependent(const Vec2& clientPx, String& error) {
    if (rtv_ != nullptr) {
        rtv_->Release();
        rtv_ = nullptr;
    }
    ID3D11Texture2D* back = nullptr;
    HRESULT hr = swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    if (SUCCEEDED(hr)) {
        hr = dev_->CreateRenderTargetView(back, nullptr, &rtv_);
        back->Release();
    }
    if (FAILED(hr)) {
        error = L"RenderTargetView failed";
        return false;
    }
    width_ = clientPx.x;
    height_ = clientPx.y;
    const float rt[4] = {width_, height_, 0.f, 0.f};
    ctx_->UpdateSubresource(cb_, 0, nullptr, rt, 0, 0);
    return true;
}

void Device::Resize(const Vec2& clientPx) {
    if (swap_ == nullptr || clientPx.x <= 0.f || clientPx.y <= 0.f) return;
    if (clientPx.x == width_ && clientPx.y == height_) return;

    if (rtv_ != nullptr) {   // ResizeBuffers 前必须释放后台缓冲引用
        rtv_->Release();
        rtv_ = nullptr;
    }
    if (FAILED(swap_->ResizeBuffers(0, static_cast<UINT>(clientPx.x), static_cast<UINT>(clientPx.y),
                                    DXGI_FORMAT_UNKNOWN, 0))) {
        return;   // 设备丢失的完整恢复路径列 P0 后段（10-Rendering §7）
    }
    String err;
    CreateSizeDependent(clientPx, err);
}

void Device::EnsureVertexBuffer(uint32_t vertexCount) {
    if (vb_ != nullptr && vbCapacity_ >= vertexCount) return;
    if (vb_ != nullptr) {
        vb_->Release();
        vb_ = nullptr;
    }
    while (vbCapacity_ < vertexCount) vbCapacity_ = vbCapacity_ == 0 ? 4096 : vbCapacity_ * 2;
    D3D11_BUFFER_DESC bd{};
    // 三批共用一块动态 VB，容量按最大顶点尺寸（形状批 48B）预留
    bd.ByteWidth = vbCapacity_ * sizeof(ShapeVertex);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev_->CreateBuffer(&bd, nullptr, &vb_))) return;
}

void Device::Render() {
    if (rtv_ == nullptr) return;
    constexpr float kBlack[4] = {0.f, 0.f, 0.f, 1.f};
    ctx_->ClearRenderTargetView(rtv_, kBlack);

    const uint32_t quads = painter_.QuadCount();
    const uint32_t gquads = painter_.GlyphQuadCount();
    const uint32_t squads = painter_.ShapeQuadCount();
    const uint32_t total = quads + gquads + squads;
    if (total == 0 || total > kMaxQuads) return;

    EnsureVertexBuffer(total * 4);
    if (vb_ == nullptr) return;

    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx_->Map(vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    auto* dst = static_cast<uint8_t*>(m.pData);
    memcpy(dst, painter_.Vertices(), static_cast<size_t>(quads) * 4 * sizeof(Vertex));
    memcpy(dst + static_cast<size_t>(quads) * 4 * sizeof(Vertex), painter_.GlyphVertices(),
           static_cast<size_t>(gquads) * 4 * sizeof(GlyphVertex));
    memcpy(dst + (static_cast<size_t>(quads) * 4 * sizeof(Vertex) +
                   static_cast<size_t>(gquads) * 4 * sizeof(GlyphVertex)),
           painter_.ShapeVertices(), static_cast<size_t>(squads) * 4 * sizeof(ShapeVertex));
    ctx_->Unmap(vb_, 0);

    ID3D11Buffer* vb = vb_;
    constexpr UINT kStride = sizeof(Vertex);
    constexpr UINT kOffset = 0;
    constexpr UINT kStrideG = sizeof(GlyphVertex);
    constexpr UINT kStrideS = sizeof(ShapeVertex);
    constexpr D3D11_VIEWPORT kVp{0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    D3D11_VIEWPORT vp = kVp;
    vp.Width = width_;
    vp.Height = height_;

    ctx_->IASetVertexBuffers(0, 1, &vb, &kStride, &kOffset);
    ctx_->IASetIndexBuffer(ib_, DXGI_FORMAT_R32_UINT, 0);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetConstantBuffers(0, 1, &cb_);
    ctx_->RSSetState(raster_);
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->OMSetBlendState(blend_, nullptr, 0xFFFFFFFFu);

    // 矩形批（SDF 圆角矩形）
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
    if (quads > 0) ctx_->DrawIndexed(quads * 6, 0, 0);

    // 字形批：按图集页分组，跨页才切换纹理（10-Rendering §6.1）。
    // 顶点步长与矩形批不同（20B vs 32B），重绑顶点缓冲并偏移到字形顶点段。
    if (gquads > 0) {
        const UINT gOffset = quads * 4 * sizeof(Vertex);
        ctx_->IASetInputLayout(layoutG_);
        ctx_->IASetVertexBuffers(0, 1, &vb, &kStrideG, &gOffset);
        ctx_->VSSetShader(vsG_, nullptr, 0);
        ctx_->PSSetShader(psG_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        for (const GlyphGroup& g : painter_.GlyphGroups()) {
            ID3D11ShaderResourceView* srv = FindGlyphSrv(g.page);
            if (srv == nullptr) continue;
            ctx_->PSSetShaderResources(0, 1, &srv);
            // 共享索引缓冲的索引按矩形批的绝对顶点号编排；字形批换了步长，
            // 用负 BaseVertexLocation 把索引重定基到本批首个字形顶点
            ctx_->DrawIndexed(g.vertexCount / 4 * 6, (quads + g.firstVertex / 4) * 6,
                              -static_cast<INT>(quads * 4 + g.firstVertex));
        }
        // 恢复矩形批绑定（下一帧 Render 开头会重设，这里保持状态一致）
        ctx_->IASetVertexBuffers(0, 1, &vb, &kStride, &kOffset);
    }

    // 形状批（SDF 图元）：步长 48B，顶点段在矩形批与字形批之后。
    if (squads > 0) {
        const UINT sOffset = quads * 4 * sizeof(Vertex) + gquads * 4 * sizeof(GlyphVertex);
        ctx_->IASetInputLayout(layoutS_);
        ctx_->IASetVertexBuffers(0, 1, &vb, &kStrideS, &sOffset);
        ctx_->VSSetShader(vsS_, nullptr, 0);
        ctx_->PSSetShader(psS_, nullptr, 0);
        ctx_->DrawIndexed(squads * 6, (quads + gquads) * 6,
                          -static_cast<INT>((quads + gquads) * 4));
    }
}

ID3D11ShaderResourceView* Device::FindGlyphSrv(uint32_t id) {
    for (GlyphPageTex& t : pages_) {
        if (t.id == id) return t.srv;
    }
    return nullptr;
}

void Device::UploadGlyphPage(uint32_t id, uint32_t w, uint32_t h, const uint8_t* bits) {
    GlyphPageTex* pt = nullptr;
    for (GlyphPageTex& t : pages_) {
        if (t.id == id) { pt = &t; break; }
    }
    if (pt == nullptr) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(dev_->CreateTexture2D(&td, nullptr, &tex))) return;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.Format = DXGI_FORMAT_R8_UNORM;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(dev_->CreateShaderResourceView(tex, &srvd, &srv))) {
            tex->Release();
            return;
        }
        GlyphPageTex np;
        np.id = id;
        np.tex = tex;
        np.srv = srv;
        pages_.push_back(np);
        pt = &pages_.back();
    }
    const D3D11_BOX box{0, 0, 0, w, h, 1};
    ctx_->UpdateSubresource(pt->tex, 0, &box, bits, w, 0);
}

void Device::ReleaseGlyphPage(uint32_t id) {
    for (auto it = pages_.begin(); it != pages_.end(); ++it) {
        if (it->id == id) {
            if (it->tex != nullptr) it->tex->Release();
            if (it->srv != nullptr) it->srv->Release();
            pages_.erase(it);
            return;
        }
    }
}

void Device::Present() {
    if (swap_ != nullptr) swap_->Present(1, 0);   // VSync 默认开（ADR-013）
}

}  // namespace dui::graphics
