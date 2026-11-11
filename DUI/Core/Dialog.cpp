// ============================================================================
//  DUI/Core/Dialog.cpp — 系统对话框的 Win32 / COM 实现（17-Dialogs）
//
//  · 文件 / 保存 / 文件夹：Common Item Dialog（IFileDialog，Vista+，系统 PMv2
//    自动缩放）；保存走 IFileSaveDialog，文件夹走 FOS_PICKFOLDERS。
//  · 消息框：MessageBoxW。
//  · COM 沿用 WIC 的进程级 CoInitializeEx(COINIT_MULTITHREADED)，不 CoUninitialize。
//  · 无异常：失败经 DialogResult / MessageResult 返回；COM 接口用 ComReleaser
//    兜底 Release。
// ============================================================================

#include "Core/Dialog.h"

#include <objbase.h>
#include <shobjidl_core.h>

#include <utility>
#include <vector>

// 系统库用 #pragma comment(lib) 随本编译单元携带，不改根目录 DUI.props
// （与 Graphics/ImageCodec.cpp 的 windowscodecs.lib 同一做法）。
// CLSID 用 __uuidof(FileOpenDialog/FileSaveDialog)，无需 uuid.lib。
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace dui {

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

// COM 单元校验 + 对话框创建/设置/展示，结果直接折叠成 DialogResult
DialogResult<std::vector<String>> RunFileDialog(Mode mode, void* owner,
                                                const wchar_t* title,
                                                const wchar_t* initialPath,
                                                std::span<const FileTypeFilter> filters,
                                                size_t filterIndex,
                                                const wchar_t* defaultExtension,
                                                bool multiSelect) {
    if (owner == nullptr) {
        return DialogResult<std::vector<String>>::Failed(
            L"窗口尚未创建（需在 Application::Run 之后调用）");
    }

    // UI 线程由 Application::Run 用 OleInitialize 建成 STA；此处再请求 STA 只返回
    // S_FALSE。若线程已被别人初始化成 MTA（RPC_E_CHANGED_MODE），Common Item Dialog
    // 的 Show() 会在创建窗口前死锁 —— 明确报错而不是把界面挂死。
    static const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (comInit == RPC_E_CHANGED_MODE) {
        return DialogResult<std::vector<String>>::Failed(
            L"文件对话框需要 STA 单元（UI 线程 COM 已被初始化为 MTA）");
    }

    APTTYPE apt = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qual = APTTYPEQUALIFIER_NONE;
    // OleInitialize 建立的是 main STA（APTTYPE_MAINSTA），普通 CoInitializeEx(STA)
    // 是 APTTYPE_STA —— 两者都能跑 Common Item Dialog，都要放行。
    if (FAILED(CoGetApartmentType(&apt, &qual)) ||
        (apt != APTTYPE_STA && apt != APTTYPE_MAINSTA)) {
        return DialogResult<std::vector<String>>::Failed(
            L"文件对话框需要 STA 单元（UI 线程 COM 单元异常）");
    }

    const GUID clsid = (mode == Mode::Save) ? __uuidof(FileSaveDialog)
                                           : __uuidof(FileOpenDialog);
    IFileDialog* raw = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw));
    if (FAILED(hr) || raw == nullptr) {
        String error;
        SetError(error, L"创建文件对话框失败", hr);
        return DialogResult<std::vector<String>>::Failed(std::move(error));
    }
    ComReleaser dlgHolder(raw);
    IFileDialog* dlg = raw;

    FILEOPENDIALOGOPTIONS opts = 0;
    if (FAILED(dlg->GetOptions(&opts))) opts = 0;
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    switch (mode) {
    case Mode::Open:
        opts |= FOS_FILEMUSTEXIST;
        if (multiSelect) opts |= FOS_ALLOWMULTISELECT;
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
        return DialogResult<std::vector<String>>::Failed(L"设置对话框选项失败");
    }
    if (title != nullptr && *title != L'\0') dlg->SetTitle(title);

    // FileTypeFilter 与 COMDLG_FILTERSPEC 同构（两个 const wchar_t*），span 直接
    // 重解释为 COMDLG_FILTERSPEC 数组；数组必须活到 Show() 返回（调用点同作用域）。
    if (mode != Mode::Folder && !filters.empty()) {
        const auto* specs = reinterpret_cast<const COMDLG_FILTERSPEC*>(filters.data());
        dlg->SetFileTypes(static_cast<UINT>(filters.size()), specs);
        const size_t idx = (filterIndex < filters.size()) ? filterIndex : 0;
        dlg->SetFileTypeIndex(static_cast<UINT>(idx + 1));
    }

    if (mode == Mode::Save && defaultExtension != nullptr && *defaultExtension != L'\0') {
        IFileSaveDialog* save = nullptr;
        if (SUCCEEDED(dlg->QueryInterface(IID_PPV_ARGS(&save))) && save != nullptr) {
            ComReleaser s(save);
            save->SetDefaultExtension(defaultExtension);
        }
    }

    ApplyInitialPath(dlg, initialPath);

    hr = dlg->Show(static_cast<HWND>(owner));
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return DialogResult<std::vector<String>>::Cancelled();
    }
    if (FAILED(hr)) {
        String error;
        SetError(error, L"显示文件对话框失败", hr);
        return DialogResult<std::vector<String>>::Failed(std::move(error));
    }

    std::vector<String> paths;
    if (mode == Mode::Open && multiSelect) {
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
                        if (GetPath(item, path)) paths.push_back(std::move(path));
                    }
                }
            }
        }
    } else {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item != nullptr) {
            ComReleaser it(item);
            String path;
            if (GetPath(item, path)) paths.push_back(std::move(path));
        }
    }

    // 虚拟 Shell 项没有文件系统路径：跳过；一个都没有时按失败报。
    if (paths.empty()) {
        return DialogResult<std::vector<String>>::Failed(L"未取得文件路径");
    }
    return DialogResult<std::vector<String>>::Succeeded(std::move(paths));
}

