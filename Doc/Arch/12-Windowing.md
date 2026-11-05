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
| `Text.Raster` | `pixelsPerDip = scale`；按新 `pixelSize` 重新光栅化字形（ADR-003：DPI 是离散事件；取整策略见 §9-3） | 不跨尺度复用位图（复用必糊）；**渲染模式固定 `NATURAL`** —— `NATURAL_SYMMETRIC` 的 symmetrization 会改写字形轮廓，且只在**新 pixelSize 档位**暴露（见 11-Text §6） |
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

### 6.1 无边框窗口与自定义标题栏（`Chrome::Borderless`）

窗口形态由构造参数声明，框架负责底层的系统对接，业务只画界面、只报"命中区"：

```cpp
struct WindowOptions {
    const wchar_t* title = L"";
    float width  = 800.f;      // DIP
    float height = 600.f;      // DIP
    Chrome chrome = Chrome::System;   // System / Borderless
};

class Window {
public:
    // 自绘标题栏按钮要用的窗口操作
    void Minimize();  void MaximizeOrRestore();  void Close();
    bool IsMaximized() const;
    Vec2 ClientSizeDip() const;      // 客户区 DIP 尺寸（边框热区判定用）

protected:
    // 非客户区命中：ptDip = 客户区 DIP；返回 true → 采用 hit
    //   Caption = 拖动可移动窗口；Left/Right/Bottom/四个角 = 拖边框可缩放
    virtual bool OnNcHitTest(Vec2 ptDip, HitTestCode& hit) { return false; }
    // 尺寸 / 最大化还原状态变化（业务自绘按钮换图标）
    virtual void OnStateChanged() {}
};
```

框架侧的实现要点（Platform 层，公共 API 不暴露 windows.h）：

| 关注点 | 做法 |
| --- | --- |
| 窗口样式 | `WS_POPUP \| WS_THICKFRAME \| WS_SYSMENU \| WS_MINIMIZEBOX \| WS_MAXIMIZEBOX` + `WS_EX_APPWINDOW`。`WS_THICKFRAME` 必须保留 —— `HTLEFT` 等缩放命中码只有带它才生效；`WS_POPUP` 去掉系统标题栏 |
| 阴影 | `DwmExtendFrameIntoClientArea(hwnd, {-1,-1,-1,-1})`（负边距 = 边框外扩 1px，DWM 在窗口外侧画投影）；Win11 另加圆角 `DWMWA_WINDOW_CORNER_PREFERENCE=33` 与去边框色 `DWMWA_BORDER_COLOR=34`（Win10 SDK 无常量，手写） |
| 客户区 | `WM_NCCALCSIZE`（`wParam=TRUE`）**原样保留 `rgrc[0]` 并返回 0**：入参 `rgrc[0]` 是系统建议的新窗口矩形，出参即客户区矩形；不调 `DefWindowProc` 扣边框 → 客户区覆盖整窗（否则 `WS_THICKFRAME` 会在内容外留一圈非客户区，边框色设为"无"时那圈透出桌面）。`rgrc[1]` 是**上一次**的窗口矩形，绝不可赋给 `rgrc[0]` |
| 尺寸语义 | 无边框时 `width/height` 就是**整窗**尺寸（不做 `AdjustWindowRect` 边框补偿 —— 补了会大出左右各 8px，业务按 DIP 布局就错位） |
| 最大化 | `WM_GETMINMAXINFO` 把 `ptMaxPosition` / `ptMaxSize` 约束到工作区：否则系统把窗口放到比工作区还大（边框外扩），底边/右边被屏幕切掉 |
| 命中 | `WM_NCHITTEST`（屏幕坐标 → 客户区 DIP）→ 上抛业务 → 返回 `HTCAPTION` / `HT*`，**移动与缩放由系统接管**，业务不必自己处理鼠标与窗口几何 |

业务侧的坑：

1. **四边四角的缩放热区必须先于标题栏判定**：上边这条窄带压在标题栏上，若先判 `HTCAPTION`，上边与左上 / 右上角会全部变成"移动窗口"，拖不动大小。顺序应为：四边四角 → `HT*` → 按钮区 `HTCLIENT` → 标题栏其余 `HTCAPTION` → 内容区 `HTCLIENT`。
2. **标题栏里的按钮必须排除在 `HTCAPTION` 热区之外**：命中标题栏的点击会被系统当成"拖标题栏"，按钮收不到 `WM_LBUTTONDOWN`，永远点不动。按钮区只需让出最上面一条 `kResizeBorder` 给上边 / 右上角缩放（原生窗口的标题栏顶端同样是一条 resize 带），其余部分返回 `HTCLIENT` 即可照常点击。
3. **边框热区会吃掉客户区边缘几像素**：缩放热区（示例 8 DIP）覆盖客户区四边，内容贴边处点不到 —— 布局时留出余量，或把热区做窄。热区之外建议显式返回 `HTCLIENT`（不要 `return false` 落到 `DefWindowProc`），否则 `WS_THICKFRAME` 会在窗口最外圈又报一套 `HT*`，最大化时容易误触缩放。
4. **最大化后 `IsMaximized()` 为真**：此时没有可拖的边框，缩放热区要自行禁用。
5. **拖边框缩放时 Windows 进入模态循环**：主消息循环被挂起，`WM_SIZE` 连续到达 → 框架在 `OnResized` 里**就地渲染**（否则拖动过程中内容不跟手，松开才更新）。
6. 双击标题栏最大化需要框架支持双击事件，当前未提供。

