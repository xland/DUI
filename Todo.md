# DUI 开发任务清单（Todo）

> 最后更新：2026-11-09。
> 进度：**M1–M5.31 全部完成**（窗口/样式/布局/绘制/异步/文本/DPI/动画/Canvas/Scroller/
> TextBox/Image/Gif/无边框窗口/Grid/Position/zIndex/有序指令显示列表…）；
> **M6 主体完成**（命令式属性动画 + Paint 级 2D transform），仅剩收尾（见 §2）。
> 历史里程碑已归档为索引（§5）；实现踩坑与结论在 `Doc/Pitfalls.md`；
> 里程碑细节与验收历史用 git log 查。

---

## 0. 构建速查

| 项 | 要求 |
| --- | --- |
| 工具链 | Visual Studio 18（PlatformToolset v145），x64 单平台，Windows SDK 10.0 |
| 编译基线 | 根目录 `DUI.props`（/MT、/EHs-c-、/GR-、/utf-8、/W4、C++20），所有工程 import，**不要单独改** |

```cmd
cd /d <项目根>
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" DUI.slnx -p:Configuration=Release -p:Platform=x64 -m -v:m -nologo
```

- MSBuild 路径用 vswhere 定位；**必须编译 `DUI.slnx`**（单编 vcxproj 时 `$(SolutionDir)` 未定义）。
- 命令行全量重建刷 LNK4020 时加 `-p:UseMultiToolTask=true`（详见 `Doc/Pitfalls.md §2.9`）。
- 产物：`Build\<工程名>\Debug|Release\`；中间产物 `Build\Temp\...`（已 gitignore）。

---

## 1. 约定（编码时对照；坑的细节在 `Doc/Pitfalls.md`）

1. **API 变更三步走**：先改文档，再改示例，最后动实现（M4 隧道阶段推迟的教训）。
2. **无异常 / 无 RTTI**（/EHs-c-、/GR-）：错误走 `Result<T>`；避开会抛的 STL 用法（`vector::insert`、`ofstream`）。
3. **类作用域同名遮蔽**：方法名与类型同名（`Color`/`Display`/`FontWeight`/`Lifetime`）时，类内引用类型写全限定 `dui::Xxx`。
4. **链式返回左值**（`Style&` / `Element&`）：收样式的参数一律 `const Style&`，不要 `&&`。
5. **SetStyle 一律拷贝持有**（经 `ownedStyle_`）：链式临时可直传不悬空；派生链 `parent_` 与字体族指针仍指向外部，生命周期由调用方保证（建议字面量）。
6. **状态位契约**：`SetState` 相同值幂等；标脏帧末合并。
7. **DPI 红线**：不做 System aware 回退、不做位图缩放、非整数档自绘、描边像素对齐（12-Windowing §7.1）。
8. **公共头允许 windows.h**（2026-11-11 取消「公共头无 windows.h」约束；M5.27 起已实际引入）。仍保持：d3d11 / dwrite 等重图形头只随实现层头出现、不进 UI 公共头；import lib 用 `#pragma comment(lib)` 随编译单元携带，不动 `DUI.props`。
9. **属性表只追加不重排已有 ID**；变长结构（TrackList）不走定长 `StyleValue`，相应地不可动画 / 无状态变体 / 无运行时 `Set`。
10. **分层单向依赖**：Core 不依赖 App/UI（窗口事件经 `NativeWindowHost` 接口上抛）；UI 层不碰 D3D。功能类能力（窗口壳、对话框、剪贴板、任务队列等）归 Core，允许直接用系统 API（2026-11-11 起 Platform 层整体并入 Core，原「App 层注入 UI 函数指针」方案已废除）。
11. **没道理的问题先查 `Doc/Pitfalls.md`**，再上网搜。

---

## 2. 当前任务

### 2.1 M6 收尾

- [ ] **观测面板**：帧耗时分段计时（收集 / 排序 / 光栅 / Present 提交）+ Draw Call 计数。
      它是 `10-Rendering §9-5` 分段缓存立项判定的前置（P95 收集 > 0.5ms 且占比 > 15%、连续 100 帧）。
- [ ] **README 截图**（各示例运行效果）。

### 2.2 验证类（需人工 / 特定环境）

- [ ] 跨显示器 DPI 拖动断言（150%/200% 混合环境实测；发送 `WM_DPICHANGED` 可模拟，见 Pitfalls §3.2）。
- [ ] 空闲 CPU 归零复核（WaitMessage 路径 + 最小化停表已在 M4/M5.9 实现，做一次任务管理器复核）。

### 2.3 M7 系统对话框（已实现，待人工实测）

