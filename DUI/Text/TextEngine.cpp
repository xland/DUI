// ============================================================================
//  DUI/Text/TextEngine.cpp — DWrite 工厂/系统字体集/段落排版/字形光栅与绘制
// ============================================================================

#include "Text/TextEngine.h"

#include <dwrite_3.h>

#include <cmath>
#include <cwchar>

#include "Graphics/Graphics.h"

#pragma comment(lib, "dwrite.lib")

namespace dui::text {

namespace {

// 文本光栅化参数（对标 Qt Quick 6.11 / Skia(Chromium·Avalonia) 的 DirectWrite 路径）：
//
//  ① 灰度 AA：DWrite 必须显式请求 DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE，
//     且纹理类型用 DWRITE_TEXTURE_ALIASED_1x1（1 字节/像素；该名字有误导，
//     但灰度 AA 同样用它，见 Skia SkScalerContext_DW.cpp 注释）。
//     旧写法（IDWriteFactory::CreateGlyphRunAnalysis + CLEARTYPE_3x1 + 取 G 通道）
//     拿到的是**子像素**覆盖率：单通道无法表达整像素覆盖 → 笔画边缘失真、
//     粗细不均、颗粒感（Qt/Skia 都不这么干）。
//
//  ② 亚像素定位：字形位置量化到 1/4 像素（4 相位），相位进图集键，
//     各相位分别光栅（相位通过 transform 平移烘焙进位图，位移单位=像素）。
//     对齐 Qt 的 m_subPixelPositionCount=4 与 Skia 的 subX 量化。
//     逐字形取整到整像素会让相邻字形相位失配 → 笔画粗细跳变。
//
//  ③ 渲染模式：< 20px 用 NATURAL（横向 AA，锐利），≥ 20px 用
//     NATURAL_SYMMETRIC（横纵都 AA，曲线/斜线更平滑）——Skia 的同款经验规则。
//
//  ④ 不做 gamma/contrast 后处理：Qt(FreeType) 与 Skia 都不对 coverage 做
//     S 曲线增强；Skia 的 pre-blend 是方向相反的线性化表，且仅在 sRGB surface
//     生效。自造 S 曲线会把 AA 过渡带压窄 → 观感发虚且颗粒。

// 亚像素相位：1/4 像素，4 个相位（0/0.25/0.5/0.75 px）
constexpr float kSubPixelScale = 4.f;
constexpr float kSubPixelStep  = 1.f / kSubPixelScale;
// ≥ 该像素字号启用对称（横纵都抗锯齿）渲染
constexpr uint32_t kSymmetricPixelSize = 20;

// Draw 的用户上下文（layout → renderer 回调携带）
struct DrawCtx {
    Engine*            engine;
    graphics::Painter* painter;
    float              scale;
    Color              color;
};

// 12-Windowing §9-3：pixelSize = round(fontSizeDip × scale)，< 9px 向上夹紧
uint32_t PixelSizeOf(float fontEmSizeDip, float scale) {
    int px = static_cast<int>(fontEmSizeDip * scale + 0.5f);
    if (px < 9) px = 9;
    return static_cast<uint32_t>(px);
}

// 字形绘制回调：把排版结果的每个字形 quad 送进 Painter（图集 UV）。
// 位置：排版坐标（DIP）× scale → 物理像素；**保留小数**并按 1/4 像素量化
// 求相位（对齐 Qt 6.11 的 4 相位定位 / Skia subX），相位进图集键 → 各相位
// 分别光栅；位图已烘焙相位，quad 落在整数像素（POINT 采样 1:1 呈现）。
class GlyphRenderer final : public IDWriteTextRenderer {
public:
    // 进程级单例：引用计数恒为 1
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return 1; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return 1; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) noexcept override {
        if (out == nullptr) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) ||
            riid == __uuidof(IDWriteTextRenderer)) {
            *out = static_cast<IDWriteTextRenderer*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) noexcept override {
        *disabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* m) noexcept override {
        *m = DWRITE_MATRIX{1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* ppd) noexcept override {
        *ppd = 1.f;   // quad 位置自行按 scale 换算（字形按像素尺寸光栅，ADR-003）
        return S_OK;
    }

    // 下划线/删除线/内联对象：富文本 P1 起接入
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) noexcept override { return S_OK; }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void* ctx, FLOAT baselineOriginX, FLOAT baselineOriginY,
                                           DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) noexcept override {
        auto& c = *static_cast<DrawCtx*>(ctx);
        const uint32_t px = PixelSizeOf(run->fontEmSize, c.scale);
        float pen = 0.f;   // run 内前进量（DIP）
        for (uint32_t i = 0; i < run->glyphCount; ++i) {
            // glyphOffsets 为 null 表示全部零偏移（DWrite 约定）
            const float advance = run->glyphOffsets != nullptr ? run->glyphOffsets[i].advanceOffset : 0.f;
            const float ascend  = run->glyphOffsets != nullptr ? run->glyphOffsets[i].ascenderOffset : 0.f;
            // 字形原点（基线左端）设备坐标：保留小数，相位信息由此而来
            const float gx = (baselineOriginX + pen + advance) * c.scale;
            const float gy = (baselineOriginY - ascend) * c.scale;
            pen += run->glyphAdvances[i];

            // 1/4 像素量化：q4 = round(gx*4)，相位 = q4 mod 4，整数部分 = floor(q4/4)
            const int q4 = static_cast<int>(std::lround(gx * kSubPixelScale));
            const uint32_t phase = static_cast<uint32_t>((q4 % 4 + 4) % 4);
            const float gxFloor = std::floor(static_cast<float>(q4) * kSubPixelStep);

            const GlyphUV& g = c.engine->GetGlyph(run->fontFace, run->glyphIndices[i], px, phase);
            if (g.w == 0) continue;   // 空白等无位图字形
            // 位图内已烘焙相位偏移（left/top 为该相位下相对原点的整数偏移）
            c.painter->AddGlyphQuad(g.page, gxFloor + static_cast<float>(g.left),
                                    std::floor(gy) + static_cast<float>(g.top),
                                    static_cast<float>(g.w), static_cast<float>(g.h),
                                    g.u0, g.v0, g.u1, g.v1, c.color);
        }
        return S_OK;
    }
};

