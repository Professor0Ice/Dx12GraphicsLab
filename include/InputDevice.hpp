#pragma once
#include "Common.hpp"

class InputDevice
{
public:
    explicit InputDevice(HWND window);
    ~InputDevice();
    void ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void EndFrame();

    bool IsDown(uint8_t key) const { return m_keys[key]; }
    bool WasPressed(uint8_t key) const { return m_keys[key] and not m_previousKeys[key]; }
    DirectX::XMFLOAT2 ConsumeMouseDelta();
    bool MouseCaptured() const { return m_captured; }
    void SetMouseCaptured(bool captured);

private:
    HWND m_window = nullptr;
    std::array<bool, 256> m_keys{};
    std::array<bool, 256> m_previousKeys{};
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    bool m_captured = false;
};
