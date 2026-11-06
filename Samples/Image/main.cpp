// ============================================================================
//  Image/main.cpp — dui::Image 控件示例
//
//  运行：
//    Image.exe                加载工程内的 image.png + demo.gif（构建时拷到 exe 目录）
//    Image.exe <图片路径>      静态演示改用指定图片（Windows 内置解码器支持的格式：
//                              PNG / JPEG / BMP / GIF / TIFF / ICO …）
//
//  演示：大图（896×896）用样式限定盒子尺寸 → Image 按 contain 等比缩放并居中，
//        图像始终不变形；Prop::Opacity；以及 GIF 动画按帧延迟播放。
// ============================================================================

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <utility>

#include "DUI/DUI.h"

namespace {

// exe 所在目录 + 资源文件名（vcxproj PostBuildEvent 把资源拷到 exe 目录）
std::wstring AssetPathInExeDir(const wchar_t* name) {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    const auto slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) path.resize(slash + 1);
    return path + name;
}

class ImageWindow final : public dui::Window {
public:
    explicit ImageWindow(std::wstring path)
        : dui::Window(dui::WindowOptions{
              .title  = L"dui::Image 示例",
              .width  = 760.f,
              .height = 900.f,
          }),
          path_(std::move(path)) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .AlignItems(dui::Align::Start)
            .Gap(10.f)
            .Padding(18.f)
            .Background(0xFFF3F4F6));

        root->AddChild<dui::Text>(L"dui::Image —— 解码走 Windows 内置解码器（WIC）",
                                  dui::Style().FontSize(17.f).Color(0xFF1F2933));

        // 1) 正方形盒子：大图等比缩到 180×180，完整落在窗口内
        root->AddChild<dui::Text>(L"样式 180×180：等比缩放，不变形",
                                  dui::Style().FontSize(13.f).Color(0xFF4B5563));
        auto* fit = root->AddChild<dui::Image>(dui::Style()
            .Width(180.f).Height(180.f).Padding(6.f).Background(0xFFE5E7EB));
        if (!fit->LoadFile(path_)) {
            root->AddChild<dui::Text>(fit->LastError().c_str(),
                                      dui::Style().FontSize(13.f).Color(0xFFB42318));
        }

        // 2) 盒子比例与图像不同：contain 取 min，图像 160×160 居中、两侧留白
        root->AddChild<dui::Text>(L"样式 240×160：等比缩放并居中（不拉伸）",
                                  dui::Style().FontSize(13.f).Color(0xFF4B5563));
        auto* letterbox = root->AddChild<dui::Image>(dui::Style()
            .Width(240.f).Height(160.f).Padding(6.f).Background(0xFFE5E7EB));
        letterbox->LoadFile(path_);

        // 3) 透明度：Prop::Opacity（乘到纹理 alpha），尺寸仍由样式控制
        root->AddChild<dui::Text>(L"样式 140×140 + opacity 0.5",
                                  dui::Style().FontSize(13.f).Color(0xFF4B5563));
        auto* faded = root->AddChild<dui::Image>(dui::Style()
            .Width(140.f).Height(140.f).Opacity(0.5f));
        faded->LoadFile(path_);

        // 4) 动画 GIF：dui::Gif 统一走多帧解码 + disposal 合成，按帧延迟换帧播放；
        //    Pause() / Play() / Stop() / SetLoopCount(n)（0 = 无限）可控制播放
        root->AddChild<dui::Text>(L"动画 GIF：WIC 多帧 + 帧延迟播放",
                                  dui::Style().FontSize(13.f).Color(0xFF4B5563));
        auto* anim = root->AddChild<dui::Gif>(dui::Style()
            .Width(320.f).Height(134.f).Background(0xFFE5E7EB));
        if (!anim->LoadFile(AssetPathInExeDir(L"demo.gif"))) {
            root->AddChild<dui::Text>(anim->LastError().c_str(),
                                      dui::Style().FontSize(13.f).Color(0xFFB42318));
        }
    }

private:
    std::wstring path_;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring path;
    if (argv != nullptr && argc > 1) path = argv[1];
    if (argv != nullptr) LocalFree(argv);
    if (path.empty()) path = AssetPathInExeDir(L"image.png");

    dui::Application app;
    ImageWindow window(path);
    return app.Run(window);
}
