# 分层与模块

> 状态：草案 v0.1 — 待讨论确认。

## 1. 依赖规则

```
App ──→ UI ──→ Text ──→ Graphics ──→ Platform ──→ Core
```

- **单向依赖**，下层不得引用上层，禁止跨层调用。
- **Core 不依赖 Win32**，保证其可独立测试、可被裁剪。
- 只有 App 层做组装；UI 层只依赖 Text 与 Graphics 的抽象接口，不依赖具体后端实现。

## 2. 模块清单

### Core（无平台依赖）

| 模块 | 职责 |
| --- | --- |
| Memory | Arena、Pool/Slab、对象池；热路径分配入口 |
| Containers | 侵入式链表、小容量向量、开放寻址哈希表（替代 STL 时提供） |
| String | UTF-8 / UTF-16 视图与转换、SSO 字符串 |
| Math | Vec2、Rect、Mat3x2、Color（预乘 alpha） |
| Log / Assert | 日志与断言，Release 下可整体编译掉 |
| Ref | 引用计数与作用域句柄（是否用原子计数见 ADR-007） |
| Time | 时间戳、计时器、动画时钟基础 |

### Platform（Win32）

| 模块 | 职责 |
| --- | --- |
| Window | HWND 封装、窗口样式、创建/销毁 |
| MessageLoop | 消息泵、消息翻译与分发 |
| Input | 鼠标/键盘/触摸/触控笔 → 归一化输入事件 |
| Dpi | 显示器信息、DPI 变化、缩放换算 |
| Ime | 输入法候选窗定位与组合串处理 |
| Clipboard / DragDrop | 剪贴板与拖放 |
| Cursor | 光标资源管理 |

### Graphics（Direct3D 11 + DXGI）

| 模块 | 职责 |
| --- | --- |
| Device | D3D11 设备/上下文、DXGI 工厂、适配器选择、设备丢失恢复 |
| SwapChain | Flip Model 交换链、尺寸变更、Present 策略 |
| Resource | Texture2D、Buffer、Shader、Sampler、状态对象 |
| Batcher | 顶点/实例环形缓冲、批合并、Draw Call 提交 |
| Painter | 绘制指令收集（显示列表） |
| Atlas | 通用纹理图集（字形/图标共用） |
| Clip | 矩形/圆角矩形裁剪栈 |

### Text（DirectWrite）

| 模块 | 职责 |
| --- | --- |
| FontCollection | 系统字体枚举 + 内嵌字体加载 |
| Shaping | 基于 `IDWriteTextAnalyzer` 的成形、换行、双向文本 |
| Layout | 段落排版、度量、命中测试（文本坐标 ↔ 字符索引） |
| Raster | 字形光栅化（方案见 ADR-003） |
| GlyphAtlas | 字形缓存与淘汰策略 |

### UI

| 模块 | 职责 |
| --- | --- |
| Element | 元素树（父子关系、Z 序、可见性、裁剪） |
| Layout | 布局算法（见 ADR-005 / ADR-006） |
| Style | 样式解析与继承、主题 |
| Event | 事件路由、命中测试、焦点、键盘导航 |
| Invalidate | 脏区标记与合并、重绘调度 |
| Animation | 属性动画、缓动、合成器驱动 |
| Widgets | 具体控件实现（可裁剪） |

### App

| 模块 | 职责 |
| --- | --- |
| Application | 进程级初始化、全局资源、退出流程 |
| Theme | 默认主题与资源（颜色、字号、图标） |
| HighWindow | 高层窗口封装（组合 Platform.Window + UI + Graphics） |

## 3. 一帧的生命周期

```
[输入]  Win32 消息 → 归一化输入事件
   ↓
[分发]  命中测试 → 事件沿元素树冒泡/隧道 → 处理器
   ↓
[失效]  处理器修改状态 → 标记脏元素 / 脏矩形
   ↓
[布局]  仅对受影响的脏子树做 Layout（自底向上度量，自顶向下排列）
   ↓
[收集]  遍历脏区域内的元素 → 生成绘制指令（显示列表）
   ↓
[光栅]  指令排序 → 批合并 → 顶点生成 → 提交 Draw Call
   ↓
[呈现]  Present（按需：无脏区则不呈现、不占用 CPU）
```

关键点：**"没有失效就没有工作"**。空闲时不做任何布局、绘制、Present，CPU 归零。

## 4. 内存策略（概要）

- 每个窗口持有固定容量的竞技场（Arena），元素/布局结果/绘制指令从中分配，窗口关闭时整体释放。
- 文本与图片等缓存为**进程级共享**、有容量上限、按 LRU 淘汰。
- 长生命周期对象（字体、纹理、着色器）用引用计数；短生命周期对象用竞技场，不逐个释放。
- 具体方案见后续 `13-Memory.md`。

## 5. 可裁剪性

编译期宏控制：`DUI_ENABLE_TEXT`、`DUI_ENABLE_IMAGE`、`DUI_ENABLE_ANIMATION`、`DUI_ENABLE_A11Y` 等。
关闭后对应模块代码不参与编译，也不参与链接，直接体现在体积指标上。
