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

// 行信息（编辑控件用；一次遍历取全部行）
struct LineInfo {
    uint32_t start = 0;    // 行首字符索引
    uint32_t end = 0;      // 行尾字符索引（不含换行符）
    float    top = 0.f;    // 行顶（相对排版原点，DIP）
    float    height = 0.f; // 行高（DIP）
    float    baseline = 0.f;  // 基线（相对行顶，DIP）—— IME 候选窗口对齐点
};

class Engine {
public:
    static Engine& Get();

    bool Ready() const noexcept { return factory_ != nullptr && factory2_ != nullptr && baseFormat_ != nullptr; }
    GlyphAtlas& Atlas() noexcept { return atlas_; }
    // phase = 水平亚像素相位（0..3，每相位 1/4 像素；对齐 Qt 6.11 的 4 相位定位）
    const GlyphUV& GetGlyph(const void* face, uint32_t glyphIndex, uint32_t pixelSize, uint32_t phase);

    // ensure 语义：缓存键全等则复用（measure 每帧调用，命中零 COM 开销）。
    // family = nullptr / 空串 → 引擎默认（微软雅黑）；否则须为系统已安装字体族名，
    // 或经 LoadFontFile 装入的字体族名
    Paragraph* Shape(Paragraph* cached, const wchar_t* text, float fontSize,
                     uint32_t weight, float lineHeight, float maxWidth,
                     const wchar_t* family);
    void Discard(Paragraph* p);

    // 加载未安装的字体文件（ttf/otf）为进程内字体集合（DWrite FontCollectionBuilder，
    // Win10+）。成功后 familyOut 返回其字体族名（可用于 Shape/FontFamily）；
    // 引擎生命周期内只装一个文件，重复调用直接返回已装族名。
    bool LoadFontFile(const wchar_t* path, String& familyOut);

    // 收集字形 quad。originDip = 文本原点（元素 bounds + padding）的 DIP 坐标
    void Draw(Paragraph* p, graphics::Painter& painter, float scale, Vec2 originDip, Color color);

    // ---- 命中测试与几何（编辑控件用；M5.9）----
    // 局部 DIP 坐标 → 最近的字符插入点（0..文本长度）
    uint32_t HitTestPoint(Paragraph* p, Vec2 localDip);
    // 光标矩形（相对排版原点）：宽 1 DIP，高 = 行高
    bool CaretRect(Paragraph* p, uint32_t index, Rect& out);
    // 选区矩形（相对排版原点）：逐行一个矩形
    void SelectionRects(Paragraph* p, uint32_t start, uint32_t end, std::vector<Rect>& out);
    // 全部行的字符区间与几何（一次遍历）
    void LineInfos(Paragraph* p, std::vector<LineInfo>& out);

    // 单字符的**墨迹中心**（相对基线，DIP，向上为正）：掩码字符垂直居中补偿用。
    // bullet 类字符（•/●）的字形墨迹贴着基线，直接绘制会明显偏下；测量后
    // 可把圆点补偿到行高中心。字体缺该字形或度量失败 → 返回 false。
    bool GlyphInkCenterDip(Paragraph* p, wchar_t ch, float& outCenterDip);

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
    IDWriteFontCollection* customFonts_ = nullptr;  // LoadFontFile 装入的字体集合
    String                 customFamily_;          // 集合内首字体的族名（空 = 未装入）
    GlyphAtlas             atlas_;
};

}  // namespace dui::text
