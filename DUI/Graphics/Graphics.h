// ============================================================================
//  DUI/Graphics/Graphics.h — 绘制指令收集与 CPU 侧资源（公共头，进 DUI.h）
//
//  M3：矩形 / 圆角矩形（SDF 片元裁形，10-Rendering §6.2）。
//  对齐已定决策：ADR-001（D3D11 + DXGI）、ADR-013（BGRA8、预乘 Alpha、VSync）、
//  10-Rendering D2（按需全量光栅）。
//  B2 有序显示列表：Painter 记录指令 + 状态槽，Device::Render 按指令顺序
//  流式发射顶点并按段提交（10-Rendering §6.1），单元内严格 painter's algorithm。
//
//  公共面：Painter（自绘控件的绘制接口）+ 顶点/指令结构 + TextureStore。
//  D3D 设备（Device）依赖 d3d11.h，而该头经 rpcndr.h 引入 `#define small char`
//  这类污染业务的宏 → Device 留在内部头 Graphics/Device.h，本头保持纯 C++。
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "Core/Core.h"

namespace dui::graphics {

class Device;   // Graphics/Device.h：App 层内部使用（D3D11 设备，公共头不暴露）

// 顶点：物理像素空间；矩形参数供片元 SDF 求值（1 像素过渡带 AA，§9-3）
struct Vertex {
    float    x, y;      // 物理像素
    float    lx, ly;    // 相对矩形中心的偏移（像素）
    float    hw, hh;    // 半宽 / 半高（像素）
    float    r0, r1, r2, r3;  // 四角圆角半径：左上 / 右上 / 右下 / 左下（像素）
    uint32_t color;     // R8G8B8A8 内存序（straight alpha；预乘在 PS 完成）
};  // 44B

// 字形顶点（10-Rendering §6.1：pos + uv + rgba8 ≈ 20B）；图集采样批与矩形批分离
struct GlyphVertex {
    float    x, y;      // 物理像素
    float    u, v;      // 图集页 UV
    uint32_t color;     // straight alpha；PS 内乘图集覆盖率后预乘
};

// ---- 有序绘制指令（10-Rendering §6.1）--------------------------------------
// 收集期只记录指令 + 状态槽，顶点在 Device::Render 里按指令顺序流式发射。
// 相邻同批/同资源的指令合并成段提交 → 单元内严格 painter's algorithm。
enum class PaintOp : uint8_t { Rect = 0, Glyph = 1, Shape = 2, Image = 3, Gradient = 4 };

struct PaintState {
    Transform2D xform{};                 // DIP 空间组合变换（发射期 .Scaled(scale)）
    float originDipX = 0.f, originDipY = 0.f;
};

// 定宽指令（按最坏情形 Shape 的参数定宽，80B）。p[] 布局：
//   Rect  : dipRect.x/y/w/h, r0/r1/r2/r3（四角圆角半径 DIP）
//   Glyph : x/y/w/h（布局空间物理像素）, u0/v0/u1/v1
//   Shape : cx/cy/hw/hh, p0x/p0y, p1x/p1y, extra, halfStroke, kind,
//           r0/r1/r2/r3（四角圆角半径，物理像素）
//   Image : dipRect.x/y/w/h
//   Gradient: dipRect.x/y/w/h, r0/r1/r2/r3, dx/dy(渐变方向单位向量),
//             t0/t1(渐变轴在盒上的投影范围, DIP)。resource 高 8 位 = spread，
//             低 24 位 = LUT 纹理 id。
struct PaintCommand {
    uint32_t op = 0;
    uint32_t state = 0;
    uint32_t resource = 0;      // Glyph: 图集页 / Image: 纹理 id / 其余 0
    uint32_t color = 0;         // 已打包 R8G8B8A8（含 opacity）
    float    p[16]{};
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
//   kind 2 圆角矩形    ：params = 半 ext，radii = 四角圆角半径
//   kind 3 阴影圆角矩形：params = 半 ext，radii = 四角圆角半径，halfStroke = blur
// halfStroke = 0 表示填充，> 0 表示描边（沿等距线偏移）。
// 注意字段顺序与 Graphics.cpp 的输入布局逐字节对应
// （POSITION x,y | TEXCOORD0 lx,ly | TEXCOORD1 params | TEXCOORD2 radii
//   | TEXCOORD3 extra,kind | TEXCOORD4 halfStroke | COLOR）。
struct ShapeVertex {
    float    x, y;        // 物理像素：图元中心
    float    lx, ly;      // 局部坐标（相对中心，物理像素）
    float    p0x, p0y;    // 形状参数 0
    float    p1x, p1y;    // 形状参数 1
    float    r0, r1, r2, r3;  // 四角圆角半径：左上 / 右上 / 右下 / 左下（物理像素）
    float    extra;       // 胶囊半径（物理像素）
    float    kind;        // 0 胶囊 / 1 椭圆 / 2 圆角矩形 / 3 阴影圆角矩形
    float    halfStroke;  // 0 = 填充；> 0 = 描边半宽 / 模糊半径（物理像素）
    uint32_t color;       // R8G8B8A8 内存序（straight alpha；预乘在 PS 完成）
};

// 渐变顶点（渐变批：LUT 采样 + 圆角矩形 SDF 裁形）。
// u = LUT 采样坐标：CPU 按 CSS 角度把角点投影到渐变轴、再按周期归一；可 <0 或 >1，
//     由 sampler 地址模式（CLAMP/WRAP/MIRROR = Pad/Repeat/Reflect）处理 spread；
// local/ext/radii 供 PS 求 SDF 覆盖率，与 Rect 批同规则；
// color 只承载累计不透明度（R=G=B=A=opacity，PS 里乘进 LUT alpha）。
struct GradientVertex {
    float    x, y;        // 物理像素
    float    u;           // LUT 采样 u（可 <0/>1，由 sampler 地址模式处理 spread）
    float    lx, ly;      // 相对矩形中心的偏移（物理像素）
    float    hw, hh;      // 半宽 / 半高（物理像素）
    float    r0, r1, r2, r3;  // 四角圆角半径（物理像素）
    uint32_t color;       // 累计不透明度（alpha 有效）
};  // 48B

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

