// ============================================================================
//  Image/main.cpp — dui::Image 控件示例
//
//  运行：
//    Image.exe                无参数 → 程序生成一张 32 位 BMP 测试图并加载
//    Image.exe <图片路径>      加载指定图片（Windows 内置解码器支持的格式：
//                              PNG / JPEG / BMP / GIF / TIFF / ICO …）
//
//  演示：自然尺寸（像素 ÷ DPI 缩放）、布局拉伸、透明度。
// ============================================================================

#include <windows.h>
#include <shellapi.h>

#include <cstring>
#include <memory>
#include <string>

#include "DUI/DUI.h"

namespace {

// 生成一张 32 位 BMP 测试图（彩色棋盘 + 对角渐变）→ %TEMP%\dui_image_demo.bmp
// 自造 BMP 是为了"零外部依赖"；WIC 解码 BMP 与其它格式走同一条路径。
// 注：不用 std::vector::insert —— 项目的编译基线是 /EHs-c-（异常关闭），
// 触发该模板会报 C4530；改用裸数组 + memcpy。
std::wstring WriteTestImage() {
    constexpr int kW = 160;
    constexpr int kH = 120;
    const size_t rowBytes = static_cast<size_t>(kW) * 4;
    const size_t pxBytes = rowBytes * kH;
    const size_t total = 14 + 40 + pxBytes;

    std::unique_ptr<unsigned char[]> buf(new unsigned char[total]);
    std::memset(buf.get(), 0, total);
    unsigned char* p = buf.get();
    auto put16 = [&p](unsigned int v) {
        *p++ = static_cast<unsigned char>(v & 0xFFu);
        *p++ = static_cast<unsigned char>((v >> 8) & 0xFFu);
    };
    auto put32 = [&p](unsigned int v) {
        *p++ = static_cast<unsigned char>(v & 0xFFu);
        *p++ = static_cast<unsigned char>((v >> 8) & 0xFFu);
        *p++ = static_cast<unsigned char>((v >> 16) & 0xFFu);
        *p++ = static_cast<unsigned char>((v >> 24) & 0xFFu);
    };
    // BITMAPFILEHEADER（14 字节）
    put16(0x4D42);                                       // 'BM'（小端：先 'B' 后 'M'）
    put32(static_cast<unsigned int>(total));             // bfSize
    put16(0);                                            // bfReserved1
    put16(0);                                            // bfReserved2
    put32(14 + 40);                                      // bfOffBits（像素数据偏移）
    // BITMAPINFOHEADER（40 字节；biPlanes / biBitCount 各 2 字节）
    put32(40);                                           // biSize
    put32(static_cast<unsigned int>(kW));                // biWidth
    put32(static_cast<unsigned int>(kH));                // biHeight（正数 = 自下而上）
    put16(1);                                            // biPlanes
    put16(32);                                           // biBitCount
    put32(0);                                            // biCompression = BI_RGB
    put32(static_cast<unsigned int>(pxBytes));           // biSizeImage
    put32(2835);                                         // biXPelsPerMeter
    put32(2835);                                         // biYPelsPerMeter
    put32(0);                                            // biClrUsed
    put32(0);                                            // biClrImportant

    // BMP 行序自下而上
    for (int y = kH - 1; y >= 0; --y) {
        for (int x = 0; x < kW; ++x) {
            const bool checker = ((x / 16) + (y / 16)) % 2 == 0;
            *p++ = static_cast<unsigned char>(checker ? 40 : 200);          // B
            *p++ = static_cast<unsigned char>(x * 255 / (kW - 1));         // G
            *p++ = static_cast<unsigned char>(y * 255 / (kH - 1));         // R
            *p++ = 255;                                                   // A
        }
    }

    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    const std::wstring path = std::wstring(tempDir) + L"dui_image_demo.bmp";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    DWORD written = 0;
    WriteFile(h, buf.get(), static_cast<DWORD>(total), &written, nullptr);
    CloseHandle(h);
    return path;
}

class ImageWindow final : public dui::Window {
public:
    explicit ImageWindow(std::wstring path)
        : dui::Window(dui::WindowOptions{
              .title  = L"dui::Image 示例",
              .width  = 760.f,
              .height = 560.f,
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

        // 1) 自然尺寸：像素 ÷ DPI 缩放 → 1 图像像素 = 1 物理像素（不糊）
        root->AddChild<dui::Text>(L"自然尺寸：", dui::Style().FontSize(13.f).Color(0xFF4B5563));
        auto* natural = root->AddChild<dui::Image>(dui::Style().Padding(6.f).Background(0xFFE5E7EB));
        if (!natural->LoadFile(path_)) {
            root->AddChild<dui::Text>(natural->LastError().c_str(),
                                      dui::Style().FontSize(13.f).Color(0xFFB42318));
        }

        // 2) 布局拉伸：宽度/高度样式给更大的盒子 → 纹理线性采样放大
        root->AddChild<dui::Text>(L"拉伸到 320×240：", dui::Style().FontSize(13.f).Color(0xFF4B5563));
        auto* scaled = root->AddChild<dui::Image>(
            dui::Style().Width(320.f).Height(240.f).Padding(6.f).Background(0xFFE5E7EB));
        scaled->LoadFile(path_);

        // 3) 透明度：Prop::Opacity（乘到纹理 alpha）
        root->AddChild<dui::Text>(L"opacity 0.5：", dui::Style().FontSize(13.f).Color(0xFF4B5563));
        auto* faded = root->AddChild<dui::Image>(dui::Style().Opacity(0.5f));
        faded->LoadFile(path_);
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
    if (path.empty()) path = WriteTestImage();

    dui::Application app;
    ImageWindow window(path);
    return app.Run(window);
}
