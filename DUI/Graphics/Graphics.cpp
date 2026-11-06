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

// 图像批（彩色纹理采样）：BGRA8 纹理 × 颜色；输出预乘（ADR-013）。
// 顶点格式与字形批相同（pos + uv + color）→ 复用 vsG_ / layoutG_，只换 PS。
constexpr char kPSI[] = R"(
Texture2D g_tex : register(t0);
SamplerState g_sampler : register(s0);
struct PSInI {
    float4 svpos : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR;
};
float4 PSMain(PSInI i) : SV_Target {
    float4 t = g_tex.Sample(g_sampler, i.uv);   // BGRA8（straight alpha）
    float a = t.a * i.color.a;
    return float4(t.rgb * i.color.rgb * a, a);   // straight → 预乘（ADR-013）
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

// 指令在物理像素空间的 AABB，用于视口剔除。四类 op 的坐标语义不同
// （Rect / Image 存 DIP、Glyph 存布局空间物理像素、Shape 存含 origin 的物理像素），
// 先各自还原矩形，再施加状态槽的有效矩阵取四角包围盒。
Rect CommandBounds(const PaintCommand& c, const PaintState& st,
                   const Transform2D& eff, float scale) {
    float x0, y0, x1, y1;
    if (c.op == static_cast<uint32_t>(PaintOp::Rect) ||
        c.op == static_cast<uint32_t>(PaintOp::Image)) {
        x0 = (c.p[0] + st.originDipX) * scale;
        y0 = (c.p[1] + st.originDipY) * scale;
        x1 = x0 + c.p[2] * scale;
        y1 = y0 + c.p[3] * scale;
    } else if (c.op == static_cast<uint32_t>(PaintOp::Glyph)) {
        x0 = c.p[0] + st.originDipX * scale;
        y0 = c.p[1] + st.originDipY * scale;
        x1 = x0 + c.p[2];
        y1 = y0 + c.p[3];
    } else {   // Shape：p[9] 是描边半宽，SDF 会在该外扩范围内着色
        const float pad = c.p[9];
        x0 = c.p[0] - c.p[2] - pad;
        y0 = c.p[1] - c.p[3] - pad;
        x1 = c.p[0] + c.p[2] + pad;
        y1 = c.p[1] + c.p[3] + pad;
    }
    if (eff.IsIdentity()) return Rect{x0, y0, x1 - x0, y1 - y0};
    const Vec2 a = eff.Apply({x0, y0}), b = eff.Apply({x1, y0});
    const Vec2 d = eff.Apply({x0, y1}), e = eff.Apply({x1, y1});
    const float l = std::min(std::min(a.x, b.x), std::min(d.x, e.x));
    const float t = std::min(std::min(a.y, b.y), std::min(d.y, e.y));
    const float r = std::max(std::max(a.x, b.x), std::max(d.x, e.x));
    const float bo = std::max(std::max(a.y, b.y), std::max(d.y, e.y));
    return Rect{l, t, r - l, bo - t};
}

}  // namespace

// ---------------------------------------------------------------------------
// Painter
// ---------------------------------------------------------------------------

void Painter::Reset() {
    commands_.clear();
    states_.clear();
    states_.push_back(PaintState{});
    curState_ = 0;
    originDipX_ = 0.f;
    originDipY_ = 0.f;
}

// 状态槽：与当前槽完全相同则复用，避免每元素一槽。
void Painter::UseState(const PaintState& next) {
    const PaintState& cur = states_[curState_];
    if (cur.originDipX == next.originDipX && cur.originDipY == next.originDipY &&
        cur.xform.a == next.xform.a && cur.xform.b == next.xform.b &&
        cur.xform.c == next.xform.c && cur.xform.d == next.xform.d &&
        cur.xform.tx == next.xform.tx && cur.xform.ty == next.xform.ty) return;
    states_.push_back(next);
    curState_ = static_cast<uint32_t>(states_.size()) - 1;
}

void Painter::SetOrigin(float dipX, float dipY, float) {
    originDipX_ = dipX;
    originDipY_ = dipY;
    PaintState next = states_[curState_];
    next.originDipX = dipX;
    next.originDipY = dipY;
    UseState(next);
}

