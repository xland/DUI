// ============================================================================
//  DUI/Platform/Dialog.cpp — 系统对话框的 Win32 / COM 实现（17-Dialogs）
//
//  · 文件 / 保存 / 文件夹：Common Item Dialog（IFileDialog，Vista+，系统 PMv2
//    自动缩放）；保存走 IFileSaveDialog，文件夹走 FOS_PICKFOLDERS。
//  · 消息框：MessageBoxW。
//  · COM 沿用 WIC 的进程级 CoInitializeEx(COINIT_MULTITHREADED)，不 CoUninitialize。
//  · 无异常：所有失败填 String& error；COM 接口用 ComReleaser 兜底 Release。
// ============================================================================

#include "Platform/Dialog.h"

#include <objbase.h>
#include <shobjidl_core.h>

#include <utility>
#include <vector>

// 系统库用 #pragma comment(lib) 随本编译单元携带，不改根目录 DUI.props
// （与 Graphics/ImageCodec.cpp 的 windowscodecs.lib 同一做法）。
// CLSID 用 __uuidof(FileOpenDialog/FileSaveDialog)，无需 uuid.lib。
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace dui::platform {

namespace {

struct ComReleaser {
    explicit ComReleaser(IUnknown* obj = nullptr) noexcept : ptr(obj) {}
    ~ComReleaser() { if (ptr != nullptr) ptr->Release(); }
    ComReleaser(const ComReleaser&) = delete;
    ComReleaser& operator=(const ComReleaser&) = delete;
    IUnknown* ptr;
};

void AppendHex8(String& s, HRESULT hr) {
    static const wchar_t kDigits[] = L"0123456789ABCDEF";
    unsigned v = static_cast<unsigned>(hr);
    wchar_t buf[8];
    for (int i = 7; i >= 0; --i) {
        buf[i] = kDigits[v & 0xFu];
        v >>= 4;
    }
    s.append(buf, 8);
}

void SetError(String& error, const wchar_t* what, HRESULT hr) {
    error = what;
    error += L" (HRESULT 0x";
    AppendHex8(error, hr);
    error += L")";
}

bool GetPath(IShellItem* item, String& out) {
    wchar_t* psz = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) || psz == nullptr) return false;
    out.assign(psz);
    CoTaskMemFree(psz);
    return !out.empty();
}

// 初始路径：目录 → SetFolder；含文件名 → 拆目录 + SetFileName（另存为的
// 建议文件名通常尚不存在，不能要求路径可解析）。失败一律忽略，退回系统默认。
void ApplyInitialPath(IFileDialog* dlg, const wchar_t* path) {
    if (path == nullptr || *path == L'\0') return;

    String full(path);
    bool isDir = false;
    const DWORD attr = GetFileAttributesW(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) isDir = true;
    if (!full.empty() && (full.back() == L'\\' || full.back() == L'/')) isDir = true;

    if (isDir) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&item))) &&
            item != nullptr) {
            ComReleaser r(item);
            dlg->SetFolder(item);
        }
        return;
    }

    const size_t pos = full.find_last_of(L"\\/");
    if (pos == String::npos) {
        dlg->SetFileName(full.c_str());
        return;
    }
    String dir = full.substr(0, pos);
    // 盘符根目录（如 C 盘根）需保留结尾分隔符，否则 "C:" 无法解析成 ShellItem
    if (pos == 2 && full.size() >= 3 && full[1] == L':') dir = full.substr(0, 3);
    const String name = full.substr(pos + 1);
    if (!dir.empty()) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
            item != nullptr) {
            ComReleaser r(item);
            dlg->SetFolder(item);
        }
    }
    if (!name.empty()) dlg->SetFileName(name.c_str());
}

enum class Mode : uint8_t { Open, Save, Folder };

struct FileDialogParams {
    void* owner;
    const wchar_t* title;
    const wchar_t* initialPath;
    const NativeFileFilter* filters;
    size_t filterCount;
    size_t filterIndex;
    const wchar_t* defaultExtension;
    bool multiSelect;
};

