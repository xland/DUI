# 图像

> 状态：v0.1 — `dui::Image` 控件已落地（WIC 解码 + 图像批 + 纹理仓库）。
> 相关：[10-Rendering.md](./10-Rendering.md)（批与呈现）、[11-Text.md](./11-Text.md)（字形图集，同构的"CPU 侧数据在 UI、GPU 侧资源在 Graphics"分层）、[04-Layout.md](./04-Layout.md)（自然尺寸与布局）、[12-Windowing.md](./12-Windowing.md)（DPI）。

---

## 1. 能力与边界

| 项 | 现状 |
| --- | --- |
| 支持格式 | **Windows 默认能解码的**：`PNG / JPEG / BMP / DIB / GIF（首帧）/ TIFF / ICO` 等（取决于系统安装的 WIC 解码器） |
| 不支持 | Windows 解不了的格式（WebP / AVIF 等）；多帧动画（GIF / TIFF 只取第 0 帧）；SVG（矢量） |
| 缩放 | 默认**自然尺寸**显示；显式给 `Width`/`Height` 时按 **contain**（等比缩放 + 居中）适配，图像永不变形 |
| 圆角 / 遮罩 | 不支持（图像按矩形贴图，无 SDF 裁形） |
| 多倍图 | 不支持（只有 1 倍图；高 DPI 下按物理像素 1:1 显示，见 §4） |

---

## 2. 分层与数据流

```
dui::Image (UI 层)
   │  LoadFile / LoadMemory
   ▼
graphics::DecodeImageFile (Graphics 层, WIC)  →  BGRA8 像素（straight alpha）
   │
   ▼
graphics::TextureStore::Register()   ← CPU 侧像素存这里（shared_ptr 句柄由控件持有）
   │
   │  App 层每帧：TextureStore::Sync(device)
   ▼
Device::UploadImage()  →  ID3D11Texture2D（B8G8R8A8_UNORM）+ SRV
   │
   ▼
Painter::AddImageQuad()  →  图像批（顶点格式同字形批，PS 换成彩色纹理采样）
```

- **UI 层不碰 D3D**：控件只持有 `std::shared_ptr<void>`（指向 `graphics::ImageTexture`，公共头不引入 windows.h —— 与 `TextBox::para_` 的 pimpl 模式一致）。
- **WIC 在 Graphics 层**：解码是平台能力（WIC 是系统组件），放 Graphics 层避免 UI 层直接依赖 Windows API（01-Layering §1）。
- **纹理仓库是进程级单例**（`graphics::TextureStore::Instance()`），与字形图集同构：CPU 侧数据在 UI 层，GPU 侧资源在 Graphics 层，App 层每帧同步一次。

### 2.1 链接

WIC 的 import lib 在 SDK 里叫 **`windowscodecs.lib`**（没有 `wincodec.lib`），且不在 MSVC 默认链接列表里。做法：在 `Graphics/ImageCodec.cpp` 里 `#pragma comment(lib, "windowscodecs.lib")` 随该编译单元携带链接指令 —— 不必改根目录 `DUI.props` 的公共编译基线（它同时提供 `CLSID_WICImagingFactory` / `IID_IWICImagingFactory` 符号）。

---

## 3. 绘制批

- 顶点格式与字形批相同（`pos + uv + color`，20 B/顶点）→ **复用 `vsG_` / `layoutG_`**，只新增图像 PS（`kPSI`：采样彩色纹理 × 颜色 → 预乘输出，ADR-013）。
- 采样器：字形用 POINT（1:1 像素对齐），图像用 **LINEAR**（图像常被缩放绘制）。
- 按纹理分组（`ImageGroup`），跨纹理才切换 SRV —— 与字形批按图集页分组同构。
- 索引编排沿用既有的"全局 quad 序号 + 负 BaseVertexLocation"约定（10-Rendering §6.1），图像批是**第四批**，排在形状批之后。

### 3.1 z 序（重要）

批序固定为 矩形 → 字形 → 形状 → 图像，因此**同一绘制单元内图像总在最上层**（会把后画的文字盖住）。为此 `Kind::Image` 元素在收集期**单独切一个绘制单元**（`App/Frame.cpp` 的 `Collector`），单元顺序即元素树顺序 → 图像与兄弟元素的 z 序与普通元素一致。

---

## 4. DPI

- 自然尺寸 = 图像像素尺寸 ÷ 当前 DPI 缩放（`StyleContext::scale`）→ 100% 下 1 图像像素 = 1 物理像素，**跨 DPI 不糊**。
- 绘制时纹理铺满内容盒 → 物理尺寸 = 布局尺寸 × scale。
- `StyleContext` 新增 `scale` 字段（遍历期 DPI 缩放），由 `RunLayout(root, sizeDip, scale)` 灌入（App 层传 `native.Scale()`）。
- 不做多倍图（`@2x`）挑选：跨 DPI 时同一张图按物理像素 1:1 显示，密度不足时由线性插值兜底。

## 4.1 布局与适配

图像控件有两个特殊处理（`UI/Layout.cpp` + `UI/Image.cpp`），避免图像被布局拉伸变形：

1. **不被交叉轴 Stretch 拉满**：`AlignItems` 默认 Stretch 会把子项交叉轴拉满 → 图像会被横向拉宽。`Kind::Image` 子项未显式给该轴尺寸时，交叉轴用**自然尺寸**（显式给了 `Width`/`Height` 才按用户尺寸铺开）。
2. **不参与主轴收缩**：空间不足时 flex-shrink 会压缩子项高度 → 图像被压扁。`Kind::Image` 子项从收缩计算中排除（保持自然尺寸，空间不足时溢出而非变形）。
3. **绘制按 contain 等比居中**：`Image::OnPaint` 把纹理按 `min(盒宽/自然宽, 盒高/自然高)` 等比缩放后居中绘制 —— 即便盒子比例与图像不同（用户显式给了宽高），图像内容也不变形。

---

## 5. 已知限制

1. 只有 contain 一种适配（等比缩放 + 居中）；没有 `cover` / `fill` / `object-position`。
2. 无圆角、阴影、遮罩（图像走的是纹理采样路径，没有 SDF 裁形）。
3. GIF / TIFF 只取首帧。
4. 纹理常驻显存直到控件释放（无 LRU 淘汰；字形图集有 LRU，见 11-Text §4）。
5. 图像元素无子元素（`dui::Image` 继承 `Element` 而非 `Panel`）。

---

## 6. 用法

```cpp
auto* img = root->AddChild<dui::Image>(dui::Style().Padding(6.f));
if (!img->LoadFile(L"logo.png")) {
    // 失败原因在 img->LastError()
}
```

- `LoadFile(const wchar_t*)` / `LoadFile(const String&)`：从文件加载。
- `LoadMemory(const void*, uint32_t)`：从内存加载（格式由内容嗅探）。
- `Clear()`：清除图像（GPU 纹理在下一次纹理同步时回收）。
- `PixelWidth() / PixelHeight()`：图像像素尺寸。
- `Prop::Opacity` 乘到纹理 alpha；`Prop::Padding` 决定内容盒（图像画在内容盒内）。