void Painter::SetTransform(const Transform2D& dipXform) {
    PaintState next = states_[curState_];
    next.xform = dipXform;
    UseState(next);
}

PaintCommand& Painter::NewCommand(PaintOp op, uint32_t resource, uint32_t packed) {
    commands_.push_back(PaintCommand{static_cast<uint32_t>(op), curState_, resource, packed, {}});
    return commands_.back();
}

// 0xAARRGGBB（straight）→ R8G8B8A8 内存序；alpha 乘 opacity（近似合成）
static uint32_t PackColor(Color color, float opacity) {
    const uint32_t a8 = static_cast<uint32_t>(static_cast<float>(color >> 24) * opacity + 0.5f);
    return ((color & 0xFFu) << 16) | (color & 0xFF00u) |
           ((color & 0xFF0000u) >> 16) | (a8 << 24);
}

void Painter::AddGlyphQuad(uint32_t page, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1, Color color) {
    if (w <= 0.f || h <= 0.f) return;
    // 字形坐标是布局空间物理像素（绑定当前 scale 下的图集位图），
    // origin 与变换在发射期施加；DPI 变化时此类指令需整体重收集。
    PaintCommand& c = NewCommand(PaintOp::Glyph, page, PackColor(color, 1.f));
    c.p[0] = x; c.p[1] = y; c.p[2] = w; c.p[3] = h;
    c.p[4] = u0; c.p[5] = v0; c.p[6] = u1; c.p[7] = v1;
}

// 图像四边形：把纹理铺满 dipRect（DIP → 像素；origin 为滚动偏移）
void Painter::AddImageQuad(uint32_t texture, const Rect& dipRect, Color color, float) {
    if (dipRect.w <= 0.f || dipRect.h <= 0.f) return;
    PaintCommand& c = NewCommand(PaintOp::Image, texture, PackColor(color, 1.f));
    c.p[0] = dipRect.x; c.p[1] = dipRect.y;
    c.p[2] = dipRect.w; c.p[3] = dipRect.h;
}

// 形状图元：记录一条 Shape 指令（参数均为物理像素，调用方已完成 DIP → 像素换算）。
// 含 origin 的坐标与局部 SDF 参数原样保留，变换在发射期施加。
void Painter::PushShapeQuad(float cx, float cy, float hw, float hh,
                            float p0x, float p0y, float p1x, float p1y,
                            float extra, float halfStroke, Color color, uint32_t kind) {
    if (hw <= 0.f || hh <= 0.f) return;
    PaintCommand& c = NewCommand(PaintOp::Shape, 0, PackColor(color, 1.f));
    c.p[0] = cx; c.p[1] = cy; c.p[2] = hw; c.p[3] = hh;
    c.p[4] = p0x; c.p[5] = p0y; c.p[6] = p1x; c.p[7] = p1y;
    c.p[8] = extra; c.p[9] = halfStroke; c.p[10] = static_cast<float>(kind);
}

// 线段：胶囊 SDF（圆头端点），线宽 = widthDip
void Painter::StrokeLine(Vec2 p0, Vec2 p1, Color color, float widthDip, float scale) {
    p0.x += originDipX_;
    p0.y += originDipY_;
    p1.x += originDipX_;
    p1.y += originDipY_;
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
    center.x += originDipX_;
    center.y += originDipY_;
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
    center.x += originDipX_;
    center.y += originDipY_;
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
    const float ox = (dipRect.x + originDipX_) * scale;
    const float oy = (dipRect.y + originDipY_) * scale;
    const float w = dipRect.w * scale;
    const float h = dipRect.h * scale;
    if (w <= 0.f || h <= 0.f) return;
    const float half = std::max(widthDip * scale, 1.f) * 0.5f;
    float radius = radiusDip * scale;
    if (radius < 1.f) radius = 0.f;
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;
    const float cx = ox + w * 0.5f;
    const float cy = oy + h * 0.5f;
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    PushShapeQuad(cx, cy, hw + half, hh + half, hw, hh, 0.f, 0.f, radius, half, color, 2);
}

