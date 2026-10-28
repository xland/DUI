# 窗口与高 DPI

> 状态：草案 v0.1 — **DPI 设计已定案**（§9 结论记录）；窗口形态的其余细节（多窗口、全屏、DWM 集成）待后续补充。
> 需求来源：框架要支持高分屏（150% / 200%），并且要在跨显示器拖动时跟随目标显示器的 DPI 切换，切换过程不能糊、不能闪、不能无谓重排。
> 相关：ADR-014（DPI 与缩放）、ADR-009（最低 Windows 版本）、ADR-003（字形光栅方案）、[01-Layering.md](./01-Layering.md)（Dpi / HighWindow 模块）、[04-Layout.md](./04-Layout.md)、[11-Text.md](./11-Text.md)、[15-Properties.md](./15-Properties.md)。

## 1. 需求

| 编号 | 需求 | 验收口径 |
| --- | --- | --- |
| R1 | 高分屏清晰 | 200% 缩放下文字与描边不发虚，字形按物理像素重新光栅化 |
| R2 | 跟随 DPI 切换 | 窗口从 100% 显示器拖到 150% 显示器，内容按新 DPI 重建，无需重启 |
| R3 | 布局代码与 DPI 无关 | 同一份 `Style` / 布局代码在任何缩放下都不改一个数字 |
| R4 | 切换不做无谓重排 | 窗口 DIP 尺寸不变时只重绘，不重新布局 |
| R5 | 非整数档可用 | 125% / 175% 下不出现明显错位 |
| R6 | 多显示器一致 | 窗口同时接触两台显示器时行为可预期（单窗口单缩放） |

---

## 2. 感知级别

**Per-Monitor V2**（`DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`），通过应用清单声明：

```xml
<asmv3:application>
  <asmv3:windowsSettings>
    <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
    <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
  </asmv3:windowsSettings>
</asmv3:application>
```

选 V2 而非 V1 的理由：

| 项 | V2 | V1 |
| --- | --- | --- |
| `WM_DPICHANGED` 附带建议窗口矩形 | 有（直接 `SetWindowPos`，不用自己算） | 无，需自己按比例重算 |
| 非客户区（边框、标题栏、菜单、系统对话框） | 系统自动缩放 | 需手工处理 `WM_NCCALCSIZE` / `EnableNonClientDpiScaling` |
| 顶层窗口子窗口的 DPI 继承 | 自动 | 需手工 `SetThreadDpiAwarenessContext` |

代价：V2 要求 **Windows 10 1703+**。若 ADR-009 最终把最低版本压到 1703 以下，则必须回退 V1 并补非客户区缩放路径（列为待确认）。

不做 `System DPI aware` / `Unaware` 兼容模式：单一感知级别，避免同一份代码走两套坐标逻辑。

---

## 3. 坐标与单位

三套坐标，**转换只发生在两个边界**：

| 坐标系 | 用途 | 边界 |
| --- | --- | --- |
| 物理像素 | Win32 消息、鼠标绝对位置、交换链后台缓冲尺寸 | ← 入口（Input）与出口（Present） |
| 客户区 DIP | 布局、样式、命中测试、绘制指令、元素 Rect | 中间全程使用 |
| 屏幕 DIP | 多显示器定位、绝对定位元素、浮层/提示窗 | 需要时显式换算 |

```
scale = dpi / 96.0f          // 96 dpi = 1.0，150% = 1.5
dip   = px  / scale
px    = dip * scale
```

规则：

- **`Element::Bounds()` 等一切对外暴露的几何量都是 DIP**，不暴露物理像素。
- `Length` 的默认单位是 `Dip`（[15-Properties.md](./15-Properties.md) §2）。
- `Length::Px` 专供"必须是物理像素"的场合：1px 发丝线、与系统对齐的 1 像素分割线。在 150% 下 `1 Dip` 描边会变成 1.5 像素而发虚，此时应写 `Length::Px(1)`。

---

## 4. DPI 变化时序

`WM_DPICHANGED` 在消息循环内处理，早于 [01-Layering.md](./01-Layering.md) §3 的帧生命周期：

