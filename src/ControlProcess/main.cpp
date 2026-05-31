/**
 * @file main.cpp
 * @brief ControlProcess 入口点
 *
 * 控制进程：Win32 窗口界面，通过 IPC 与 RenderProcess 通信。
 */

#include "ControlPanel.h"

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{

    mangke::ControlPanel panel;

    if (!panel.Create(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"创建控制面板窗口失败", L"错误", MB_ICONERROR);
        return 1;
    }

    return panel.Run();
}