- [x] D1 公共 API（`App/Dialog.h` + `Window` 方法）/ D2 平台实现（`Platform/Dialog.*` + `App/Dialog.cpp`）/ D3 `Samples/Dialog`（含 `MacroCompat.cpp` 宏兼容守卫）。
- [x] Debug/Release x64 全解决方案编译、`/W4` 无新增警告、`DUI.props` 零改动（D4 的编译部分）。
- [x] **M7.1 修复「点击后界面卡死」**：UI 线程 COM 由 MTA 改 STA（`Application::Run` 用 `OleInitialize`，ADR-027）；`Platform/Dialog.cpp` 加 `CoGetApartmentType` 校验（放行 `APTTYPE_STA` / `APTTYPE_MAINSTA`）。自动化点击复测：四类弹窗弹出 / 关闭、主循环恢复、进程响应正常，15 个示例启动无回归（详见 `Pitfalls §4.11`）。
- [ ] 人工实测 `Doc/Arch/17-Dialogs.md §7` D4 的七项：DPI / 无边框窗口 / 焦点保留 / 后台回投 / 空闲 CPU / 边界输入 / 既有示例无回归。

---

## 3. 待办池（各里程碑"已知限制"汇总，做之前先读对应参考）

### 渲染
- [ ] 收集期视口剔除：M5.19 做了**指令级**剔除（发射前 scissor 求交），但收集仍遍历全树——超长列表的收集成本仍与内容总量成正比（M5.6 遗留）。
- [ ] 静态索引缓冲上限 16384 quad：超出需扩容（M5.5 起沿用）。
- [ ] 多边形无 GPU 解析式填充 AA（三角形填充边缘锯齿）；圆弧按 6° 折线近似（M5.5）。
- [ ] Draw Call = O(段数) ≈ 绘制序列批切换次数，与 R3 有张力（M5.17 的已知代价）。
- [ ] `StyleValue` 8B → 20B（Thickness 四边内嵌、字体族指针槽）：变长 Decl 形态列优化（M3/M5 偏差，见 03-Style §8）。
- [ ] `CornerRadius` 四角独立半径：`Style::CornerRadius(Thickness)` 已可存（`extra[3]`），绘制侧 `DrawBackground` 只读 `.f`——需 Graphics 层支持（M5.23）。
- [ ] `Animate()` 标签白名单只有 `float` / `Color`，不支持 Thickness 补间（M5.23）。

### 文本
- [ ] IME 组合串内联预览（现 `GCS_COMPSTR` 丢弃，拼音不在框内显示；候选窗定位已解决）（M5.9）。
- [ ] `Ctrl+←/→` 单词级移动、自绘选区手柄、富文本（M5.9）。
- [ ] 真实 ClearType（三通道图集 + 底色感知混合）作为灰度 AA 的备选路线（M5）。
- [ ] `DWRITE_TEXT_METRICS` 级缓存与文本分段计时（M5）。

### 布局
- [ ] `FlexWrap` 仅 Row，Column 不支持换行（M5.7）。
- [ ] Grid：无 `rowGap`/`columnGap`（统一用 gap）、TrackList 不可动画 / 无状态变体 / 无运行时 Set（M5.21 定界，非缺陷）。

### 控件与交互
- [ ] `PointerEvent` 无按键掩码 / 修饰键字段；指针事件无防抖节流（约定开发者自理）（M5.8）。
- [ ] Scroller：无平滑滚动 / 惯性（与 Animation 联动，P1）、无键盘滚动（PageUp/Down / 方向键）、滚动量按像素无行吸附（M5.6）。
- [ ] 无边框窗口：标题栏双击最大化（框架无双击事件）、阴影样式不可定制、缩放热区占用边缘像素需布局留余量（M5.11）。
- [ ] 异步：任务体 `std::function` 有分配（队列节点固定池 07-Threading D5）、退出排空完整版（R6）、`WhenAll/WhenAny`（M4）。

### 对话框（M7 已知限制）

- UI 线程 COM **必须 STA**（`Application::Run` 用 `OleInitialize` 建立，ADR-027）：MTA 下 `IFileDialog::Show` 死锁已实测并修复；不用 `Run` 的自定义宿主必须自己初始化 STA，否则文件类弹窗返回 `Error`（`Pitfalls §4.11`）。
- 清单无 `longPathAware`：超长路径可能拿不到 `SIGDN_FILESYSPATH`；虚拟 Shell 项不返回文件系统路径。
- `TaskDialogIndirect` 高级能力（自定义按钮/超链接/页脚）、自绘 DUI 对话框窗口、文件夹多选、异步版、文件读写：未做（`17-Dialogs §8`）。

### CSS 偏差（有意保留，非缺陷；改动需先立 ADR）
- transform / `contain` 祖先不成为定位 CB；`Fixed` 恒走窗口级层；无 stacking context，负 `zIndex` 不沉到流内之下（M5.18）；margin 不塌陷。

---

## 4. 远期（P2，有前置条件，非工作量问题）

