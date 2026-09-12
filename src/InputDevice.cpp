#include "InputDevice.hpp"

InputDevice::InputDevice(HWND window) : m_window(window)
{
    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x01;
    device.usUsage = 0x02;
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = window;
    if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
        throw std::runtime_error("RegisterRawInputDevices failed");
    SetMouseCaptured(true);
}

InputDevice::~InputDevice()
{
    SetMouseCaptured(false);
}

void InputDevice::SetMouseCaptured(bool captured)
{
    if (m_captured == captured)
        return;
    m_captured = captured;
    if (captured)
    {
        SetCapture(m_window);
        RECT rectangle{};
        GetClientRect(m_window, &rectangle);
        POINT topLeft{ rectangle.left, rectangle.top };
        POINT bottomRight{ rectangle.right, rectangle.bottom };
        ClientToScreen(m_window, &topLeft);
        ClientToScreen(m_window, &bottomRight);
        rectangle = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
        ClipCursor(&rectangle);
        while (ShowCursor(FALSE) >= 0) {}
    }
    else
    {
        ReleaseCapture();
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
    }
}

void InputDevice::ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam < m_keys.size())
            m_keys[wParam] = true;
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam < m_keys.size())
            m_keys[wParam] = false;
        break;
    case WM_KILLFOCUS:
        m_keys.fill(false);
        SetMouseCaptured(false);
        break;
    case WM_SETFOCUS:
        SetMouseCaptured(true);
        break;
    case WM_LBUTTONDOWN:
        SetMouseCaptured(true);
        break;
    case WM_INPUT:
        if (m_captured)
        {
            UINT size = 0;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            std::vector<std::byte> data(size);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, data.data(), &size,
                                sizeof(RAWINPUTHEADER)) == size)
            {
                const auto* input = reinterpret_cast<const RAWINPUT*>(data.data());
                if (input->header.dwType == RIM_TYPEMOUSE)
                {
                    m_mouseDeltaX += static_cast<float>(input->data.mouse.lLastX);
                    m_mouseDeltaY += static_cast<float>(input->data.mouse.lLastY);
                }
            }
        }
        break;
    }
}

DirectX::XMFLOAT2 InputDevice::ConsumeMouseDelta()
{
    DirectX::XMFLOAT2 result{ m_mouseDeltaX, m_mouseDeltaY };
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    return result;
}

void InputDevice::EndFrame()
{
    m_previousKeys = m_keys;
}

