# 线程与异步

> 状态：草案 v0.1 — 待讨论确认。
> 需求来源：开发者能在自己的代码（如事件处理函数）中**方便地切到子线程处理业务，并方便地回到 UI 线程**更新界面。
> 相关：ADR-007（异常关闭）、ADR-012（单线程 + 字体/图片加载可后台）、[06-Events.md](./06-Events.md)（处理器中发起异步）、[01-Layering.md](./01-Layering.md)（消息泵）。

## 1. 需求

| 编号 | 需求 | 验收口径 |
| --- | --- | --- |
| R1 | 一行切后台 | 事件处理器里一行代码把耗时工作投到后台线程 |
| R2 | 一行回 UI | 后台完成后一行代码回 UI 线程更新界面（标脏 → 正常渲染管线） |
| R3 | 两种风格 | 回调风格开箱即用；C++20 协程风格（`co_await` 切换线程）可选 |
| R4 | 可取消 | 离开页面/元素销毁后，未完成任务可取消，完成回调可安全作废 |
| R5 | 不破坏单线程 UI | 元素树/绘制仅 UI 线程可碰；后台线程拿到的是纯数据 |
| R6 | 退出干净 | 应用退出时排空/终止后台任务，不悬挂、不崩溃 |

### 1.1 约束（沿用既定决策）

- **异常关闭**（ADR-007 **已定**）——它是评估 WinRT 引入方式的关键前提（见 §2）。
- 无第三方库（00-Overview §1）：WinRT 属系统组件，不违反"无依赖"，但引入方式有代价。
- 体积小：异步基础设施目标控制在数百行量级。
- UI 单线程（ADR-012）：本设计**不改**该原则，只提供"出去干活、回来更新"的通道。

---

## 2. 关键取舍：是否引入 WinRT 的多线程机制（D1）

用户提议参考 WinRT（`DispatcherQueue` + `IAsyncAction` + `resume_background`/`resume_foreground`）。分三种引入方式评估：

| 维度 | C++/WinRT（官方投影） | WinRT 裸 COM（WRL/手写） | **自研精简 Dispatcher（建议）** |
| --- | --- | --- | --- |
| 异常 | **不可用**：投影全面以异常传递错误（`hresult_error`），与 ADR-007 硬冲突 | 可用（HRESULT） | 可用 |
| 依赖 | RoInitialize / combase / 单元（apartment）模型 | 同左 | `kernel32`（线程）+ `user32`（唤醒），本就允许 |
| 体积 | 投影头文件编译负担大，生成代码可观 | 中 | ~300–500 行 |
| 协程 | `IAsyncAction` / `resume_*` 现成 | 需自行包装 | 自写 awaiter（~百行，语义对齐） |
| 与消息泵集成 | DispatcherQueueController 自带集成 | 同左 | `PostMessage` 唤醒，天然集成 |
| 心智模型 | 熟悉 WinRT 的开发者直接上手 | 同左 | API 独立设计，以直观为先 |

**已定：不引入 WinRT 实现，自研精简 Dispatcher；API 按易用性自定，不刻意对齐 WinRT 命名。**

- 决定性理由是异常（ADR-007 已定）：C++/WinRT 无法在 `/EHs-c-` 下使用；裸 COM 绕开异常但要背上 RoInitialize/apartment 状态与激活开销，换来的功能（Timer、优先级）自研并不难。
- 命名原则：怎么直观怎么来 —— 任务投递为成员调用 `app.Dispatcher().Post(fn)`；协程切换点为命名空间内的**函数** `co_await dui::Background()` / `co_await dui::Foreground()`（用函数而非标签类型：调用点读起来是"动作"，可加参数如 `Background(pool)`，且不必让用户书写 awaiter 类型名）；一律 `dui::` 限定，避免与业务代码里的同名概念冲突。
- 若未来某能力（如 `DispatcherQueueTimer` 的合成器对齐）确有价值，可单独以裸 COM 接入，不阻塞本设计。

---

## 3. 模型

