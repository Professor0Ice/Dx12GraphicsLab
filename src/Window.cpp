#include "Window.hpp"

Window::Window(HINSTANCE instance, UINT width, UINT height, std::wstring title, bool visible)
    : m_instance(instance), m_width(width), m_height(height)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = StaticWindowProc;
    windowClass.hInstance = m_instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = m_className.c_str();
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("RegisterClassExW failed");

    RECT rectangle{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rectangle, style, FALSE);
    m_hwnd = CreateWindowExW(
        0, m_className.c_str(), title.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        nullptr, nullptr, m_instance, this);
    if (!m_hwnd)
        throw std::runtime_error("CreateWindowExW failed");

    if (visible)
    {
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
    }
}

Window::~Window()
{
    if (m_hwnd)
        DestroyWindow(m_hwnd);
    UnregisterClassW(m_className.c_str(), m_instance);
}

bool Window::ProcessMessages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
            return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

LRESULT CALLBACK Window::StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Window* window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        window = static_cast<Window*>(create->lpCreateParams);
        window->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    return window ? window->WindowProc(message, wParam, lParam)
                  : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Window::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (m_messageHandler)
        m_messageHandler(message, wParam, lParam);

    switch (message)
    {
    case WM_CLOSE:
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}
