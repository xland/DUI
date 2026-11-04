// ============================================================================
//  DUI/DUI.h — 公共入口：业务代码与示例唯一需要 include 的头
//
//  分层（01-Layering §1）：App ──→ UI ──→ Text ──→ Graphics ──→ Platform ──→ Core
//  本头只聚合已实现的层；Text/Graphics/Platform 落地后在此追加。
// ============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Threading.h"
#include "UI/UI.h"
#include "UI/Canvas.h"
#include "UI/Scroller.h"
#include "App/App.h"

namespace dui {

// 加载未安装的字体文件（ttf/otf，如图标字体）到文本引擎（进程内生效），
// 返回其字体族名（可传给 dui::Style().FontFamily(...)）；失败返回空串。
// 建议在 OnCreate 阶段调用；引擎生命周期内只装一个文件，重复调用返回已装族名。
// 实现在 Text/TextEngine.cpp（Text 层内部头的公共转发）。
String LoadFontFamily(const wchar_t* fontFilePath);

}  // namespace dui