```
UI 线程                                后台
─────────────                         ─────────────
消息泵 ──▶ [调度] 排空 UI 队列          固定线程池（N 条）
   │                                    ▲ 取任务
   ▼                                    │
输入 → 事件分发 → 动画 → 布局 → 绘制      执行业务（纯数据，不碰 UI）
   ▲                                    │
   └────── Post(任务) ◀── 完成回投 ──────┘
```

| 概念 | 定义 |
| --- | --- |
| `Dispatcher` | 每线程一个任务队列 + 泵。UI 线程的 Dispatcher 由 `Application` 持有，在消息循环中排空 |
| `Background` | 进程级固定线程池（并发度 = min(4, 硬件并发)，D2），任务 FIFO |
| 任务 | 移动-only 可调用体（fn+ctx 或 SBO 闭包），队列节点从池分配，有界 |
| `CancellationToken` | 取消令牌：排队中直接丢弃；运行中由任务自行轮询（框架不杀线程） |

### 3.1 线程规则（R5，唯一铁律）

- **后台线程只碰数据**：不触碰元素树、样式、绘制、字体排版结果。
- **引用计数只归 UI 线程**：可引用对象（纹理、字体、样式、资源、元素）仅由 UI 线程持有与增减引用，引用计数**非原子**（与 ADR-007 自洽，ADR-025）。工作线程只处理纯数据（字节、解码后的位图内存），跨线程以 **move 传递所有权**。
- 需要更新界面 → 必然经过 `ui.Post(...)` 回投，回投任务里标脏后走正常管线。
- Debug 下提供 `AssertUiThread()` 与对象构造期线程校验；后台线程构造可引用对象属违例。
- 违规不做运行时防护（检不出来也不便宜），文档与示例明确。

---

## 4. API 形态

### 4.1 回调风格（P0）

```cpp
Dispatcher& ui = Application::Current().Dispatcher();

// 事件处理器里发起（[06-Events.md] 场景）
btn->OnClick([this, path]{
    ui.Background([path]{
        Image img = DecodeImage(path);          // 子线程：纯数据
        ui.Post([this, img]{                    // 回 UI 线程
            view->SetImage(img);                // 碰 UI，安全
        });
    });
});

// 取消
auto op = ui.Background(fn);
op.Cancel();                                    // 排队中直接丢弃；运行中交令牌自查
```

### 4.2 协程风格（**体验主路径**，P0 最小集，R3）

> 协程风格是**开发者体验的主路径**：与回调风格平级，文档、示例、API 命名都以协程为主；回调风格作为等价备选并存。

```cpp
Task<Image> LoadImageAsync(String path) {
    co_await dui::Background();                 // 切到线程池
    Image  img = DecodeImage(path);
    co_await dui::Foreground();                 // 切回 UI 线程
    co_return img;
}

// 事件处理器里
btn->OnClick([this]{ LoadImageAsync(path).Then([this](Result<Image> r){
    if (r) view->SetImage(r.Value());
}).Start(); });
```

```cpp
// 切换点声明形态：命名空间内函数，返回 awaiter
namespace dui {
    BackgroundAwaiter Background() noexcept;   // 切到线程池
    ForegroundAwaiter Foreground() noexcept;   // 切回 UI 线程
}
```

- 自写 `Task<T>` promise 与上述 awaiter：挂起时把 continuation 投递到目标 Dispatcher；**awaiter 类型是实现细节**，用户只写 `dui::Background()`，不需要书写类型名。
- 不用"标签对象"写法（`co_await Background{}`）：函数调用读起来是"动作"，可扩展参数（如 `Background(pool)`），也避免了无括号时像漏写调用的观感。
- 与 `ui.Background(fn)` 区分：**带参数的那个是投递任务**（成员函数），**不带参数的是协程切换点**（命名空间函数）。
- **协程体内不用异常**（与 ADR-007 一致）：错误经 `Result<T>` 传递（对齐 ADR-007 错误传递方向）；未捕获异常路径统一 `terminate`。
- MSVC 在 `/EHs-c-` 下协程本身可用（无 try/catch 即可），实现时验证。

