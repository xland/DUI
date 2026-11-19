// ============================================================================
//  DUI/App/DropTarget.cpp — IDropTarget 实现（M5.85）
// ============================================================================

#include "App/DropTarget.h"

#include "Core/Core.h"   // String / Vec2

#include <shellapi.h>    // DragQueryFileW

namespace dui {

DropTarget::DropTarget(HWND hwnd, IDropTargetHost* host, ScaleFn scaleFn, void* scaleCtx)
    : hwnd_(hwnd), host_(host), scaleFn_(scaleFn), scaleCtx_(scaleCtx) {}

DropTarget::~DropTarget() = default;

STDMETHODIMP DropTarget::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DropTarget::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&refs_));
}

STDMETHODIMP_(ULONG) DropTarget::Release() {
    const ULONG refs = static_cast<ULONG>(InterlockedDecrement(&refs_));
    if (refs == 0) delete this;
    return refs;
}

bool DropTarget::ExtractPaths(IDataObject* dataObj, std::vector<String>& paths) {
    paths.clear();
    if (dataObj == nullptr) return false;
    FORMATETC fmt{};
    fmt.cfFormat = CF_HDROP;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;
    STGMEDIUM medium{};
    if (FAILED(dataObj->GetData(&fmt, &medium))) return false;
    bool ok = false;
    if (medium.hGlobal != nullptr) {
        const HDROP drop = static_cast<HDROP>(medium.hGlobal);
        const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        paths.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            const UINT n = ::DragQueryFileW(drop, i, nullptr, 0);
            std::vector<wchar_t> buf(static_cast<size_t>(n) + 1);
            ::DragQueryFileW(drop, i, buf.data(), static_cast<UINT>(buf.size()));
            paths.emplace_back(buf.data());
        }
        ok = count > 0;
    }
    ReleaseStgMedium(&medium);
    return ok;
}

Vec2 DropTarget::ToDip(POINTL pt) const {
    POINT p{static_cast<LONG>(pt.x), static_cast<LONG>(pt.y)};
    ::ScreenToClient(hwnd_, &p);
    const float scale = (scaleFn_ != nullptr && scaleFn_(scaleCtx_) > 0.f)
                        ? scaleFn_(scaleCtx_) : 1.f;
    return Vec2{p.x / scale, p.y / scale};
}

STDMETHODIMP DropTarget::DragEnter(IDataObject* dataObj, DWORD, POINTL pt, DWORD* effect) {
    if (effect == nullptr) return E_POINTER;
    *effect = DROPEFFECT_NONE;
    lastPaths_.clear();
    if (host_ == nullptr || !ExtractPaths(dataObj, lastPaths_)) return S_OK;
    if (host_->OnDragEnterFiles(lastPaths_, ToDip(pt))) *effect = DROPEFFECT_COPY;
    return S_OK;
}

STDMETHODIMP DropTarget::DragOver(DWORD, POINTL pt, DWORD* effect) {
    if (effect == nullptr) return E_POINTER;
    *effect = DROPEFFECT_NONE;
    if (host_ == nullptr || lastPaths_.empty()) return S_OK;
    if (host_->OnDragOverFiles(lastPaths_, ToDip(pt))) *effect = DROPEFFECT_COPY;
    return S_OK;
}

STDMETHODIMP DropTarget::DragLeave() {
    if (host_ != nullptr) host_->OnDragLeaveFiles();
    lastPaths_.clear();
    return S_OK;
}

STDMETHODIMP DropTarget::Drop(IDataObject* dataObj, DWORD, POINTL pt, DWORD* effect) {
    if (effect == nullptr) return E_POINTER;
    *effect = DROPEFFECT_NONE;
    lastPaths_.clear();
    if (host_ == nullptr) return S_OK;
    std::vector<String> paths;
    if (!ExtractPaths(dataObj, paths)) return S_OK;
    host_->OnDropFiles(paths, ToDip(pt));
    *effect = DROPEFFECT_COPY;
    return S_OK;
}

}  // namespace dui
