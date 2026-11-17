// ============================================================================
//  DUI/UI/TextUtil.h — 文本分段规则集（UI 层内部共享，不进公共头 DUI.h）
//
//  谁在用：
//    · Text      —— 双击选词 / 三击选段、Ctrl+←/→ 按词移动
//    · TextBox   —— 双击选词 / 三击选行（M5.73）
//  规则（与浏览器 / Windows 记事本一致）：
//    · CJK 逐字成词（浏览器对中文的行为：双击一个汉字只选中它）
//    · ASCII 字母 / 数字 / 下划线连成一个词
//    · 代理对算一个码点（不被拆开）
//    · 空格、标点各自成段（双击空格选到一个空格）
//
//  抽出来的原因：Text 与 TextBox 的选词行为必须**完全一致**（同一个输入框里
//  静态文本和可编辑文本双击选出的词不同会很怪），规则放一处才不会走偏。
// ============================================================================
#pragma once

#include <algorithm>

#include "Core/Core.h"

namespace dui {
namespace seg {

// 字符类别（分词用）
enum class CharClass : uint8_t { Space, Word, Cjk, Surrogate, Punct };

inline CharClass ClassOf(wchar_t c) {
    if (c >= 0x4E00 && c <= 0x9FFF) return CharClass::Cjk;   // CJK 统一表意
    if (c >= 0x3400 && c <= 0x4DBF) return CharClass::Cjk;   // 扩展 A
    if (c >= 0x3040 && c <= 0x30FF) return CharClass::Cjk;   // 日文假名 / 注音
    if (c >= 0xAC00 && c <= 0xD7AF) return CharClass::Cjk;   // 韩文音节
    if (c >= 0xD800 && c <= 0xDFFF) return CharClass::Surrogate;
    if (c < 0x80) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_')
            return CharClass::Word;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return CharClass::Space;
        return CharClass::Punct;
    }
    return CharClass::Punct;
}

inline bool IsHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
inline bool IsLowSurrogate(wchar_t c)  { return c >= 0xDC00 && c <= 0xDFFF; }

// 索引吸附到码点边界（代理对的后半段不算独立位置）
inline uint32_t SnapToCodePoint(const String& s, uint32_t i) {
    if (i > 0 && i < s.size() && IsLowSurrogate(s[i]) && IsHighSurrogate(s[i - 1])) return i - 1;
    return i;
}

// 按字符移动（跳过代理对：一次一个码点）
inline uint32_t NextIndex(const String& s, uint32_t i, int dir) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    if (dir < 0) {
        if (i == 0) return 0;
        const uint32_t j = i - 1;
        if (j > 0 && IsLowSurrogate(s[j]) && IsHighSurrogate(s[j - 1])) return j - 1;
        return j;
    }
    if (i >= n) return n;
    const uint32_t j = i + 1;
    if (j < n && IsHighSurrogate(s[i]) && IsLowSurrogate(s[j])) return j + 1;
    return j;
}

// index 处字符所属的同类段 [a, b)：CJK 逐字、ASCII 词整体、代理对算一个码点
inline void SameClassSpan(const String& s, uint32_t index, uint32_t& a, uint32_t& b) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    const CharClass c = ClassOf(s[index]);
    if (c == CharClass::Surrogate) {
        // 代理对：整体算一个码点
        if (index + 1 < n && IsHighSurrogate(s[index]) && IsLowSurrogate(s[index + 1])) {
            a = index;
            b = index + 2;
        } else if (index > 0 && IsLowSurrogate(s[index]) && IsHighSurrogate(s[index - 1])) {
            a = index - 1;
            b = index + 1;
        } else {
            a = index;
            b = index + 1;
        }
        return;
    }
    a = index;
    b = index + 1;
    while (a > 0 && ClassOf(s[a - 1]) == c) --a;
    while (b < n && ClassOf(s[b]) == c) ++b;
}

// 词边界（Ctrl+← / Ctrl+→）：向左取所在词的开头，向右取所在词的末尾
inline uint32_t WordEdge(const String& s, uint32_t i, int dir) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    if (dir < 0) {
        const uint32_t j = std::min(i, n);
        if (j == 0) return 0;
        uint32_t a = 0, b = 0;
        SameClassSpan(s, j - 1, a, b);
        return a;
    }
    uint32_t j = std::min(i, n);
    if (j >= n) return n;
    if (ClassOf(s[j]) == CharClass::Space) {
        while (j < n && ClassOf(s[j]) == CharClass::Space) ++j;
        if (j >= n) return n;
    }
    uint32_t a = 0, b = 0;
    SameClassSpan(s, j, a, b);
    return b;
}

}  // namespace seg
}  // namespace dui
