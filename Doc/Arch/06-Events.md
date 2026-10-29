# 输入与事件

> 状态：草案 v0.1 — 待讨论确认。
> 需求来源：开发者能**方便地为界面元素绑定事件处理程序**（点击、键盘、悬停……），并支持精确的底层事件控制。
> 相关：[01-Layering.md](./01-Layering.md)（Platform.Input → UI.Event）、[03-Style.md](./03-Style.md)（状态变体由事件维护）、[05-Animation.md](./05-Animation.md)（transform 需作用于命中测试）、ADR-018（纯 C++、链式 API 倾向）。

## 1. 需求

| 编号 | 需求 | 验收口径 |
| --- | --- | --- |
| R1 | 便捷绑定 | 一行代码绑定最常见事件：`btn->OnClick(fn)`；无需继承、无需实现接口 |
| R2 | 多种绑定形态 | 无捕获 lambda、成员函数 + 上下文、自由函数均可直接绑定 |
| R3 | 分层可深可浅 | 常用语义事件开箱即用；需要时也能拿到原始 `PointerMove` 等底层事件 |
| R4 | 路由可控 | 事件支持**捕获（隧道）与冒泡双阶段**，可标记"已处理"终止传播；父容器能拦截子元素事件 |
| R5 | 焦点与键盘导航 | 焦点切换（点击/Tab）、焦点态驱动样式（`State::Focused`） |
| R6 | 拖拽可用 | 按下后即使指针移出元素，后续事件仍送达该元素（指针捕获） |
| R7 | 生命周期安全 | 元素销毁自动摘除其处理器；分发过程中销毁元素不崩溃 |
| R8 | 热路径零分配 | 分发路径无堆分配；注册期（构建界面时）允许少量分配 |

### 1.1 约束（沿用既定决策）

- 单线程（ADR-012）：事件分发在 UI 线程同步完成，无队列、无锁。
- 无异常（ADR-007 **已定**）/ 无 RTTI（同条 ADR 草案，待拍板）：事件类型用整数 ID；处理器存储不用 `std::function` 的类型擦除开销（见 D1）。
- 输入归一化在 Platform 层完成（Win32 消息 → 归一化事件，[01-Layering.md](./01-Layering.md) §2），UI 层不接触 `HWND`/`WPARAM`。
- 动画 transform（[05-Animation.md](./05-Animation.md) §6）必须参与命中测试 —— 事件与绘制的坐标变换保持一致。

---

## 2. 总体思路

```
Win32 消息 ──Platform.Input──▶ 归一化事件 ──UI.Event──▶ 命中测试 → 路由 → 处理器
                                                                        │
                                                        修改状态 / 标脏 → 布局 → 绘制
```

- **事件是只读描述 + 一个可写标志**：分发时在栈上构造事件对象（无堆分配），处理器读取细节、设置 `handled`。
- **处理器存在元素上**：随元素创建/销毁，天然满足 R7 的一半（销毁即摘除）。
- **两层 API**：
  - 底层：`AddHandler(EventId, handler)` 覆盖全部事件；
  - 语义糖：控件暴露具名方法（`OnClick` / `OnTextChanged`…），内部转接底层事件 —— 便利性来自这层（R1/R3）。

### 2.1 与 CSS/WPF/浏览器的对照

| 概念 | CSS / DOM | WPF | DUI |
| --- | --- | --- | --- |
| 绑定 | `addEventListener` | `+=` 委托 | `OnClick(fn)` / `AddHandler(id, fn)` |
| 冒泡 | 是（`stopPropagation`） | 是（`Handled`） | 是（`e.handled = true`） |
| 捕获阶段 | `capture: true` | `Preview*` 隧道 | `Phase::Tunnel`（已定，D2） |
| 指针捕获 | `setPointerCapture` | `CaptureMouse` | `SetPointerCapture()` |

---

## 3. 事件清单

| 类别 | 事件 | 说明 |
| --- | --- | --- |
| 指针 | `PointerDown / Move / Up / Cancel` | 坐标为**元素本地坐标**；含按键掩码 |
| 指针 | `PointerEnter / Leave` | 直达事件，不走路由；由命中测试差分驱动（含 transform 变化） |
| 指针 | `Wheel` | 滚动增量，冒泡（容器滚动用） |
| 语义 | `Click / DoubleClick / RightClick` | 控件级：按下 + 抬起同元素且未拖出（阈值内） |
| 键盘 | `KeyDown / KeyUp / Char` | 路由到焦点元素后冒泡（加速键场景） |
| 焦点 | `GotFocus / LostFocus` | 直达事件 |
| 文本 | `TextInput / Composition*` | IME 组合串事件，与 Text 编辑联动（见 [11-Text.md](./11-Text.md) §5） |
| 生命周期 | `Loaded / Unloaded` | 元素进入/离开可见树（注册全局资源、启动动画的钩子） |

> 触摸/触控笔在 Platform 层归一化为指针事件 + 压力/倾斜扩展字段，UI 层不区分设备类型（与 [01-Layering.md](./01-Layering.md) 输入归一化一致）。

---

## 4. 绑定 API