```
WM_DPICHANGED
  ├─ 1. newScale = HIWORD(wParam) / 96.0f；与旧值相同 → 直接返回
  ├─ 2. 按 lParam 建议矩形 SetWindowPos（不自己算尺寸，否则窗口会跳变）
  ├─ 3. 更新窗口 scale，重新计算客户区 DIP 尺寸
  ├─ 4. 交换链 ResizeBuffers（新的物理像素尺寸）
  ├─ 5. 分级失效：
  │      · 字形：新 pixelSize 不可用 → 按新尺寸光栅化（旧档位仍在缓存里，LRU 淘汰）
  │      · 图片：按新 scale 选择/重采样
  │      · 布局：仅当客户区 DIP 尺寸变化才 LayoutDirty
  │      · 绘制：整窗 PaintDirty（每个像素都可能变）
  ├─ 6. 触发 Window::OnDpiChanged（业务可换资源 / 调字号档位）
  └─ 7. 重算鼠标当前位置（物理 → DIP），避免 hover 状态错位
```

**关键取舍：DPI 变化 ≠ 重新布局。** 系统给出的建议矩形是按比例放大的物理矩形，换算回 DIP 后客户区尺寸通常不变，因此布局结果可直接复用，只做重绘。只有当窗口 DIP 尺寸真的变了（业务在 `OnDpiChanged` 里改了尺寸、或系统给的矩形比例不同），才标 `LayoutDirty`。

---

## 5. 各子系统的响应

| 子系统 | DPI 变化时做什么 | 不做什么 |
| --- | --- | --- |
| `SwapChain` | `ResizeBuffers` 到新物理尺寸，重建渲染目标 | — |
| `Text.Layout` | 排版仍用 DIP 宽度（DWrite layout 坐标为 DIP） | 不重新排版（除非 DIP 尺寸变） |
| `Text.Raster` | `pixelsPerDip = scale`；按新 `pixelSize` 重新光栅化字形（ADR-003：DPI 是离散事件；取整策略见 §9-3） | 不跨尺度复用位图（复用必糊） |
| `GlyphAtlas` | 新档位进缓存，旧档位保留待 LRU 淘汰 | 不主动清空（拖回原屏可复用） |
| 图片 | 按新 scale 选择多倍图或重采样 | — |
| `Input` | 消息里的物理坐标 / scale → DIP | 命中测试不做换算（全程 DIP） |
| `Ime` | 候选窗坐标必须是物理像素（`ImmSetCandidateWindow`） | — |
| `Cursor` | 系统光标由 OS 缩放 | — |
| `Animation` | `transform.scale` 是逻辑倍数，最终像素 = `dip × dpiScale × animScale` | 动画值不因 DPI 变化而重置 |
| `Invalidate` | 整窗标脏 | 不整树重排 |

`pixelSize = round(fontSizeDip × scale)` 的取整策略见 §7-3。

---

## 6. API 形态

```cpp
namespace dui {

struct DpiChanged final {
    float oldScale;          // 96 dpi = 1.0f
    float newScale;
    Rect  suggestedDip;      // 系统建议的新窗口矩形（已换算为 DIP）
};

class Window {
public:
    float Scale() const;                        // 当前缩放因子
    float DipToPx(float dip) const;
    float PxToDip(float px) const;

protected:
    // 默认实现只做框架内必需的工作；业务可重写以换资源、调字号档位
    virtual void OnDpiChanged(const DpiChanged& e) {}
};

}  // namespace dui
```

使用侧（窗口类里按需重写）：

```cpp
void MyWindow::OnDpiChanged(const dui::DpiChanged& e) {
    // 例：图标按档位换源；小屏位图换大图
    icon_->SetImage(e.newScale >= 2.0f ? L"icon@2x.png" : L"icon.png");
}
```

`Scale()` 是**窗口级**而非进程级：多窗口可各自处于不同显示器、不同缩放。

---

## 7. 限制与坑（显式声明）

### 7.1 高保真的硬要求（不可用"省事优化"换取）

清晰度不靠版本号保证，靠下面五条纪律。任何一条破例，界面就会糊：

1. **绝不回退 `System DPI aware`**。跨屏时系统会对整个窗口做**位图拉伸**——文字立刻全糊。Per-Monitor 是清晰度的底线，不是可选项。
2. **禁止位图缩放整窗/整层**。不做"先渲染到低分辨率再放大"的省事优化；所有绘制按当前 `scale` 直接生成几何与字形。
3. **非整数档必须自绘**。125% / 175% 下若依赖系统位图缩放，等于回到第 1 条的老路。正确做法是按 `px = dip × 1.25` 直接算几何、按像素尺寸光栅化字形（见 §9-2）。
4. **像素对齐（pixel snapping）**。最终矩形与描边在光栅化前对齐到物理像素网格：分割线、边框、1px 描边对齐到整像素（或半像素，由 API 约定），否则 125% 下会看到"两端实中间虚"的灰线。**只对描边/分割线/边框做**，文本位置不做（文本有自己的基线对齐与 hinting）。
5. **图案优先矢量，位图优先多倍图**。圆角、图标底、分割线全部矢量绘制；位图资源按 `scale` 选合适倍率，避免"小图放大"。文字使用灰度 AA + 正确的 gamma 校正，高 DPI 下不启用 ClearType 子像素（与预乘 Alpha 合成冲突，ADR-003）。

