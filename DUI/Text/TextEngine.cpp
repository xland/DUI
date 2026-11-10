// ============================================================================
//  DUI/Text/TextEngine.cpp — DWrite 工厂/系统字体集/段落排版/字形光栅与绘制
// ============================================================================

#include "Text/TextEngine.h"

#include <dwrite_3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>

#include "Graphics/Graphics.h"

#pragma comment(lib, "dwrite.lib")

namespace dui::text {

namespace {

// 文本光栅化参数（对标 Qt Quick 6.11 / Skia(Chromium·Avalonia) 的 DirectWrite 路径）：
//
//  ① ClearType 子像素抗锯齿：DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE +
//     DWRITE_TEXTURE_CLEARTYPE_3x1（每像素 3 字节 = 左/中/右子像素覆盖率）。
//     灰度 AA（ALIASED_1x1）只有整像素覆盖，横向分辨率是子像素的 1/3，
//     小字号汉字观感发虚；ClearType 把横向三倍的覆盖率交给渲染端逐通道混合。
//     图集页为 RGBA8（R/G/B 覆盖，A = 最大值）。
//
//  ② 渲染模式：统一用 DWRITE_RENDERING_MODE_NATURAL（不做
//     symmetrization）。
//     曾按 Skia 的经验规则对 ≥20px 用 NATURAL_SYMMETRIC（横纵都 AA，曲线更
//     平滑），但 **symmetrization 会改写字形轮廓**（把上半部镜像对称到下半部），
//     对上下结构的汉字破坏明显：实测 24px 的「第」字下部两横被挤到几乎粘连。
//     该问题只在**新字号档位**出现 —— 也就是系统 DPI 变化（新 pixelSize）后
//     才会用新档位重光栅，看起来"只在 DPI 变化时文字出问题"。
//     汉字为主的界面不需要 symmetrization，故统一用 NATURAL（跨字号形态一致）。
//
//  ③ 不做 gamma/contrast 后处理：Qt(FreeType) 与 Skia 都不对 coverage 做
//     S 曲线增强；Skia 的 pre-blend 是方向相反的线性化表，且仅在 sRGB surface
//     生效。自造 S 曲线会把 AA 过渡带压窄 → 观感发虚且颗粒。

// 亚像素相位：4 相位（1/4 像素 = DWrite NATURAL 模式的内部量化粒度，对齐
// Qt 6.11 的 4 相位定位 / Skia subX）。相位按 x 的小数部分量化进图集键，
// 各相位分别光栅（相位烘焙进覆盖率位图），quad 落回整数像素供 POINT 采样。
// 代价：图集内存 ×4（LRU 预算兜底）。
constexpr float kSubPixelScale = 4.f;
constexpr float kSubPixelStep  = 1.f / kSubPixelScale;
constexpr int   kSubPixelPhases = static_cast<int>(kSubPixelScale);

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

// ---------------------------------------------------------------------------
//  绘制效果（富文本按 run 取色）
//
//  颜色挂在 layout 的 run 区间上（IDWriteTextLayout::SetDrawingEffect），
//  DrawGlyphRun / DrawUnderline / DrawStrikethrough 的回调第 6 参数
//  clientDrawingEffect 就是它。引用计数归 layout —— SetDrawingEffect 会 AddRef，
//  layout 释放时 Release；Shape 里我们只清掉自己那份初始引用。
//
//  单样式段落完全不创建该对象（Draw 的 color 参数兜底），故旧路径零开销。
// ---------------------------------------------------------------------------

// 私有 IID：用于区分"我们的颜色对象"与其他来源的绘制效果
constexpr GUID kColorEffectIid = {
    0x7f3a1c62, 0x9b44, 0x4d1e, {0x8a, 0x51, 0x2c, 0x6d, 0x0f, 0x93, 0xb7, 0x12}};

class ColorEffect final : public IUnknown {
public:
    explicit ColorEffect(Color color) noexcept : color_(color) {}

    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs_; }

    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) noexcept override {
        if (out == nullptr) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == kColorEffectIid) {
            *out = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    Color color() const noexcept { return color_; }

private:
    ULONG refs_ = 1;
    Color color_ = 0;
};

// effect 为空 / 非本模块对象 → 用调用方传入的兜底色
Color EffectColor(IUnknown* effect, Color fallback) noexcept {
    if (effect == nullptr) return fallback;
    void* raw = nullptr;
    if (FAILED(effect->QueryInterface(kColorEffectIid, &raw)) || raw == nullptr) return fallback;
    const Color color = static_cast<ColorEffect*>(raw)->color();
    static_cast<IUnknown*>(raw)->Release();
    return color;
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

    // 下划线 / 删除线：位置与厚度由 DWrite 算好（DIP），直接画一条线
    HRESULT STDMETHODCALLTYPE DrawUnderline(void* ctx, FLOAT baselineOriginX, FLOAT baselineOriginY,
                                            const DWRITE_UNDERLINE* underline,
                                            IUnknown* effect) noexcept override {
        if (underline == nullptr) return S_OK;
        auto& c = *static_cast<DrawCtx*>(ctx);
        const Color color = EffectColor(effect, c.color);
        const float y = baselineOriginY + underline->offset;
        c.painter->StrokeLine({baselineOriginX, y}, {baselineOriginX + underline->width, y},
                              color, underline->thickness, c.scale);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void* ctx, FLOAT baselineOriginX, FLOAT baselineOriginY,
                                                const DWRITE_STRIKETHROUGH* strike,
                                                IUnknown* effect) noexcept override {
        if (strike == nullptr) return S_OK;
        auto& c = *static_cast<DrawCtx*>(ctx);
        const Color color = EffectColor(effect, c.color);
        const float y = baselineOriginY + strike->offset;
        c.painter->StrokeLine({baselineOriginX, y}, {baselineOriginX + strike->width, y},
                              color, strike->thickness, c.scale);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) noexcept override { return S_OK; }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void* ctx, FLOAT baselineOriginX, FLOAT baselineOriginY,
                                           DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown* effect) noexcept override {
        auto& c = *static_cast<DrawCtx*>(ctx);
        const Color runColor = EffectColor(effect, c.color);   // 富文本：该 run 的颜色
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

            // 相位量化：q = round(gx * 相位分母)，相位 = q mod 分母，整数部分 = floor(q/分母)。
            // 4 相位 = DWrite NATURAL 的 1/4 像素内部量化粒度；取整误差 ≤ 1/8 像素。
            const int q4 = static_cast<int>(std::lround(gx * kSubPixelScale));
            const uint32_t phase =
                static_cast<uint32_t>((q4 % kSubPixelPhases + kSubPixelPhases) % kSubPixelPhases);
            const float gxFloor = std::floor(static_cast<float>(q4) * kSubPixelStep);

            const GlyphUV& g = c.engine->GetGlyph(run->fontFace, run->glyphIndices[i], px, phase);
            if (g.w == 0) continue;   // 空白等无位图字形
            // 位图内已烘焙相位偏移（left/top 为该相位下相对原点的整数偏移）
            c.painter->AddGlyphQuad(g.page, gxFloor + static_cast<float>(g.left),
                                    std::floor(gy) + static_cast<float>(g.top),
                                    static_cast<float>(g.w), static_cast<float>(g.h),
                                    g.u0, g.v0, g.u1, g.v1, runColor);
        }
        return S_OK;
    }
};