### 4.1 三种形态（R2）

```cpp
// 1) 无捕获 lambda / 自由函数
btn->OnClick([](ClickEvent& e){ DoThing(); });

// 2) 成员函数 + 上下文（最常用）
page->AddChild<Button>(L"添加")->OnClick(this, &SettingsPage::OnAddClicked);

// 3) 底层全事件
item->AddHandler(Event::PointerDown, { this, &ListItem::OnPress });
```

### 4.2 处理器签名与存储

```cpp
template<typename E>                       // E = 具体事件类型（编译期，无 RTTI）
class Handler {                            // 16 B：fn 指针 + ctx 指针
    void (*fn)(void* ctx, E& e);
    void* ctx;
};
```

- 分发为**直接函数调用**（trampoline），无虚表、无堆分配（R8）。
- 需要捕获状态的 lambda：走成员函数 + 上下文形态；或使用 `SmallFn`（SBO 闭包，内联 16 B，溢出到 Arena）作为备选（D1）。
- 每元素 `SmallVec<HandlerEntry, 2>` 内联存储，超出溢出到 Arena —— 注册发生在构建期，非热路径。

### 4.3 事件对象

```cpp
struct EventBase {
    Element* source;      // 处理器宿主：冒泡路由中当前收到事件的元素（M3 起按此实现——
                          // 点击落在子元素上时，source 是绑定处理器并收到事件的祖先）
    bool     handled;     // 处理器可置 true 终止后续路由
    uint64_t timestamp;
};
struct PointerEvent : EventBase {
    Vec2   position;      // 元素本地坐标（已应用 transform 逆变换）
    uint32_t buttons, modifiers;
    float  wheelDelta;
};
```

栈上构造、按引用传递，全程零分配。

---

## 5. 路由

```
[捕获/隧道]  根 ──▶ … ──▶ 父 ──▶ 目标     预判/拦截（父容器先看）
[冒泡]       根 ◀── … ◀── 父 ◀── 目标     常规处理
             e.handled = true → 终止其后所有阶段
```

- **可路由事件都有两个阶段**：处理器声明所属阶段，默认冒泡（§5.1）。
- **终止规则**：任一阶段置 `handled` → 跳过其后全部阶段（含目标元素自己的后续处理器）。
- **目标元素**：先派发其捕获处理器，再派发冒泡处理器。
- **直达事件**（`Enter/Leave`、`GotFocus/LostFocus`、`Loaded/Unloaded`）不走路由、无阶段，只送目标元素。
- `Click` 等语义事件合成后同样走双阶段（从目标开始）。
- **指针捕获 ≠ 捕获阶段**：`SetPointerCapture()`（R6）指"抬起前 Move/Up 直达某元素"；"捕获阶段"指路由的隧道阶段。两者独立、常配合使用（拖拽 = 指针捕获 + 冒泡 Move）。
- **命中测试读变换**：`translation` / `scale` / `rotation` 均为非继承属性，命中测试在遍历外通过 `Style::Resolve(element, Prop)` 读取（[15-Properties.md](./15-Properties.md) §5.2），成本可控。

### 5.1 阶段声明 API

```cpp
// 冒泡（默认）
btn->AddHandler(Event::PointerDown, h);

// 捕获/隧道（父容器预判）
list->AddHandler(Event::PointerDown, h, Phase::Tunnel);
```

设计：**单一事件 ID 表 + `Phase` 参数**，而非 WPF 式成对 Preview ID —— 避免 ID 膨胀，同一事件可在两阶段各注册处理器。语义糖（`OnClick` 等）只覆盖冒泡；捕获场景走 `AddHandler`（低频、本就偏底层）。

- `Click` 等语义事件由事件系统合成后**从目标元素开始路由**（父列表可统一收点击）。
- 指针捕获（R6）：`element->SetPointerCapture()` 后，`Move/Up` 不再命中测试，直达捕获者直至抬起或释放 —— 拖拽、滑块、菜单外点关闭的基础。

---

## 6. 焦点与键盘导航

- 焦点唯一，挂在元素上；`Focus()` / `Blur()` / `ClearFocus()`。
- **Tab 链**：按树序（深度优先）+ 元素可设 `focusable` / `tabStop` 排除；`Shift+Tab` 反向。
- 焦点变化 → 更新元素 `State::Focused` → 样式状态变体自动生效（[03-Style.md](./03-Style.md) §6.2），无需开发者干预。
- 方向键导航（列表/网格内移动焦点）由具体控件实现，事件系统只提供路由与焦点服务。
- 加速键/快捷键：`KeyDown` 冒泡到根，由根上注册的加速键表匹配（P1）。

---

## 7. 生命周期与分发期修改（R7）

| 场景 | 语义 |
| --- | --- |
| 元素销毁 | 其处理器随元素一并析构，无需手动解绑 |
| 分发中修改元素树 / 标脏 | 允许；布局绘制在分发结束后统一进行 |
| 分发中**销毁**元素（或祖先） | 延迟删除：记入删除队列，本轮分发结束统一执行；删除后剩余事件不派发给它 |
| 分发中增删处理器 | 允许；新增处理器对本事件不生效（快照语义） |
| 处理器持有的上下文指针 | 裸指针，生命周期由开发者负责；跨元素订阅的托管句柄（weak token）列 P2（D5） |

