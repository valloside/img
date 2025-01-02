#include "GUIapp.h"
#include <cmath>
#include <opencv2/opencv.hpp>

int GUIapp::start()
{
    if (int res = GUIapp::initWindow())
        return res;

    if (int res = GUIapp::initImGui())
        return res;

    if (int res = GUIapp::setupBackend())
        return res;

    cv::setUseOptimized(false);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;
        static int threshold = -10;
        static int d = 0;
        if (threshold < 0)
        {
            ++threshold;
        }
        else if (s_wndMinimized || s_enableSleepOptimize) // 动态控制刷新率
        {
            ++threshold;
            if (threshold > 25)
                threshold = 25;
            ++d;
            d %= threshold + 1;
            if (d < threshold)
            {
                Sleep(5);
                continue;
            }
        }
        else
        {
            threshold = 0;
        }

        GUIapp::imGuiStuff();
        s_enableSleepOptimize = !s_keyDownEvent;
    }

    GUIapp::endApp();
    return 0;
}

int GUIapp::initWindow()
{
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    // Create application window
    ImGui_ImplWin32_EnableDpiAwareness();
    GUIapp::wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"img", nullptr};
    ::RegisterClassExW(&wc);
    GUIapp::hwnd = ::CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"img", WS_OVERLAPPEDWINDOW, 100, 100, 1920, 1080, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    return 0;
}

int GUIapp::initImGui()
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    [[maybe_unused]] ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows
    // io.ConfigViewportsNoAutoMerge = true;
    // io.ConfigViewportsNoTaskBarIcon = true;
    // io.ConfigViewportsNoDefaultParent = true;
    // io.ConfigDockingAlwaysTabBar = true;
    // io.ConfigDockingTransparentPayload = true;
    // io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: Experimental. THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP!
    // io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI: Experimental.

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle &style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1;
    }
    return 0;
}

int GUIapp::setupBackend()
{
    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(GUIapp::hwnd);
    ImGui_ImplDX11_Init(GUIapp::s_pd3dDevice, GUIapp::s_pd3dDeviceContext);

    ImGuiIO &io = ImGui::GetIO();
    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // io.Fonts->AddFontDefault();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    [[maybe_unused]] ImFont *font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 27.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    IM_ASSERT(font != nullptr);
    return 0;
}

void GUIapp::imGuiStuff()
{
    // Handle window being minimized or screen locked
    if (s_swapChainOccluded && s_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
    {
        ::Sleep(10);
        return;
    }
    s_swapChainOccluded = false;

    // Handle window resize (we don't resize directly in the WM_SIZE handler)
    if (s_resizeWidth != 0 && s_resizeHeight != 0)
    {
        CleanupRenderTarget();
        s_pSwapChain->ResizeBuffers(0, s_resizeWidth, s_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
        s_resizeWidth = s_resizeHeight = 0;
        CreateRenderTarget();
    }

    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    GUIappImpl::renderUI();

    // Rendering
    ImGui::Render();

    const float clear_color_with_alpha[4] = {clear_color.x, clear_color.y, clear_color.z, clear_color.w};
    s_pd3dDeviceContext->OMSetRenderTargets(1, &s_mainRenderTargetView, nullptr);
    s_pd3dDeviceContext->ClearRenderTargetView(s_mainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Update and Render additional Platform Windows
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    // Present
    HRESULT hr = s_pSwapChain->Present(1, 0); // Present with vsync
    // HRESULT hr = s_pSwapChain->Present(0, 0); // Present without vsync
    s_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
}

void GUIapp::endApp()
{
    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

/// Helper functions
#pragma region Helper Funcs

bool GUIapp::CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    // createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL       featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &s_pSwapChain, &s_pd3dDevice, &featureLevel, &s_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &s_pSwapChain, &s_pd3dDevice, &featureLevel, &s_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void GUIapp::CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (s_pSwapChain)
    {
        s_pSwapChain->Release();
        s_pSwapChain = nullptr;
    }
    if (s_pd3dDeviceContext)
    {
        s_pd3dDeviceContext->Release();
        s_pd3dDeviceContext = nullptr;
    }
    if (s_pd3dDevice)
    {
        s_pd3dDevice->Release();
        s_pd3dDevice = nullptr;
    }
}

void GUIapp::CreateRenderTarget()
{
    ID3D11Texture2D *pBackBuffer;
    s_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    s_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &s_mainRenderTargetView);
    pBackBuffer->Release();
}

void GUIapp::CleanupRenderTarget()
{
    if (s_mainRenderTargetView)
    {
        s_mainRenderTargetView->Release();
        s_mainRenderTargetView = nullptr;
    }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI GUIapp::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    static bool resizingWindow = false;

    switch (msg)
    {
    case WM_ENTERSIZEMOVE:
        resizingWindow = true;
        return 0;
    case WM_EXITSIZEMOVE:
    case WM_CAPTURECHANGED:
        resizingWindow = false;
        break;
    case WM_SIZE: {
        s_wndMinimized = wParam == SIZE_MINIMIZED;
        if (s_wndMinimized)
            return 0;

        s_resizeWidth = (UINT)LOWORD(lParam); // Queue resize
        s_resizeHeight = (UINT)HIWORD(lParam);
        if (resizingWindow)
            GUIapp::imGuiStuff();
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    case WM_DPICHANGED:
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)
        {
            // const int dpi = HIWORD(wParam);
            // printf("WM_DPICHANGED to %d (%.0f%%)\n", dpi, (float)dpi / 96.0f * 100.0f);
            const RECT *suggested_rect = (RECT *)lParam;
            ::SetWindowPos(hWnd, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        break;
    case WM_DROPFILES: {
        std::wstring buffer(MAX_PATH, 0);
        UINT         filecount = DragQueryFileW((HDROP)wParam, -1, nullptr, 0);
        for (size_t i = 0; i < filecount; i++)
        {
            DragQueryFileW((HDROP)wParam, i, buffer.data(), MAX_PATH);
            GUIappImpl::onDropImageFile(std::move(buffer));
            buffer.resize(MAX_PATH);
        }
        s_enableSleepOptimize = false;
        break;
    }
    case WM_MOUSEMOVE:
        s_enableSleepOptimize = false;
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_KEYDOWN:
        s_enableSleepOptimize = false;
        s_keyDownEvent = true;
        break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_KEYUP:
        s_enableSleepOptimize = false;
        s_keyDownEvent = false;
        break;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

#pragma endregion
