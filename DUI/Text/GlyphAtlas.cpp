// ============================================================================
//  DUI/Text/GlyphAtlas.cpp — 图集放置 / LRU 淘汰 / 整页释放
// ============================================================================

#include "Text/GlyphAtlas.h"

#include <algorithm>
#include <functional>

namespace dui::text {

namespace {

constexpr uint32_t kPageSize = 1024;        // R8 灰度页：1MB/页
constexpr size_t   kBudgetBytes = 4 << 20;  // 4MB 上限，超出淘汰最久未用（ADR-003）

}  // namespace

size_t GlyphAtlas::KeyHash::operator()(const Key& k) const noexcept {
    const size_t h1 = std::hash<const void*>{}(k.face);
    const size_t h2 = std::hash<uint32_t>{}(k.glyph);
    const size_t h3 = std::hash<uint32_t>{}(k.px);
    const size_t h4 = std::hash<uint32_t>{}(k.phase);
    return h1 ^ (h2 * 0x9E3779B97F4A7C15ull) ^ (h3 * 0xC2B2AE3D27D4EB4Full) ^
           (h4 * 0x165667B19E3779F9ull);
}

const GlyphUV& GlyphAtlas::Get(void* ctx, const void* face, uint32_t glyph,
                                uint32_t px, uint32_t phase, RasterFn raster) {
    const Key key{face, glyph, px, phase};
    const auto it = map_.find(key);
    if (it != map_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return it->second.uv;
    }

    std::vector<uint8_t> bits;
    int left = 0, top = 0, w = 0, h = 0;
    if (!raster(ctx, face, glyph, px, phase, bits, left, top, w, h)) {
        w = 0;
        h = 0;
    }
    if (w > static_cast<int>(kPageSize) || h > static_cast<int>(kPageSize)) {
        w = 0;   // 超大字形不进图集（轮廓退化路径列后续，ADR-003）
    }

    Entry e;
    if (w > 0) e.uv = Place(bits, left, top, w, h);
    lru_.push_front(key);
    e.lru = lru_.begin();
    return map_.emplace(key, e).first->second.uv;
}

GlyphUV GlyphAtlas::Place(const std::vector<uint8_t>& bits, int left, int top, int w, int h) {
    const uint32_t uw = static_cast<uint32_t>(w);
    const uint32_t uh = static_cast<uint32_t>(h);
    while (bytes_ + bits.size() > kBudgetBytes && !lru_.empty()) EvictBack();

    // 首个放得下的页（shelf 游标试推进）；无则开新页。
    // 字形间留 1px 空隙：UV 浮点误差不致采到相邻字形（保险，POINT 采样本身无渗色）
    AtlasPage* page = nullptr;
    uint32_t cx = 0, cy = 0, crh = 0;
    for (AtlasPage& p : pages_) {
        uint32_t x = p.cursorX, y = p.cursorY, rh = p.rowH;
        if (x + uw + 1 > p.w) { x = 0; y += rh; rh = 0; }
        if (y + uh <= p.h) { page = &p; cx = x; cy = y; crh = rh; break; }
    }
    if (page == nullptr) {
        AtlasPage np;
        np.id = nextId_++;
        np.w = np.h = kPageSize;
        np.bits.assign(static_cast<size_t>(kPageSize) * kPageSize, 0);
        pages_.push_back(np);
        page = &pages_.back();
    }

    for (int row = 0; row < h; ++row) {
        const auto dst = page->bits.begin() + static_cast<ptrdiff_t>(cy + row) * kPageSize + cx;
        const auto src = bits.begin() + static_cast<ptrdiff_t>(row) * w;
        std::copy(src, src + w, dst);
    }

    GlyphUV uv;
    uv.page = page->id;
    uv.u0 = static_cast<float>(cx) / static_cast<float>(page->w);
    uv.v0 = static_cast<float>(cy) / static_cast<float>(page->h);
    uv.u1 = uv.u0 + static_cast<float>(uw) / static_cast<float>(page->w);
    uv.v1 = uv.v0 + static_cast<float>(uh) / static_cast<float>(page->h);
    uv.left = left;
    uv.top = top;
    uv.w = w;
    uv.h = h;
    page->cursorX = cx + uw + 1;
    page->cursorY = cy;
    page->rowH = crh > uh ? crh : uh;
    page->dirty = true;
    page->live++;
    bytes_ += bits.size();
    return uv;
}

void GlyphAtlas::EvictBack() {
    const Key key = lru_.back();
    const auto it = map_.find(key);
    if (it == map_.end()) {   // map_ 与 lru_ 应严格一致；失配只可能来自实现错误
        lru_.pop_back();
        return;
    }
    for (auto pit = pages_.begin(); pit != pages_.end(); ++pit) {
        if (pit->id == it->second.uv.page && --pit->live == 0) {
            bytes_ -= pit->bits.size();
            freed_.push_back(pit->id);
            pages_.erase(pit);
            break;
        }
    }
    map_.erase(it);
    lru_.pop_back();
}

}  // namespace dui::text
