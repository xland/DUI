# DUI 图像指南（Image 开发者文档）

相关：[Arch/16-Image.md](./Arch/16-Image.md)（架构与实现细节）、[Input.md](./Input.md) §5.7（富文本插图）、[Styles.md](./Styles.md)（样式属性）、[CustomCtrl.md](./CustomCtrl.md)（自绘控件）、[Font.md](./Font.md)（矢量图标，不依赖位图）。

本指南只讲"应用里怎么用图像"，实现原理见上面的架构文档。

---

## 1. 快速开始

### 1.1 三个图像控件

| 控件 | 用途 | 加载 API | 说明 |
| --- | --- | --- | --- |
| `dui::Image` | 静态位图 | `LoadFile` / `LoadMemory` | 动画文件只取第 0 帧 |
| `dui::Gif` | 动画 GIF | `LoadFile`（多帧） | `Image` 的派生，加载即播放，带播放控制 |
| `dui::Svg` | 矢量图标 | `LoadSvgFile` / `LoadSvgMemory` | 按显示尺寸光栅，放大不糊；图标级子集 |

三者都是普通 `Element`，用 `AddChild` 挂到任意容器，样式（`Width` / `Height` / `Padding` / `Opacity` 等）与文本、按钮完全一致。

### 1.2 最小示例

```cpp
auto* img = root->AddChild<dui::Image>(dui::Style()
    .Width(180.f).Height(180.f).Padding(6.f));
if (!img->LoadFile(L"logo.png")) {
    // 失败原因在 img->LastError()（UTF-16）
    root->AddChild<dui::Text>(img->LastError().c_str());
}
```

只需 `#include "DUI/DUI.h"`；WIC 的链接指令随 `ImageCodec.cpp` 自带，应用工程不用额外配置。

### 1.3 支持格式

位图走 Windows 内置解码器（WIC）：**PNG / JPEG / BMP / DIB / GIF / TIFF / ICO** 等，取决于系统装了哪些解码器。**不支持 WebP / AVIF** 等 Windows 解不了的格式。SVG 不走 WIC，由 `dui::Svg` 单独处理（§5）。

---

## 2. 尺寸、DPI 与适配

### 2.1 自然尺寸 = 像素 ÷ DPI

不设 `Width` / `Height` 时，图像按自然尺寸显示：像素尺寸除以当前 DPI 缩放，即 100% 下 1 图像像素 = 1 物理像素，跨 DPI 不糊。不做 `@2x` 多倍图挑选，密度不足时由线性插值兜底。

`PixelWidth()` / `PixelHeight()` 返回的是**像素**尺寸，不是 DIP。

### 2.2 显式宽高 → contain 等比居中

给了 `Width` / `Height` 后，图像按 `min(盒宽/自然宽, 盒高/自然高)` 等比缩放后居中绘制（contain），**永不变形**。盒子比例与图像不同时会留白，留白区显示元素的 `Background`：

```cpp
auto* letterbox = root->AddChild<dui::Image>(dui::Style()
    .Width(240.f).Height(160.f).Padding(6.f)
    .Background(0xFFE5E7EB));   // 两侧留白露出底色
letterbox->LoadFile(path_);
```

只有 contain 一种适配；没有 `cover` / `fill` / `object-position`。想裁切可以配合 `Overflow::Hidden` 缩小元素盒（只做矩形裁剪，见 [Styles.md](./Styles.md) §9.1）。

### 2.3 两条布局特权

为避免被 flex 拉伸变形，`Image` / `Gif` / `Svg` 有两个特殊处理：

1. **交叉轴不被 Stretch 拉满**：未显式给该轴尺寸时用自然尺寸（显式给了才按用户尺寸铺开）。
2. **不参与主轴收缩**：空间不足时保持自然尺寸、溢出，而不是被压扁。

### 2.4 透明度与内边距

`Opacity` 由绘制层折进累计不透明度（自身与祖先链），纹理按该因子淡出；`Padding` 决定内容盒，图像画在内容盒内。

```cpp
root->AddChild<dui::Image>(dui::Style()
    .Width(140.f).Height(140.f).Opacity(0.5f))->LoadFile(path_);
```

---

## 3. 静态位图：`dui::Image`

```cpp
bool LoadFile(const wchar_t* path);            // 也有 const String& 重载
bool LoadMemory(const void* data, uint32_t size);  // 格式由内容嗅探
void Clear();                                  // 清除图像并停止动画
uint32_t PixelWidth() const, PixelHeight() const;  // 未加载为 0
const String& LastError() const;               // 最近一次加载失败原因
```

