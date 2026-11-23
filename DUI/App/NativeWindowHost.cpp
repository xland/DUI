// ============================================================================
//  DUI/App/NativeWindowHost.cpp — 事件接口的默认实现
// ============================================================================

#include "App/NativeWindowHost.h"

namespace dui {

NativeWindowHost::~NativeWindowHost() = default;

bool NativeWindowHost::OnWindowDestroyed() {
    return true;
}

void NativeWindowHost::OnDpiScaleChanging(float newScale) {
    (void)newScale;
}

bool NativeWindowHost::OnSetCursor(Vec2 ptDip) {
    (void)ptDip; return false;
}

bool NativeWindowHost::OnNcHitTest(Vec2 ptDip, int& hit) {
    (void)ptDip; (void)hit; return false;
}

bool NativeWindowHost::OnDragEnterFiles(const std::vector<String>& paths, Vec2 ptDip) {
    (void)paths; (void)ptDip; return false;
}

bool NativeWindowHost::OnDragOverFiles(const std::vector<String>& paths, Vec2 ptDip) {
    (void)paths; (void)ptDip; return false;
}

void NativeWindowHost::OnDragLeaveFiles() {}

void NativeWindowHost::OnDropFiles(const std::vector<String>& paths, Vec2 ptDip) {
    (void)paths; (void)ptDip;
}

void NativeWindowHost::OnTimer(uintptr_t id) {
    (void)id;
}

void NativeWindowHost::OnActivated(bool active) {
    (void)active;
}

}  // namespace dui