- [ ] tile 脏区光栅 + 合成层：受 `10-Rendering §5` 已定 D2（FLIP 下按需全量光栅）约束，须先建脏矩形收集系统与持久离屏纹理，且 profile 证明全量光栅是瓶颈。
- [ ] 不变子树指令缓存 / 显示列表分段缓存：受 `10-Rendering §9-5` 双指标约束（P95 收集 > 0.5ms 且占比 > 15%、连续 100 帧），先落地 §2.1 观测面板。
- [ ] 窗口级 DComp 动画（透明度/位置不重绘）：05-Animation §7.4。
- [ ] SDF 无级缩放、用户级 UI 缩放（与系统 DPI 相乘）：12-Windowing §9。
- [ ] 元素级 DComp 动画：明确近期不做（05-Animation §7.4）。

---

## 5. 已完成里程碑索引（细节见 git log 与 Doc/Pitfalls.md）

| 里程碑 | 日期 | 内容 |
| --- | --- | --- |
| M1 | 10-28 | API 骨架冻结，示例编译链接通过 |
| M2 | 10-29 | Platform.Window + 消息循环 + PerMonitorV2 清单注入 |
| M3 | 10-29 | 样式级联解析 + Flex 布局 + D3D11 矩形绘制 + 按需渲染循环 |
| M4 | 10-29 | Dispatcher + 线程池 + 真协程 + 命中测试/Click/hover·pressed |
| M5 | 10-29 | DirectWrite 文本 + 字形图集 + DPI 切换分级失效（含三次锐度整改） |
| M5.5 | 10-29 | `Canvas` 2D 绘图控件 + 形状批（OnPaint 钩子落地） |
| M5.6 | 10-29 | `Scroller` 滚动容器 + 多绘制单元裁剪 + 滚动条 |
| M5.7 | 11-04 | 自定义字体文件（ttf/otf/ttc）+ FlexWrap + Style/StyleBuilder 合一 |
| M5.8 | 11-04 | 元素级指针事件（Enter/Leave/Move/Down/Up/Wheel） |
| M5.9 | 11-04 | 键盘事件 + 焦点/Tab 链 + `TextBox`/`TextArea`（含 IME 定位、密码掩码） |
| M5.10 | 11-04 | `Image`（WIC）+ 图像批 + 纹理仓库 + GIF 帧（`16-Image.md`） |
| M5.11 | 11-05 | 无边框窗口 + 自绘标题栏 + DWM 阴影 + 拖拽缩放（`WindowOptions::frameless`） |
| M5.12 | 11-06 | `PostUi` 安全回投 + `Task<void>` + Samples/Async |
| M5.13 | 11-06 | `position`（Static/Relative/Absolute/Fixed）+ Left/Top/Right/Bottom |
| M5.14 | 11-06 | Fixed 任意层级通用（窗口级 Fixed 层，绘制/命中一致） |
| M5.15 | 11-06 | Absolute 的 `zIndex` 层叠排序（四处消费同一有序列表） |
| M5.16 | 11-06 | `Samples/Position` 拆分定位演示 |
| M5.17 | 11-06 | **B2 有序指令显示列表**（Painter 指令化，跨批 Z 序严格） |
| M5.18 | 11-06 | Absolute 最近定位祖先（完整 CSS 语义 + 层叠归属同步） |
| M5.19 | 11-06 | P1 渲染开销优化（图像单元清理 + 指令级视口剔除） |
| M5.20 | 11-06 | `Samples/CustomCtrl` 自定义控件全链路（ValueSlider） |
| M5.21 | 11-06 | Grid 布局（四类轨道 + 自动放置 + 显式跨格） |
| M5.22 | 11-06 | `margin` / `textAlign` 消费链路补齐 |
| M5.23 | 11-06 | 运行时 `Set()` 支持非标量属性（Thickness / 字体族） |
| M5.24 | 11-06 | `Samples/Style` 样式演示独立成示例 |
| M6（主体） | — | 命令式 `Animate()` + Paint 级 transform + GIF/闪烁统一时钟；声明式动画已从文档移除 |
| M7 | 11-07 | 系统对话框：`OpenFile` / `SaveFile` / `OpenFolder` / `MessageBox` + 三态 `DialogResult` + C++20 `std::span` 过滤器 + 失活焦点抑制 + `Samples/Dialog`（`Doc/Arch/17-Dialogs.md`） |
| M7.1 | 11-07 | 修复「点击后界面卡死」：UI 线程 COM 由 MTA 改 STA（`OleInitialize` + 单元校验，ADR-027）；自动化点击复测四类弹窗 / 15 个示例（`Doc/Pitfalls.md §4.11`） |
| M5.25 | 11-09 | `Samples/Dialog` 六个可点击项按钮化（背景色 + `When(State::Hover)` 效果），`Doc/Dialogs.md §2` 同步；"封装 Button 组件"任务取消（现有 `dui::Button` 已够用），框架零改动 |
| M5.26 | 11-09 | 内置矢量 `Icon` 组件：`dui::IconName` 强类型枚举（9 图样）+ `dui::Icon` 控件 + `dui::DrawIcon()` 底层绘制；尺寸只走 `Style().Width/Height`、颜色走继承 `Color`；新建 `Samples/Icon`；`Samples/CustomFrame` 标题栏按钮由 iconfont 换成 Icon（移除字体加载与 PostBuildEvent） |
| M5.27 | 11-09 | 公共 API 暴露 `HWND`（`Window::NativeHandle()`，公共头改为引入 `windows.h`）；窗口渲染统一走 `WS_EX_NOREDIRECTIONBITMAP` + `CreateSwapChainForComposition` + DirectComposition（ADR-010 落地），`WindowOptions::transparent` 决定 `AlphaMode`/清屏 alpha；新建 `Samples/CircleWindow`（圆形半透明窗口 / `SetWindowRgn` 圆裁剪 / 全窗口 HTCAPTION 拖动 / Canvas 细线圆边框） |
| M5.28 | 11-09 | 系统原生弹出菜单：`dui::Menu` 链式构建（`Add` / `Separator` / `AddSubMenu` / `AddCheck`）+ `Window::ShowMenu(menu[, posDip])`（`TrackPopupMenuEx` + `TPM_RETURNCMD`）；`PointerEvent::button` + `PointerButton` 枚举，平台层转发 `WM_RBUTTONUP`（右键抬起不参与左键 pressed / 焦点 / Click 合成）；新建 `Samples/Menu` |
| M5.29 | 11-09 | 事件对象统一：删除 `ClickEvent`，`OnClick` 回调改收 `PointerEvent&`（`pos` = 抬起处、`button` = Left）；`click` / `tunnelClick` 槽并入惰性 `Handlers`，`DispatchClick` 复用 `PhasedDispatch`（删手写双阶段派发与 `friend`，未绑 Click 的元素少占 64B）；9 个示例 + `Doc/Dialogs.md` / `06-Events.md` 签名同步，运行时语义不变 |
| M5.30 | 11-09 | 新增 `dui::Svg`（派生 `Image`）：WIC 无内置 SVG 解码器，改走 Direct2D 内置 SVG（`ID2D1DeviceContext5`，Win10 1703+）按显示尺寸 × DPI 光栅成 BGRA8 → `TextureStore`，渲染核心零改动；`LoadSvgFile` / `LoadSvgMemory`、viewBox 固有尺寸、尺寸/DPI 变化重光栅；`App/Frame.cpp` 纹理同步移到收集之后；示例并入 `Samples/Image`（含 `icon.svg`）；ADR-017 / `16-Image.md` / `00-Overview` 同步 |
| M5.31 | 11-09 | 元素阴影（方案 1：SDF 衰减带）：新增 `Prop::Shadow` + `Style::Shadow(blur, color, offsetX, offsetY)`（f/u/extra 槽装下，无需 ShadowRef）；`Painter::ShadowRoundRect` 走 Shape 批新增 `kind=3`，把 1px AA 过渡带换成以元素边界为中心的 S 形衰减带（d=0 处 cov=0.5，对齐高斯边界半强度），quad 外扩 blur；`Collector::DrawShadow` 在背景之前绘制（透明背景也有投影，不影响命中）；零纹理/零离屏/零缓存，未设阴影元素零开销；示例并入 `Samples/Style` |
| M5.32 | 11-11 | 剪贴板下沉为 Core 公共能力：新增 `Core/Clipboard`（静态类直接 Win32：`ReadText/WriteText` CF_UNICODETEXT + `HasFormat/ReadFormat/WriteFormat` 任意格式 mime+字节，"image/png"→注册格式 PNG、"image/dib"→CF_DIB），同批补图像读写 `Clipboard::Image`（BGRA8 straight alpha 顶向下，与 `graphics::DecodedImage` 同约定）↔ CF_DIB（读支持 32/24bpp 标准 BGRA 布局、BI_RGB alpha 全 0 视为不透明，系统自动从 CF_DIBV5 合成；写 32bpp 顶向下 BI_RGB），`RichEdit` 复制/剪切/粘贴切换调用；废除「UI 层函数指针注入 + App 装配」方案（`UI/UI.h` 注入段、`TextBox.cpp` 管道、`Platform` 两函数、`App.cpp` 接线全删）；同步取消「Core 不依赖 Win32」约束（`01-Layering` 改为"功能服务层"定位）与 `Pitfalls §4.5` 旧公共头边界条目 |
| M5.33 | 11-11 | **Platform 层并入 Core**（链路变为 App→UI→Text→Graphics→Core）：`Platform/Window.{h,cpp}`、`Dialog.{h,cpp}`、`DUI.manifest` 五件 git mv 至 `Core/`；`namespace dui::platform` 拍平为 `dui`（App 层 57 处引用同步；`App/Dialog.cpp` 的 `SaveFile` 调用因成员方法遮蔽改显式限定 `dui::SaveFile`；`App.h` 前向声明 `NativeWindow` 移出匿名 namespace）；`DUI.props` manifest 路径、vcxproj/filters（`ObjectFileName` 改 `Core_Dialog.obj` 保 MSB8027 规避）；文档同步：`01-Layering` 链图/模块表删 Platform 节、`Todo` 约定 #10（「Core 不依赖 App/UI」）、`Pitfalls §4.7/4.8/4.11`、`02-Decisions`、`06-Events`、`12-Windowing`、`17-Dialogs` 当前结构引用（历史记录行保留） |
| M5.34 | 11-11 | **Menu / Dialog 并入 Core 公共服务**（App 层只留 Window 方法薄封装）：`App/Menu.{h,cpp}` git mv 至 `Core/`（新增 `ShowPopupMenu` 服务函数，`Window::ShowMenu` 只做坐标换算 + `dialogDepth_`）；`App/Dialog.h` 公共类型（`DialogResult` / 选项 / 枚举）并入 `Core/Dialog.h` 后删除——公共服务函数直接收公共类型返回 `DialogResult`，原「App 层 POD/枚举转换层」（`ToNativeFilters` / `ToNativeIcon` 等 ~50 行）消失，`FileTypeFilter` 与 `COMDLG_FILTERSPEC` 同构直接重解释；`App/Dialog.cpp` 收缩为 owner 取值 + 深度计数薄封装（含从 Menu.cpp 移来的 `ShowMenu` 方法）；`DUI.h`/`App.h` include 更新、vcxproj/filters 登记 |
| M5.35 | 11-11 | **窗口壳 / 对话框 / 菜单迁入 App 层**（App = 应用运行时层）：`Core/Window.{h,cpp}`、`Core/Menu.{h,cpp}` git mv 至 `App/`；`Core/Dialog.{h,cpp}` 迁入 `App/` 并与原 `App/Dialog.cpp` **合并为单文件**（服务实现 + Window 薄封装同处，MSB8027 特例与 `ObjectFileName` 一并取消）；`Core` 收敛为「基础 + 通用服务」（Core.h / Json / Utf8 / Threading / Dispatcher / Clipboard / manifest）；`DUI.h`/`App.h`/各 cpp include 与注释同步，vcxproj/filters 更新；文档：`01-Layering`（Core/App 表重写、Core 定位改「基础与通用服务层」）、`00-Overview`、`12-Windowing`、`17-Dialogs`、`Pitfalls §4.7/4.8/4.11`、`02-Decisions` ADR-027 |
| M5.36 | 11-11 | **目录层级收敛（ADR-028）**：`Text/`（`TextEngine.*` / `GlyphAtlas.*`）git mv 至 `Graphics/`、`RichText/`（`RichDoc*` / `CssValue.*`）git mv 至 `UI/`，链路由 `App→UI→Text→Graphics→Core` 变为 `App→UI→Graphics→Core`；**Graphics 升格为正式公共层**——`Graphics/Graphics.h` + `Graphics/ImageCodec.h` 聚合进 `DUI/DUI.h`，`Samples/CustomCtrl` 取消额外内部头目录 `$(SolutionDir)DUI\` 与多余 include（自绘控件只 include `DUI/DUI.h` 即得 `graphics::Painter` 完整类型）；全仓 include 路径重写（29 处）、vcxproj/filters 路径更新（`Text`/`RichText` filter 节点删除，补登漏登的 `RichDocEdit.*`）；命名空间 `dui::text` / `dui::richtext` 保持不变；文档：`01-Layering`（链图 + Graphics/UI 表重写）、`00-Overview` 目录树、`02-Decisions` ADR-016 + 新增 ADR-028、`11-Text` L1/L2/L3 归属、`04-Layout` / `10-Rendering` 的「Text 层」措辞、`RichEditor` 路径；Debug + Release 编译通过 |
| M5.37 | 11-11 | **从内存加载字体**（资源嵌入 exe 场景）：`DUI.h` 新增 `dui::LoadFontFamilyFromMemory(const void* data, size_t size)`——底层用 DirectWrite 官方 in-memory loader（`IDWriteFactory5::CreateInMemoryFontFileLoader`，Win10 1703+ 与 ADR-009 基线对齐），`ownerObject` 传 nullptr 由 DWrite 自行拷贝字体字节 → 调用方源缓冲可立即释放；装载器需保持注册（引擎持有并在析构时注销）；顺带把自定义字体从「引擎只装一个」改为**可装多个**：每个字体自带单字体集合 + 族名，排版时按族名挑集合挂到 layout（`Shape`/`ApplyRuns`/`HasFamily` 同步改走 `CustomCollectionOf`），文件加载与内存加载两个入口语义一致、可叠加；文档：`DUI.h` 注释（含资源 API 用法片段）、`15-Properties` fontFamily 行；Debug + Release 编译通过 |
| M5.38 | 11-11 | **消除公共头的 D3D 宏污染 + 两类构建噪音**：① **回归修复**——上一轮把 `Graphics/Graphics.h` 聚合进 `DUI.h` 后，`<d3d11.h>`（第 26-27 行 `#include "rpc.h"`/`rpcndr.h` → `rpcndr.h:201` `#define small char`）进入所有业务 TU → `Samples/Image` 的 `dui::Svg* small` 被改写成 `dui::Svg* char` → C2062，示例编译失败。修法：D3D 设备 `Device` 拆到**内部头** `Graphics/Device.h`（新增），公共 `Graphics/Graphics.h` 只留 Painter / 顶点指令 / TextureStore（纯 C++、不再含 windows.h）；`Graphics.cpp` / `App.cpp` / `Frame.cpp` 改 include Device.h；vcxproj/filters 登记新头。② **C28251**（VS 实时代码分析，非编译器警告）：SDK 的 `winbase.h:1075` 给 `wWinMain` 声明加了 SAL 批注，19 个示例的入口定义统一改为 `int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)`。③ **LNK4020**（PDB 类型记录已损坏）判定为辅助警告：先修真正的错误、清 Build 重编可消除，不影响 exe 运行。文档：`Pitfalls §4.5`（d3d11.h 不能进公共头）、`§2.9`（LNK4020 补充）、`§4.12`（新增 C28251）、`01-Layering` Device 标注；**Debug + Release 全解决方案（库 + 19 示例）零错误零警告** |
| M5.39 | 11-11 | **RichEditor：图片块支持「图前 / 图后」两个光标位**：图片原是零长度定位点（光标只能停在左缘），现改为占一个光标位宽 —— `offset 0` = 图前（贴图片左缘）、`offset 1` = 图后（贴图片右缘）。命中测试按图片盒中点分：点在图片右半（含图片右侧的空白）落图后、左半落图前 → 「点击图像后面的空白」光标落在图像右侧，此时 Backspace 删除该图。配套：`TextLenOf` 对图片返回 1（`ClampToDoc` 放行 offset 1）、`CaretRectOf` 按 offset 画左/右缘、图片伪视觉行 `end = 1`（End 落图后）、`MoveHorz` 支持图前 ↔ 图后 ↔ 相邻块、`DeleteForward` 区分图前（删图）/ 图后（删下一块）、`LeaveImageToText` 按图前/图后分别落到前段末尾/后段开头（输入 / 回车 / 粘贴一致）、图片块内（图前 ↔ 图后）不计为选区。文档：`RichEditor.md §3.6` 补「图片块的光标位」条目、`RichEdit.h` 头注释；Debug + Release 编译通过 |
| M5.40 | 11-11 | **RichEditor：代码块（CodeBlock）可编辑**：代码块原是只读块（`BuildItem` 的 `!code` 把它挡在 `editables` 之外，光标进不去），现改为可进入、可增删字符。代码内容存放在 `Block::code`（整段字符串含 `'\n'`，与 `runs` 分离），故模型层编辑原语按 `kind` 分流：`BlockText`/`InsertText`/`DeleteRange`/`StyleAt`/`SplitBlock`/`AppendRuns` 对 `BlockKind::Code` 直接操作 `code`（其余仍走 `runs`）。视图层：`IsCaretKind` 纳入 Code、`BuildItem` 去掉 `!code` 排除 → 代码块登记进 `editables`（光标可落、可选区、可复制）；**回车 = 块内换行**（往 `code` 插 `'\n'`，不拆块）；**粘贴 = 整段插入**（多行文本原样进 `code`，不拆成段落）；**块边界不跨块合并**（代码块与邻块之间的 Backspace / Del 不合并不吞字，跨块选区删除时两块各自清空、保持独立）；多行由 DWrite 强制换行形成多个视觉行（上下键 / Home / End 在行内生效，行号列随 `code` 重建）。文档：`RichEditor.md §3.6` 补「代码块可编辑」条目（含"块内不留 `\n`"原则的例外说明）、`RichEdit.h` 头注释、`RichDocEdit.h` 文件头注释；Debug + Release 编译通过 |
| M5.41 | 11-11 | **RichEditor：删图后光标落在图片原来的位置**：`EraseImageAt` 原本统一落 `ClampToDoc(Cursor{index, 0})` —— 图片是最后一个可编辑块时，clamp 把光标拽回**前一块的开头**（示例文档里图片紧跟代码块之后 → 光标跳回代码块第一个字符前）。改为按几何位置落点：后面还有可编辑块 → 该块开头（图片就在它前面）；没有 → 前一个可编辑块的**末尾**（`EndCursor(index-1)`，代码块即最后一个字符之后）。删图的三条路径（光标在图上退格 / 段首退格删前邻图 / 段尾 Del 删后邻图）都经由此函数，落点一致。文档：`RichEditor.md §3.6`「图片块的光标位」条目补落点规则；Debug + Release 编译通过 |
| M5.42 | 11-11 | **控件改名：`RichEdit` → `RichEditor`**（公共 API 破坏性变更）：框架内所有公共控件类都是名词（`Text` / `Button` / `Icon` / `Image` / `Gif` / `Svg` / `TextBox` / `TextArea` / `Canvas` / `Scroller` / `Panel` / `Window` / `Menu` / `Element`），`RichEdit` 是**唯一的动词短语**（"rich edit" 是动作），与自身命名惯例不一致；且仓库里 "RichEditor" 已是既定名（特性文档 `Doc/Arch/RichEditor.md`、示例工程 `Samples/RichEditor`、解决方案条目），类名是唯一例外。改动：`git mv DUI/UI/RichEdit.{h,cpp}` → `RichEditor.{h,cpp}`（保历史）；类名 / 构造析构 / 73 处限定名同步；`UI/UI.h` include 与文件清单注释、`DUI.vcxproj` + `.filters` 登记项、`Samples/RichEditor/main.cpp`（`dui::RichEditor`）、`RichDocEdit.h` 文件头注释同步；文档：`11-Text.md`（6 处）、`02-Decisions.md`（4 处）、`RichEditor.md`（8 处）、`01-Layering.md` RichText 行补控件落地位置。模型层 `RichDoc` / `RichDocJson` 命名不变（模型 / 编解码，与控件分工清晰），模型层编辑原语模块的文件名另见 M5.43；历史记录行保留原样。Debug + Release 编译通过 |
| M5.43 | 11-11 | **模型层编辑原语模块改名：`RichDocEdit` → `RichDocEditor`**：`git mv DUI/UI/RichDocEdit.{h,cpp}` → `RichDocEditor.{h,cpp}`（保历史），命名族统一为「`RichDoc` + 名词能力后缀」—— `RichDoc`（模型） / `RichDocJson`（编解码） / `RichDocEditor`（编辑原语），与控件侧「`RichEdit` → `RichEditor`」的动词短语处理保持一致。**注意它仍是模型层模块**（命名空间 `dui::richtext`，纯函数 `BlockText`/`InsertText`/`DeleteRange`/`SplitBlock`/`AppendRuns`/`MergeRuns`，不知道控件存在），文件名带 Editor 不代表它是控件 —— 为此在头注释里补了一句层次说明（"模型层模块，不是控件：这里只管文档数据怎么改，光标 / 命中 / 排版 / 绘制全在 `UI/RichEditor.*`"）。配套：两个文件的文件头注释与自 include、`RichEditor.cpp` 的 include 与文件头注释、`DUI.vcxproj` + `.filters` 登记项、`02-Decisions.md` ADR-028 落地记录里的文件名同步。符号（`dui::richtext::*` 函数）**未改名**，外部代码不受影响。Debug + Release 编译通过 |
| M5.44 | 11-11 | **RichEditor：命中带按「文字区 / 图片行」划分，修掉「点图片右上角空白光标跳进代码块」**：几何事实 —— 代码块盒高 = 文字高 + 2×`kCodePadding`（上下各 10 DIP 内边距），而示例文档里图片 `margin.top = 0` → **图片顶边紧贴代码块盒底边**，中间夹着一条 10 DIP 的淡灰条（代码块背景）。旧 `HitTestDoc` 用 `item.blockBox`（含内边距）判竖直范围 → 点在那条灰条里被判成落在代码块内 → DWrite hit-test 落到代码最后一行 → 光标跳进代码块；而图片的命中范围只有图片盒，不含上方那条 → 点图片右上角空白被代码块抢走。改法：引入**命中带 band** —— 文本块取 `textBox`（文字区，不含内边距），图片块取 `blockBox` 并**向上延伸到上一块命中带底**（上一块的下内边距 / 间隙归「图片这一行」）；间隙判定同步改用相邻命中带边界。效果：点图片上方那条灰条 → 按水平位置落图前 / 图后（右侧 → 图后）；点代码文字本身仍进代码块。Debug + Release 编译通过 |
| M5.45 | 11-11 | **RichEditor：光标在图后按 Del 删掉后面的横线（分割线）**：分割线（`BlockKind::Rule`）是**不可编辑块**，不在 `editables` 里 —— 此前没有任何路径能删它（示例文档里图片后面紧跟一条横线，光标在图后按 Del 毫无反应）。新增能力：① `NextBlockAfter` / `NextBlockRec` —— 按**文档顺序**（跨容器 / 跨层级）找某块之后的第一个块（`editables` 只含可编辑块，装饰块得按文档树走）；② `EraseDecorBlock` —— 把装饰块从容器里摘掉（走 `FindSpot` 定位），光标落到指定位置；③ `DeleteAfterImage` —— 图后按 Del 的完整语义：**分割线 → 删掉它**（光标留在图后）、后邻图片 → 删掉它、后邻正文块 → 光标移进该块开头（图片不能与正文合并，否则图片会被 `AppendRuns` 变成文本块 —— 这是旧代码在「图后 + 后邻文本块」时的隐藏 bug，本次一并修掉）。另外「正文块末尾按 Del」时若右邻是分割线也删掉它（同一 `NextBlockAfter` 通路）。文档：`RichEditor.md §3.6`「图片块的光标位」补「块边界上的 Del」；Debug + Release 编译通过 |
| M5.46 | 11-11 | **RichEditor：光标在图前按 Backspace 删掉前面的内容**：此前 `DeleteBackward` 对图片的两个光标位一视同仁 —— `if (IsImageAt(c.block)) return EraseImageAt(c.block);` —— 图前按退格删的是**图**（光标右边的东西），与"退格只删光标左边紧邻的东西"矛盾。改为按光标位分流：**图后（offset 1）**左边紧邻的就是图片 → 删图（保持 M5.39 的既定行为）；**图前（offset 0）**左边紧邻的是前一个块 → 删前块的内容 —— 前块紧邻的是分割线（不可编辑的装饰块）→ 删掉它（与「图后 Del 删后邻装饰块」对称）；前块是图片 → 删图；前块是文本 / 代码块 → **光标挪到前块末尾再退格**，删掉它的最后一个字符（代码块走 `RichDocEditor` 的 Code 分支，删的是 `code` 里的字符）。顺带统一了块的"边界 = 光标位"语义：图前是图片的左边界（Del 删图 / 退格删前块），图后是图片的右边界（退格删图 / Del 删后块）。文档：`RichEditor.md §3.6`「图片块的光标位」补「退格只删光标左边紧邻的东西」；Debug + Release 编译通过 |
| M5.47 | 11-11 | **RichEditor：命中测试引入「就近竞争」——点图片右侧上方落在图后**：M5.44 只把"图片正上方那 10 DIP"（代码块下内边距）划归图片这一行，用户点的位置更高 —— 落在**代码块最后一行文字**上（那行右侧是空的），仍被判给代码块。根因：命中测试只按"竖直归属哪一行"决定，光标位置一律吸到该行行尾；而示例文档里代码块只有 3 行短字（文字宽 ≈ 120）却占满整行宽（图片又大又居中，`textW` 远小于 `availW`），点空白处吸到行尾等于把光标拽到几百像素外的左侧。改法：光标位本质都是"贴在某处的小矩形"（文本位置 = 字符格；图前 / 图后 = 贴在图片左右缘、**与图片等高**的 1px 竖线），点击时取**离点击点最近**的那个 —— `HitTestDoc` 的文本分支算出行内命中位置后，与**相邻图片块**的图前 / 图后比距离（`DistToRect`，平方距离；图片的竖线向本块方向延伸：下方图片向上盖到本块内容底、上方图片向下盖到本块内容顶），更近者胜。效果：点代码块右侧空白（图片右上方）→ 落在图片右缘的**图后**；点代码文字本身 → 仍是代码（距离 ≈ 0）；无相邻图片时行为完全不变。文档：`RichEditor.md §3.6` 补「就近竞争」；Debug + Release 编译通过 |
| M5.48 | 11-11 | **根治 LNK4020「DUI.pdb 中的类型记录已损坏」（并行链接时每个 .obj 一条）**：这次不是"辅助警告"，是实打实的并发写坏共享 PDB —— Release 的库开了 **/GL**，库的调试信息放在共享 PDB（`$(IntDir)DUI.pdb`，VC targets 另复制一份 `_LibPdb = $(OutDir)DUI.pdb`）；链接示例时 **link.exe 会把 LTCG 原生代码信息回写进这份 PDB**，VS/命令行并行链接 19 个示例时多个 link.exe **并发改写同一份 PDB** → 类型流写坏 → 其余链接器读到损坏类型流 → 每个 .obj 报一条 LNK4020。定位手法：① 单工程链接 / 串行构建**不复现**，并行 `msbuild DUI.slnx /m` **必现**；② 并行构建前后对比库 PDB **大小**：3,567,616 → 3,248,128 字节（被回写重整过）。修法：**库改用 `/Z7`**（`DUI.vcxproj` Release 加 `<DebugInformationFormat>OldStyle</DebugInformationFormat>`）—— 调试信息内嵌 .obj，链接器从 .obj 直读类型记录，不再有可写的共享 PDB，竞态消失；顺带让库自包含（可分发）。代价：lib 44 MB → 61.6 MB，**exe 体积不变**（调试信息照旧进 exe 自己的 PDB，实测 8.5 MB）。**Debug 配置不动**（无 /GL → 无 LTCG 回写 → 本来无此竞态，已并行全量验证零警告）。踩坑：`/Z7` 在 MSBuild 里的枚举值是 `OldStyle`，写 `Embedded` 会报"元素 DebugInformationFormat 具有无效值"。文档：`Doc/Pitfalls.md §2.9` 补「LNK4020 的真正成因与修法」 |

---

## 6. 备忘

- **新建子工程**：目录放 `Samples/<工程名>/`，**照抄 `Samples/HelloWorld/HelloWorld.vcxproj`**（OutDir/IntDir 指到 `Build\`，写在 Microsoft.Cpp.props 之后），挂入 `DUI.slnx`；别忘了 `.vcxproj.filters`。
- 新建源文件记得同步 `DUI/DUI.vcxproj`（和 `.filters`）。
- 每完成一个任务：更新本文档 + `00-Overview §6` 工程状态。
- 构建产物整体在 `Build/`，已加入 `.gitignore`。
- 示例清单与说明见 `00-Overview §6` 与 `README.md`。