- 加载失败返回 `false`，原因在 `LastError()`（例如路径为空、格式解码失败）；成功时该串为空。
- `LoadMemory` 接收**纯图像字节**，不识别 `data:` 前缀（见 §7）。
- `Clear()` 后 GPU 纹理在下一次纹理同步时回收，不立刻释放。
- `Image` 无子元素（继承 `Element` 而非 `Panel`）。

---

## 4. 动画：`dui::Gif`

`LoadFile` 在 `Gif` 上统一走 WIC 多帧解码 + disposal 合成，加载后自动播放；单帧图退化为静态显示。

```cpp
auto* gif = root->AddChild<dui::Gif>(dui::Style().Width(320.f).Height(134.f));
if (!gif->LoadFile(L"demo.gif")) {
    root->AddChild<dui::Text>(gif->LastError().c_str());
}
gif->SetLoopCount(3);   // 播 3 轮停在最后一帧；0 = 无限（默认）
```

| 方法 | 语义 |
| --- | --- |
| `Pause()` | 停当前帧、保留游标、从时间源注销（暂停期间不空转） |
| `Play()` | 从暂停处继续，跳过暂停时长；已播完则从头重播 |
| `Stop()` | 回首帧、重置轮数；**保留帧序列**，`Play()` 可从头重播 |
| `SetLoopCount(n)` | `0` = 无限；`n > 0` 播完 n 轮停最后一帧。可在加载前设置 |

限制：不解析 GIF 文件内的 Netscape 循环次数（只由 `SetLoopCount` 控制）；没有 seek、逐帧事件、播放速度调节。`Image::LoadAnimatedFile` 与 `Gif::LoadFile` 都可用，在 `Gif` 上二者等价。

---

## 5. 矢量图像：`dui::Svg`

```cpp
auto* svg = root->AddChild<dui::Svg>(dui::Style().Width(96.f).Height(96.f));
if (!svg->LoadSvgFile(L"icon.svg")) {
    root->AddChild<dui::Text>(svg->LastError().c_str());
}
svg->LoadSvgMemory(bytes, size);        // 也可从内存加载
Vec2 size = svg->IntrinsicSize();       // viewBox 固有尺寸，单位是 DIP
```

- **按显示尺寸光栅**：加载时不立刻光栅（那时还不知道显示尺寸），按内容盒 DIP × DPI 生成纹理，尺寸或 DPI 变化才重做 —— 同一份 SVG 在 24 / 96 DIP 下分别光栅，放大不糊。
- **固有尺寸**：取根 `<svg>` 的 `viewBox`（其次 `width` / `height`），都没有时默认 24×24；也可用 `Style().Width()/Height()` 显式指定。
- **图标级子集**：支持 `path / rect / circle / ellipse / line / polygon / polyline / use / defs / clipPath / linearGradient / radialGradient` 等与 `fill / stroke / transform / viewBox` 等属性；**不支持** `<text>`、滤镜、蒙版、CSS、动画、外部资源。
- **静默降级**：不支持的元素/属性被忽略，其余尽量渲染且**不报错**，`LastError()` 拿不到线索。导出 SVG 时请把文字转路径、不用滤镜/蒙版/CSS；1703 上长度只用无单位值或百分比。
- 需要浏览器级 SVG 兼容时，框架不提供（不做 WebView 集成）。

---

## 6. 在 RichEditor 中插图

```cpp
editor->InsertImage(L"D:\\pics\\photo.png");                    // 文件路径
editor->InsertImage(L"data:image/png;base64,iVBORw0KGgo...");   // 或 data URI
```

在当前光标处插入图片块，保留撤销栈；尺寸按图像自然比例自动定盒，插入后光标落在图后。图片块默认**左对齐**；要居中，在文档 JSON 里给该块写 `"alignment": "center"`（`start` / `center` / `end`）。

应用无需自己接线，以下三种方式 `RichEditor` 已内置：

1. **拖入图像文件**：悬停时显示网页式插入光标，释放时插到预览位置（只吃 PNG/JPG/BMP/GIF/TIFF/ICO，其它文件显示不可放）。
2. **Ctrl+V 剪贴板位图**：截图工具 / 浏览器复制的图像 → PNG → data URI 插入。
3. **Ctrl+V 图像文件路径**：资源管理器复制文件（`CF_HDROP`）→ 筛图像扩展名 → 按路径插图。

