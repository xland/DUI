// ============================================================================
//  DUI/RichText/CssValue.h — CSS 值白名单解析（文档数据 → 强类型）
//
//  框架内部尺寸一律是不带单位的 float DIP，没有 px/em/% 单位系统，
//  因此富文本 JSON 里的 CSS 字符串必须在这一层落地（RichEditor.md §3.3）。
//
//  统一约定：解析失败返回 false，调用方保留默认值并自行计数 ——
//  这里不抛错、不写诊断，保持纯函数。
// ============================================================================

#pragma once

#include "Core/Core.h"
#include "RichText/RichDoc.h"

namespace dui {
namespace richtext {

// 边框简写："4px solid #007bff"
struct BorderSpec {
    float width = 0.f;
    Color color = 0;
    bool  solid = false;
};

// 长度："32px" / "32" / "20pt" / "1.2em"（em 需要 emBase = 父字号）
bool ParseLength(const String& text, float& out, float emBase = 0.f);

// 颜色："#2c3e50" / "#abc" / "#aarrggbb"
bool ParseColor(const String& text, Color& out);

// 字重："normal" / "bold" / "600"
bool ParseWeight(const String& text, uint32_t& out);

// 四边："2px" / "2px 6px" / "1px 2px 3px" / "1px 2px 3px 4px"（CSS 展开规则）
bool ParseThickness(const String& text, Thickness& out, float emBase = 0.f);

// 边框简写："4px solid #007bff" / "none"
bool ParseBorder(const String& text, BorderSpec& out);

// 行高："1.6" → {1.6, isMultiple=true}；"24px" → {24, false}
bool ParseLineHeight(const String& text, float& out, bool& isMultiple);

// 水平对齐："left" / "center" / "right" / "justify"（justify 暂归 End）
bool ParseAlign(const String& text, Align& out);

// 字体样式 / 装饰线
bool ParseFontStyle(const String& text, bool& italic);
bool ParseDecoration(const String& text, bool& underline, bool& strike);

// 族名列表："Arial, sans-serif" → 第一个本机可用族名。
// 全不可用时 out = 列表第一个（仍返回 false），调用方按"回退默认字体"处理。
bool ParseFontFamily(const String& text, String& out);

// 尺寸："300" / "300px" / "auto" / "100%"
//   isAuto  → 自动（按宽高比）
//   ratio>0 → 百分比（相对容器）
bool ParseSize(const String& text, float& outPx, bool& isAuto, float& outRatio);

}  // namespace richtext
}  // namespace dui
