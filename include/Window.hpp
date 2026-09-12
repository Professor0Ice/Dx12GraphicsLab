#pragma once
#include "Common.hpp"

class Window
{
public:
    using MessageHandler = std::function<void(UINT, WPARAM, LPARAM)>;

    Window(HINSTANCE instance, UINT width, UINT height, std::wstring title, bool visible = true);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ProcessMessages();
    HWND Handle() const { return m_hwnd; }
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }
    void SetMessageHandler(MessageHandler handler) { m_messageHandler = std::move(handler); }
    void SetTitle(const std::wstring& title) const { SetWindowTextW(m_hwnd, title.c_str()); }

private:
    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_instance = nullptr;
    HWND m_hwnd = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;
    std::wstring m_className = L"Dx12LaboratoryWindow";
    MessageHandler m_messageHandler;
};
