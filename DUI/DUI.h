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
#include "App/App.h"
