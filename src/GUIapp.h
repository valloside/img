#pragma once

#include <string>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <tchar.h>

class GUIapp
{
public:
    static int start();

private:
    inline static WNDCLASSEXW wc;
    inline static HWND        hwnd;

    inline static const ImVec4 clear_color = ImVec4(0.2, 0.2, 0.2, 1);

    static int  initWindow();
    static int  initImGui();
    static int  setupBackend();
    static void imGuiStuff();
    static void endApp();

public:
    // Data
    inline static ID3D11Device           *s_pd3dDevice = nullptr;
    inline static ID3D11DeviceContext    *s_pd3dDeviceContext = nullptr;
    inline static IDXGISwapChain         *s_pSwapChain = nullptr;
    inline static ID3D11RenderTargetView *s_mainRenderTargetView = nullptr;
    inline static UINT                    s_resizeWidth = 0, s_resizeHeight = 0;
    inline static bool                    s_swapChainOccluded = false;
    inline static bool                    s_wndMinimized = false;
    inline static bool                    s_enableSleepOptimize = false;
    inline static bool                    s_keyDownEvent = false;

private:
    friend class GUIappImpl;
    class GUIappImpl
    {
    public:
        static void renderUI();

        static void onDropImageFile(std::wstring &&path);
    };

    static bool           CreateDeviceD3D(HWND hWnd);
    static void           CleanupDeviceD3D();
    static void           CreateRenderTarget();
    static void           CleanupRenderTarget();
    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};