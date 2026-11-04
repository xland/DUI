// ============================================================================
//  DUI/UI/Panel.h — 容器基类与滚动占位（Widgets 的最小集）
//  AddChild "创建即定型"（03-Style §6.1）——样式必须是最后一个参数。
// ============================================================================

#pragma once

#include <type_traits>

#include "UI/Element.h"

namespace dui {

class Panel : public Element {
public:
    template <class T>
    T* AddChild() {
        static_assert(std::is_base_of_v<Element, T>, "AddChild<T>: T must derive from dui::Element");
        auto* e = new T();
        AttachChild(e);
        return e;
    }
    template <class T>
    T* AddChild(const Style& s) {
        auto* e = AddChild<T>();
        e->SetStyle(s);
        return e;
    }
    template <class T>
    T* AddChild(const StyleBuilder& b) {            // 一次性样式内联（链式返回左值，故收 const&）
        auto* e = AddChild<T>();
        e->SetStyle(b);                             // 传 Builder 走 owned 拷贝；Build() 临时会悬空
        return e;
    }
    template <class T>
    T* AddChild(const wchar_t* text) {              // 仅限有文本构造的控件（Text/Button）
        auto* e = new T(text);
        AttachChild(e);
        return e;
    }
    template <class T>
    T* AddChild(const wchar_t* text, const Style& s) {
        auto* e = AddChild<T>(text);
        e->SetStyle(s);
        return e;
    }
    template <class T>
    T* AddChild(const wchar_t* text, const StyleBuilder& b) {
        auto* e = AddChild<T>(text);
        e->SetStyle(b);                             // 同上：Build() 临时悬空
        return e;
    }
};

}  // namespace dui