bool RunFileDialog(Mode mode, const FileDialogParams& p,
                   std::vector<String>& out, bool& cancelled, String& error) {
    out.clear();
    cancelled = false;
    error.clear();
    if (p.owner == nullptr) {
        error = L"窗口尚未创建（需在 Application::Run 之后调用）";
        return false;
    }

    // UI 线程由 Application::Run 用 OleInitialize 建成 STA；此处再请求 STA 只返回
    // S_FALSE。若线程已被别人初始化成 MTA（RPC_E_CHANGED_MODE），Common Item Dialog
    // 的 Show() 会在创建窗口前死锁 —— 明确报错而不是把界面挂死。
    static const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (comInit == RPC_E_CHANGED_MODE) {
        error = L"文件对话框需要 STA 单元（UI 线程 COM 已被初始化为 MTA）";
        return false;
    }

    APTTYPE apt = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qual = APTTYPEQUALIFIER_NONE;
    // OleInitialize 建立的是 main STA（APTTYPE_MAINSTA），普通 CoInitializeEx(STA)
    // 是 APTTYPE_STA —— 两者都能跑 Common Item Dialog，都要放行。
    if (FAILED(CoGetApartmentType(&apt, &qual)) ||
        (apt != APTTYPE_STA && apt != APTTYPE_MAINSTA)) {
        error = L"文件对话框需要 STA 单元（UI 线程 COM 单元异常）";
        return false;
    }

    const GUID clsid = (mode == Mode::Save) ? __uuidof(FileSaveDialog)
                                           : __uuidof(FileOpenDialog);
    IFileDialog* raw = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw));
    if (FAILED(hr) || raw == nullptr) {
        SetError(error, L"创建文件对话框失败", hr);
        return false;
    }
    ComReleaser dlgHolder(raw);
    IFileDialog* dlg = raw;

    FILEOPENDIALOGOPTIONS opts = 0;
    if (FAILED(dlg->GetOptions(&opts))) opts = 0;
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    switch (mode) {
    case Mode::Open:
        opts |= FOS_FILEMUSTEXIST;
        if (p.multiSelect) opts |= FOS_ALLOWMULTISELECT;
        break;
    case Mode::Save:
        opts |= FOS_OVERWRITEPROMPT;
        break;
    case Mode::Folder:
        opts |= FOS_PICKFOLDERS;
        break;
    default:
        break;
    }
    if (FAILED(dlg->SetOptions(opts))) {
        SetError(error, L"设置对话框选项失败", E_FAIL);
        return false;
    }
    if (p.title != nullptr && *p.title != L'\0') dlg->SetTitle(p.title);

    // COMDLG_FILTERSPEC 数组必须活到 Show() 返回（局部 vector，不提前销毁）。
    std::vector<COMDLG_FILTERSPEC> specs;
    if (mode != Mode::Folder && p.filters != nullptr && p.filterCount > 0) {
        specs.reserve(p.filterCount);
        for (size_t i = 0; i < p.filterCount; ++i) {
            COMDLG_FILTERSPEC spec{p.filters[i].name, p.filters[i].pattern};
            specs.push_back(spec);
        }
        dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        const size_t idx = (p.filterIndex < p.filterCount) ? p.filterIndex : 0;
        dlg->SetFileTypeIndex(static_cast<UINT>(idx + 1));
    }

    if (mode == Mode::Save && p.defaultExtension != nullptr && *p.defaultExtension != L'\0') {
        IFileSaveDialog* save = nullptr;
        if (SUCCEEDED(dlg->QueryInterface(IID_PPV_ARGS(&save))) && save != nullptr) {
            ComReleaser s(save);
            save->SetDefaultExtension(p.defaultExtension);
        }
    }

    ApplyInitialPath(dlg, p.initialPath);

    hr = dlg->Show(static_cast<HWND>(p.owner));
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        cancelled = true;
        return false;
    }
    if (FAILED(hr)) {
        SetError(error, L"显示文件对话框失败", hr);
        return false;
    }

    if (mode == Mode::Open && p.multiSelect) {
        IFileOpenDialog* open = nullptr;
        if (SUCCEEDED(dlg->QueryInterface(IID_PPV_ARGS(&open))) && open != nullptr) {
            ComReleaser o(open);
            IShellItemArray* arr = nullptr;
            if (SUCCEEDED(open->GetResults(&arr)) && arr != nullptr) {
                ComReleaser a(arr);
                DWORD count = 0;
                arr->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(arr->GetItemAt(i, &item)) && item != nullptr) {
                        ComReleaser it(item);
                        String path;
                        if (GetPath(item, path)) out.push_back(std::move(path));
                    }
                }
            }
        }
    } else {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item != nullptr) {
            ComReleaser it(item);
            String path;
            if (GetPath(item, path)) out.push_back(std::move(path));
        }
    }

    // 虚拟 Shell 项没有文件系统路径：跳过；一个都没有时按失败报。
    if (out.empty()) {
        error = L"未取得文件路径";
        return false;
    }
    return true;
}

