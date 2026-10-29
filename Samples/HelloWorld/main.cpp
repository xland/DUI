// ============================================================================
//  HelloWorld/main.cpp — 最小示例：窗口正中一行“你好，World”
//
//  构建产物（Build 布局，DUI.slnx 根目录相对）：
//    中间产物  Build\Temp\HelloWorld\Debug|Release\
//    最终产物  Build\HelloWorld\Debug|Release\HelloWorld.exe
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

class HelloWindow final : public dui::Window {
public:
    HelloWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"HelloWorld",
              .width  = 800.f,
              .height = 600.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        // 默认列方向：主轴 = 垂直、交叉轴 = 水平，双 Center → 文字落在窗口正中
        root->SetStyle(dui::StyleBuilder()
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center)
            .Background(0xFFF5F5F5));
        root->AddChild<dui::Text>(L"你好，World", dui::StyleBuilder()
            .FontSize(24.f)
            .Color(0xFF1F1F1F));
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    HelloWindow window;
    return app.Run(window);
}