GlyphRenderer& Renderer() {    static GlyphRenderer r;
    return r;
}

// 字形探针渲染器：只捕获 layout **实际使用**的字体面，不绘制任何内容。
// 用途：字形度量（掩码字符垂直居中补偿）。必须走渲染回调才能拿到真实字体面 ——
// layout 可能因缺字回退到别的字体，与请求的族名无关。
class GlyphProbe final : public IDWriteTextRenderer {
public:
    ~GlyphProbe() { if (face_ != nullptr) face_->Release(); }

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
        *ppd = 1.f;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) noexcept override { return S_OK; }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT, FLOAT, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) noexcept override {
        if (face_ == nullptr && run != nullptr && run->fontFace != nullptr) {
            face_ = const_cast<IDWriteFontFace*>(run->fontFace);
            face_->AddRef();
        }
        return S_OK;
    }

    IDWriteFontFace* face_ = nullptr;
};

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
    // GlyphRunAnalysis（墨迹测量；位图目标不可用时也能兜底）
    factory_->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(&factory2_));
    // ClearType 光栅：GDI 位图渲染目标 + 系统渲染参数 = 与 GDI 同源的
    // gamma/对比度增强（GlyphRunAnalysis 不做这些）；hinting 等级取 NATURAL，
    // 与 Direct2D/Notepad3 的文本路径一致（见 RasterCb）。
    factory_->GetGdiInterop(&gdi_);
    factory_->CreateRenderingParams(&renderParams_);
    factory_->GetSystemFontCollection(&fonts_, FALSE);
    factory_->CreateTextFormat(L"Microsoft YaHei", fonts_, DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                               16.f, L"en-us", &baseFormat_);
}

