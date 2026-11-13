// ============================================================================
//  DUI/DUI.h — 公共入口：业务代码与示例唯一需要 include 的头
//
//  分层（01-Layering §1）：App ──→ UI ──→ Graphics ──→ Core
//  本头只聚合已实现的层；各层落地后在此追加。
//
//  维护约定：
//  · 新增/移动公共模块头 → 在此登记（对外影响集中在本文件，便于盘点）。
//  · UI/UI.h 聚合的是核心控件；Canvas / Scroller / Svg 三个较重的控件在此
//    单列，避免每个 TU 都吃进它们的编译开销。
//  · Graphics 是公共渲染面（Painter / Device / 图像解码）：自绘控件经本头
//    即可拿到完整类型，无需再暴露框架内部头目录。
// ============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Threading.h"
#include "Core/Utf8.h"
#include "Core/Json.h"
#include "Graphics/Graphics.h"
#include "Graphics/ImageCodec.h"
#include "UI/UI.h"
#include "UI/Canvas.h"
#include "UI/Scroller.h"
#include "UI/Svg.h"
#include "UI/RichDoc.h"
#include "UI/CssValue.h"
#include "UI/RichDocJson.h"
#include "App/Dialog.h"
#include "App/Menu.h"
#include "App/App.h"

namespace dui {

// 加载未安装的字体文件（ttf/otf/ttc，如图标字体）到文本引擎（进程内生效），
// 返回其字体族名（可传给 dui::Style().FontFamily(...)）；失败返回空串。
// 建议在 OnCreate 阶段调用；可多次调用装入不同字体（各自独立生效）。
//
// 格式限制：DirectWrite 只认 SFNT 容器，故仅支持 ttf/otf/ttc；
// woff/woff2 是 Web 字体封装（Brotli 压缩 + 表变换），不支持，
// 需先用工具（Google woff2 / fontTools）转成 ttf 再加载。
// 实现在 Graphics/TextEngine.cpp（Graphics 层内部头的公共转发）。
String LoadFontFamily(const wchar_t* fontFilePath);

// 从内存加载字体（把字体文件作为资源嵌入 exe 的场景）：fontData 指向完整的
// SFNT 文件字节、fontDataSize 为其长度。引擎内部拷贝一份（DirectWrite 的
// in-memory loader），调用返回后源缓冲可立即释放。
// 其余约定与 LoadFontFamily 相同：返回字体族名，失败返回空串。
//   HRSRC    hres  = FindResourceW(nullptr, L"IDR_FONT", L"RT_RCDATA");
//   HGLOBAL  hglob = LoadResource(hres);
//   const void* bytes = GlobalLock(hglob);
//   String family = dui::LoadFontFamilyFromMemory(bytes, SizeofResource(hres, RT_RCDATA));
//   GlobalUnlock(hglob);
String LoadFontFamilyFromMemory(const void* fontData, size_t fontDataSize);

// 本机是否存在该字体族：系统已安装的族，或经 LoadFontFamily /
// LoadFontFamilyFromMemory 装入的自定义族。空串 / nullptr → false。
// 用途：族名拼错时 DWrite 会静默回退默认字体（不报错），需要显式降级时用它判定；
//       CSS font-family 列表取"第一个可用族名"也走同一判定。
bool HasFontFamily(const wchar_t* family);

}  // namespace dui