    // 累计不透明度（祖先链乘积）：设置后本 Painter 发射的所有带色指令都乘上它。
    // 父元素的 Opacity 经由它自动作用于自身背景 / 边框 / 文字 / 阴影与整棵子树
    // （CSS opacity 语义；绘制层按元素逐个预乘，不走离屏合成层）。
    void SetOpacity(float opacity);
    float Opacity() const noexcept { return opacity_; }

    // 圆角矩形填充。四角半径独立（CornerRadii）；统一圆角用便捷重载。
    void FillRoundedRect(const Rect& dipRect, const CornerRadii& radii, Color color, float opacity, float scale);
    void FillRoundedRect(const Rect& dipRect, float radiusDip, Color color, float opacity, float scale);
    void AddGlyphQuad(uint32_t page, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1, Color color);
    // 图像四边形：把纹理铺满 dipRect（DIP 坐标 + scale，与 FillRoundedRect 同语义）
    void AddImageQuad(uint32_t texture, const Rect& dipRect, Color color, float scale);
    // 渐变四边形：LUT 纹理（1D 色带）铺满 dipRect 并按圆角裁形。
    // dir = 渐变方向单位向量（DIP 坐标，已按 CSS 角度换算）；spread = 0 Pad / 1 Repeat /
    // 2 Reflect，编码进 resource 高 8 位，发射期选 sampler 地址模式。
    // period = LUT 一个周期占整条渐变线的比例（Pad 恒为 1；Repeat/Reflect 为色标跨度），
    // 发射期据此把盒内 u 缩放到跨若干周期。opacity 走累计态。
    void AddGradientQuad(uint32_t lutTexture, const Rect& dipRect, const CornerRadii& radii,
                         Vec2 dirDip, uint8_t spread, float period, float scale);

    // 形状指令（DIP 坐标 + scale，与 FillRoundedRect 同语义；2D 绘图控件用）
    void StrokeLine(Vec2 p0, Vec2 p1, Color color, float widthDip, float scale);
    void FillEllipse(Vec2 center, float rxDip, float ryDip, Color color, float scale);
    void StrokeEllipse(Vec2 center, float rxDip, float ryDip, Color color, float widthDip, float scale);
    // 圆角矩形描边：沿等距线偏移 halfStroke（路径居中于描边带）
    void StrokeRoundRect(const Rect& dipRect, const CornerRadii& radii, float widthDip, Color color, float scale);
    void StrokeRoundRect(const Rect& dipRect, float radiusDip, float widthDip, Color color, float scale);

    // 阴影（box-shadow 子集）：把元素边框盒按 blur 软化成投影（Shape 批 kind=3）。
    // quad 外扩 blur，SDF 仍以 dipRect 为边界；blur<=0 时由调用方退化成普通矩形。
    void ShadowRoundRect(const Rect& dipRect, const CornerRadii& radii, float blurDip, Color color, float scale);
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
                       float extra, float halfStroke, Color color, uint32_t kind,
                       const CornerRadii& radii = CornerRadii{});

    std::vector<PaintCommand> commands_;
    std::vector<PaintState> states_;
    float originDipX_ = 0.f, originDipY_ = 0.f;   // 形状 API 记录期换算用
    float opacity_ = 1.f;   // 累计不透明度（祖先链乘积）
    uint32_t curState_ = 0;
};

// 虚线折线：沿折线按 Dash 图案在 CPU 端切段，逐段 StrokeLine（零 GPU 改动）。
// dash.on <= 0 时退化为逐段实线。供 Canvas 自绘与元素边框虚线共用。
void StrokeDashedPolyline(Painter& p, const Vec2* points, size_t count,
                          Color color, float widthDip, const Dash& dash, float scale);

// 一个绘制单元 = 一个 Painter + 裁剪矩形（物理像素）。
// 滚动容器的内容单独成单元，用 scissor 裁到视口内；单元顺序即绘制顺序。
struct DrawUnit {
    const Painter* painter = nullptr;
    Rect clipPx{};        // 物理像素裁剪矩形；w/h <= 0 表示整单元不绘制
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
    // 纹理 id 是否仍在仓库中（供各 Device 清理自己持有的已回收纹理）
    bool Has(uint32_t id) const noexcept;

private:
    std::vector<std::shared_ptr<ImageTexture>> textures_;
    std::unordered_set<uint32_t> ids_;   // Has() 的 O(1) 索引，与 textures_ 同步增删
    uint32_t nextId_ = 1;
};

}  // namespace dui::graphics
