// BMU Test Tool — Win32 + DirectX 11 entry point
// Based on the Dear ImGui example_win32_directx11 sample.

#include "App.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>

#include <d3d11.h>
#include <tchar.h>

#include <memory>

// Forward declare Win32 message handler from imgui backend.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// DirectX 11 globals
// ---------------------------------------------------------------------------
static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain *g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0;
static UINT g_ResizeHeight = 0;
static ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();

// ---------------------------------------------------------------------------
// Win32 window procedure
// ---------------------------------------------------------------------------
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    {
        return true;
    }

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            break;
        }
        g_ResizeWidth = static_cast<UINT>(LOWORD(lParam));
        g_ResizeHeight = static_cast<UINT>(HIWORD(lParam));
        return 0;

    case WM_SYSCOMMAND:
        // Suppress the beep on Alt+key press.
        if ((wParam & 0xFFF0) == SC_KEYMENU)
        {
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPSTR /*lpCmdLine*/,
    _In_ int /*nCmdShow*/)
{
    // Create application window
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"BmuTestTool";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"BMU Test Tool",
        WS_OVERLAPPEDWINDOW,
        100,
        100,
        1600,
        900,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // ── ImGui + ImPlot setup ─────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Font — Roboto Mono Regular must live next to the executable.
    constexpr float kBaseFontSize = 20.0f;
    if (!io.Fonts->AddFontFromFileTTF("RobotoMono-Regular.ttf", kBaseFontSize))
    {
        io.Fonts->AddFontDefault(); // fallback
    }

    ImGui::StyleColorsDark();
    constexpr float kUiScale = 1.15f;
    ImGui::GetStyle().ScaleAllSizes(kUiScale);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // ── Application ──────────────────────────────────────────────────────────
    auto app = std::make_unique<bmu_app::App>();

    constexpr float kClearColor[4] = {0.10f, 0.10f, 0.10f, 1.0f};

    // ── Render loop ──────────────────────────────────────────────────────────
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
            {
                done = true;
            }
        }

        if (done)
        {
            break;
        }

        // Handle window resize
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(
                0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // New frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render application
        app->render();

        // Render ImGui draw data
        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(
            1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(
            g_mainRenderTargetView, kClearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // vsync
    }

    // ── Teardown ─────────────────────────────────────────────────────────────
    app.reset();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);

    return 0;
}

// ---------------------------------------------------------------------------
// D3D11 helpers
// ---------------------------------------------------------------------------

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
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

    constexpr UINT kCreateDeviceFlags = 0;
    constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    constexpr UINT kNumFeatureLevels =
        static_cast<UINT>(sizeof(kFeatureLevels) / sizeof(kFeatureLevels[0]));

    D3D_FEATURE_LEVEL feature_level;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        kCreateDeviceFlags,
        kFeatureLevels,
        kNumFeatureLevels,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &feature_level,
        &g_pd3dDeviceContext);

    if (FAILED(hr))
    {
        return false;
    }

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D *back_buffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer)
    {
        g_pd3dDevice->CreateRenderTargetView(
            back_buffer, nullptr, &g_mainRenderTargetView);
        back_buffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}
