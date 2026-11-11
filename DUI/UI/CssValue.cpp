// ============================================================================
//  DUI/UI/CssValue.cpp — CSS 值白名单解析（文档数据 → 强类型）
//
//  只用无状态的小工具函数：不依赖 locale（自己走数字），不抛错、不写日志。
//  唯一的外部查询是族名可用性（Text::Engine::HasFamily），这也是 CSS
//  fontFamily 列表"取第一个本机可用"的唯一实现途径。
// ============================================================================

#include "UI/CssValue.h"

#include "Graphics/TextEngine.h"

#include <cmath>

namespace dui::richtext {
namespace {

// ---- 区间视图 + 大小写不敏感比较 -------------------------------------------

struct View {
    const wchar_t* begin = nullptr;
    const wchar_t* end = nullptr;
};

bool Empty(View v) noexcept { return v.begin == v.end; }
size_t Length(View v) noexcept { return static_cast<size_t>(v.end - v.begin); }

bool IsSpace(wchar_t c) noexcept {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

bool IsDigit(wchar_t c) noexcept { return c >= L'0' && c <= L'9'; }

wchar_t Lower(wchar_t c) noexcept {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

View Trim(const wchar_t* begin, const wchar_t* end) noexcept {
    while (begin != end && IsSpace(*begin)) ++begin;
    while (end != begin && IsSpace(end[-1])) --end;
    return {begin, end};
}

View Trim(const String& text) noexcept { return Trim(text.data(), text.data() + text.size()); }

// 大小写不敏感地等于字面量
bool Equal(View v, const wchar_t* literal) noexcept {
    size_t n = 0;
    while (literal[n] != L'\0') ++n;
    if (Length(v) != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (Lower(v.begin[i]) != Lower(literal[i])) return false;
    }
    return true;
}

String ToString(View v) { return String(v.begin, v.end); }

// ---- 数字：符号 + 整数 + 可选小数，整段必须吃完 ---------------------------

bool ParseNumber(View v, float& out) noexcept {
    const wchar_t* p = v.begin;
    if (p == v.end) return false;

    bool negative = false;
    if (*p == L'+' || *p == L'-') {
        negative = (*p == L'-');
        ++p;
    }

    double value = 0.0;
    int digits = 0;
    while (p != v.end && IsDigit(*p)) {
        value = value * 10.0 + static_cast<double>(*p - L'0');
        ++p;
        ++digits;
    }
    if (p != v.end && *p == L'.') {
        ++p;
        double unit = 0.1;
        while (p != v.end && IsDigit(*p)) {
            value += static_cast<double>(*p - L'0') * unit;
            unit *= 0.1;
            ++p;
            ++digits;
        }
    }

    if (digits == 0 || p != v.end) return false;
    const float result = static_cast<float>(negative ? -value : value);
    if (!std::isfinite(result)) return false;
    out = result;
    return true;
}

// ---- 十六进制 ----

bool ParseHexDigit(wchar_t c, uint32_t& out) noexcept {
    if (c >= L'0' && c <= L'9') {
        out = static_cast<uint32_t>(c - L'0');
        return true;
    }
    const wchar_t low = Lower(c);
    if (low >= L'a' && low <= L'f') {
        out = static_cast<uint32_t>(low - L'a' + 10);
        return true;
    }
    return false;
}

bool ParseHex32(View v, uint32_t& out) noexcept {
    uint32_t value = 0;
    for (const wchar_t* p = v.begin; p != v.end; ++p) {
        uint32_t digit = 0;
        if (!ParseHexDigit(*p, digit)) return false;
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

// 按空白切分，逐个交给 f（回调返回 false 即中止）
template <class Fn>
bool ForEachToken(View v, Fn&& f) {
    const wchar_t* p = v.begin;
    while (p != v.end) {
        while (p != v.end && IsSpace(*p)) ++p;
        if (p == v.end) break;
        const wchar_t* start = p;
        while (p != v.end && !IsSpace(*p)) ++p;
        if (!f(View{start, p})) return false;
    }
    return true;
}

// CSS 泛型族关键字 → 本机代表字体（DWrite 只吃具体族名）
const wchar_t* GenericFamily(View name) noexcept {
    struct Entry {
        const wchar_t* keyword;
        const wchar_t* family;
    };
    static constexpr Entry kEntries[] = {
        {L"sans-serif", L"Microsoft YaHei"},
        {L"serif",      L"Times New Roman"},
        {L"monospace",  L"Consolas"},
    };
    for (const Entry& entry : kEntries) {
        if (Equal(name, entry.keyword)) return entry.family;
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// 长度 / 颜色 / 字重
// ---------------------------------------------------------------------------

bool ParseLength(const String& text, float& out, float emBase) {
    const View v = Trim(text);
    if (Empty(v)) return false;

    // 数值部分：数字/小数点/符号，其余是单位后缀（CSS 不允许中间有空格）
    const wchar_t* p = v.begin;
    while (p != v.end && (IsDigit(*p) || *p == L'.' || *p == L'+' || *p == L'-')) ++p;

    float number = 0.f;
    if (!ParseNumber(View{v.begin, p}, number)) return false;

    const View unit{p, v.end};
    float scale = 1.f;
    if (Empty(unit) || Equal(unit, L"px") || Equal(unit, L"dip")) {
        scale = 1.f;                       // px == DIP（96dpi 基准，非整数 DPI 由 scale 统一处理）
    } else if (Equal(unit, L"pt")) {
        scale = 96.f / 72.f;
    } else if (Equal(unit, L"em")) {
        if (!(emBase > 0.f)) return false;  // 没有父字号就无从换算
        scale = emBase;
    } else {
        return false;                      // 白名单外单位
    }

    const float result = number * scale;
    if (!std::isfinite(result)) return false;
    out = result;
    return true;
}

bool ParseColor(const String& text, Color& out) {
    const View v = Trim(text);
    if (Length(v) < 4 || v.begin[0] != L'#') return false;

    const View hex{v.begin + 1, v.end};
    uint32_t value = 0;
    if (!ParseHex32(hex, value)) return false;

    switch (Length(hex)) {
    case 3: {
        // #abc → 0xFFaabbcc（每位复制成两位）
        const uint32_t r = (value >> 8) & 0xF;
        const uint32_t g = (value >> 4) & 0xF;
        const uint32_t b = value & 0xF;
        out = 0xFF000000u | (r * 0x11u << 16) | (g * 0x11u << 8) | (b * 0x11u);
        return true;
    }
    case 6:
        out = 0xFF000000u | value;
        return true;
    case 8:
        out = value;                       // #aarrggbb，alpha 显式给出
        return true;
    default:
        return false;
    }
}

bool ParseWeight(const String& text, uint32_t& out) {
    const View v = Trim(text);
    if (Equal(v, L"normal")) {
        out = 400;                         // DWRITE_FONT_WEIGHT_NORMAL
        return true;
    }
    if (Equal(v, L"bold")) {
        out = 700;                         // DWRITE_FONT_WEIGHT_BOLD
        return true;
    }

    float number = 0.f;
    if (!ParseNumber(v, number) || number < 1.f || number > 1000.f) return false;
    out = static_cast<uint32_t>(number + 0.5f);
    return true;
}

// ---------------------------------------------------------------------------
// 四边 / 边框
// ---------------------------------------------------------------------------

bool ParseThickness(const String& text, Thickness& out, float emBase) {
    float values[4] = {};
    int count = 0;

    const bool ok = ForEachToken(Trim(text), [&](View token) {
        if (count == 4) return false;
        if (!ParseLength(ToString(token), values[count], emBase)) return false;
        ++count;
        return true;
    });
    if (!ok) return false;

    switch (count) {
    case 1: out = Thickness(values[0]); break;
    case 2: out = Thickness(values[0], values[1]); break;                              // 上下 | 左右
    case 3: out = Thickness(values[1], values[0], values[1], values[2]); break;         // 上 | 左右 | 下
    case 4: out = Thickness(values[3], values[0], values[1], values[2]); break;         // 上 右 下 左
    default: return false;
    }
    return true;
}

bool ParseBorder(const String& text, BorderSpec& out) {
    const View v = Trim(text);
    if (Empty(v)) return false;
    if (Equal(v, L"none")) {
        out = BorderSpec{};
        return true;
    }

    BorderSpec spec{};
    bool widthSet = false;
    bool colorSet = false;
    bool styleSolid = false;
    bool styleNone = false;

    const bool ok = ForEachToken(v, [&](View token) {
        if (token.begin[0] == L'#') {
            Color color = 0;
            if (!ParseColor(ToString(token), color)) return false;
            spec.color = color;
            colorSet = true;
            return true;
        }
        if (Equal(token, L"solid")) {
            styleSolid = true;
            return true;
        }
        if (Equal(token, L"none")) {
            styleNone = true;
            return true;
        }
        float width = 0.f;
        if (ParseLength(ToString(token), width)) {
            spec.width = width;
            widthSet = true;
            return true;
        }
        return false;
    });
    if (!ok) return false;
    if (!widthSet && !colorSet && !styleSolid && !styleNone) return false;

    if (styleNone) {
        out = BorderSpec{};
        return true;
    }
    spec.solid = styleSolid;               // 缺 style 视同 CSS 默认的 none（宽/色不生效）
    out = spec;
    return true;
}

// ---------------------------------------------------------------------------
// 行高 / 对齐 / 字体样式 / 装饰线
// ---------------------------------------------------------------------------

bool ParseLineHeight(const String& text, float& out, bool& isMultiple) {
    const View v = Trim(text);
    if (Empty(v)) return false;

    bool hasUnit = false;
    for (const wchar_t* p = v.begin; p != v.end; ++p) {
        if (!IsDigit(*p) && *p != L'.' && *p != L'+' && *p != L'-') {
            hasUnit = true;
            break;
        }
    }

    if (hasUnit) {
        float dip = 0.f;
        if (!ParseLength(text, dip)) return false;
        out = dip;
        isMultiple = false;
        return true;
    }

    float multiple = 0.f;
    if (!ParseNumber(v, multiple) || multiple < 0.f) return false;
    out = multiple;
    isMultiple = true;
    return true;
}

bool ParseAlign(const String& text, Align& out) {
    const View v = Trim(text);
    if (Equal(v, L"left") || Equal(v, L"start")) {
        out = Align::Start;
        return true;
    }
    if (Equal(v, L"center")) {
        out = Align::Center;
        return true;
    }
    if (Equal(v, L"right") || Equal(v, L"end")) {
        out = Align::End;
        return true;
    }
    if (Equal(v, L"justify")) {
        out = Align::End;                  // 暂归 End（无两端对齐实现）
        return true;
    }
    return false;
}

bool ParseFontStyle(const String& text, bool& italic) {
    const View v = Trim(text);
    if (Equal(v, L"normal")) {
        italic = false;
        return true;
    }
    if (Equal(v, L"italic") || Equal(v, L"oblique")) {
        italic = true;
        return true;
    }
    return false;
}

bool ParseDecoration(const String& text, bool& underline, bool& strike) {
    const View v = Trim(text);
    if (Empty(v)) return false;
    if (Equal(v, L"none")) {
        underline = false;
        strike = false;
        return true;
    }

    bool hasUnderline = false;
    bool hasStrike = false;
    const bool ok = ForEachToken(v, [&](View token) {
        if (Equal(token, L"underline")) {
            hasUnderline = true;
            return true;
        }
        if (Equal(token, L"line-through") || Equal(token, L"strikethrough")) {
            hasStrike = true;
            return true;
        }
        return false;
    });
    if (!ok) return false;

    underline = hasUnderline;
    strike = hasStrike;
    return true;
}

// ---------------------------------------------------------------------------
// 族名列表 / 尺寸
// ---------------------------------------------------------------------------

bool ParseFontFamily(const String& text, String& out) {
    String first;                          // 列表里第一个非空族名（全不可用时的回退值）
    const wchar_t* p = text.data();
    const wchar_t* end = p + text.size();

    for (;;) {
        const wchar_t* comma = p;
        while (comma != end && *comma != L',') ++comma;

        View name = Trim(p, comma);
        // 去掉成对引号："Times New Roman" / 'Times New Roman'
        if (Length(name) >= 2 &&
            (name.begin[0] == L'"' || name.begin[0] == L'\'') &&
            name.end[-1] == name.begin[0]) {
            name = Trim(name.begin + 1, name.end - 1);
        }

        if (!Empty(name)) {
            const String family = ToString(name);
            if (first.empty()) first = family;

            const wchar_t* generic = GenericFamily(name);
            const String probe = (generic != nullptr) ? String(generic) : family;
            if (text::Engine::Get().HasFamily(probe.c_str())) {
                out = probe;
                return true;
            }
        }

        if (comma == end) break;
        p = comma + 1;
    }

    out = first;
    return false;
}

bool ParseSize(const String& text, float& outPx, bool& isAuto, float& outRatio) {
    const View v = Trim(text);
    if (Empty(v)) return false;

    if (Equal(v, L"auto")) {
        outPx = 0.f;
        isAuto = true;
        outRatio = 0.f;
        return true;
    }

    if (v.end[-1] == L'%') {
        float percent = 0.f;
        if (!ParseNumber(View{v.begin, v.end - 1}, percent) || percent <= 0.f) return false;
        outPx = 0.f;
        isAuto = false;
        outRatio = percent / 100.f;
        return true;
    }

    float dip = 0.f;
    if (!ParseLength(text, dip)) return false;
    outPx = dip;
    isAuto = false;
    outRatio = 0.f;
    return true;
}

}  // namespace dui::richtext
