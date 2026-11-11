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

---

## 6. 备忘

- **新建子工程**：目录放 `Samples/<工程名>/`，**照抄 `Samples/HelloWorld/HelloWorld.vcxproj`**（OutDir/IntDir 指到 `Build\`，写在 Microsoft.Cpp.props 之后），挂入 `DUI.slnx`；别忘了 `.vcxproj.filters`。
- 新建源文件记得同步 `DUI/DUI.vcxproj`（和 `.filters`）。
- 每完成一个任务：更新本文档 + `00-Overview §6` 工程状态。
- 构建产物整体在 `Build/`，已加入 `.gitignore`。
- 示例清单与说明见 `00-Overview §6` 与 `README.md`。
