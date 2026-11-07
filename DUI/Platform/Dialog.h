// ============================================================================
//  DUI/Platform/Dialog.h — 系统对话框的平台侧接口（内部头，不进 DUI.h）
//
//  Platform 不反向依赖 App/UI（01-Layering §1）：这里只放 POD 参数与本层枚举，
//  公共选项 / 公共枚举由 App/Dialog.cpp 转换后传入。
// ============================================================================

#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/Core.h"

namespace dui::platform {

struct NativeFileFilter {
    const wchar_t* name;
    const wchar_t* pattern;
};

enum class MsgIcon : uint8_t { None, Information, Warning, Error, Question };
enum class MsgButtons : uint8_t { Ok, OkCancel, YesNo, YesNoCancel, RetryCancel,
                                  AbortRetryIgnore, CancelRetryContinue };
enum class NativeMessageResult : uint8_t { Failed, Ok, Cancel, Yes, No, Retry, Abort,
                                           Ignore, TryAgain, Continue };

// true = 成功（out 有效）；false = 取消或失败，由 cancelled / error 区分。
// owner 为 HWND（以 void* 传递，公共 API 不含 windows.h）。
bool PickFiles(void* owner, const wchar_t* title, const wchar_t* initialPath,
               const NativeFileFilter* filters, size_t filterCount, size_t filterIndex,
               bool multiSelect, std::vector<String>& out, bool& cancelled, String& error);
bool SaveFile(void* owner, const wchar_t* title, const wchar_t* initialPath,
              const NativeFileFilter* filters, size_t filterCount, size_t filterIndex,
              const wchar_t* defaultExtension,
              String& out, bool& cancelled, String& error);
bool PickFolder(void* owner, const wchar_t* title, const wchar_t* initialPath,
                String& out, bool& cancelled, String& error);

NativeMessageResult ShowMessageBox(void* owner, const wchar_t* text, const wchar_t* title,
                                   MsgIcon icon, MsgButtons buttons, int defaultButton);

}  // namespace dui::platform

// 与 App/Dialog.h 同理：winuser.h 的 #define MessageBox MessageBoxW 会让
// Window::MessageBox 在库内 TU 里被替换成另一个符号，故包含本头后统一取消。
#ifdef MessageBox
#undef MessageBox
#endif
