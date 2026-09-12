#include "Framework.hpp"

Framework::Framework(HINSTANCE instance)
    : m_window(instance, 1600, 900, L"DX12 Graphics Laboratory"),
      m_input(m_window.Handle()),
      m_camera(static_cast<float>(m_window.Width()) / m_window.Height()),
      m_renderer(m_window.Handle(), m_window.Width(), m_window.Height())
{
    m_window.SetMessageHandler([this](UINT message, WPARAM wParam, LPARAM lParam)
    {
        m_input.ProcessMessage(message, wParam, lParam);
    });
}

int Framework::Run()
{
    m_timer.Reset();
    while (m_window.ProcessMessages())
    {
        m_timer.Tick();
        if (m_input.WasPressed(VK_ESCAPE))
            m_input.SetMouseCaptured(false);
        if (m_input.WasPressed('1')) m_renderer.SetCullingMode(CullingMode::Disabled);
        if (m_input.WasPressed('2')) m_renderer.SetCullingMode(CullingMode::Frustum);
        if (m_input.WasPressed('3')) m_renderer.SetCullingMode(CullingMode::Octree);
        if (m_input.WasPressed('O')) m_renderer.ToggleOctreeCulling();
        if (m_input.WasPressed('T')) m_textureAnimationEnabled = !m_textureAnimationEnabled;

        if (m_textureAnimationEnabled)
            m_textureAnimationTime += m_timer.DeltaSeconds();

        const auto mouse = m_input.ConsumeMouseDelta();
        m_camera.Update(m_input, mouse, m_timer.DeltaSeconds());
        m_renderer.Render(m_camera, m_textureAnimationTime);
        UpdateWindowTitle(m_timer.DeltaSeconds());
        m_input.EndFrame();
    }
    return 0;
}

void Framework::UpdateWindowTitle(float deltaTime)
{
    m_titleTimer += deltaTime;
    if (m_titleTimer < 0.25f) return;
    m_titleTimer = 0.0f;
    const wchar_t* mode = L"Octree";
    if (m_renderer.GetCullingMode() == CullingMode::Disabled) mode = L"Disabled";
    else if (m_renderer.GetCullingMode() == CullingMode::Frustum) mode = L"Frustum";
    std::wstringstream title;
    title << L"DX12 Lab | WASD + mouse | Octree culling [O]: "
          << (m_renderer.IsOctreeCullingEnabled() ? L"ON" : L"OFF")
          << L" | mode [1/2/3]: " << mode
          << L" | textures [T]: " << (m_textureAnimationEnabled ? L"MOVING" : L"PAUSED")
          << L" | visible " << m_renderer.VisibleObjectCount() << L" / " << m_renderer.TotalObjectCount();
    m_window.SetTitle(title.str());
}
