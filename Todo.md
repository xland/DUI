# 待办清单：性能 / 内存 / 编译体积 / 死代码

> 来源：框架全代码审查（2026-11-24）。已修复项（BUG 1–7、BUG 3 边界、Json.cpp 两个性能点）不再列入。
> 排序：按优先级统一排列，每条标注类别与修复难度。路径相对仓库根目录。

## 评估后暂缓

- `DUI/Graphics/TextEngine.cpp:733` — 缓存字形批次：需处理 scale / origin / 元素颜色 / 图集 page 淘汰四个失效维度（page 失效须给 GlyphAtlas 加 generation 并跨模块联动），动 4 文件 100+ 行；而省掉的仅是 DWrite 回调与一次哈希查找，1000 字约几十微秒、不足帧预算 1%，且一旦遗漏 page 失效会导致文字错位。收益风险不划算，2026-11-24 评估后决定不改。
- `DUI/UI/StyleResolve.cpp:85` — 缓存属性解析结果：失效源太多（动画每帧写 / 状态切换 / Set 调用 / Style 派生链变化），遗漏即"样式不更新"隐性 bug；只给 Style 加 Prop→下标索引则每 Style 多几百字节且值拷贝变重。线性扫的是紧凑小数组，实测量级微秒，收益不足 2% 帧预算。2026-11-24 评估后暂缓。
- `DUI/DUI.props:24` — /MT 静态 CRT 是 ADR-008 明确选定的架构决策；`DUI.props` 头部注释强调库与子工程必须统一，否则 LNK2038。改 /MD 要求所有使用方同步切换，收益是 exe 小几百 KB、代价是引入 CRT DLL 依赖。2026-11-24 评估后不改。
- `DUI/DUI.vcxproj:74` — /Z7 是刻意选择：注释说明改回共享 PDB 会在多个示例并行链接时并发写坏 PDB（LNK4020 类型流损坏）。2026-11-24 评估后不改。
- `DUI/Graphics/TextEngine.cpp:493` / `DUI/App/Frame.cpp:335` / `DUI/UI/StyleResolve.cpp:298` / `DUI/UI/StyleResolve.cpp:234` — 缓存命中/父链/zIndex/定位祖先缓存：与前两条同类，失效源多（动画/状态/Set/图集淘汰），收益微秒级，风险大于收益。2026-11-24 评估后暂缓。
- `DUI/UI/Text.cpp:105` — 省略号 clipped 字符串：Shape 命中判定要用 `p->text` 比对，缓存该串需改缓存契约，收益极小。2026-11-24 评估后不改。
- `DUI/UI/Animation.cpp:140` — TickAnimations 深拷贝 g_active：注释明确说明完成回调可启动/取消动画甚至销毁元素，快照是遍历正确性的必需措施，不能省。2026-11-24 评估后确认不改。
- `DUI/UI/Layout.cpp:291` — MeasureElement 的 Display 重复解析：省一次枚举解析，收益可忽略，不值得改。2026-11-24 评估后不改。
- `DUI/UI/Image.cpp:133` — 动图首帧双份像素：`TextureStore::Register` 需要独立像素副本（纹理与帧数据生命周期不同），是必要拷贝；每帧切帧的像素拷贝同理。2026-11-24 评估后确认不改。
- `DUI/DUI.vcxproj:31` — /GL 是 Release 既定 LTCG 策略，利于最终 exe 体积与优化；`DUI/DUI.props:11` 用 /O2 是速度优先的合理默认，改 /O1 全局降速换体积不划算。2026-11-24 评估后不改。
- `DUI/Core/Threading.h:40` — PostUi(life,fn)、`DUI/Core/Core.h:139` — Result<T>：均为公开 API，框架内零调用不等于无用，删除破坏兼容。2026-11-24 评估后不改。
- `DUI/Core/Threading.h:87` — ScheduleThen<T> 未标 inline：函数模板天然 ODR 合并，唯一的 void 全特化（:110）已标 inline；原判断为误报。2026-11-24 核验后不改。

### 低优先级 18 条（2026-11-24 逐条评估，全部非必要不改）

- `Graphics.cpp:1067` totalVerts：绘制命令在 Collect 阶段已按可见树收集，屏外元素不进 PaintCommand，原假设不成立。
- `Graphics.cpp:1324` FindGlyphSrv/HasGlyphPage/HasImage：每设备纹理数个位数到几十，O(小常数)；哈希索引需与增删同步，收益不可测。
- `TextEngine.cpp:813` SelectionRects buf(16)：out 存 Rect 而非 metrics，无法直接复用；高亮行数个位数，栈分配极廉价。
- `Canvas.cpp:33` 虚线 path：Canvas 是公共 API，path 返回给调用方跨函数使用，改 scratch 要改接口契约。
- `EventBus.cpp:34` 失效监听器：元素级 On<T> 走 Lifetime 失效、Dispatch 时清扫；全局 On 本就是进程级生命周期。
- `Threading.h:27` + `Element.h:523` Lifetime make_shared：侵入式改造涉及所有 Element 生命周期语义，高风险。
- `Image.cpp:81` images_ snapshot：注释明确说明遍历时会被 Remove，快照是正确性必需。
- `Layout.cpp:67` + `StyleResolve.cpp:298` 深度缓冲池：深度上限 kMaxDepth=200，最坏几百字节空 vector。
- `DUI.props:11` /Gw：COMDAT 折叠主要在链接 exe 时生效，静态库影响微小，且已开 /Gy + LTCG。
- `EventBus.h:40` + `Element.h:218` 模板膨胀：事件总线是本轮新引入公开 API，下沉 .cpp 要改模板签名，风险大于收益。
- `TextUtil.h:28` inline 函数：仅 2 个 TU 包含，链接器 /OPT:ICF 会合并。
- `Graphics.cpp:48` HLSL 常量：7KB 常驻 .rdata，外部加载引入文件依赖，不值。
- `Utf8.h:24` WideToUtf8、`Style.h:374` 滚动条/尺寸 setter、`Clipboard.h:41` WriteImage/任意格式接口、`Animation.h:168` IsRunning/IsFinished、`Window.h:94` DipToPx/PxToDip、`RichDoc.h:132` BlockTextLength/Report 辅助：均为公开 API，框架内零调用不等于无用，删除破坏兼容。

## 需确认后再动（公开 API，框架内零调用不等于无用）

- `DUI/Core/EventBus.h:32` — 自定义事件总线（On/Emit/OffCustomListener/CustomEvent、Element::On<T>）全仓零调用，但为本轮按需求新引入的公开 API，**不要删除**。
- `DUI/Core/Json.h:155` — json::Write/WriteOptions 与 Value::Set\*/Remove/Reserve/Path/GetDouble 及各工厂零调用，序列化半边目前无人用；若确认长期不用可裁，否则保留。
- `DUI/Core/Threading.h:132` + `:144` — Task::Then（含 void 特化）与 Task::Start() 零调用，Samples/Async 明确注释不用 Start()/Then()；属被 eager co_await 取代的旧入口，是否保留兼容需确认。
- `DUI/App/App.h:79` — App::SetTimer/KillTimer 公开定时器 API 在 Samples 中零调用（应用内主要用 NativeWindow 定时器），但 BUG 2 刚修过其派发逻辑，属活跃 API，保留。
- `DUI/App/App.h:68` — QuitOnLastWindowClosed() getter/setter 零调用，Samples/Tray 改用 WindowCount 轮询；作为公开 API 建议保留或明确废弃。