Engine::~Engine() {
    if (bmpRT_ != nullptr) bmpRT_->Release();
    if (renderParams_ != nullptr) renderParams_->Release();
    if (gdi_ != nullptr) gdi_->Release();
    if (baseFormat_ != nullptr) baseFormat_->Release();
    if (fonts_ != nullptr) fonts_->Release();
    if (factory2_ != nullptr) factory2_->Release();
    if (factory_ != nullptr) factory_->Release();
}

bool Engine::LoadFontFile(const wchar_t* path, String& familyOut) {
    familyOut.clear();
    if (!Ready()) return false;
    if (customFonts_ != nullptr) { familyOut = customFamily_; return true; }   // 只装一次

    // 字体文件 → 字体面引用 → FontSet → 字体集合（IDWriteFactory3，Win10+；
    // classic 的 CustomFontCollection + Loader 样板太重，此处不走）
    IDWriteFactory3* f3 = nullptr;
    if (FAILED(factory_->QueryInterface(&f3))) return false;

    bool ok = false;
    IDWriteFontFile* file = nullptr;
    IDWriteFontFaceReference* ref = nullptr;
    IDWriteFontSetBuilder* sb = nullptr;
    IDWriteFontSet* set = nullptr;
    IDWriteFontCollection1* c = nullptr;
    if (SUCCEEDED(factory_->CreateFontFileReference(path, nullptr, &file)) &&
        SUCCEEDED(f3->CreateFontFaceReference(file, 0, DWRITE_FONT_SIMULATIONS_NONE, &ref)) &&
        SUCCEEDED(f3->CreateFontSetBuilder(&sb)) &&
        SUCCEEDED(sb->AddFontFaceReference(ref)) &&
        SUCCEEDED(sb->CreateFontSet(&set)) &&
        SUCCEEDED(f3->CreateFontCollectionFromFontSet(set, &c))) {
        // 取首字体的族名（iconfont 类单字体重只有一个族）
        IDWriteFontFamily* fam = nullptr;
        if (SUCCEEDED(c->GetFontFamily(0, &fam))) {
            IDWriteLocalizedStrings* names = nullptr;
            UINT32 len = 0;
            if (SUCCEEDED(fam->GetFamilyNames(&names)) && names->GetCount() > 0 &&
                SUCCEEDED(names->GetStringLength(0, &len))) {
                std::vector<wchar_t> buf(len + 1);
                if (SUCCEEDED(names->GetString(0, buf.data(), len + 1))) {
                    customFamily_ = buf.data();
                }
            }
            if (names != nullptr) names->Release();
            fam->Release();
        }
        if (!customFamily_.empty()) {
            customFonts_ = c;   // 生命周期归引擎
            familyOut = customFamily_;
            ok = true;
        } else {
            c->Release();
        }
    }
    if (set != nullptr) set->Release();
    if (sb != nullptr) sb->Release();
    if (ref != nullptr) ref->Release();
    if (file != nullptr) file->Release();
    f3->Release();
    return ok;
}

bool Engine::HasFamily(const wchar_t* family) const noexcept {
    if (family == nullptr || family[0] == 0) return false;
    if (customFonts_ != nullptr && customFamily_ == family) return true;
    if (fonts_ == nullptr) return false;

    BOOL exists = FALSE;
    UINT32 index = 0;
    if (FAILED(fonts_->FindFamilyName(family, &index, &exists))) return false;
    return exists != FALSE;
}