在无序列表项里插图会按光标把当前项切成两段，图片夹在中间，前后各成一个独立列表；一次拖入多图只拆一次、连续插在切分点。完整交互与限制见 [Input.md](./Input.md) §5.7。

---

## 7. 剪贴板图像与 data URI

### 7.1 读写剪贴板图像

```cpp
dui::Clipboard::Image cimg;
if (dui::Clipboard::ReadImage(cimg)) {
    // cimg.width / cimg.height / cimg.pixels（BGRA8，行序自上而下）
}
dui::Clipboard::WriteImage(cimg);   // 覆盖语义：先 EmptyClipboard
```

`Clipboard::Image` 与 `graphics::DecodedImage` 的像素约定一致（BGRA8 straight alpha），可互相搬运。读写走 `CF_DIB`，只支持 32bpp / 24bpp。相关 API 还有 `ReadFiles`（资源管理器复制的文件列表）与 `HasFormat/ReadFormat/WriteFormat`（`"image/png"` / `"image/dib"`）。剪贴板操作须在 UI 线程调用。

### 7.2 data URI 的边界

- **只有 `RichEditor` 认识 data URI**，且只接受 `data:image/...;base64,` 形式。
- `graphics::DecodeImageMemory`、`dui::Image::LoadFile` / `LoadMemory` **都按纯文件/纯字节处理**，不解析 `data:` 前缀。要在 `Image` 控件里显示 data URI，需自行剥掉 `;base64,` 并 base64 解码，再走 `LoadMemory`。
- 框架未公开 base64 编码 API。要把位图转 data URI，可用 `graphics::EncodePng` 得到 PNG 字节后自行 base64 编码：

```cpp
dui::graphics::DecodedImage di;
di.width = cimg.width; di.height = cimg.height;
di.pixels = std::move(cimg.pixels);
std::vector<uint8_t> png;
dui::String err;
if (dui::graphics::EncodePng(di, png, &err)) {
    // png 为 PNG 容器字节；自行 base64 后拼 L"data:image/png;base64," + ...
}
```

---

## 8. 底层 API（多数应用用不到）

```cpp
// 解码（Graphics 层，WIC）
bool DecodeImageFile(const wchar_t* path, DecodedImage& out, String* err = nullptr);
bool DecodeImageMemory(const void* data, uint32_t size, DecodedImage& out, String* err = nullptr);
bool EncodePng(const DecodedImage& img, std::vector<uint8_t>& out, String* err = nullptr);

// DecodedImage：BGRA8 straight alpha、行序自上而下，pixels 长度 = width*height*4
```

- 解码结果统一为 BGRA8；失败时 `err` 填简短 UTF-16 原因。静态接口只取第 0 帧；动画用 `DecodeAnimatedImageFile`。
- `graphics::TextureStore::Instance()` 是进程级纹理仓库（CPU 侧像素），`Register()` 返回 `shared_ptr` 句柄，`Sync(device)` 由 App 每帧调用上传脏纹理、回收已释放纹理。UI 层通过 `dui::Image` 间接使用它。
- `Painter::AddImageQuad(textureId, rect, color, scale)` 是底层绘制原语：纹理 id 由 `TextureStore` 内部持有，**业务层拿不到，不要直接调用**。自绘控件里要显示位图，请挂 `dui::Image` 子控件；`dui::Canvas` 只有矢量图元，无图像 API（见 [CustomCtrl.md](./CustomCtrl.md) §3.5）。

---

## 9. 已知限制

1. 适配方式只有 contain；无 `cover` / `fill` / `object-position`。
2. 无圆角 / 阴影 / 遮罩（图像走纹理采样，没有 SDF 裁形）。
3. 动画仅 GIF；纹理常驻显存直到控件释放（无 LRU 淘汰）。
4. 不做多倍图；高 DPI 按物理像素 1:1 显示。
5. SVG 是图标级子集，不支持项静默忽略；无浏览器级兼容。
6. `AddImageQuad` 无九宫格 / border-image 能力。

完整清单与实现约束见 [Arch/16-Image.md](./Arch/16-Image.md) §5。

**可运行示例**：`Samples/Image`（contain 缩放、Opacity、Gif 播放控制、Svg 多尺寸光栅），工程文件 `Samples/Image/Image.vcxproj`。
