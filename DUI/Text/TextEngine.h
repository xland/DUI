// ============================================================================
//  DUI/Text/TextEngine.h — DirectWrite 排版与字形绘制（内部头，不进 DUI.h）
//
//  排版基座（11-Text §3 已定）：IDWriteTextLayout 段落级直接用，自排
//  （IDWriteTextAnalyzer）为瓶颈后备，不预做。一个 Text 元素 = 一个段落 =
//  一个缓存的 IDWriteTextLayout（键：文本/字号/字重/行高/换行宽）。
//
//  单位（12-Windowing §5）：排版与测量全程 DIP；字形按像素尺寸离散光栅
//  （pixelSize = round(fontSizeDip × scale)，< 9px 夹紧，§9-3）。
// ============================================================================
#pragma once

#include <dwrite_2.h>

#include <cstdint>
#include <vector>

#include "Core/Core.h"
#include "Text/GlyphAtlas.h"

namespace dui::graphics {
class Painter;
}

namespace dui::text {

// 段落排版结果（Text 元素经 pimpl 持有；生命周期归元素）
struct Paragraph {
    IDWriteTextLayout* layout = nullptr;
    // 缓存键：任一变化才重建（11-Text §3 按需失效）
    String   text;
    String   family;             // 字体族；空 = 引擎默认（微软雅黑）
    float    fontSize = 0.f;     // DIP
    uint32_t weight = 0;         // DWRITE_FONT_WEIGHT
    float    lineHeight = 0.f;   // 倍数（03-Style）
    float    maxWidth = 0.f;     // 换行宽（DIP）
    Vec2     size;               // 排版结果（DIP）
};

class Engine {
public:
    static Engine& Get();

    bool Ready() const noexcept { return factory_ != nullptr && factory2_ != nullptr && baseFormat_ != nullptr; }
    GlyphAtlas& Atlas() noexcept { return atlas_; }
    // phase = 水平亚像素相位（0..3，每相位 1/4 像素；对齐 Qt 6.11 的 4 相位定位）
    const GlyphUV& GetGlyph(const void* face, uint32_t glyphIndex, uint32_t pixelSize, uint32_t phase);

    // ensure 语义：缓存键全等则复用（measure 每帧调用，命中零 COM 开销）。
    // family = nullptr / 空串 → 引擎默认（微软雅黑）；否则须为系统已安装字体族名
    Paragraph* Shape(Paragraph* cached, const wchar_t* text, float fontSize,
                     uint32_t weight, float lineHeight, float maxWidth,
                     const wchar_t* family);
    void Discard(Paragraph* p);

    // 收集字形 quad。originDip = 文本原点（元素 bounds + padding）的 DIP 坐标
    void Draw(Paragraph* p, graphics::Painter& painter, float scale, Vec2 originDip, Color color);

private:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    static bool RasterCb(void* ctx, const void* fontFace, uint32_t glyphIndex,
                         uint32_t pixelSize, uint32_t phase,
                         std::vector<uint8_t>& out,
                         int& left, int& top, int& w, int& h);

    IDWriteFactory*        factory_ = nullptr;
    IDWriteFactory2*       factory2_ = nullptr;   // 灰度 AA + 1/4 像素相位（CreateGlyphRunAnalysis 带 antialiasMode）
    IDWriteFontCollection* fonts_ = nullptr;
    IDWriteTextFormat*     baseFormat_ = nullptr;   // 家族/风格基座；字号字重由 layout 覆盖
    GlyphAtlas             atlas_;
};

}  // namespace dui::text
