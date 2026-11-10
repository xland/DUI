#include "Core/Json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <system_error>
#include <vector>

namespace dui {
namespace json {

namespace detail {

struct Member {
    String key;
    Value  value;
};

struct ObjectData {
    std::vector<Member> items;
};

struct ArrayData {
    std::vector<Value> items;
};

}  // namespace detail

namespace {
const String kEmptyString;
}  // namespace

// ---------------------------------------------------------------------------
//  Value：生命周期（深拷贝 / 移动）
// ---------------------------------------------------------------------------
Value::Value() noexcept = default;
Value::~Value() = default;
Value::Value(Value&& other) noexcept = default;
Value& Value::operator=(Value&& other) noexcept = default;

Value::Value(const Value& other)
    : type_(other.type_), bool_(other.bool_), num_(other.num_), str_(other.str_) {
    if (other.obj_) obj_ = std::make_unique<detail::ObjectData>(*other.obj_);
    if (other.arr_) arr_ = std::make_unique<detail::ArrayData>(*other.arr_);
}

Value& Value::operator=(const Value& other) {
    if (this != &other) {
        Value tmp(other);
        *this = std::move(tmp);
    }
    return *this;
}

// ---------------------------------------------------------------------------
//  Value：取值
// ---------------------------------------------------------------------------
bool Value::AsBool(bool def) const noexcept {
    return type_ == Type::Bool ? bool_ : def;
}

double Value::AsDouble(double def) const noexcept {
    return type_ == Type::Number ? num_ : def;
}

int64_t Value::AsInt(int64_t def) const noexcept {
    if (type_ != Type::Number || !std::isfinite(num_)) return def;
    const double value = std::trunc(num_);   // 与 static_cast<int64_t> 的截断语义一致
    const double low = static_cast<double>(std::numeric_limits<int64_t>::min());
    const double high = static_cast<double>(std::numeric_limits<int64_t>::max());
    if (value < low || value >= high) return def;
    return static_cast<int64_t>(value);
}

int Value::AsInt32(int def) const noexcept {
    const int64_t value = AsInt(static_cast<int64_t>(def));
    if (value < -2147483648LL || value > 2147483647LL) return def;
    return static_cast<int>(value);
}

float Value::AsFloat(float def) const noexcept {
    return type_ == Type::Number ? static_cast<float>(num_) : def;
}

const String& Value::AsString() const noexcept {
    return type_ == Type::String ? str_ : kEmptyString;
}

// ---------------------------------------------------------------------------
//  Value：容器
// ---------------------------------------------------------------------------
size_t Value::Size() const noexcept {
    if (type_ == Type::Array && arr_) return arr_->items.size();
    if (type_ == Type::Object && obj_) return obj_->items.size();
    return 0;
}

const String& Value::KeyAt(size_t index) const noexcept {
    if (type_ == Type::Object && obj_ && index < obj_->items.size()) {
        return obj_->items[index].key;
    }
    return kEmptyString;
}

const Value* Value::Find(const wchar_t* key) const noexcept {
    if (type_ != Type::Object || !obj_ || key == nullptr) return nullptr;
    for (const detail::Member& member : obj_->items) {
        if (member.key == key) return &member.value;
    }
    return nullptr;
}

Value* Value::Find(const wchar_t* key) noexcept {
    return const_cast<Value*>(static_cast<const Value*>(this)->Find(key));
}

const Value* Value::At(size_t index) const noexcept {
    if (type_ == Type::Array && arr_ && index < arr_->items.size()) {
        return &arr_->items[index];
    }
    if (type_ == Type::Object && obj_ && index < obj_->items.size()) {
        return &obj_->items[index].value;
    }
    return nullptr;
}

Value* Value::At(size_t index) noexcept {
    return const_cast<Value*>(static_cast<const Value*>(this)->At(index));
}

const Value* Value::Path(const wchar_t* path) const noexcept {
    if (path == nullptr) return nullptr;
    const Value* cur = this;
    size_t i = 0;
    while (cur != nullptr && path[i] != L'\0') {
        if (path[i] == L'.') {
            ++i;
            continue;
        }
        if (path[i] == L'[') {
            ++i;
            size_t index = 0;
            bool hasDigit = false;
            while (path[i] >= L'0' && path[i] <= L'9') {
                index = index * 10 + static_cast<size_t>(path[i] - L'0');
                ++i;
                hasDigit = true;
            }
            if (!hasDigit || path[i] != L']') return nullptr;
            ++i;
            cur = cur->At(index);
            continue;
        }
        const size_t begin = i;
        while (path[i] != L'\0' && path[i] != L'.' && path[i] != L'[') ++i;
        const String key(path + begin, i - begin);
        cur = cur->Find(key.c_str());
    }
    return cur;
}

// ---------------------------------------------------------------------------
//  Value：便捷读
// ---------------------------------------------------------------------------
bool Value::GetBool(const wchar_t* key, bool def) const noexcept {
    const Value* v = Find(key);
    return v != nullptr ? v->AsBool(def) : def;
}

int Value::GetInt(const wchar_t* key, int def) const noexcept {
    const Value* v = Find(key);
    return v != nullptr ? v->AsInt32(def) : def;
}

double Value::GetDouble(const wchar_t* key, double def) const noexcept {
    const Value* v = Find(key);
    return v != nullptr ? v->AsDouble(def) : def;
}

String Value::GetString(const wchar_t* key, const wchar_t* def) const noexcept {
    const Value* v = Find(key);
    if (v != nullptr && v->type_ == Type::String) return v->str_;
    return String(def != nullptr ? def : L"");
}

// ---------------------------------------------------------------------------
//  Value：写入
// ---------------------------------------------------------------------------
void Value::Set(const wchar_t* key, Value v) {
    if (key == nullptr) return;
    if (type_ != Type::Object || !obj_) {
        type_ = Type::Object;
        str_.clear();
        arr_.reset();
        obj_ = std::make_unique<detail::ObjectData>();
    }
    for (detail::Member& member : obj_->items) {
        if (member.key == key) {
            member.value = std::move(v);
            return;
        }
    }
    detail::Member member;
    member.key = key;
    member.value = std::move(v);
    obj_->items.push_back(std::move(member));
}

void Value::SetString(const wchar_t* key, const wchar_t* v) {
    Set(key, Str(v));
}

void Value::SetString(const wchar_t* key, const String& v) {
    Set(key, Str(v));
}

void Value::SetInt(const wchar_t* key, int64_t v) {
    Set(key, Int(v));
}

void Value::SetBool(const wchar_t* key, bool v) {
    Set(key, Bool(v));
}

void Value::Push(Value v) {
    if (type_ != Type::Array || !arr_) {
        type_ = Type::Array;
        str_.clear();
        obj_.reset();
        arr_ = std::make_unique<detail::ArrayData>();
    }
    arr_->items.push_back(std::move(v));
}

bool Value::Remove(const wchar_t* key) {
    if (type_ != Type::Object || !obj_ || key == nullptr) return false;
    for (size_t i = 0; i < obj_->items.size(); ++i) {
        if (obj_->items[i].key == key) {
            obj_->items.erase(obj_->items.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void Value::Reserve(size_t count) {
    if (type_ == Type::Object && obj_) {
        obj_->items.reserve(count);
    } else if (type_ == Type::Array && arr_) {
        arr_->items.reserve(count);
    }
}

// ---------------------------------------------------------------------------
//  工厂
// ---------------------------------------------------------------------------
Value Null() {
    return Value();
}

Value Bool(bool v) {
    Value out;
    out.type_ = Value::Type::Bool;
    out.bool_ = v;
    return out;
}

Value Number(double v) {
    Value out;
    out.type_ = Value::Type::Number;
    out.num_ = v;
    return out;
}

Value Int(int64_t v) {
    return Number(static_cast<double>(v));
}

Value Str(const wchar_t* v) {
    Value out;
    out.type_ = Value::Type::String;
    if (v != nullptr) out.str_ = v;
    return out;
}

Value Str(const String& v) {
    Value out;
    out.type_ = Value::Type::String;
    out.str_ = v;
    return out;
}

Value Array() {
    Value out;
    out.type_ = Value::Type::Array;
    out.arr_ = std::make_unique<detail::ArrayData>();
    return out;
}

Value Object() {
    Value out;
    out.type_ = Value::Type::Object;
    out.obj_ = std::make_unique<detail::ObjectData>();
    return out;
}

// ---------------------------------------------------------------------------
//  Error
// ---------------------------------------------------------------------------
String Error::ToString() const {
    String out;
    wchar_t buf[192];
    const int n = swprintf_s(buf, L"line %u, col %u (offset %zu): ",
                             static_cast<unsigned>(line),
                             static_cast<unsigned>(column),
                             offset);
    if (n > 0) out.append(buf, static_cast<size_t>(n));
    out += message;
    if (!path.empty()) {
        out += L"  [";
        out += path;
        out.push_back(L']');
    }
    return out;
}

// ---------------------------------------------------------------------------
//  解析（递归下降；严格 JSON，不支持注释与尾逗号）
// ---------------------------------------------------------------------------
namespace {

constexpr int kMaxDepth = 200;

bool IsDigit(wchar_t c) noexcept {
    return c >= L'0' && c <= L'9';
}

class Parser {
public:
    Parser(const String& text, Error& error) noexcept : s_(text), err_(error) {}

    bool Run(Value& out) {
        if (!s_.empty() && s_[0] == static_cast<wchar_t>(0xFEFF)) i_ = 1;   // UTF-16 BOM
        if (!ParseValue(out)) return false;
        SkipWs();
        if (i_ != s_.size()) return Fail(L"trailing characters after document");
        return true;
    }

private:
    struct DepthScope {
        explicit DepthScope(int& depth) noexcept : ref(depth) { ++ref; }
        ~DepthScope() { --ref; }
        DepthScope(const DepthScope&) = delete;
        DepthScope& operator=(const DepthScope&) = delete;
        int& ref;
    };

    void SkipWs() noexcept {
        while (i_ < s_.size()) {
            const wchar_t c = s_[i_];
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
                ++i_;
            } else {
                break;
            }
        }
    }

    bool Fail(const wchar_t* msg) {
        if (!err_.Failed()) {
            err_.message = msg;
            err_.offset = i_;
            err_.path = BuildPath();
            uint32_t line = 1;
            uint32_t column = 1;
            for (size_t k = 0; k < i_ && k < s_.size(); ++k) {
                if (s_[k] == L'\n') {
                    ++line;
                    column = 1;
                } else {
                    ++column;
                }
            }
            err_.line = line;
            err_.column = column;
        }
        return false;
    }

    String BuildPath() const {
        String out;
        for (const String& seg : path_) {
            if (!seg.empty() && seg[0] == L'[') {
                out += seg;
            } else {
                if (!out.empty()) out.push_back(L'.');
                out += seg;
            }
        }
        return out;
    }

    bool ParseValue(Value& out);
    bool ParseObject(Value& out);
    bool ParseArray(Value& out);
    bool ParseString(String& out);
    bool ParseNumber(Value& out);
    bool ParseLiteral(const wchar_t* literal);
    bool ReadHex4(uint32_t& out);

    const String& s_;
    Error& err_;
    size_t i_ = 0;
    int depth_ = 0;
    std::vector<String> path_;
};

bool Parser::ParseValue(Value& out) {
    if (depth_ >= kMaxDepth) return Fail(L"nesting too deep");
    SkipWs();
    if (i_ >= s_.size()) return Fail(L"unexpected end of input");

    const wchar_t c = s_[i_];
    switch (c) {
    case L'{':
        return ParseObject(out);
    case L'[':
        return ParseArray(out);
    case L'"': {
        String text;
        if (!ParseString(text)) return false;
        out = Str(text);
        return true;
    }
    case L't':
        if (!ParseLiteral(L"true")) return false;
        out = Bool(true);
        return true;
    case L'f':
        if (!ParseLiteral(L"false")) return false;
        out = Bool(false);
        return true;
    case L'n':
        if (!ParseLiteral(L"null")) return false;
        out = Null();
        return true;
    default:
        break;
    }
    if (c == L'-' || IsDigit(c)) return ParseNumber(out);
    return Fail(L"unexpected character");
}

bool Parser::ParseObject(Value& out) {
    DepthScope scope(depth_);
    if (depth_ > kMaxDepth) return Fail(L"nesting too deep");

    ++i_;   // '{'
    Value obj = Object();
    SkipWs();
    if (i_ < s_.size() && s_[i_] == L'}') {
        ++i_;
        out = std::move(obj);
        return true;
    }

    for (;;) {
        SkipWs();
        if (i_ >= s_.size() || s_[i_] != L'"') return Fail(L"expected object key string");
        String key;
        if (!ParseString(key)) return false;

        SkipWs();
        bool ok = true;
        if (i_ >= s_.size() || s_[i_] != L':') {
            ok = Fail(L"expected ':' after object key");
        } else {
            ++i_;
            path_.push_back(key);
            Value v;
            ok = ParseValue(v);
            if (ok) obj.Set(key.c_str(), std::move(v));
            path_.pop_back();
        }
        if (!ok) return false;

        SkipWs();
        if (i_ >= s_.size()) return Fail(L"unexpected end of object");
        if (s_[i_] == L',') {
            ++i_;
            continue;
        }
        if (s_[i_] == L'}') {
            ++i_;
            out = std::move(obj);
            return true;
        }
        return Fail(L"expected ',' or '}'");
    }
}

bool Parser::ParseArray(Value& out) {
    DepthScope scope(depth_);
    if (depth_ > kMaxDepth) return Fail(L"nesting too deep");

    ++i_;   // '['
    Value arr = Array();
    SkipWs();
    if (i_ < s_.size() && s_[i_] == L']') {
        ++i_;
        out = std::move(arr);
        return true;
    }

    for (size_t index = 0;; ++index) {
        path_.push_back(L"[" + std::to_wstring(index) + L"]");
        Value v;
        const bool ok = ParseValue(v);
        path_.pop_back();
        if (!ok) return false;
        arr.Push(std::move(v));

        SkipWs();
        if (i_ >= s_.size()) return Fail(L"unexpected end of array");
        if (s_[i_] == L',') {
            ++i_;
            continue;
        }
        if (s_[i_] == L']') {
            ++i_;
            out = std::move(arr);
            return true;
        }
        return Fail(L"expected ',' or ']'");
    }
}

bool Parser::ReadHex4(uint32_t& out) {
    if (i_ + 4 > s_.size()) return Fail(L"truncated \\u escape");
    uint32_t value = 0;
    for (int k = 0; k < 4; ++k) {
        const wchar_t h = s_[i_++];
        value <<= 4;
        if (h >= L'0' && h <= L'9') {
            value |= static_cast<uint32_t>(h - L'0');
        } else if (h >= L'a' && h <= L'f') {
            value |= static_cast<uint32_t>(h - L'a' + 10);
        } else if (h >= L'A' && h <= L'F') {
            value |= static_cast<uint32_t>(h - L'A' + 10);
        } else {
            return Fail(L"invalid hex digit in \\u escape");
        }
    }
    out = value;
    return true;
}

bool Parser::ParseString(String& out) {
    // 进入时 s_[i_] == '"'
    ++i_;
    out.clear();
    for (;;) {
        if (i_ >= s_.size()) return Fail(L"unterminated string");
        const wchar_t c = s_[i_++];
        if (c == L'"') return true;
        if (c != L'\\') {
            if (c < 0x20) return Fail(L"control character in string");
            out.push_back(c);
            continue;
        }

        if (i_ >= s_.size()) return Fail(L"unterminated escape");
        const wchar_t esc = s_[i_++];
        switch (esc) {
        case L'"':  out.push_back(L'"');  break;
        case L'\\': out.push_back(L'\\'); break;
        case L'/':  out.push_back(L'/');  break;
        case L'b':  out.push_back(L'\b'); break;
        case L'f':  out.push_back(L'\f'); break;
        case L'n':  out.push_back(L'\n'); break;
        case L'r':  out.push_back(L'\r'); break;
        case L't':  out.push_back(L'\t'); break;
        case L'u': {
            uint32_t cp = 0;
            if (!ReadHex4(cp)) return false;
            // 高代理 + 后续 \uXXXX 低代理 → 合成一个补充平面码点
            if (cp >= 0xD800u && cp <= 0xDBFFu && i_ + 1 < s_.size() &&
                s_[i_] == L'\\' && s_[i_ + 1] == L'u') {
                const size_t save = i_;
                i_ += 2;
                uint32_t low = 0;
                if (ReadHex4(low) && low >= 0xDC00u && low <= 0xDFFFu) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                } else {
                    i_ = save;   // 不是合法代理对：回退，按普通码点输出
                }
            }
            if (cp >= 0x10000u) {
                cp -= 0x10000u;
                out.push_back(static_cast<wchar_t>(0xD800u + (cp >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00u + (cp & 0x3FFu)));
            } else {
                out.push_back(static_cast<wchar_t>(cp));
            }
            break;
        }
        default:
            return Fail(L"invalid escape sequence");
        }
    }
}

bool Parser::ParseNumber(Value& out) {
    const size_t begin = i_;
    if (i_ < s_.size() && s_[i_] == L'-') ++i_;

    if (i_ >= s_.size()) return Fail(L"invalid number");
    if (s_[i_] == L'0') {
        ++i_;
    } else if (s_[i_] >= L'1' && s_[i_] <= L'9') {
        while (i_ < s_.size() && IsDigit(s_[i_])) ++i_;
    } else {
        return Fail(L"invalid number");
    }

    if (i_ < s_.size() && s_[i_] == L'.') {
        ++i_;
        if (i_ >= s_.size() || !IsDigit(s_[i_])) return Fail(L"invalid number fraction");
        while (i_ < s_.size() && IsDigit(s_[i_])) ++i_;
    }

    if (i_ < s_.size() && (s_[i_] == L'e' || s_[i_] == L'E')) {
        ++i_;
        if (i_ < s_.size() && (s_[i_] == L'+' || s_[i_] == L'-')) ++i_;
        if (i_ >= s_.size() || !IsDigit(s_[i_])) return Fail(L"invalid number exponent");
        while (i_ < s_.size() && IsDigit(s_[i_])) ++i_;
    }

    const String token = s_.substr(begin, i_ - begin);
    wchar_t* end = nullptr;
    const double value = std::wcstod(token.c_str(), &end);
    if (end != token.c_str() + token.size()) return Fail(L"invalid number");
    out = Number(value);
    return true;
}

bool Parser::ParseLiteral(const wchar_t* literal) {
    const size_t len = std::wcslen(literal);
    if (i_ + len > s_.size()) return Fail(L"unexpected end of input");
    for (size_t k = 0; k < len; ++k) {
        if (s_[i_ + k] != literal[k]) return Fail(L"invalid literal");
    }
    i_ += len;
    return true;
}

}  // namespace

Value Parse(const String& text, Error& error) {
    error = Error();
    Parser parser(text, error);
    Value out;
    if (!parser.Run(out)) return Null();
    return out;
}

// ---------------------------------------------------------------------------
//  序列化
// ---------------------------------------------------------------------------
namespace {

void AppendIndent(String& out, const WriteOptions& opt, uint32_t level) {
    if (opt.indent == 0) return;
    out.push_back(L'\n');
    out.append(static_cast<size_t>(opt.indent) * static_cast<size_t>(level), L' ');
}

void AppendNumber(String& out, double v) {
    if (!std::isfinite(v)) {   // JSON 没有 NaN / Inf
        out += L"null";
        return;
    }

    const double truncated = std::floor(v);
    if (v == truncated && v >= -9007199254740992.0 && v <= 9007199254740992.0) {
        wchar_t buf[32];
        const int n = swprintf_s(buf, L"%lld", static_cast<long long>(v));
        if (n > 0) out.append(buf, static_cast<size_t>(n));
        return;
    }

    char buf[64];
    const std::to_chars_result res = std::to_chars(buf, buf + sizeof(buf), v);
    if (res.ec != std::errc()) {
        out += L"null";
        return;
    }
    for (const char* p = buf; p != res.ptr; ++p) out.push_back(static_cast<wchar_t>(*p));
}

void AppendEscaped(String& out, const String& text, bool escapeNonAscii) {
    out.push_back(L'"');
    for (const wchar_t ch : text) {
        switch (ch) {
        case L'"':  out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\b': out += L"\\b";  break;
        case L'\f': out += L"\\f";  break;
        case L'\n': out += L"\\n";  break;
        case L'\r': out += L"\\r";  break;
        case L'\t': out += L"\\t";  break;
        default: {
            const uint32_t code = static_cast<uint32_t>(static_cast<uint16_t>(ch));
            if (code < 0x20u || (escapeNonAscii && code > 0x7Eu)) {
                wchar_t buf[8];
                const int n = swprintf_s(buf, L"\\u%04x", static_cast<unsigned>(code));
                if (n > 0) out.append(buf, static_cast<size_t>(n));
            } else {
                out.push_back(ch);
            }
            break;
        }
        }
    }
    out.push_back(L'"');
}

void WriteValue(const Value& v, String& out, const WriteOptions& opt, uint32_t level) {
    switch (v.GetType()) {
    case Value::Type::Null:
        out += L"null";
        break;
    case Value::Type::Bool:
        out += v.AsBool() ? L"true" : L"false";
        break;
    case Value::Type::Number:
        AppendNumber(out, v.AsDouble());
        break;
    case Value::Type::String:
        AppendEscaped(out, v.AsString(), opt.escapeNonAscii);
        break;
    case Value::Type::Array: {
        const size_t count = v.Size();
        if (count == 0) {
            out += L"[]";
            break;
        }
        out.push_back(L'[');
        for (size_t i = 0; i < count; ++i) {
            if (i != 0) out.push_back(L',');
            AppendIndent(out, opt, level + 1);
            WriteValue(*v.At(i), out, opt, level + 1);
        }
        AppendIndent(out, opt, level);
        out.push_back(L']');
        break;
    }
    case Value::Type::Object: {
        const size_t count = v.Size();
        if (count == 0) {
            out += L"{}";
            break;
        }
        out.push_back(L'{');
        for (size_t i = 0; i < count; ++i) {
            if (i != 0) out.push_back(L',');
            AppendIndent(out, opt, level + 1);
            AppendEscaped(out, v.KeyAt(i), opt.escapeNonAscii);
            out.push_back(L':');
            if (opt.indent != 0) out.push_back(L' ');
            WriteValue(*v.At(i), out, opt, level + 1);
        }
        AppendIndent(out, opt, level);
        out.push_back(L'}');
        break;
    }
    }
}

}  // namespace

String Write(const Value& v, const WriteOptions& opt) {
    String out;
    WriteValue(v, out, opt, 0);
    if (opt.trailingNewline) out.push_back(L'\n');
    return out;
}

}  // namespace json
}  // namespace dui