UINT IconFlags(MessageIcon icon) {
    switch (icon) {
    case MessageIcon::Information: return MB_ICONINFORMATION;
    case MessageIcon::Warning:     return MB_ICONWARNING;
    case MessageIcon::Error:       return MB_ICONERROR;
    case MessageIcon::Question:    return MB_ICONQUESTION;
    case MessageIcon::None:        return 0;
    default:                       return 0;
    }
}

UINT ButtonFlags(MessageButtons buttons) {
    switch (buttons) {
    case MessageButtons::Ok:                  return MB_OK;
    case MessageButtons::OkCancel:            return MB_OKCANCEL;
    case MessageButtons::YesNo:               return MB_YESNO;
    case MessageButtons::YesNoCancel:         return MB_YESNOCANCEL;
    case MessageButtons::RetryCancel:         return MB_RETRYCANCEL;
    case MessageButtons::AbortRetryIgnore:    return MB_ABORTRETRYIGNORE;
    case MessageButtons::CancelRetryContinue: return MB_CANCELTRYCONTINUE;
    default:                                  return MB_OK;
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

DialogResult<std::vector<String>> PickFiles(void* owner, const OpenFileOptions& options) {
    return RunFileDialog(Mode::Open, owner, options.title, options.initialPath,
                         options.filters, options.filterIndex, nullptr,
                         options.multiSelect);
}

DialogResult<String> SaveFile(void* owner, const SaveFileOptions& options) {
    DialogResult<std::vector<String>> r = RunFileDialog(
        Mode::Save, owner, options.title, options.initialPath,
        options.filters, options.filterIndex, options.defaultExtension, false);
    if (!r.IsOk()) {
        return r.IsCancelled() ? DialogResult<String>::Cancelled()
                               : DialogResult<String>::Failed(r.Error());
    }
    return DialogResult<String>::Succeeded(std::move(r.Value()[0]));
}

DialogResult<String> PickFolder(void* owner, const OpenFolderOptions& options) {
    DialogResult<std::vector<String>> r = RunFileDialog(
        Mode::Folder, owner, options.title, options.initialPath, {}, 0, nullptr, false);
    if (!r.IsOk()) {
        return r.IsCancelled() ? DialogResult<String>::Cancelled()
                               : DialogResult<String>::Failed(r.Error());
    }
    return DialogResult<String>::Succeeded(std::move(r.Value()[0]));
}

MessageResult ShowMessageBox(void* owner, const MessageOptions& options) {
    if (owner == nullptr) return MessageResult::Failed;
    const UINT flags = IconFlags(options.icon) | ButtonFlags(options.buttons) |
                       DefaultButtonFlags(options.defaultButton);
    const int id = ::MessageBoxW(static_cast<HWND>(owner),
                                 options.text != nullptr ? options.text : L"",
                                 options.title != nullptr ? options.title : L"", flags);
    switch (id) {
    case IDOK:        return MessageResult::Ok;
    case IDCANCEL:    return MessageResult::Cancel;
    case IDYES:       return MessageResult::Yes;
    case IDNO:        return MessageResult::No;
    case IDRETRY:     return MessageResult::Retry;
    case IDABORT:     return MessageResult::Abort;
    case IDIGNORE:    return MessageResult::Ignore;
    case IDTRYAGAIN:  return MessageResult::TryAgain;
    case IDCONTINUE:  return MessageResult::Continue;
    default:          return MessageResult::Failed;
    }
}

}  // namespace dui
