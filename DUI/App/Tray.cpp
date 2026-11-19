// ============================================================================
//  DUI/App/Tray.cpp — Shell_NotifyIconW 系统托盘实现
// ============================================================================

#include "App/Tray.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include <shellapi.h>   // NOTIFYICONDATAW / Shell_NotifyIconW / NIM_* / NIF_*

// Shell_NotifyIconW 来自 shell32（Dialog.cpp 亦携带，重复无害，保证本 TU 自包含）
#pragma comment(lib, "shell32.lib")

namespace dui {

namespace {

constexpr wchar_t kWndClass[] = L"DUI_Tray_v1";
constexpr UINT kCallback = WM_APP + 1;
constexpr UINT kIconId = 1;
bool g_classRegistered = false;

// 从 .ico 文件字节里挑出最适合托盘的小图标帧，拷到 DWORD 对齐缓冲
// （CreateIconFromResourceEx 要求 presbits DWORD 对齐）。格式非法返回 false。
bool PickIcoFrame(const void* data, size_t size, std::vector<uint32_t>& alignedBits,
                  DWORD& bytesInRes) {
    const auto* p = static_cast<const uint8_t*>(data);
    if (p == nullptr || size < 6) return false;
    const auto u16 = [p](size_t off) -> uint16_t {
        return static_cast<uint16_t>(p[off] | (p[off + 1] << 8));
    };
    const auto u32 = [p](size_t off) -> uint32_t {
        return static_cast<uint32_t>(p[off]) |
               (static_cast<uint32_t>(p[off + 1]) << 8) |
               (static_cast<uint32_t>(p[off + 2]) << 16) |
               (static_cast<uint32_t>(p[off + 3]) << 24);
    };
    if (u16(0) != 0 || u16(2) != 1) return false;   // reserved=0, type=icon
    const uint16_t count = u16(4);
    if (count == 0 || size < 6u + static_cast<size_t>(count) * 16u) return false;

    const int want = ::GetSystemMetrics(SM_CXSMICON);
    int best = -1, bestDelta = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const size_t e = 6u + static_cast<size_t>(i) * 16u;
        const int w = p[e] == 0 ? 256 : p[e];       // 0 表示 256
        const int delta = w > want ? w - want : want - w;
        if (best < 0 || delta < bestDelta) { best = i; bestDelta = delta; }
    }
    const size_t e = 6u + static_cast<size_t>(best) * 16u;
    const DWORD bytes = u32(e + 8);                 // dwBytesInRes
    const DWORD offset = u32(e + 12);               // dwImageOffset
    if (bytes == 0 || static_cast<uint64_t>(offset) + bytes > size) return false;

    alignedBits.assign((static_cast<size_t>(bytes) + 3) / 4, 0);
    std::memcpy(alignedBits.data(), p + offset, bytes);
    bytesInRes = bytes;
    return true;
}

}  // namespace

Tray::Tray() = default;

Tray::~Tray() {
    Hide();
    if (ownedIcon_ != nullptr) {
        DestroyIcon(ownedIcon_);
        ownedIcon_ = nullptr;
    }
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Tray::EnsureWindow() {
    if (hwnd_ != nullptr) return true;
    if (!g_classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Tray::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWndClass;
        wc.style = CS_DBLCLKS;   // 收到双击回调（WM_LBUTTONDBLCLK）
        if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            lastError_ = L"RegisterClassExW failed (code " + std::to_wstring(GetLastError()) + L")";
            return false;
        }
        g_classRegistered = true;
    }
    // HWND_MESSAGE：不显示、不进任务栏、不影响窗口计数；仅接收托盘回调消息。
    hwnd_ = CreateWindowExW(0, kWndClass, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), this);
    if (hwnd_ == nullptr) {
        lastError_ = L"CreateWindowExW failed (code " + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    id_ = kIconId;
    return true;
}

