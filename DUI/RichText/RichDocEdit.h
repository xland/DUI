// ============================================================================
//  DUI/RichText/RichDocEdit.h — 文档模型编辑原语（纯函数，无排版 / UI 依赖）
//
//  编辑的核心难点在"段内 run 序列"上：插入一个字符要落进正确的 run、跨 run 的
//  删除要拆段，且相邻同样式 run 必须合并（否则每次输入都会多一个 run）。
//  这些操作与光标、几何无关，放在模型层做纯函数，RichEdit 只负责交互与坐标。
//
//  偏移一律是段内 UTF-16 码元偏移（与 String 下标一致）。
// ============================================================================

#pragma once

#include "Core/Core.h"
#include "RichText/RichDoc.h"

namespace dui {
namespace richtext {

// runs 拼接出的段内文本（编辑定位 / 复制 / 命中用）
String BlockText(const Block& block);

// 代理对吸附：offset 落在低代理上时退到高代理（DWrite 命中可能返回半个码点）
uint32_t SnapBoundary(const String& text, uint32_t offset);

// 段内 offset 处的字符样式：段中取所在 run，段尾取最后一个 run，空块返回空样式
RunStyle StyleAt(const Block& block, uint32_t offset);

bool SameRunStyle(const RunStyle& a, const RunStyle& b);

// 在 offset 处插入文本（样式取 style），相邻同样式 run 合并
void InsertText(Block& block, uint32_t offset, const String& text, const RunStyle& style);

// 删除 [start, end)
void DeleteRange(Block& block, uint32_t start, uint32_t end);

// 回车分裂：src 保留 [0, offset)，返回新块（[offset, end) + 同块级样式 / kind）
Block SplitBlock(Block& src, uint32_t offset);

// 合并：把 src 的 runs 追加到 dst 末尾（src 不动，删除交给调用方）
void AppendRuns(Block& dst, const Block& src);

// 合并相邻同样式 run（删除空 run）
void MergeRuns(Block& block);

}  // namespace richtext
}  // namespace dui
