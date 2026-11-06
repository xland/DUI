// ============================================================================
//  DUI/UI/Gif.cpp — 动画图像控件的播放控制门面
// ============================================================================

#include "UI/Gif.h"

namespace dui {

bool Gif::LoadImpl(const wchar_t* path) {
    if (!Image::LoadAnimatedFile(path)) return false;   // 限定调用，避免虚递归
    SetAnimationLoopCount(loopCount_);                  // 应用加载前设置的循环次数
    return true;
}

bool Gif::LoadFile(const wchar_t* path) { return LoadImpl(path); }

bool Gif::LoadAnimatedFile(const wchar_t* path) { return LoadImpl(path); }

void Gif::Pause() { PauseAnimation(); }
void Gif::Play()  { PlayAnimation(); }
void Gif::Stop()  { StopAnimation(); }

void Gif::SetLoopCount(uint32_t count) {
    loopCount_ = count;
    SetAnimationLoopCount(count);
}

}  // namespace dui
