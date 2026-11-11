// ============================================================================
//  DUI/UI/RichDocJson.h — JSON 文档 → RichDoc（RichEditor.md §2 形态）
//
//  解析层次：JSON 语法（Core/Json）→ 结构（本节）→ 样式值白名单（CssValue）
//
//  错误策略（与 CssValue 一致：坏数据不中断）：
//    · 文本不是合法 JSON / 根节点不是对象 → 返回 false，报告一条
//    · 未知块类型 / 未知行内类型 / 未知样式键 / 非法值 → 跳过并分类计数，
//      取值失败时保留继承来的默认值，文档继续解析
//  诊断只在 out.report（第一条进 firstIssue，其余只计数）。
// ============================================================================

#pragma once

#include "Core/Core.h"
#include "UI/RichDoc.h"

namespace dui {
namespace richtext {

// 解析 JSON 文本为文档模型；成功返回 true（此时 out.blocks 可用，
// out.report 可能仍记录了若干结构问题）。
bool ParseRichDoc(const String& jsonText, RichDoc& out);

}  // namespace richtext
}  // namespace dui