Paragraph* Engine::Shape(Paragraph* p, const ShapeInput& in) {
    if (!Ready() || in.text == nullptr) return nullptr;

    const wchar_t* fam = (in.family != nullptr && in.family[0] != 0) ? in.family : nullptr;
    const uint32_t runCount = (in.runs != nullptr) ? in.runCount : 0;

    if (p != nullptr && p->fontSize == in.fontSize && p->weight == in.weight &&
        p->lineHeight == in.lineHeight && p->maxWidth == in.maxWidth &&
        p->runCount == runCount &&
        (runCount == 0 || p->runVersion == in.runVersion) &&
        wcscmp(p->family.c_str(), fam != nullptr ? fam : L"") == 0 &&
        wcscmp(p->text.c_str(), in.text) == 0) {
        return p;   // 缓存命中（run 内容变化靠 runVersion 判定，不做深比较）
    }

    if (p == nullptr) p = new Paragraph();
    if (p->layout != nullptr) { p->layout->Release(); p->layout = nullptr; }
    p->text = in.text;
    p->family = fam != nullptr ? fam : L"";
    p->fontSize = in.fontSize;
    p->weight = in.weight;
    p->lineHeight = in.lineHeight;
    p->maxWidth = in.maxWidth;
    p->runs = (runCount > 0) ? in.runs : nullptr;
    p->runCount = runCount;
    p->runVersion = in.runVersion;
    const uint32_t len = static_cast<uint32_t>(p->text.size());

    const HRESULT hr = factory_->CreateTextLayout(p->text.c_str(), len, baseFormat_,
                                                  in.maxWidth, 0.f, &p->layout);
    if (SUCCEEDED(hr) && len > 0) {
        // ① 段落默认（整段一次设置）
        const DWRITE_TEXT_RANGE all{0, len};
        p->layout->SetFontSize(in.fontSize, all);
        // 权重 0 = 未设置：DWRITE_FONT_WEIGHT 合法值从 1 起，0 会让 DWrite 就近
        // 匹配到家族里最细的 Light 面（微软雅黑 Light）→ 全局文字偏细偏灰
        p->layout->SetFontWeight(
            static_cast<DWRITE_FONT_WEIGHT>(in.weight != 0 ? in.weight : 400), all);
        if (fam != nullptr) {
            // 自定义字体集合（LoadFontFile 装入）：layout 默认挂在系统集合上，
            // 必须连集合一起替换，否则族名命中不到 → DWrite 静默回退默认字体
            if (customFonts_ != nullptr && customFamily_ == fam) {
                p->layout->SetFontCollection(customFonts_, all);
            }
            // 开发者指定字体族（系统已安装任意字体）；未安装时 DWrite 自动回退默认
            p->layout->SetFontFamilyName(fam, all);
        }
        // ② run 覆盖（仅富文本走这里；单样式完全不进）
        if (runCount > 0) ApplyRuns(p->layout, in, len);
        // ③ 行距（段落级）
        IDWriteTextLayout3* l3 = nullptr;
        if (SUCCEEDED(p->layout->QueryInterface(&l3))) {
            // 03-Style lineHeight = 倍数 → DWrite PROPORTIONAL（行距与基线同倍缩放）
            l3->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, in.lineHeight, in.lineHeight);
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
    p->size = {m.width, len > 0 ? m.height : in.fontSize * in.lineHeight};
    return p;
}

void Engine::ApplyRuns(IDWriteTextLayout* layout, const ShapeInput& in, uint32_t textLen) {
    // 同色 run 复用同一个效果对象（一屏文本的颜色种类通常个位数）
    struct EffectSlot {
        Color color;
        IUnknown* effect;
    };
    std::vector<EffectSlot> effects;

    for (uint32_t i = 0; i < in.runCount; ++i) {
        const TextRun& run = in.runs[i];
        if (run.length == 0 || run.start >= textLen) continue;
        const uint32_t length = std::min(run.length, textLen - run.start);
        const DWRITE_TEXT_RANGE range{run.start, length};

        if (run.fontSize > 0.f) layout->SetFontSize(run.fontSize, range);
        if (run.weight != 0) {
            layout->SetFontWeight(static_cast<DWRITE_FONT_WEIGHT>(run.weight), range);
        }
        if (run.family != nullptr && run.family[0] != 0) {
            if (customFonts_ != nullptr && customFamily_ == run.family) {
                layout->SetFontCollection(customFonts_, range);
            }
            layout->SetFontFamilyName(run.family, range);
        }
        if (run.italic) layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
        if (run.underline) layout->SetUnderline(TRUE, range);
        if (run.strike) layout->SetStrikethrough(TRUE, range);

        if ((run.color >> 24) != 0u) {   // A != 0：该 run 自带颜色
            IUnknown* effect = nullptr;
            for (const EffectSlot& slot : effects) {
                if (slot.color == run.color) {
                    effect = slot.effect;
                    break;
                }
            }
            if (effect == nullptr) {
                effect = static_cast<IUnknown*>(new ColorEffect(run.color));
                effects.push_back(EffectSlot{run.color, effect});
            }
            layout->SetDrawingEffect(effect, range);
        }
    }

    // 只清掉自己那份初始引用；layout 经 SetDrawingEffect 持有其余引用
    for (const EffectSlot& slot : effects) slot.effect->Release();
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

// ---------------------------------------------------------------------------
// 命中测试与几何（M5.9，编辑控件用）
//
// 只用基类 IDWriteTextLayout 的接口（HitTestPoint / HitTestTextPosition /
// HitTestTextRange / GetLineMetrics），不依赖 IDWriteTextLayout1 的扩展。
// ---------------------------------------------------------------------------

namespace {

// 取全部行度量（E_NOT_SUFFICIENT_BUFFER 时扩容重试）
bool FetchLineMetrics(IDWriteTextLayout* layout, std::vector<DWRITE_LINE_METRICS>& out) {
    out.clear();
    UINT32 count = 0;
    std::vector<DWRITE_LINE_METRICS> buf(64);
    HRESULT hr = layout->GetLineMetrics(buf.data(), static_cast<UINT32>(buf.size()), &count);
    if (hr == E_NOT_SUFFICIENT_BUFFER) {
        buf.resize(count);
        hr = layout->GetLineMetrics(buf.data(), static_cast<UINT32>(buf.size()), &count);
    }
    if (FAILED(hr)) return false;
    out.assign(buf.begin(), buf.begin() + count);
    return true;
}

}  // namespace

uint32_t Engine::HitTestPoint(Paragraph* p, Vec2 localDip) {
    if (p == nullptr || p->layout == nullptr) return 0;
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    p->layout->HitTestPoint(localDip.x, localDip.y, &trailing, &inside, &m);
    const uint32_t pos = m.textPosition;
    // 命中字符的后半边 → 插入点在该字符之后
    if (trailing && pos < static_cast<uint32_t>(p->text.size())) return pos + 1;
    return pos;
}

bool Engine::CaretRect(Paragraph* p, uint32_t index, Rect& out) {
    if (p == nullptr || p->layout == nullptr) return false;
    const uint32_t len = static_cast<uint32_t>(p->text.size());
    // isTrailingHit：该位置是否为"尾随"（位于行/文本末）。
    //   末尾（index == len）必须传 TRUE —— 否则 DWrite 返回的是**最后一个字符的
    //   前缘**，光标会压在最后一个字符上（位置错、且与文字粘连显得粗）。
    //   中间位置传 FALSE → 返回该字符的前缘（插入点语义正确）。
    const bool trailing = (index >= len);
    FLOAT x = 0.f, y = 0.f;
    DWRITE_HIT_TEST_METRICS m{};
    if (FAILED(p->layout->HitTestTextPosition(index, trailing, &x, &y, &m))) return false;
    float h = m.height;
    if (!(h > 0.f)) {
        // 文本末尾等位置度量退化 → 退回行高
        std::vector<LineInfo> lines;
        LineInfos(p, lines);
        for (const LineInfo& li : lines) {
            if (index >= li.start && index <= li.end) {
                h = li.height;
                y = li.top;
                break;
            }
        }
        if (!(h > 0.f)) h = p->size.y;
    }
    out = Rect{x, y, 1.f, h};
    return true;
}

void Engine::SelectionRects(Paragraph* p, uint32_t start, uint32_t end, std::vector<Rect>& out) {
    out.clear();
    if (p == nullptr || p->layout == nullptr || start >= end) return;
    // HitTestTextRange：返回选区覆盖的逐行几何（正是高亮需要的矩形）
    std::vector<DWRITE_HIT_TEST_METRICS> buf(16);
    UINT32 count = 0;
    HRESULT hr = p->layout->HitTestTextRange(start, end - start, 0.f, 0.f, buf.data(),
                                              static_cast<UINT32>(buf.size()), &count);
    if (hr == E_NOT_SUFFICIENT_BUFFER) {
        buf.resize(count);
        hr = p->layout->HitTestTextRange(start, end - start, 0.f, 0.f, buf.data(),
                                         static_cast<UINT32>(buf.size()), &count);
    }
    if (FAILED(hr)) return;
    const UINT32 n = (count < buf.size()) ? count : static_cast<UINT32>(buf.size());
    for (UINT32 i = 0; i < n; ++i) {
        const DWRITE_HIT_TEST_METRICS& m = buf[i];
        out.push_back(Rect{m.left, m.top, m.width, m.height});
    }
}

void Engine::LineInfos(Paragraph* p, std::vector<LineInfo>& out) {
    out.clear();
    if (p == nullptr || p->layout == nullptr) return;
    std::vector<DWRITE_LINE_METRICS> lm;
    if (!FetchLineMetrics(p->layout, lm)) return;
    uint32_t start = 0;
    float top = 0.f;
    for (const DWRITE_LINE_METRICS& m : lm) {
        LineInfo info;
        info.start = start;
        info.end = start + m.length - m.newlineLength;   // 不含换行符
        info.top = top;
        info.height = m.height;
        info.baseline = m.baseline;                      // 相对行顶（DWRITE_LINE_METRICS）
        out.push_back(info);
        start += m.length;
        top += m.height;
    }
}

// 单字符墨迹中心（相对基线，向上为正）。
// 用 GDI 兼容度量（GetGlyphOutline）：gmBlackBoxY = 墨迹顶相对基线的高度（向上
// 为正），gmBlackBoxHeight = 墨迹高；两者均以 em 为单位（emSize 传 1）。
// 单字符墨迹中心（相对基线，向上为正）。
//
// 取字体面的方式：走一次「探针渲染」（只捕获 fontFace，不绘制）—— layout 可能
// 因缺字回退到别的字体，与请求的族名无关，只有渲染回调里的 fontFace 是真的。
//
// 墨迹盒用 GlyphRunAnalysis 的 alpha 纹理范围（真实光栅墨迹，与实际渲染一致；
// 不用 GetDesignGlyphMetrics —— 设计度量的 vertical-origin 语义与实际基线
// 位置对不上，会把补偿算偏几个像素）。
bool Engine::GlyphInkCenterDip(Paragraph* p, wchar_t ch, float& outCenterDip) {
    outCenterDip = 0.f;
    if (p == nullptr || p->layout == nullptr || !(p->fontSize > 0.f)) return false;

    GlyphProbe probe;
    p->layout->Draw(nullptr, &probe, 0.f, 0.f);
    if (probe.face_ == nullptr) return false;

    const UINT32 cp = static_cast<UINT32>(ch);
    UINT16 glyphIndex = 0;
    // 旧版 SDK 的 GetGlyphIndices 只有 3 个参数（无 exists 输出）；
    // 字形索引 0 = .notdef（字体缺该字符）→ 视为测不到
    if (FAILED(probe.face_->GetGlyphIndices(&cp, 1, &glyphIndex)) || glyphIndex == 0) return false;

    // 以 4 倍字号测量：alpha 纹理范围是像素对齐的，放大后精度更高（1/4 单位）
    const float emSize = p->fontSize * 4.f;
    FLOAT advance = 0.f;
    DWRITE_GLYPH_RUN run{};
    run.fontFace = probe.face_;
    run.fontEmSize = emSize;
    run.glyphCount = 1;
    run.glyphIndices = &glyphIndex;
    run.glyphAdvances = &advance;
    run.bidiLevel = 0;

    IDWriteGlyphRunAnalysis* an = nullptr;
    if (FAILED(factory2_->CreateGlyphRunAnalysis(&run, nullptr, DWRITE_RENDERING_MODE_NATURAL,
                                                 DWRITE_MEASURING_MODE_NATURAL,
                                                 DWRITE_GRID_FIT_MODE_DISABLED,
                                                 DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                                                 0.f, 0.f, &an))) {
        return false;
    }
    RECT rc{};
    // 范围相对基线、y 向下为正（字形在基线上方 → top/bottom 为负）
    const bool ok = SUCCEEDED(an->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &rc)) &&
                    rc.bottom > rc.top;
    if (ok) {
        const float top = static_cast<float>(rc.top);
        const float h = static_cast<float>(rc.bottom - rc.top);
        outCenterDip = -(top + h * 0.5f) / 4.f;   // 中心（向上为正），换回 DIP
    }
    an->Release();
    return ok;
}

const GlyphUV& Engine::GetGlyph(const void* face, uint32_t glyphIndex, uint32_t pixelSize,
                                uint32_t phase) {
    return atlas_.Get(this, face, glyphIndex, pixelSize, phase, &Engine::RasterCb);
}

bool Engine::RasterCb(void* ctx, const void* face, uint32_t glyphIndex, uint32_t pixelSize,
                      uint32_t phase, std::vector<uint8_t>& out, int& left, int& top, int& w, int& h) {
    Engine& en = *static_cast<Engine*>(ctx);
    if (en.gdi_ == nullptr || en.renderParams_ == nullptr) return false;

    // 光栅走 IDWriteBitmapRenderTarget：内部就是 DWrite 的 ClearType 渲染器
    // （"GDI" 只指输出是 GDI DIB 内存），gamma 与对比度增强来自系统渲染参数。
    // （GlyphRunAnalysis 即使指定 CLEARTYPE 模式也不做 gamma/对比度，
    //   小字号汉字因此明显偏细偏灰。）
    // 位图按字号档位缓存：边长 = 3 倍字高，容得下上下伸展与降部。
    const uint32_t need = pixelSize * 3 + 16;
    if (en.bmpRT_ == nullptr || en.bmpSize_ != need) {
        if (en.bmpRT_ != nullptr) {
            en.bmpRT_->Release();
            en.bmpRT_ = nullptr;
        }
        en.bmpBits_ = nullptr;
        en.bmpStride_ = 0;
        en.bmpSize_ = 0;
        IDWriteBitmapRenderTarget* rt = nullptr;
        if (FAILED(en.gdi_->CreateBitmapRenderTarget(nullptr, need, need, &rt)) || rt == nullptr) {
            return false;
        }
        rt->SetPixelsPerDip(1.f);   // DIP = 像素：run 的 emSize 就是物理像素尺寸
        HDC dc = rt->GetMemoryDC();
        HBITMAP hbmp = static_cast<HBITMAP>(GetCurrentObject(dc, OBJ_BITMAP));
        DIBSECTION ds{};
        if (hbmp == nullptr || GetObjectW(hbmp, sizeof(ds), &ds) == 0 ||
            ds.dsBm.bmBits == nullptr) {
            rt->Release();
            return false;
        }
        en.bmpRT_ = rt;
        en.bmpBits_ = static_cast<uint8_t*>(ds.dsBm.bmBits);
        en.bmpStride_ = static_cast<uint32_t>(ds.dsBm.bmWidthBytes);
        en.bmpSize_ = need;
    }

    // 白底黑字：GDI 的 ClearType 合成依赖背景色 —— "暗字亮底"方向的 gamma/对比度
    // 增强才能让笔画变实（Notepad3 观感的来源）。此前用"黑底白字"取的是亮字方向
    // 的增强曲线，套到黑字白底上方向相反 → 边缘偏灰偏细。
    // 输出像素即"GDI 在白底上画黑字"的最终颜色，反解覆盖率：cov = 255 − pixel。
    // 图集里存增强后的覆盖率，GPU 端逐通道混合的结果与 GDI 直绘一致。
    // （深底亮字是近似：增强方向不匹配，笔画略偏粗 —— 与 Skia 的 pre-blend 取舍一致。）
    std::memset(en.bmpBits_, 0xFF, static_cast<size_t>(en.bmpStride_) * en.bmpSize_);

    FLOAT adv = 0.f;
    UINT16 gi = static_cast<UINT16>(glyphIndex);
    IDWriteFontFace* fontFace = static_cast<IDWriteFontFace*>(const_cast<void*>(face));
    // run 的 advance 必须是真实值：GDI 的 ClearType 要按字符格做 hinting，
    // advance = 0 会让它退化成"没有格子"的自由定位，笔画 hinting 随之失效。
    DWRITE_FONT_METRICS fm{};
    DWRITE_GLYPH_METRICS gm{};
    fontFace->GetMetrics(&fm);
    if (fm.designUnitsPerEm > 0 && SUCCEEDED(fontFace->GetDesignGlyphMetrics(&gi, 1, &gm))) {
        adv = static_cast<FLOAT>(gm.advanceWidth) * static_cast<FLOAT>(pixelSize) /
              static_cast<FLOAT>(fm.designUnitsPerEm);
    }

    DWRITE_GLYPH_RUN run{};
    run.fontFace = fontFace;
    run.fontEmSize = static_cast<FLOAT>(pixelSize);
    run.glyphCount = 1;
    run.glyphIndices = &gi;
    run.glyphAdvances = &adv;
    run.bidiLevel = 0;

    // 原点放在位图上部：基线取 3/4 高度处；x 加上相位小数偏移（1/4 像素粒度），
    // 子像素相位由 DWrite NATURAL 光栅烘焙进覆盖率位图（图集键含相位，各相位
    // 分别缓存）。绘制端 quad 取整像素（gxFloor + left），相位随位图一起还原。
    const FLOAT originX = static_cast<FLOAT>(en.bmpSize_ / 2) +
                          static_cast<FLOAT>(phase) / kSubPixelScale;
    const FLOAT originY = static_cast<FLOAT>(en.bmpSize_ * 3 / 4);
    RECT bb{};
    // NATURAL 测量 → CLEARTYPE_NATURAL 渲染：不做 GDI 网格拟合（笔画纤细锐利，
    // 与 Direct2D/Notepad3 的 D2D 文本路径同款）；GDI_CLASSIC 的全 hinting 会把
    // 笔画加重，观感偏粗。
    if (FAILED(en.bmpRT_->DrawGlyphRun(originX, originY, DWRITE_MEASURING_MODE_NATURAL, &run,
                                       en.renderParams_, 0x00000000u, &bb))) {
        return false;
    }

    const int x0 = static_cast<int>(std::max<LONG>(bb.left, 0));
    const int y0 = static_cast<int>(std::max<LONG>(bb.top, 0));
    const int x1 = static_cast<int>(std::min<LONG>(bb.right, static_cast<LONG>(en.bmpSize_)));
    const int y1 = static_cast<int>(std::min<LONG>(bb.bottom, static_cast<LONG>(en.bmpSize_)));
    if (x1 <= x0 || y1 <= y0) return true;   // 空白字形：合法的无位图占位

    // DIB（32bpp BI_RGB）内存序 B,G,R,X → 图集 RGBA8
    const int gw = x1 - x0;
    const int gh = y1 - y0;
    out.resize(static_cast<size_t>(gw) * gh * 4);
    for (int y = 0; y < gh; ++y) {
        const uint8_t* s = en.bmpBits_ + static_cast<size_t>(y0 + y) * en.bmpStride_ +
                           static_cast<size_t>(x0) * 4;
        uint8_t* d = out.data() + static_cast<size_t>(y) * gw * 4;
        for (int x = 0; x < gw; ++x, s += 4, d += 4) {
            // 反解覆盖率：像素越暗 = 覆盖率越高。DIB 内存序 B,G,R → 图集 R,G,B
            const uint8_t b = static_cast<uint8_t>(255u - s[0]);
            const uint8_t g = static_cast<uint8_t>(255u - s[1]);
            const uint8_t r = static_cast<uint8_t>(255u - s[2]);
            d[0] = r;
            d[1] = g;
            d[2] = b;
            d[3] = r > g ? (r > b ? r : b) : (g > b ? g : b);
        }
    }

    w = gw;
    h = gh;
    left = x0 - static_cast<int>(originX);
    top = y0 - static_cast<int>(originY);
    return true;
}

}  // namespace dui::text

// ---------------------------------------------------------------------------
// 公共 API（DUI.h）：加载字体文件 → 返回字体族名
// ---------------------------------------------------------------------------

namespace dui {

String LoadFontFamily(const wchar_t* fontFilePath) {
    String family;
    text::Engine::Get().LoadFontFile(fontFilePath, family);
    return family;
}

}  // namespace dui