配套示例：`Samples/CustomFrame`（自绘标题栏 + iconfont 图标按钮 + 阴影 + 移动 + 缩放）。

---

## 7. 限制与坑（显式声明）

### 7.1 高保真的硬要求（不可用"省事优化"换取）

清晰度不靠版本号保证，靠下面五条纪律。任何一条破例，界面就会糊：

1. **绝不回退 `System DPI aware`**。跨屏时系统会对整个窗口做**位图拉伸**——文字立刻全糊。Per-Monitor 是清晰度的底线，不是可选项。
2. **禁止位图缩放整窗/整层**。不做"先渲染到低分辨率再放大"的省事优化；所有绘制按当前 `scale` 直接生成几何与字形。
3. **非整数档必须自绘**。125% / 175% 下若依赖系统位图缩放，等于回到第 1 条的老路。正确做法是按 `px = dip × 1.25` 直接算几何、按像素尺寸光栅化字形（见 §9-2）。
4. **像素对齐（pixel snapping）**。最终矩形与描边在光栅化前对齐到物理像素网格：分割线、边框、1px 描边对齐到整像素（或半像素，由 API 约定），否则 125% 下会看到"两端实中间虚"的灰线。**只对描边/分割线/边框做**，文本位置不做（文本有自己的基线对齐与 hinting）。
5. **图案优先矢量，位图优先多倍图**。圆角、图标底、分割线全部矢量绘制；位图资源按 `scale` 选合适倍率，避免"小图放大"。文字使用**灰度 AA**（DWrite 整像素覆盖率直出）+ **1/4 像素亚像素定位**（4 相位，相位进图集键），高 DPI 下不启用 ClearType 子像素（与预乘 Alpha 合成冲突，ADR-003）。

### 7.2 其它限制

1. **不做单窗口内分区不同 DPI**。窗口跨两台不同 DPI 显示器时，取窗口主体所在显示器的 DPI，整窗一个 scale。Chrome 那种"窗口内逐段 DPI"不做（合成与命中测试复杂度爆炸）。
2. **拖动过程中 `WM_DPICHANGED` 可能连续触发**，必须按帧合并，只处理最后一次，否则会反复重建交换链。DPI 变化处理本身要幂等：`newScale == 当前 scale` 时立即返回。
3. **非整数档的取整**：`125%` 下 1 DIP = 1.25 像素。DIP → 像素统一在**最终排列结果**上取整（而非中间每一步），否则误差累积导致错位；描边类用 `Length::Px` 规避。
4. **字形缓存按像素尺寸离散**，每档一份位图（ADR-003 已接受该代价）。多显示器来回拖动会积累多个档位，靠 LRU 淘汰。
5. **DPI 变化的触发源不止拖动**：最小化/还原、显示器热插拔、远程桌面会话切换、系统设置里改缩放比，都会触发。处理路径必须统一，不能假设"只有拖动才变"。
6. **`GetCursorPos` / `ScreenToClient` 在 DPI 感知进程下返回物理像素**，所有坐标入口都要过一遍换算，漏一处就是命中偏移。
7. **最小化时不得按"有脏标记"进帧**。最小化 → 客户区尺寸为 0 → 渲染帧直接返回且**不清脏标记** → 脏标记恒为真 → 消息循环永远进不了 `WaitMessage()` → 退化成忙转（CPU 100%）。正确做法：最小化期间直接阻塞等消息，恢复时由 `WM_SIZE` → 重排 + 重绘；脏标记保留不丢（保证最小化期间的内容变更不丢）。同时停掉光标闪烁节拍（窗口不可见，闪烁无意义），恢复后自动重启。
8. **`WM_ACTIVATE` 必须上抛给上层**：`WA_INACTIVE` 表示用户已把交互交给别的窗口 → UI 层的焦点应随之离开（编辑控件失焦：选区消失、光标隐藏、闪烁停表，见 [06-Events.md](./06-Events.md) §3.3）。注意最小化同样发 `WA_INACTIVE`，须区分（最小化保留焦点，还原后可直接继续输入）—— 判定要延迟到下一轮消息处理之后（`WM_SIZE` 到达后 `IsIconic()` 才准）。
9. **无边框窗口的 `WM_NCCALCSIZE` 不能把 `rgrc[1]` 拷进 `rgrc[0]`**：`wParam=TRUE` 时
   `rgrc[0]` 入参是**系统建议的新窗口矩形**（出参写客户区矩形），`rgrc[1]` 是**上一次**的窗口矩形。
   无边框只需原样保留 `rgrc[0]` 返回 0（不调 `DefWindowProc` 扣边框）→ 客户区 = 整窗。
   若写成 `rgrc[0] = rgrc[1]`，客户区会慢一次尺寸变更；**最小化后还原时 `rgrc[1]` 恰是离屏的
   图标矩形**（实测 `-32000,-32000,-31801,-31966`，即 199×34 物理像素），客户区被压成 199×34
   → 内容画不进去、交换链也被缩到 199×34，窗口一片空白。注意此时 `WM_SIZE` 的 lParam 与
   `GetClientRect()` 都会报 199×34，且之后**不会再有**新的 `WM_SIZE` —— 所以必须从源头修正
   客户区计算，靠"还原后强制再画一帧"补不回来。

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