---

## 8. 决策点

### D1 处理器存储：`Handler`（fn+ctx）为主（建议）vs `std::function` vs `SmallFn`

| 方案 | 体 | 性 | 复 | 说明 |
| --- | --- | --- | --- | --- |
| **fn+ctx（建议）** | +++ | +++ | ++ | 16 B、直调、零分配；捕获场景引导到"成员函数+上下文"，恰好也是界面代码的主流写法 |
| `std::function` | -- | + | + | 支持任意捕获，但体积大（MSVC ~64 B）、可能分配、有类型擦除开销 |
| `SmallFn`（SBO 16 B） | + | ++ | ○ | 兼顾捕获便利与零分配；实现约百行；可作为 P1 增强 |

**建议：MVP 用 fn+ctx**；`SmallFn` 列为 P1（API 不变，仅存储升级），`std::function` 不用。

### D2 路由阶段：捕获 + 冒泡双阶段（**已拍板**）

原草案为"MVP 仅冒泡、P1 加隧道"。**拍板：两阶段均进 MVP。**

理由：父容器拦截/预判（拖拽预判、工具提示、输入过滤、列表统一收事件）是常见需求，不该让开发者等二期；实现增量主要是路由循环多一个方向，成本小。

配套：处理器以 `Phase` 参数声明阶段（默认冒泡），事件 ID 保持单表不成对 —— 原 §11-4 悬案一并解决。

### D3 便捷 API 形态：链式返回 `Element&`（建议）

`btn->Text(L"OK").OnClick(fn).Padding(8)` —— 与 ADR-018 挂起的"链式 builder"待定项在此**收敛为：提供**。事件绑定是链式最有价值的场景；样式属性仍以 `Set` / `Style` 为主，避免与样式系统双轨。

### D4 分发期销毁：延迟删除队列（建议）
见 §7。替代方案（禁止分发期销毁、断言报错）会把复杂度转嫁给开发者，不取。

### D5 上下文生命周期托管（weak token）：P2
`page` 销毁后 lambda 里 `this` 悬挂是 C++ UI 的经典坑。MVP 靠"处理器在元素上、页面通常同时销毁"降低概率；若实践反馈频繁，再引入 `Lifetime` 令牌（绑定元素存活性，自动短路）。

---

## 9. 与其它模块的接口

| 模块 | 依赖方向 | 内容 |
| --- | --- | --- |
| `Platform.Input` | 提供 | 归一化事件、IME、光标请求 |
| `UI.Invalidate` | 消费 | 处理器改状态后标脏；指针 hover 引发 `State` 变化 → 样式失效 |
| `UI.Style` | 消费 | 输入类 `State` 位（`Hover` / `Pressed` / `Focused`）由事件维护；`Disabled` 由属性推导；`Selected` / `Invalid` / `Open` 等语义位由业务或控件写入（[03-Style.md](./03-Style.md) §6.2） |
| `UI.Animation` | 双向 | transform 参与命中测试；事件触发动画（`Animate()`） |
| `UI.Widgets` | 消费 | 控件定义语义事件（`OnClick`、`OnTextChanged`…）与焦点行为 |
| `Platform.Threading` | 消费 | 处理器中发起后台任务，完成后回投 UI 线程（见 [07-Threading.md](./07-Threading.md)） |
| `Text` | 无直接依赖 | 文本编辑事件经 UI 转交（11-Text 细化） |

---

## 10. 分期

| 期 | 内容 |
| --- | --- |
| **P0** | 指针事件 + 命中测试（含 transform）、**冒泡 + 捕获双阶段** + `handled`、`Click` 合成、焦点 + Tab、fn+ctx 处理器、语义糖首批（`OnClick/OnKeyDown/OnWheel`）、延迟删除 |
| **P1** | 指针捕获、`PointerEnter/Leave` 差分、加速键表、`SmallFn` 捕获闭包、语义事件全量 |
| **P2** | weak token 生命周期托管、触摸手势识别（长按/双指）、无障碍事件（与 ADR-017 联动） |

---

## 11. 待确认

1. D1–D5 是否按建议拍板（D3 同时结束 ADR-018 的链式 API 悬案）。
2. 语义事件首批清单（§3 表）是否增减。
3. `Click` 的合成规则：拖出阈值（建议 4 dip）内抬起算点击；双击间隔跟随系统 `GetDoubleClickTime()`。
4. ~~已解决~~：不按阶段分表，单事件 ID 表 + `Phase` 参数（见 D2）。
5. 触摸手势（长按/滑动）是否提前到 P1。
6. ~~已定~~：程序化设状态位用 `element->SetState(State::Selected, v)`；写入契约（相同值幂等、标脏帧末合并）与状态全集见 [03-Style.md](./03-Style.md) §6.2。
7. `Element::Lifetime()` 返回类型命名（示例中暂用 `auto`），与 [07-Threading.md](./07-Threading.md) §4.3 的 `ui.Post(life, fn)` 对齐。