GlyphRenderer& Renderer() {
    static GlyphRenderer r;
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------

Engine& Engine::Get() {
    static Engine e;
    return e;
}

Engine::Engine() {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&factory_)))) {
        factory_ = nullptr;
        return;
    }
    // 灰度 AA + 亚像素相位需要 IDWriteFactory2 的 CreateGlyphRunAnalysis
    // （带 antialiasMode 参数的老接口无法请求灰度抗锯齿）
    factory_->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(&factory2_));
    factory_->GetSystemFontCollection(&fonts_, FALSE);
    factory_->CreateTextFormat(L"Microsoft YaHei", fonts_, DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                               16.f, L"en-us", &baseFormat_);
}

Engine::~Engine() {
    if (baseFormat_ != nullptr) baseFormat_->Release();
    if (fonts_ != nullptr) fonts_->Release();
    if (factory2_ != nullptr) factory2_->Release();
    if (factory_ != nullptr) factory_->Release();
}

Paragraph* Engine::Shape(Paragraph* p, const wchar_t* text, float fontSize,
                         uint32_t weight, float lineHeight, float maxWidth,
                         const wchar_t* family) {
    if (!Ready()) return nullptr;
    const wchar_t* fam = (family != nullptr && family[0] != 0) ? family : nullptr;
    if (p != nullptr && p->fontSize == fontSize && p->weight == weight &&
        p->lineHeight == lineHeight && p->maxWidth == maxWidth &&
        wcscmp(p->family.c_str(), fam != nullptr ? fam : L"") == 0 &&
        wcscmp(p->text.c_str(), text) == 0) {
        return p;   // 缓存命中
    }

    if (p == nullptr) p = new Paragraph();
    if (p->layout != nullptr) { p->layout->Release(); p->layout = nullptr; }
    p->text = text;
    p->family = fam != nullptr ? fam : L"";
    p->fontSize = fontSize;
    p->weight = weight;
    p->lineHeight = lineHeight;
    p->maxWidth = maxWidth;
    const uint32_t len = static_cast<uint32_t>(p->text.size());

    const HRESULT hr = factory_->CreateTextLayout(p->text.c_str(), len, baseFormat_,
                                                  maxWidth, 0.f, &p->layout);
    if (SUCCEEDED(hr) && len > 0) {
        const DWRITE_TEXT_RANGE all{0, len};
        p->layout->SetFontSize(fontSize, all);
        p->layout->SetFontWeight(static_cast<DWRITE_FONT_WEIGHT>(weight), all);
        if (fam != nullptr) {
            // 开发者指定字体族（系统已安装任意字体）；未安装时 DWrite 自动回退默认
            p->layout->SetFontFamilyName(fam, all);
        }
        IDWriteTextLayout3* l3 = nullptr;
        if (SUCCEEDED(p->layout->QueryInterface(&l3))) {
            // 03-Style lineHeight = 倍数 → DWrite PROPORTIONAL（行距与基线同倍缩放）
            l3->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, lineHeight, lineHeight);
            l3->Release();
        }
    }
    if (FAILED(hr) && p->layout != nullptr) {
        p->layout->Release();
        p->layout = nullptr;
    }

    DWRITE_TEXT_METRICS m{};
    if (p->layout != nullptr) p->layout->GetMetrics(&m);
    // 空段落保持行高占位（与 M3 估算一致），避免布局塌陷
    p->size = {m.width, len > 0 ? m.height : fontSize * lineHeight};
    return p;
}