### 7.2 其它限制

1. **不做单窗口内分区不同 DPI**。窗口跨两台不同 DPI 显示器时，取窗口主体所在显示器的 DPI，整窗一个 scale。Chrome 那种"窗口内逐段 DPI"不做（合成与命中测试复杂度爆炸）。
2. **拖动过程中 `WM_DPICHANGED` 可能连续触发**，必须按帧合并，只处理最后一次，否则会反复重建交换链。DPI 变化处理本身要幂等：`newScale == 当前 scale` 时立即返回。
3. **非整数档的取整**：`125%` 下 1 DIP = 1.25 像素。DIP → 像素统一在**最终排列结果**上取整（而非中间每一步），否则误差累积导致错位；描边类用 `Length::Px` 规避。
4. **字形缓存按像素尺寸离散**，每档一份位图（ADR-003 已接受该代价）。多显示器来回拖动会积累多个档位，靠 LRU 淘汰。
5. **DPI 变化的触发源不止拖动**：最小化/还原、显示器热插拔、远程桌面会话切换、系统设置里改缩放比，都会触发。处理路径必须统一，不能假设"只有拖动才变"。
6. **`GetCursorPos` / `ScreenToClient` 在 DPI 感知进程下返回物理像素**，所有坐标入口都要过一遍换算，漏一处就是命中偏移。

---

## 8. 分期

| 期 | 内容 |
| --- | --- |
| **P0** | Per-Monitor V2 + 全程 DIP 布局 + `WM_DPICHANGED` 完整流程 + 字形按像素尺寸重光栅 + 描边/分割线像素对齐 + `Scale()` / `OnDpiChanged` + 输入坐标换算 |
| **P1** | 多倍图资源选择（按 scale 挑 `@2x` / `@3x`）、非整数档取整优化、DPI 变化期间的状态防抖 |
| **P2** | 用户级 UI 缩放（Ctrl+滚轮 / 设置项，**与系统 DPI 相乘**，见 §9-5）、SDF 无级缩放、跨屏分区 DPI |

---

## 9. 结论记录（已定）

1. **最低版本 = Windows 10 1703**（与 ADR-009 对齐）。**不再下探**：1607 及以下只有 V1 / `System DPI aware`，跨屏必然位图拉伸，与"清晰、高保真"目标直接冲突。
   - 选择依据是**目标优先级**：[00-Overview.md](./00-Overview.md) §1 把开发体验与功能完备度排在效率/体积之前（ADR-015 不为体积设硬门槛），兼容更老系统的收益低于画面质量的损失。
   - 区分「**支持** 1703」与「**测试基线**」：1703/1709 的 DPI 相关系统缺陷较多，日常开发与回归建议在 **Win10 1809+ / Win11** 上进行，1703 只做最低版本冒烟。
2. **非整数档走自绘**：几何按 `dip × scale` 直接计算 + 字形按像素尺寸离散光栅化，不依赖系统位图缩放。理由见 §7.1-3。
3. **`pixelSize` 取整 = `round`**：`px = round(fontSizeDip × scale)`；结果小于 9 px 的小字号向上夹紧（`ceil`），避免笔画被裁切。取整只发生在**字形光栅化与最终排列结果**两处，布局与命中测试全程使用 DIP 原始值，不做中间取整（否则误差累积导致整列错位，见 §7.2-3）。
4. **多倍图 = 显式 density**：`image->SetSource(src, density)`（或等价的资源清单声明），框架**不做** `@2x` 文件名后缀的隐式文件系统探测 —— 隐式匹配依赖命名约定与运行时 IO，错误难以在编译期暴露。P1 落地。
5. **用户级 UI 缩放 = P2，与系统 DPI 相乘**：`effectiveScale = systemScale × userScale`，应用内缩放（Ctrl+滚轮 / 设置项）是对系统缩放的进一步放大。**不采用覆盖式**：覆盖会破坏"跟随系统设置"的直觉，也让"系统 150% + 应用 100%"这一常见诉求无法表达。实现上与 DPI 变化共用同一套重建路径（只是 scale 来源不同）。
