#include "Core/Utf8.h"

#include <cstring>
#include <cwchar>

namespace dui {
namespace {

constexpr uint32_t kReplacement = 0xFFFD;

// 解码一个 UTF-8 序列；used 返回实际消耗的字节数（至少 1）。
// 非法序列返回 U+FFFD，调用方按 used 继续，不会死循环。
uint32_t DecodeOne(const char* p, size_t size, size_t& used) noexcept {
    const unsigned char b0 = static_cast<unsigned char>(p[0]);
    if (b0 < 0x80) {
        used = 1;
        return b0;
    }

    size_t need = 0;
    uint32_t cp = 0;
    if ((b0 & 0xE0) == 0xC0) {
        need = 1;
        cp = b0 & 0x1Fu;
    } else if ((b0 & 0xF0) == 0xE0) {
        need = 2;
        cp = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
        need = 3;
        cp = b0 & 0x07u;
    } else {
        used = 1;
        return kReplacement;
    }

    if (size < need + 1) {
        used = size;
        return kReplacement;
    }
    for (size_t i = 1; i <= need; ++i) {
        const unsigned char b = static_cast<unsigned char>(p[i]);
        if ((b & 0xC0) != 0x80) {
            used = i;
            return kReplacement;
        }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    used = need + 1;

    // 过长编码 / 超出 Unicode 范围 / 落在代理区，都按非法处理
    static const uint32_t kMinCodePoint[4] = {0u, 0x80u, 0x800u, 0x10000u};
    if (cp < kMinCodePoint[need] || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        return kReplacement;
    }
    return cp;
}

void PushUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

}  // namespace

String Utf8ToWide(const char* utf8, size_t size) {
    String out;
    if (utf8 == nullptr || size == 0) return out;

    out.reserve(size);
    size_t i = 0;
    while (i < size) {
        size_t used = 0;
        uint32_t cp = DecodeOne(utf8 + i, size - i, used);
        i += (used == 0) ? 1 : used;

        if (cp >= 0x10000u) {
            cp -= 0x10000u;
            out.push_back(static_cast<wchar_t>(0xD800u + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00u + (cp & 0x3FFu)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

String Utf8ToWide(const char* utf8) {
    if (utf8 == nullptr) return String();
    return Utf8ToWide(utf8, std::strlen(utf8));
}

std::string WideToUtf8(const wchar_t* text, size_t size) {
    std::string out;
    if (text == nullptr || size == 0) return out;

    out.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        uint32_t cp = static_cast<uint32_t>(static_cast<uint16_t>(text[i]));
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            bool paired = false;
            if (i + 1 < size) {
                const uint32_t lo = static_cast<uint32_t>(static_cast<uint16_t>(text[i + 1]));
                if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    ++i;
                    paired = true;
                }
            }
            if (!paired) cp = kReplacement;
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            cp = kReplacement;   // 孤低代理
        }
        PushUtf8(out, cp);
    }
    return out;
}

std::string WideToUtf8(const wchar_t* text) {
    if (text == nullptr) return std::string();
    return WideToUtf8(text, std::wcslen(text));
}

std::string WideToUtf8(const String& text) {
    return WideToUtf8(text.c_str(), text.size());
}

}  // namespace dui
