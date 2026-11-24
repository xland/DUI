// ============================================================================
//  DUI/Core/EventBus.cpp — 全局自定义事件总线实现
// ============================================================================

#include "Core/EventBus.h"

#include "Core/Dispatcher.h"

#include <algorithm>
#include <vector>

namespace dui {

namespace {

struct Listener {
    uint64_t id = 0;
    String   type;
    std::function<void(const CustomEvent&)> fn;
    Lifetime life;
};

struct Registry {
    std::vector<Listener> listeners;
    uint64_t nextId = 1;
};

// 进程级注册表。Add/Off/drain 均在 UI 线程，无锁（与 Dispatcher 同约定）。
Registry& Reg() {
    static Registry r;
    return r;
}

// 清理失效监听（元素销毁）并派发快照。
void Dispatch(const CustomEvent& ev) {
    Registry& r = Reg();
    std::vector<std::function<void(const CustomEvent&)>> snapshot;
    snapshot.reserve(r.listeners.size());
    for (auto it = r.listeners.begin(); it != r.listeners.end();) {
        if (!it->life) {
            it = r.listeners.erase(it);
            continue;
        }
        if (it->type == ev.type) snapshot.push_back(it->fn);
        ++it;
    }
    // 快照调用：监听器内部可 Off/On，不影响本轮遍历。
    for (auto& fn : snapshot) fn(ev);
}

}  // namespace

uint64_t AddCustomListener(const String& type,
                           std::function<void(const CustomEvent&)> fn, Lifetime life) {
    if (!fn) return 0;
    Registry& r = Reg();
    const uint64_t id = r.nextId++;
    r.listeners.push_back(Listener{id, type, std::move(fn), std::move(life)});
    return id;
}

bool OffCustomListener(uint64_t id) noexcept {
    Registry& r = Reg();
    auto it = std::find_if(r.listeners.begin(), r.listeners.end(),
                           [id](const Listener& l) { return l.id == id; });
    if (it == r.listeners.end()) return false;
    r.listeners.erase(it);
    return true;
}

void Emit(const String& type, std::any detail) {
    Dispatcher* ui = Dispatcher::Ui();
    if (ui == nullptr) return;
    CustomEvent ev;
    ev.type = type;
    ev.detail = std::move(detail);
    ui->Post([ev = std::move(ev)] { Dispatch(ev); });
}

}  // namespace dui