void Painter::FillRoundedRect(const Rect& dipRect, float radiusDip, Color color, float opacity, float) {
    if (dipRect.w <= 0.f || dipRect.h <= 0.f) return;
    // 存 DIP 原值：origin / scale / 变换都在发射期施加（10-Rendering §6.2）
    PaintCommand& c = NewCommand(PaintOp::Rect, 0, PackColor(color, opacity));
    c.p[0] = dipRect.x; c.p[1] = dipRect.y;
    c.p[2] = dipRect.w; c.p[3] = dipRect.h;
    c.p[4] = radiusDip;
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
                                   reinterpret_cast<ID3D11DeviceChild*>(layoutS_),
                                   reinterpret_cast<ID3D11DeviceChild*>(psI_),
                                   reinterpret_cast<ID3D11DeviceChild*>(samplerI_)}) {
        if (res != nullptr) res->Release();
    }
    for (GlyphPageTex& t : pages_) {
        if (t.tex != nullptr) t.tex->Release();
        if (t.srv != nullptr) t.srv->Release();
    }
    for (ImageTex& t : images_) {
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

    // 图像管线（彩色纹理采样批）：顶点格式同字形批 → 复用 vsG_/layoutG_，
    // 只需新的 PS（彩色纹理）与线性采样器（图像常被缩放绘制）
    ID3DBlob* psIBlob = nullptr;
    if (!CompileShader(kPSI, "PSMain", "ps_5_0", &psIBlob, error)) return false;
    hr = dev_->CreatePixelShader(psIBlob->GetBufferPointer(), psIBlob->GetBufferSize(), nullptr, &psI_);
    psIBlob->Release();
    if (FAILED(hr)) {
        error = L"CreatePixelShader(image) failed";
        return false;
    }

    D3D11_SAMPLER_DESC sdi{};
    // 线性：图像绘制尺寸常与像素尺寸不等（布局拉伸 / DPI 换算），双线性避免锯齿
    sdi.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sdi.AddressU = sdi.AddressV = sdi.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdi.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sdi.MaxLOD = D3D11_FLOAT32_MAX;
    hr = dev_->CreateSamplerState(&sdi, &samplerI_);
    if (FAILED(hr)) {
        error = L"CreateSamplerState(image) failed";
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
    // 裁剪（scissor）：滚动容器的内容裁到视口内（每单元 RSSetScissorRects）
    rd.ScissorEnable = TRUE;
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

void Device::Render(const DrawUnit* units, uint32_t unitCount, float scale) {
    if (rtv_ == nullptr) return;
    constexpr float kBlack[4] = {0.f, 0.f, 0.f, 1.f};
    ctx_->ClearRenderTargetView(rtv_, kBlack);
    if (units == nullptr || unitCount == 0) return;

    // 每条指令 = 一个 quad；VB 按段顺序线性排布（段内 stride 一致）
    uint32_t totalVerts = 0;
    for (uint32_t i = 0; i < unitCount; ++i)
        totalVerts += static_cast<uint32_t>(units[i].painter->Commands().size()) * 4;
    if (totalVerts == 0) return;

    EnsureVertexBuffer(totalVerts);
    if (vb_ == nullptr) return;

    segments_.clear();
    unitFirstSeg_.clear();
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx_->Map(vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    auto* dst = static_cast<uint8_t*>(m.pData);
    uint32_t written = 0;   // 已写入字节数；各段 quad 字节数均为 16 的倍数 → 写入地址对齐
    constexpr uint32_t kQuadBytes[4] = {4 * sizeof(Vertex), 4 * sizeof(GlyphVertex),
                                        4 * sizeof(ShapeVertex), 4 * sizeof(GlyphVertex)};
    constexpr float kSx[4] = {-1.f, 1.f, 1.f, -1.f};
    constexpr float kSy[4] = {-1.f, -1.f, 1.f, 1.f};

    for (uint32_t u = 0; u < unitCount; ++u) {
        const uint32_t firstSeg = static_cast<uint32_t>(segments_.size());
        unitFirstSeg_.push_back(firstSeg);   // scissor 是单元级 → 段不得跨单元合并
        const Painter& p = *units[u].painter;
        const Rect& unitClip = units[u].clipPx;
        for (const PaintCommand& c : p.Commands()) {
            const PaintState& st = p.States()[c.state];
            const float opx = st.originDipX * scale, opy = st.originDipY * scale;
            // E = Translate(originPx) · xform(px) · Translate(−originPx)：
            // 顶点位置已是 (布局 + origin)·scale，先去 origin、施加变换、再加回来。
            const Transform2D eff = p.StateTransform(c.state, scale);
            // 指令级视口剔除：与单元 scissor 不相交就不发射顶点、不进段。
            // kPad 覆盖 SDF 的 1 像素 AA 过渡带与描边外扩，避免误剔除边缘图元。
            if (unitClip.w <= 0.f || unitClip.h <= 0.f) continue;
            const Rect box = CommandBounds(c, st, eff, scale);
            constexpr float kPad = 2.f;
            if (box.Right() + kPad <= unitClip.x || box.x - kPad >= unitClip.Right() ||
                box.Bottom() + kPad <= unitClip.y || box.y - kPad >= unitClip.Bottom()) continue;
            const bool ident = eff.IsIdentity();
            auto Xf = [&](float x, float y) -> Vec2 {
                return ident ? Vec2{x, y} : eff.Apply(Vec2{x, y});
            };

            uint8_t* out = dst + written;
            if (c.op == static_cast<uint32_t>(PaintOp::Rect)) {
                const float x = (c.p[0] + st.originDipX) * scale;
                const float y = (c.p[1] + st.originDipY) * scale;
                const float w = c.p[2] * scale, h = c.p[3] * scale;
                float radius = c.p[4] * scale;
                if (radius < 1.f) radius = 0.f;
                if (radius > w * 0.5f) radius = w * 0.5f;
                if (radius > h * 0.5f) radius = h * 0.5f;
                const float cx = x + w * 0.5f, cy = y + h * 0.5f;
                const float hw = w * 0.5f, hh = h * 0.5f;
                Vertex* v = reinterpret_cast<Vertex*>(out);
                for (int i = 0; i < 4; ++i) {
                    const Vec2 q = Xf(cx + kSx[i] * hw, cy + kSy[i] * hh);
                    v[i] = Vertex{q.x, q.y, kSx[i] * hw, kSy[i] * hh, hw, hh, radius, c.color};
                }
            } else if (c.op == static_cast<uint32_t>(PaintOp::Glyph)) {
                const float x = c.p[0] + opx, y = c.p[1] + opy;
                const float w = c.p[2], h = c.p[3];
                const float us[4] = {c.p[4], c.p[6], c.p[6], c.p[4]};
                const float vs[4] = {c.p[5], c.p[5], c.p[7], c.p[7]};
                GlyphVertex* v = reinterpret_cast<GlyphVertex*>(out);
                for (int i = 0; i < 4; ++i) {
                    const Vec2 q = Xf(x + (kSx[i] < 0.f ? 0.f : w), y + (kSy[i] < 0.f ? 0.f : h));
                    v[i] = GlyphVertex{q.x, q.y, us[i], vs[i], c.color};
                }
            } else if (c.op == static_cast<uint32_t>(PaintOp::Image)) {
                const float x = (c.p[0] + st.originDipX) * scale;
                const float y = (c.p[1] + st.originDipY) * scale;
                const float w = c.p[2] * scale, h = c.p[3] * scale;
                GlyphVertex* v = reinterpret_cast<GlyphVertex*>(out);
                for (int i = 0; i < 4; ++i) {
                    const Vec2 q = Xf(x + (kSx[i] < 0.f ? 0.f : w), y + (kSy[i] < 0.f ? 0.f : h));
                    v[i] = GlyphVertex{q.x, q.y, kSx[i] < 0.f ? 0.f : 1.f,
                                       kSy[i] < 0.f ? 0.f : 1.f, c.color};
                }
            } else {   // Shape：参数已含 origin 的物理像素，局部 SDF 字段不施加变换
                const float cx = c.p[0], cy = c.p[1], hw = c.p[2], hh = c.p[3];
                ShapeVertex* v = reinterpret_cast<ShapeVertex*>(out);
                for (int i = 0; i < 4; ++i) {
                    const Vec2 q = Xf(cx + kSx[i] * hw, cy + kSy[i] * hh);
                    v[i] = ShapeVertex{q.x, q.y, kSx[i] * hw, kSy[i] * hh,
                                       c.p[4], c.p[5], c.p[6], c.p[7],
                                       c.p[8], c.p[10], c.p[9], c.color};
                }
            }

            // 段合并：同单元、同批、同资源、且单段 quad 数不超静态 IB 容量
            if (segments_.size() > firstSeg) {
                EmittedSegment& last = segments_.back();
                if (last.op == c.op && last.resource == c.resource && last.quadCount < kMaxQuads) {
                    ++last.quadCount;
                } else {
                    segments_.push_back(EmittedSegment{written, 1, c.op, c.resource});
                }
            } else {
                segments_.push_back(EmittedSegment{written, 1, c.op, c.resource});
            }
            written += kQuadBytes[c.op];
        }
    }
    unitFirstSeg_.push_back(static_cast<uint32_t>(segments_.size()));
    ctx_->Unmap(vb_, 0);

    constexpr D3D11_VIEWPORT kVp{0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    D3D11_VIEWPORT vp = kVp;
    vp.Width = width_;
    vp.Height = height_;

    ctx_->IASetIndexBuffer(ib_, DXGI_FORMAT_R32_UINT, 0);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetConstantBuffers(0, 1, &cb_);
    ctx_->RSSetState(raster_);
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->OMSetBlendState(blend_, nullptr, 0xFFFFFFFFu);

    // 按段提交：段顺序 = 指令顺序，故单元内严格 painter's algorithm。
    // IB 是 0-based 局部 quad 索引，每段把 VB 绑到自身字节起点 →
    // StartIndexLocation = 0 / BaseVertexLocation = 0，不再需要负 base。
    ID3D11Buffer* vb = vb_;
    constexpr UINT kStride[4] = {static_cast<UINT>(sizeof(Vertex)),
                                 static_cast<UINT>(sizeof(GlyphVertex)),
                                 static_cast<UINT>(sizeof(ShapeVertex)),
                                 static_cast<UINT>(sizeof(GlyphVertex))};
    uint32_t lastOp = 0xFFFFFFFFu, lastRes = 0xFFFFFFFFu;
    for (uint32_t u = 0; u < unitCount; ++u) {
        const uint32_t first = unitFirstSeg_[u], last = unitFirstSeg_[u + 1];
        if (first == last) continue;

        // 裁剪（scissor）：滚动容器的内容裁到视口内；空矩形表示整单元不画
        const Rect& c = units[u].clipPx;
        D3D11_RECT sr{};
        if (c.w > 0.f && c.h > 0.f) {
            sr.left = static_cast<LONG>(c.x);
            sr.top = static_cast<LONG>(c.y);
            sr.right = static_cast<LONG>(c.x + c.w);
            sr.bottom = static_cast<LONG>(c.y + c.h);
        }
        ctx_->RSSetScissorRects(1, &sr);

        for (uint32_t s = first; s < last; ++s) {
            const EmittedSegment& seg = segments_[s];
            if (seg.op != lastOp) {
                switch (seg.op) {
                case 0:   // Rect
                    ctx_->IASetInputLayout(layout_);
                    ctx_->VSSetShader(vs_, nullptr, 0);
                    ctx_->PSSetShader(ps_, nullptr, 0);
                    break;
                case 1:   // Glyph
                    ctx_->IASetInputLayout(layoutG_);
                    ctx_->VSSetShader(vsG_, nullptr, 0);
                    ctx_->PSSetShader(psG_, nullptr, 0);
                    ctx_->PSSetSamplers(0, 1, &sampler_);
                    break;
                case 2:   // Shape
                    ctx_->IASetInputLayout(layoutS_);
                    ctx_->VSSetShader(vsS_, nullptr, 0);
                    ctx_->PSSetShader(psS_, nullptr, 0);
                    break;
                default:  // Image：顶点格式同字形批，复用 vsG_/layoutG_
                    ctx_->IASetInputLayout(layoutG_);
                    ctx_->VSSetShader(vsG_, nullptr, 0);
                    ctx_->PSSetShader(psI_, nullptr, 0);
                    ctx_->PSSetSamplers(0, 1, &samplerI_);
                    break;
                }
                lastOp = seg.op;
                lastRes = 0xFFFFFFFFu;
            }
            // 跨图集页 / 跨纹理才切换 SRV
            if (seg.op == 1 || seg.op == 3) {
                if (seg.resource != lastRes) {
                    ID3D11ShaderResourceView* srv = seg.op == 1
                        ? FindGlyphSrv(seg.resource) : FindImageSrv(seg.resource);
                    if (srv == nullptr) continue;
                    ctx_->PSSetShaderResources(0, 1, &srv);
                    lastRes = seg.resource;
                }
            }
            UINT off = seg.byteStart;
            UINT stride = kStride[seg.op];
            ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &off);
            ctx_->DrawIndexed(seg.quadCount * 6, 0, 0);
        }
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

// ---------------------------------------------------------------------------
// 图像纹理（BGRA8）
// ---------------------------------------------------------------------------

ID3D11ShaderResourceView* Device::FindImageSrv(uint32_t id) {
    for (ImageTex& t : images_) {
        if (t.id == id) return t.srv;
    }
    return nullptr;
}

void Device::UploadImage(uint32_t id, uint32_t w, uint32_t h, const uint8_t* bgra) {
    ImageTex* pt = nullptr;
    for (ImageTex& t : images_) {
        if (t.id == id) { pt = &t; break; }
    }
    if (pt == nullptr) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;   // 与交换链同格式（非 sRGB）
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(dev_->CreateTexture2D(&td, nullptr, &tex))) return;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(dev_->CreateShaderResourceView(tex, &srvd, &srv))) {
            tex->Release();
            return;
        }
        ImageTex np;
        np.id = id;
        np.tex = tex;
        np.srv = srv;
        images_.push_back(np);
        pt = &images_.back();
    }
    const D3D11_BOX box{0, 0, 0, w, h, 1};
    ctx_->UpdateSubresource(pt->tex, 0, &box, bgra, w * 4, 0);   // 行距 = w*4 字节
}

void Device::ReleaseImage(uint32_t id) {
    for (auto it = images_.begin(); it != images_.end(); ++it) {
        if (it->id == id) {
            if (it->tex != nullptr) it->tex->Release();
            if (it->srv != nullptr) it->srv->Release();
            images_.erase(it);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// 纹理仓库（进程级单例）：控件登记纹理 → App 层每帧同步到 GPU
// ---------------------------------------------------------------------------

TextureStore& TextureStore::Instance() {
    static TextureStore inst;
    return inst;
}

std::shared_ptr<ImageTexture> TextureStore::Register(uint32_t w, uint32_t h,
                                                     std::vector<uint8_t>&& pixels) {
    auto tex = std::make_shared<ImageTexture>();
    tex->id = nextId_++;
    tex->width = w;
    tex->height = h;
    tex->pixels = std::move(pixels);
    tex->dirty = true;
    textures_.push_back(tex);
    return tex;
}

void TextureStore::Sync(Device& dev) {
    for (auto it = textures_.begin(); it != textures_.end(); ) {
        if (it->use_count() == 1) {
            // 只有仓库还持有 → 控件已释放该纹理：回收 GPU 资源
            dev.ReleaseImage((*it)->id);
            it = textures_.erase(it);
            continue;
        }
        if ((*it)->dirty) {
            dev.UploadImage((*it)->id, (*it)->width, (*it)->height, (*it)->pixels.data());
            (*it)->dirty = false;
        }
        ++it;
    }
}

}  // namespace dui::graphics