bool Tray::SetIconFromMemory(const void* data, size_t size) {
    std::vector<uint32_t> alignedBits;
    DWORD bytesInRes = 0;
    if (!PickIcoFrame(data, size, alignedBits, bytesInRes)) {
        lastError_ = L"invalid .ico data";
        return false;
    }
    // dwVer=0x00030000：支持现代 ICO（含 PNG 压缩帧、32bpp alpha）。
    HICON icon = ::CreateIconFromResourceEx(
        reinterpret_cast<PBYTE>(alignedBits.data()), bytesInRes, TRUE,
        0x00030000, 0, 0, LR_DEFAULTCOLOR);
    if (icon == nullptr) {
        lastError_ = L"CreateIconFromResourceEx failed (code " +
                     std::to_wstring(::GetLastError()) + L")";
        return false;
    }
    if (ownedIcon_ != nullptr) DestroyIcon(ownedIcon_);
    ownedIcon_ = icon;
    if (visible_) {   // 已显示则立即换图（只更新图标，不动提示）
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = id_;
        nid.uFlags = NIF_ICON;
        nid.hIcon = ownedIcon_;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    return true;
}

bool Tray::SetIconFromFile(const wchar_t* filePath) {
    if (filePath == nullptr || *filePath == L'\0') {
        lastError_ = L"empty icon file path";
        return false;
    }
    // 用 Win32 读文件，避开 std::ifstream 在 /EHsc 关闭下的 C4530 警告。
    HANDLE file = ::CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        lastError_ = L"cannot open icon file (code " +
                     std::to_wstring(::GetLastError()) + L")";
        return false;
    }
    LARGE_INTEGER fileSize{};
    const bool gotSize = ::GetFileSizeEx(file, &fileSize) != FALSE && fileSize.QuadPart > 0;
    if (!gotSize || static_cast<uint64_t>(fileSize.QuadPart) > 0xFFFFFFFFull) {
        ::CloseHandle(file);
        lastError_ = L"invalid icon file size";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize.QuadPart));
    DWORD read = 0;
    const bool ok = ::ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                               &read, nullptr) != FALSE && read == bytes.size();
    ::CloseHandle(file);
    if (!ok) {
        lastError_ = L"cannot read icon file";
        return false;
    }
    return SetIconFromMemory(bytes.data(), bytes.size());
}

bool Tray::Show(const wchar_t* tip) {
    return Show(ownedIcon_, tip);
}

bool Tray::Show(HICON icon, const wchar_t* tip) {
    if (!EnsureWindow()) return false;
    if (icon == nullptr) icon = LoadIconW(nullptr, IDI_APPLICATION);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = id_;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kCallback;
    nid.hIcon = icon;

    const bool existed = visible_;
    if (!existed) {
        if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
            lastError_ = L"Shell_NotifyIconW(NIM_ADD) failed";
            return false;
        }
        visible_ = true;
    }
    if (tip != nullptr) {
        wcsncpy_s(nid.szTip, _countof(nid.szTip), tip, _TRUNCATE);
        if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
            lastError_ = L"Shell_NotifyIconW(NIM_MODIFY) failed";
            return false;
        }
    }
    return true;
}

void Tray::Hide() noexcept {
    if (!visible_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = id_;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    visible_ = false;
}

bool Tray::SetTip(const wchar_t* tip) {
    if (!visible_) return false;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = id_;
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, _countof(nid.szTip), tip != nullptr ? tip : L"", _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        lastError_ = L"Shell_NotifyIconW(NIM_MODIFY) failed";
        return false;
    }
    return true;
}

LRESULT CALLBACK Tray::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    auto* self = reinterpret_cast<Tray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr && msg == kCallback && wp == self->id_) {
        switch (static_cast<UINT>(lp)) {
        case WM_LBUTTONDOWN:
            ::GetCursorPos(&self->lastClickPos_);
            break;
        case WM_LBUTTONUP:
            if (self->onClick_) self->onClick_();
            break;
        case WM_LBUTTONDBLCLK:
            if (self->onDoubleClick_) self->onDoubleClick_();
            else if (self->onClick_) self->onClick_();   // 未绑双击 → 退化为单击
            break;
        case WM_RBUTTONUP:
            if (self->onContextMenu_) {
                POINT pt{};
                ::GetCursorPos(&pt);
                // 让菜单获得前台，否则点击别处菜单可能不消失
                ::SetForegroundWindow(hwnd);
                self->onContextMenu_(Vec2{static_cast<float>(pt.x), static_cast<float>(pt.y)});
                PostMessageW(hwnd, WM_NULL, 0, 0);
            }
            break;
        default:
            break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace dui
