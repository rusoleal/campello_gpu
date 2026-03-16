//
// main.cpp
// campello_example — Windows
//
// Win32 application entry point.
// Creates a window and drives the render loop.
//

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "Renderer.h"

static std::unique_ptr<Renderer> gRenderer;

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (gRenderer && wParam != SIZE_MINIMIZED) {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w > 0 && h > 0)
                gRenderer->resize(w, h);
        }
        return 0;

    case WM_PAINT:
        if (gRenderer)
            gRenderer->render();
        ValidateRect(hwnd, nullptr);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t* kClassName  = L"campello_gpu_example";
    const wchar_t* kWindowTitle = L"campello_gpu — Triangle";

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // Create window (client area 800 × 600)
    constexpr int kW = 800, kH = 600;
    RECT r = { 0, 0, kW, kH };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExW(
        0, kClassName, kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBoxW(nullptr, L"CreateWindow failed", kWindowTitle, MB_ICONERROR);
        return 1;
    }

    // Create renderer (also creates the campello_gpu device + swapchain)
    try {
        gRenderer = std::make_unique<Renderer>(hwnd, kW, kH);
    }
    catch (const std::exception& e) {
        MessageBoxA(hwnd, e.what(), "campello_gpu error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message / render loop
    MSG msg = {};
    while (true)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Render every iteration (not only on WM_PAINT) for smooth animation.
        if (gRenderer)
            gRenderer->render();
    }

done:
    gRenderer.reset();
    return static_cast<int>(msg.wParam);
}
