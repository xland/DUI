// ============================================================================
//  Image/main.cpp — dui::Image 控件示例
//
//  运行：
//    Image.exe                加载工程内的 image.png + demo.gif（构建时拷到 exe 目录）
//    Image.exe <图片路径>      静态演示改用指定图片（Windows 内置解码器支持的格式：
//                              PNG / JPEG / BMP / GIF / TIFF / ICO …）
//
//  演示：大图（896×896）用样式限定盒子尺寸 → Image 按 contain 等比缩放并居中，
//        图像始终不变形；Prop::Opacity；dui::Gif 的帧动画与播放控制
//        （Pause / Play / Stop / SetLoopCount）；以及 dui::Svg 的矢量图标
//        （同一份 SVG 在 24 / 48 / 96 DIP 下分别重新光栅，放大不糊）。
// ============================================================================

#include <windows.h>
#include <shellapi.h>

#include <functional>
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

static const dui::Style kButton = dui::Style()
    .FontSize(13.f).Color(0xFFFFFFFF)
    .Background(0xFF374151)
    .Padding(dui::Thickness(6.f, 12.f))
    .CornerRadius(4.f)
    .Cursor(dui::Cursor::Hand)
    .FlexShrink(0.f);

static const dui::Style kButtonHover = dui::Style().Background(0xFF1F6FE5);

dui::Panel* MakeRow(dui::Panel* parent) {
    return parent->AddChild<dui::Panel>(dui::Style()
        .FlexDirection(dui::FlexDirection::Row)
        .AlignItems(dui::Align::Center)
        .Gap(8.f)
        .FlexShrink(0.f));
}

template <class F>
void AddButton(dui::Panel* parent, const wchar_t* label, F fn) {
    dui::Button* b = parent->AddChild<dui::Button>(label, kButton);
    b->OnClick([fn](dui::PointerEvent&) { fn(); });
    b->When(dui::State::Hover, kButtonHover);
}

class ImageWindow final : public dui::Window {
public:
    explicit ImageWindow(std::wstring path)
        : dui::Window(dui::WindowOptions{
              .title  = L"dui::Image 示例",
              .width  = 760.f,
              .height = 1000.f,
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

        // 4) 动画 GIF 播放控制：dui::Gif 统一走多帧解码 + disposal 合成，按帧延迟换帧
        root->AddChild<dui::Text>(L"动画 GIF：WIC 多帧 + 帧延迟播放 / 播放控制",
                                  dui::Style().FontSize(13.f).Color(0xFF4B5563));
        dui::Gif* anim = root->AddChild<dui::Gif>(dui::Style()
            .Width(320.f).Height(134.f).Background(0xFFE5E7EB));
        if (!anim->LoadFile(AssetPathInExeDir(L"demo.gif"))) {
            root->AddChild<dui::Text>(anim->LastError().c_str(),
                                      dui::Style().FontSize(13.f).Color(0xFFB42318));
        }

        // row 先挂到 root，status 后挂 → 布局顺序仍是 按钮行 在 状态文本 之前
        dui::Panel* row = MakeRow(root);
        dui::Text* status = root->AddChild<dui::Text>(
            L"就绪：GIF 加载后自动播放（默认无限循环）",
            dui::Style().FontSize(13.f).Color(0xFF0F766E).FlexShrink(0.f));

        AddButton(row, L"暂停", [anim, status] {
            anim->Pause();
            status->SetText(L"Pause()：停当前帧、保留游标，从 ticker 注销");
        });
        AddButton(row, L"播放", [anim, status] {
            anim->Play();
            status->SetText(L"Play()：从暂停处继续，跳过暂停时长");
        });
        AddButton(row, L"停止", [anim, status] {
            anim->Stop();
            status->SetText(L"Stop()：回首帧并保留帧序列，Play() 从头重播");
        });
        AddButton(row, L"循环 3 次", [anim, status] {
            anim->SetLoopCount(3);
            anim->Stop();
            anim->Play();
            status->SetText(L"SetLoopCount(3)：播完 3 轮停在最后一帧并停表");
        });
        AddButton(row, L"无限循环", [anim, status] {
            anim->SetLoopCount(0);
            anim->Stop();
            anim->Play();
            status->SetText(L"SetLoopCount(0)：无限循环（默认）");
        });

        // 5) dui::Svg：Direct2D 内置 SVG 解析（Win10 1703+，图标级子集）
        //    矢量图标按"显示尺寸 × DPI"重新光栅，所以同一份文档放大也不糊。
        root->AddChild<dui::Text>(L"Svg —— 矢量图像",
                                  dui::Style().FontSize(13.f).Color(0xFF4B5563));
        dui::Panel* svgRow = MakeRow(root);
        const std::wstring icon = AssetPathInExeDir(L"icon.svg");
        dui::Svg* small = svgRow->AddChild<dui::Svg>(dui::Style()
            .Width(24.f).Height(24.f).Background(0xFFE5E7EB));
        if (!small->LoadSvgFile(icon)) {
            root->AddChild<dui::Text>(small->LastError().c_str(),
                                      dui::Style().FontSize(13.f).Color(0xFFB42318));
        }
        dui::Svg* large = svgRow->AddChild<dui::Svg>(dui::Style()
            .Width(96.f).Height(96.f).Background(0xFFE5E7EB));
        large->LoadSvgFile(icon);
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