void Engine::Discard(Paragraph* p) {
    if (p == nullptr) return;
    if (p->layout != nullptr) p->layout->Release();
    delete p;
}

void Engine::Draw(Paragraph* p, graphics::Painter& painter, float scale, Vec2 origin, Color color) {
    if (p == nullptr || p->layout == nullptr) return;
    DrawCtx c{this, &painter, scale, color};
    p->layout->Draw(&c, &Renderer(), origin.x, origin.y);
}

const GlyphUV& Engine::GetGlyph(const void* face, uint32_t glyphIndex, uint32_t pixelSize,
                                uint32_t phase) {
    return atlas_.Get(this, face, glyphIndex, pixelSize, phase, &Engine::RasterCb);
}

bool Engine::RasterCb(void* ctx, const void* face, uint32_t glyphIndex, uint32_t pixelSize,
                      uint32_t phase, std::vector<uint8_t>& out, int& left, int& top, int& w, int& h) {
    Engine& en = *static_cast<Engine*>(ctx);
    FLOAT adv = 0.f;
    UINT16 gi = static_cast<UINT16>(glyphIndex);
    DWRITE_GLYPH_RUN run{};
    run.fontFace = static_cast<IDWriteFontFace*>(const_cast<void*>(face));
    run.fontEmSize = static_cast<FLOAT>(pixelSize);   // emSize = 像素尺寸 + ppd=1 → 光栅即物理像素
    run.glyphCount = 1;
    run.glyphIndices = &gi;
    run.glyphAdvances = &adv;
    run.bidiLevel = 0;

    // 亚像素相位：轮廓右移 phase/4 像素。transform 在 emSize 缩放之后生效，
    // 故 dx 单位即物理像素（对齐 Qt 4 相位 / Skia subX）。
    const DWRITE_MATRIX xform{1.f, 0.f, 0.f, 1.f,
                               static_cast<float>(phase) * kSubPixelStep, 0.f};
    // 小字号 NATURAL（横向 AA，锐利）；≥20px NATURAL_SYMMETRIC（横纵都 AA，
    // 曲线/斜线更平滑）——Skia 的同款经验规则。
    const DWRITE_RENDERING_MODE mode = pixelSize >= kSymmetricPixelSize
        ? DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC
        : DWRITE_RENDERING_MODE_NATURAL;

    IDWriteGlyphRunAnalysis* an = nullptr;
    // 显式请求灰度 AA：拿到 1 字节/像素的整像素覆盖率（而非子像素覆盖率）
    if (FAILED(en.factory2_->CreateGlyphRunAnalysis(&run, &xform, mode,
                                                    DWRITE_MEASURING_MODE_NATURAL,
                                                    DWRITE_GRID_FIT_MODE_ENABLED,
                                                    DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                                                    0.f, 0.f, &an))) {
        return false;
    }
    RECT rc{};
    const HRESULT hr = an->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &rc);
    if (FAILED(hr) || rc.right <= rc.left || rc.bottom <= rc.top) {
        an->Release();
        return true;   // 空白字形：合法的无位图占位
    }
    const int gw = rc.right - rc.left;
    const int gh = rc.bottom - rc.top;
    out.resize(static_cast<size_t>(gw) * gh);
    const HRESULT hrTex = an->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &rc,
                                                 out.data(), static_cast<UINT>(out.size()));
    an->Release();
    if (FAILED(hrTex)) return false;

    w = gw;
    h = gh;
    left = rc.left;   // 该相位下相对字形原点的整数偏移（含相位平移）
    top = rc.top;
    return true;
}

}  // namespace dui::text
