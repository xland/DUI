// ============================================================================
//  DUI/Core/Json.h — JSON 文本 ⇄ 值树（Core 层，无平台依赖）
//
//  设计约定（Doc/Arch/RichEditor.md §3.4 定案，此处提升为框架通用能力）：
//    · 无异常 / 无 RTTI：解析失败走 Error 出参，取值失败返回默认值
//    · 字符串一律 UTF-16（Core.h 的 String），接口不出现 char* / std::string
//    · 对象有序存储，序列化顺序 = 解析顺序（往返可比对）
//    · 数字统一 double；Int() / AsInt() 表达整数形态
//    · 严格 JSON：不支持注释与尾逗号
//    · 独立编译单元，未被引用时被静态库裁掉（ADR-016）
//
//  用法：
//      json::Error err;
//      json::Value doc = json::Parse(text, err);
//      if (err.Failed()) { Log(err.ToString()); return; }
//
//      const json::Value* blocks = doc.Find(L"blocks");
//      for (size_t i = 0; i < (blocks ? blocks->Size() : 0); ++i) {
//          const json::Value& b = *blocks->At(i);
//          const String type = b.GetString(L"type");
//      }
// ============================================================================

#pragma once

#include "Core/Core.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace dui {
namespace json {

namespace detail {
struct ObjectData;
struct ArrayData;
}  // namespace detail

// ---------------------------------------------------------------------------
//  值树
// ---------------------------------------------------------------------------
class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() noexcept;
    Value(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;
    ~Value();

    // ---- 类型 ----
    Type GetType() const noexcept { return type_; }
    bool IsNull()   const noexcept { return type_ == Type::Null; }
    bool IsBool()   const noexcept { return type_ == Type::Bool; }
    bool IsNumber() const noexcept { return type_ == Type::Number; }
    bool IsString() const noexcept { return type_ == Type::String; }
    bool IsArray()  const noexcept { return type_ == Type::Array; }
    bool IsObject() const noexcept { return type_ == Type::Object; }

    // ---- 取值：类型不符一律返回 def（不报错、不中断）----
    bool          AsBool(bool def = false) const noexcept;
    double        AsDouble(double def = 0.0) const noexcept;
    int64_t       AsInt(int64_t def = 0) const noexcept;
    int           AsInt32(int def = 0) const noexcept;
    float         AsFloat(float def = 0.f) const noexcept;
    const String& AsString() const noexcept;   // 非字符串 → 空串

    // ---- 容器 ----
    size_t        Size() const noexcept;             // array 元素数 / object 成员数
    const String& KeyAt(size_t index) const noexcept; // object 第 index 个成员的键

    // ---- 结构访问：读写只靠 Find / At，const 重载自动区分 ----
    const Value* Find(const wchar_t* key) const noexcept;   // 非 object 或缺失 → nullptr
    Value*       Find(const wchar_t* key) noexcept;
    const Value* At(size_t index) const noexcept;           // array 元素 / object 成员值
    Value*       At(size_t index) noexcept;
    // 路径查询："blocks[0].styles.fontSize"，任一层缺失 → nullptr
    const Value* Path(const wchar_t* path) const noexcept;

    // ---- 便捷读：key + 默认值，日常读不写判空 ----
    bool   GetBool(const wchar_t* key, bool def) const noexcept;
    int    GetInt(const wchar_t* key, int def) const noexcept;
    double GetDouble(const wchar_t* key, double def) const noexcept;
    String GetString(const wchar_t* key, const wchar_t* def = L"") const noexcept;

    // ---- 写：作用于当前节点 ----
    // 不是 object 时 Set 会把自身改写为空 object（原内容丢弃）；array 同理。
    void Set(const wchar_t* key, Value v);
    void SetString(const wchar_t* key, const wchar_t* v);
    void SetString(const wchar_t* key, const String& v);
    void SetInt(const wchar_t* key, int64_t v);
    void SetBool(const wchar_t* key, bool v);
    void Push(Value v);                       // array 追加
    bool Remove(const wchar_t* key);          // 键不存在 → false
    void Reserve(size_t count);

private:
    friend Value Null();
    friend Value Bool(bool v);
    friend Value Number(double v);
    friend Value Int(int64_t v);
    friend Value Str(const wchar_t* v);
    friend Value Str(const String& v);
    friend Value Array();
    friend Value Object();

    Type   type_ = Type::Null;
    bool   bool_ = false;
    double num_  = 0.0;
    String str_;
    std::unique_ptr<detail::ObjectData> obj_;
    std::unique_ptr<detail::ArrayData>  arr_;
};

// ---------------------------------------------------------------------------
//  工厂
// ---------------------------------------------------------------------------
Value Null();
Value Bool(bool v);
Value Number(double v);          // 写入时按数值形态输出（整数值输出为 1）
Value Int(int64_t v);
Value Str(const wchar_t* v);
Value Str(const String& v);
Value Array();
Value Object();

// ---------------------------------------------------------------------------
//  解析
// ---------------------------------------------------------------------------
struct Error {
    String   message;             // 空 = 无错误
    String   path;                // 出错位置的数据路径，如 "blocks[3].styles"
    size_t   offset = 0;          // UTF-16 代码单元偏移
    uint32_t line   = 0;          // 1 基
    uint32_t column = 0;          // 1 基

    bool Failed() const noexcept { return !message.empty(); }
    String ToString() const;      // "line 3, col 12: ... [blocks[1].content]"
};

// 解析失败时返回 Null，并把 error 填好；成功时 error 被清空。
Value Parse(const String& text, Error& error);

// ---------------------------------------------------------------------------
//  序列化
// ---------------------------------------------------------------------------
struct WriteOptions {
    uint32_t indent = 0;              // 0 = 紧凑单行；2 = 常规美化
    bool escapeNonAscii = false;      // true → 非 ASCII 写成 \uXXXX（纯 ASCII 文本）
    bool trailingNewline = false;
};

String Write(const Value& v, const WriteOptions& opt = {});

}  // namespace json
}  // namespace dui
