// ============================================================================
//  DUI/Text/GlyphAtlas.h — 字形 Alpha 图集（ADR-003 已定：按 (字体, 字形,
//  像素尺寸, 亚像素相位) 离散缓存，DPI/字号/缩放变化均为离散事件 → 新档位进缓存，
//  旧档位 LRU 淘汰；不做跨尺度复用采样）
//
//  本模块只管 CPU 侧位图与页管理，不依赖 D3D/DWrite：
//    · 光栅化由 TextEngine 注入回调（灰度 AA + 1/4 像素相位，10-Rendering §6.2）
//    · 页位图由 App 层（Frame）同步到 Device 的 R8 纹理
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace dui::text {

// 图集页：pageSize × pageSize 的 R8 灰度位图，shelf（行式）打包。
// 页内字形全部被淘汰后整页释放，id 经 FreedPages 通知 Device 释放纹理。
struct AtlasPage {
    uint32_t id = 0;
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> bits;                       // w*h，每字节 = 覆盖率
    uint32_t cursorX = 0, cursorY = 0, rowH = 0;     // shelf 游标
    bool dirty = false;                              // 待上传（Frame 消费后清零）
    int  live = 0;                                   // 存活字形数
};

struct GlyphUV {
    uint32_t page = 0;               // 页 id；0 = 空占位（无位图，绘制跳过）
    float u0 = 0.f, v0 = 0.f, u1 = 0.f, v1 = 0.f;
    int left = 0, top = 0;           // 相对字形原点（基线左端）的偏移，物理像素
    int w = 0, h = 0;
};

class GlyphAtlas {
public:
    // 光栅回调：out = w*h 灰度覆盖率（1 字节/像素）；光栅失败返回 false
    // phase = 水平亚像素相位（0..3，每相位 1/4 像素，对齐 Qt 的 4 相位定位）
    using RasterFn = bool (*)(void* ctx, const void* fontFace, uint32_t glyphIndex,
                              uint32_t pixelSize, uint32_t phase,
                              std::vector<uint8_t>& out,
                              int& left, int& top, int& w, int& h);

    GlyphAtlas() = default;
    ~GlyphAtlas() = default;
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    const GlyphUV& Get(void* ctx, const void* fontFace, uint32_t glyphIndex,
                       uint32_t pixelSize, uint32_t phase, RasterFn raster);

    std::vector<AtlasPage>& Pages() noexcept { return pages_; }
    const std::vector<uint32_t>& FreedPages() const noexcept { return freed_; }
    void ClearFreed() noexcept { freed_.clear(); }

private:
    struct Key {
        const void* face;
        uint32_t glyph;
        uint32_t px;
        uint32_t phase;          // 水平亚像素相位（0..3）
        bool operator==(const Key& o) const noexcept {
            return face == o.face && glyph == o.glyph && px == o.px && phase == o.phase;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept;
    };
    struct Entry {
        GlyphUV uv;
        std::list<Key>::iterator lru;    // front = 最近使用
    };

    GlyphUV Place(const std::vector<uint8_t>& bits, int left, int top, int w, int h);
    void EvictBack();

    std::unordered_map<Key, Entry, KeyHash> map_;
    std::list<Key> lru_;
    std::vector<AtlasPage> pages_;       // UV 存页 id（非索引），淘汰页安全
    std::vector<uint32_t> freed_;        // 已整页释放、待 Device 释放纹理的 id
    uint32_t nextId_ = 1;
    size_t   bytes_ = 0;                 // 当前占用量（LRU 预算依据）
};

}  // namespace dui::text
