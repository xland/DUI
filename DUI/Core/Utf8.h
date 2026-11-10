// ============================================================================
//  DUI/Core/Utf8.h — UTF-8 ⇄ UTF-16 转码（Core 层，无平台依赖）
//
//  框架内部一律 UTF-16（Core.h 的 String，ADR-026）；UTF-8 只出现在
//  文件 / 网络这类外部边界，由调用方在入口处转一次。
//
//  容错：非法字节序列 / 孤代理替换为 U+FFFD，不报错、不中断
//  （转码失败没有"部分成功"之外的有意义语义，报错只会逼调用方写无用分支）。
// ============================================================================

#pragma once

#include "Core/Core.h"
#include <cstddef>
#include <string>

namespace dui {

// UTF-8 字节序列 → UTF-16。size 为字节数（不需要 '\0' 结尾）。
String Utf8ToWide(const char* utf8, size_t size);
String Utf8ToWide(const char* utf8);                     // 以 '\0' 结尾

// UTF-16 → UTF-8 字节序列。
std::string WideToUtf8(const wchar_t* text, size_t size);
std::string WideToUtf8(const wchar_t* text);             // 以 '\0' 结尾
std::string WideToUtf8(const String& text);

}  // namespace dui
