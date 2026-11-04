// ============================================================================
//  DUI/UI/Panel.h — 容器基类（Widgets 的最小集）
//  AddChild "创建即定型"（03-Style §6.1）——样式必须是最后一个参数。
//  样式一律拷贝持有，`AddChild<Panel>(dui::Style().Gap(20.f))` 链式临时可直传。
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
};

}  // namespace dui
