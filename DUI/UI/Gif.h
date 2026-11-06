// ============================================================================
//  DUI/UI/Gif.h — 动画图像控件（播放控制）
//
//  继承 dui::Image：复用帧解码、纹理复用、MeasureContent / OnPaint 与统一 ticker，
//  只承载播放状态机（暂停 / 播放 / 停止 / 循环次数）的公共 API。
//  LoadFile 与 LoadAnimatedFile 均统一走多帧解码；单帧图退化为静态显示。
//  帧状态结构定义在 UI/Image.cpp，本头经 protected 原语访问，不暴露内部结构。
// ============================================================================
#pragma once

#include <cstdint>

#include "UI/Image.h"

namespace dui {

class Gif : public Image {
public:
    using Image::LoadFile;
    using Image::LoadAnimatedFile;

    bool LoadFile(const wchar_t* path) override;
    bool LoadAnimatedFile(const wchar_t* path) override;

    void Pause();                          // 停当前帧、保留游标、注销 ticker
    void Play();                           // 从暂停处继续；Stop 后从头重播
    void Stop();                           // 回首帧、重置循环计数、保留帧序列
    void SetLoopCount(uint32_t count);     // 0 = 无限；可在加载前设置

private:
    bool LoadImpl(const wchar_t* path);

    uint32_t loopCount_ = 0;   // 加载前设置的循环次数，加载后应用到帧状态
};

}  // namespace dui