UINT IconFlags(MsgIcon icon) {
    switch (icon) {
    case MsgIcon::Information: return MB_ICONINFORMATION;
    case MsgIcon::Warning:     return MB_ICONWARNING;
    case MsgIcon::Error:       return MB_ICONERROR;
    case MsgIcon::Question:    return MB_ICONQUESTION;
    case MsgIcon::None:        return 0;
    default:                   return 0;
    }
}

UINT ButtonFlags(MsgButtons buttons) {
    switch (buttons) {
    case MsgButtons::Ok:                  return MB_OK;
    case MsgButtons::OkCancel:            return MB_OKCANCEL;
    case MsgButtons::YesNo:               return MB_YESNO;
    case MsgButtons::YesNoCancel:         return MB_YESNOCANCEL;
    case MsgButtons::RetryCancel:         return MB_RETRYCANCEL;
    case MsgButtons::AbortRetryIgnore:    return MB_ABORTRETRYIGNORE;
    case MsgButtons::CancelRetryContinue: return MB_CANCELTRYCONTINUE;
    default:                              return MB_OK;
    }
}

UINT DefaultButtonFlags(int n) {
    switch (n) {
    case 2:      return MB_DEFBUTTON2;
    case 3:      return MB_DEFBUTTON3;
    case 4:      return MB_DEFBUTTON4;
    default:     return MB_DEFBUTTON1;
    }
}

}  // namespace

bool PickFiles(void* owner, const wchar_t* title, const wchar_t* initialPath,
               const NativeFileFilter* filters, size_t filterCount, size_t filterIndex,
               bool multiSelect, std::vector<String>& out, bool& cancelled, String& error) {
    FileDialogParams p{owner, title, initialPath, filters, filterCount, filterIndex,
                       nullptr, multiSelect};
    return RunFileDialog(Mode::Open, p, out, cancelled, error);
}

bool SaveFile(void* owner, const wchar_t* title, const wchar_t* initialPath,
              const NativeFileFilter* filters, size_t filterCount, size_t filterIndex,
              const wchar_t* defaultExtension,
              String& out, bool& cancelled, String& error) {
    FileDialogParams p{owner, title, initialPath, filters, filterCount, filterIndex,
                       defaultExtension, false};
    std::vector<String> paths;
    if (!RunFileDialog(Mode::Save, p, paths, cancelled, error)) return false;
    out = std::move(paths.front());
    return true;
}

bool PickFolder(void* owner, const wchar_t* title, const wchar_t* initialPath,
                String& out, bool& cancelled, String& error) {
    FileDialogParams p{owner, title, initialPath, nullptr, 0, 0, nullptr, false};
    std::vector<String> paths;
    if (!RunFileDialog(Mode::Folder, p, paths, cancelled, error)) return false;
    out = std::move(paths.front());
    return true;
}

NativeMessageResult ShowMessageBox(void* owner, const wchar_t* text, const wchar_t* title,
                                   MsgIcon icon, MsgButtons buttons, int defaultButton) {
    if (owner == nullptr) return NativeMessageResult::Failed;
    const UINT flags = IconFlags(icon) | ButtonFlags(buttons) |
                       DefaultButtonFlags(defaultButton);
    const int id = ::MessageBoxW(static_cast<HWND>(owner),
                                 text != nullptr ? text : L"",
                                 title != nullptr ? title : L"", flags);
    switch (id) {
    case IDOK:        return NativeMessageResult::Ok;
    case IDCANCEL:    return NativeMessageResult::Cancel;
    case IDYES:       return NativeMessageResult::Yes;
    case IDNO:        return NativeMessageResult::No;
    case IDRETRY:     return NativeMessageResult::Retry;
    case IDABORT:     return NativeMessageResult::Abort;
    case IDIGNORE:    return NativeMessageResult::Ignore;
    case IDTRYAGAIN:  return NativeMessageResult::TryAgain;
    case IDCONTINUE:  return NativeMessageResult::Continue;
    default:          return NativeMessageResult::Failed;
    }
}

}  // namespace dui::platform
