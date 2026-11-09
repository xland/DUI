// ============================================================================
//  DUI/Graphics/Graphics.h — D3D11 渲染设备与绘制指令收集
//
//  M3：矩形 / 圆角矩形（SDF 片元裁形，10-Rendering §6.2）。
//  对齐已定决策：ADR-001（D3D11 + DXGI）、ADR-013（BGRA8、预乘 Alpha、VSync）、
//  10-Rendering D2（按需全量光栅）。
//  B2 有序显示列表：Painter 记录指令 + 状态槽，Device::Render 按指令顺序
//  流式发射顶点并按段提交（10-Rendering §6.1），单元内严格 painter's algorithm。
// ============================================================================
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <memory>
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

// ---- 有序绘制指令（10-Rendering §6.1）--------------------------------------
// 收集期只记录指令 + 状态槽，顶点在 Device::Render 里按指令顺序流式发射。
// 相邻同批/同资源的指令合并成段提交 → 单元内严格 painter's algorithm。
enum class PaintOp : uint8_t { Rect = 0, Glyph = 1, Shape = 2, Image = 3 };

struct PaintState {
    Transform2D xform{};                 // DIP 空间组合变换（发射期 .Scaled(scale)）
    float originDipX = 0.f, originDipY = 0.f;
};

// 定宽指令（按最坏情形 Shape 的参数定宽，64B）。p[] 布局：
//   Rect  : dipRect.x/y/w/h, radiusDip
//   Glyph : x/y/w/h（布局空间物理像素）, u0/v0/u1/v1
//   Shape : cx/cy/hw/hh, p0x/p0y, p1x/p1y, extra, halfStroke, kind（物理像素）
//   Image : dipRect.x/y/w/h
struct PaintCommand {
    uint32_t op = 0;
    uint32_t state = 0;
    uint32_t resource = 0;      // Glyph: 图集页 / Image: 纹理 id / 其余 0
    uint32_t color = 0;         // 已打包 R8G8B8A8（含 opacity）
    float    p[11]{};
};

// 发射期段描述符（按绘制顺序线性排布于 VB；跨帧复用，不作常驻指令）
struct EmittedSegment {
    uint32_t byteStart = 0;
    uint32_t quadCount = 0;
    uint32_t op = 0;
    uint32_t resource = 0;
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

// CPU 端有序指令收集：不生成顶点，坐标换算与状态应用推迟到 Device::Render。
// 顶点格式（Vertex / GlyphVertex / ShapeVertex）仍是 GPU 侧契约，由发射器生成。
// 跨帧复用 vector（容量稳定后零分配）。
class Painter {
public:
    void Reset();

    // 状态槽：变换 / 原点变化才追加新槽，指令按索引引用（10-Rendering §6.2）。
    // scale 参数保留以兼容既有调用；缩放统一在发射期施加。
    void SetOrigin(float dipX, float dipY, float scale);
    void SetTransform(const Transform2D& dipXform);

    void FillRoundedRect(const Rect& dipRect, float radiusDip, Color color, float opacity, float scale);
    void AddGlyphQuad(uint32_t page, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1, Color color);
    // 图像四边形：把纹理铺满 dipRect（DIP 坐标 + scale，与 FillRoundedRect 同语义）
    void AddImageQuad(uint32_t texture, const Rect& dipRect, Color color, float scale);

    // 形状指令（DIP 坐标 + scale，与 FillRoundedRect 同语义；2D 绘图控件用）
    void StrokeLine(Vec2 p0, Vec2 p1, Color color, float widthDip, float scale);
    void FillEllipse(Vec2 center, float rxDip, float ryDip, Color color, float scale);
    void StrokeEllipse(Vec2 center, float rxDip, float ryDip, Color color, float widthDip, float scale);
    void StrokeRoundRect(const Rect& dipRect, float radiusDip, float widthDip, Color color, float scale);

    // 阴影（box-shadow 子集）：把元素边框盒按 blur 软化成投影（Shape 批 kind=3）。
    // quad 外扩 blur，SDF 仍以 dipRect 为边界；blur<=0 时由调用方退化成普通矩形。
    void ShadowRoundRect(const Rect& dipRect, float radiusDip, float blurDip, Color color, float scale);

    const std::vector<PaintCommand>& Commands() const noexcept { return commands_; }
    const std::vector<PaintState>& States() const noexcept { return states_; }

    // 像素空间有效矩阵 E = Translate(originPx) · xform(px) · Translate(−originPx)。
    // 发射期与 BeginClip 的裁剪 AABB 共用同一套推导，避免两处漂移。
    Transform2D StateTransform(uint32_t stateIdx, float scale) const noexcept {
        const PaintState& st = states_[stateIdx];
        const float opx = st.originDipX * scale, opy = st.originDipY * scale;
        return Transform2D::Translation(opx, opy) * st.xform.Scaled(scale)
             * Transform2D::Translation(-opx, -opy);
    }
    Transform2D EffectiveTransform(float scale) const noexcept {
        return StateTransform(curState_, scale);
    }

private:
    PaintCommand& NewCommand(PaintOp op, uint32_t resource, uint32_t packed);
    void UseState(const PaintState& next);
    void PushShapeQuad(float cx, float cy, float hw, float hh,
                       float p0x, float p0y, float p1x, float p1y,
                       float extra, float halfStroke, Color color, uint32_t kind);

    std::vector<PaintCommand> commands_;
    std::vector<PaintState> states_;
    float originDipX_ = 0.f, originDipY_ = 0.f;   // 形状 API 记录期换算用
    uint32_t curState_ = 0;
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

    // transparent = true → PREMULTIPLIED + 全透明清屏，背景可透视桌面（ADR-010）；
    // 默认 false → IGNORE + 不透明清屏，视觉与普通窗口一致。
    bool Init(HWND hwnd, bool transparent, String& error);

    // 幂等：尺寸未变直接返回。DPI 变化与窗口缩放共用此入口（12-Windowing §5）
    void Resize(const Vec2& clientPx);

    Painter& GetPainter() noexcept { return painter_; }
    // 清屏 + 按指令顺序发射顶点并提交（scale 用于 DIP → 物理像素换算）
    void Render(const DrawUnit* units, uint32_t unitCount, float scale);
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

// ---------------------------------------------------------------------------
// 图像纹理仓库（进程级单例）
//
//  控件（UI 层 dui::Image）解码图像后登记纹理并持有句柄；App 层每帧调用
//  Sync()：把脏纹理上传到 GPU，并回收控件已释放的纹理（GPU 资源）。
//  与字形图集同构：CPU 侧数据在 UI 层，GPU 侧资源在 Graphics 层。
// ---------------------------------------------------------------------------
struct ImageTexture {
    uint32_t id = 0;              // GPU 纹理 id
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // BGRA8（straight alpha），行序自上而下
    bool dirty = true;            // 待上传
};

class TextureStore {
public:
    static TextureStore& Instance();

    // 登记纹理（CPU 像素已就绪）→ 返回控件持有的句柄
    std::shared_ptr<ImageTexture> Register(uint32_t w, uint32_t h, std::vector<uint8_t>&& pixels);

    // 每帧同步：上传脏纹理 + 回收控件已释放的纹理
    void Sync(Device& dev);

private:
    std::vector<std::shared_ptr<ImageTexture>> textures_;
    uint32_t nextId_ = 1;
};

}  // namespace dui::graphics
