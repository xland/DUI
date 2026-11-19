// ============================================================================
//  DUI/App/DropTarget.h — OLE 拖放目标（IDropTarget 独立 COM 对象，M5.85）
//
//  为什么不用 DragAcceptFiles：那只发 WM_DROPFILES（释放瞬间一次性给路径），
//  没有 Enter/Over/Leave 悬停阶段，无法在鼠标未释放时显示"插入光标"预览。
//  IDropTarget 提供完整的 DragEnter/DragOver/DragLeave/Drop，Drop 时统一转成
//  host 的 OnDropFiles（与 M5.84 的元素级路由复用同一条链）。
//
//  生命周期：独立堆对象、自计数 IUnknown；RegisterDragDrop 由 OLE 持一引用，
//  窗口销毁前 RevokeDragDrop 释放。NativeWindow 另持一引用，析构时 Release。
//  注册/撤销必须在 OleInitialize 建立了 STA 的同一 UI 线程（EnsureUiCom 已满足）。
// ============================================================================
#pragma once

#include <vector>

#include <windows.h>
#include <oleidl.h>   // IDropTarget / DROPEFFECT

#include "Core/Core.h"   // String / Vec2

namespace dui {

// 宿主回调：拖放各阶段。paths = 文件路径列表；ptDip = 释放/悬停点客户区 DIP。
// Enter/Over 返回 true 表示接受（DropTarget 转成 DROPEFFECT_COPY，否则 NONE）。
class IDropTargetHost {
public:
    virtual ~IDropTargetHost() = default;
    virtual bool OnDragEnterFiles(const std::vector<String>& paths, Vec2 ptDip) = 0;
    virtual bool OnDragOverFiles(const std::vector<String>& paths, Vec2 ptDip) = 0;
    virtual void OnDragLeaveFiles() = 0;
    virtual void OnDropFiles(const std::vector<String>& paths, Vec2 ptDip) = 0;
};

// 独立 COM 对象：实现 IUnknown + IDropTarget，持有 hwnd / host / scale 提供者。
// scale 用函数指针按当前 DPI 实时取（DPI 变化后仍正确）。
class DropTarget final : public IDropTarget {
public:
    using ScaleFn = float (*)(void*);

    DropTarget(HWND hwnd, IDropTargetHost* host, ScaleFn scaleFn, void* scaleCtx);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDropTarget
    STDMETHODIMP DragEnter(IDataObject* dataObj, DWORD keyState, POINTL pt, DWORD* effect) override;
    STDMETHODIMP DragOver(DWORD keyState, POINTL pt, DWORD* effect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject* dataObj, DWORD keyState, POINTL pt, DWORD* effect) override;

private:
    ~DropTarget();

    // 从 IDataObject 取 CF_HDROP 文件列表；失败返回空。
    static bool ExtractPaths(IDataObject* dataObj, std::vector<String>& paths);

    // 屏幕坐标 → 客户区 DIP
    Vec2 ToDip(POINTL pt) const;

    LONG        refs_ = 1;
    HWND        hwnd_ = nullptr;
    IDropTargetHost* host_ = nullptr;
    ScaleFn     scaleFn_ = nullptr;
    void*       scaleCtx_ = nullptr;
    std::vector<String> lastPaths_;   // DragOver 复用 Enter 解析结果（同一 IDataObject）
};

}  // namespace dui