### 4.3 安全回投（P1，R4）

```cpp
ui.Post(view->Lifetime(), [view]{ view->SetImage(img); });   // view 已销毁 → 任务丢弃
```

`Lifetime()` 返回元素存活性令牌（对应 [06-Events.md](./06-Events.md) D5 的 weak token），事件处理器发起的回投建议一律用它，消除"回调时界面已销毁"这一最常见悬挂。

---

## 5. 与帧循环的集成

- UI 队列的唤醒用 `PostMessage`（隐藏窗口，D4）：后台线程完成即唤醒，消息泵天然合并多次唤醒。
- 排空时机：每轮消息循环开头、输入处理之前（记为帧生命周期的 **[调度]** 步）—— 后台产生的界面更新在本帧即可见。
- 队列空且无脏区、无动画 → 回到空闲，CPU 归零（不破坏"没有失效就没有工作"）。
- `PostDelayed`（延迟任务，P1）：由 UI Dispatcher 的定时器驱动，不与动画 ticker 混用。

### 5.1 与 [01-Layering.md](./01-Layering.md) 帧生命周期的衔接

```
[调度]  排空 UI Dispatcher（后台完成回投的任务）
   ↓
[输入] → [分发] → [动画] → [失效] → [布局] → [收集] → [光栅] → [呈现]
```

---

## 6. 决策点

### D1 WinRT 引入方式（建议：自研，语义对齐）
见 §2 对比表。核心论据：C++/WinRT 与"异常关闭"硬冲突；裸 COM 收益不抵复杂度。

### D2 线程池形态（建议：自研固定池）

| 方案 | 说明 |
| --- | --- |
| **自研固定池（建议）** | N = min(4, 硬件并发)；FIFO；实现小、行为可预期 |
| WinRT ThreadPool（裸 COM） | 功能全但引入 apartment/激活依赖，同 D1 否决 |
| 每任务裸线程 | 任务抖动大、创建开销高，仅长任务场景可用（列 P2） |

> 长任务（视频转码类）不占池：提供 `RunDedicated(fn)` 专线程接口（P2），避免饿死短任务。

### D3 错误传递（建议：`Result<T>`，无异常）
对齐 ADR-007：内部 API 以 `Result<T,E>`/`bool` 传递错误；`Task<T>` 协程同样返回 `Result`，不用 `try/catch`。

### D4 UI 唤醒方式（建议：PostMessage）
备选 `SetEvent` + 可警觉等待：需改造消息泵且失去消息合并；PostMessage 与现有泵零摩擦。

### D5 任务的内存来源
队列节点走固定池（每 Dispatcher 有界，如 1024），满则丢弃最旧并告警 —— 与动画池策略一致（[05-Animation.md](./05-Animation.md) §11）；拒绝无限队列。

---

## 7. 分期

| 期 | 内容 |
| --- | --- |
| **P0** | `Dispatcher`（UI 排空 + PostMessage 唤醒）、固定线程池、`Background`/`Post` 回调风格、**协程最小集**（`Task<T>` + `Background`/`Foreground` awaiter，体验主路径）、取消令牌、退出排空 |
| **P1** | 协程增强（`WhenAll`/`WhenAny` 等组合子）、`Lifetime` 安全回投、`PostDelayed` |
| **P2** | 专线程 `RunDedicated`、优先级队列、任务亲和性（同 key 串行） |

---

## 8. 待确认

1. ~~D1~~ **已拍板**：不引入 WinRT 实现、自研精简 Dispatcher、API 按易用性自定。D2–D5 仍待确认。
2. 线程池并发度（建议 min(4, 硬件并发)）是否可配。
3. 排队上限与淘汰策略（建议 1024、丢最旧 + 告警）。
4. `Task<T>` 协程是否需要 `WhenAll` / `WhenAny` 组合子（P1 或 P2）。
5. 是否需要 `ui.IsUiThread()` 断言宏，帮助开发者尽早发现后台碰 UI 的错误（Debug 断言，Release 零开销，建议加）。
