#pragma once
#include "Camera.hpp"
#include "InputDevice.hpp"
#include "RenderingSystem.hpp"
#include "Timer.hpp"
#include "Window.hpp"

class Framework
{
public:
    explicit Framework(HINSTANCE instance);
    int Run();

private:
    void UpdateWindowTitle(float deltaTime);

    Window m_window;
    InputDevice m_input;
    Timer m_timer;
    Camera m_camera;
    RenderingSystem m_renderer;
    float m_titleTimer = 0.0f;
    float m_textureAnimationTime = 0.0f;
    bool m_textureAnimationEnabled = false;
};
