#include "Framework.hpp"

Framework::Framework(HINSTANCE instance)
    : m_window(instance, 1600, 900, L"KG is pain"),
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
        if (m_input.WasPressed('1')) m_renderer.SetPostProcessMode(PostProcessMode::None);
        if (m_input.WasPressed('2')) m_renderer.SetPostProcessMode(PostProcessMode::Grayscale);
        if (m_input.WasPressed('3')) m_renderer.SetPostProcessMode(PostProcessMode::SobelEdges);
        if (m_input.WasPressed('T')) m_textureAnimationEnabled = not m_textureAnimationEnabled;

        if (m_textureAnimationEnabled)
            m_textureAnimationTime += m_timer.DeltaSeconds();

        const auto mouse = m_input.ConsumeMouseDelta();
        m_camera.Update(m_input, mouse, m_timer.DeltaSeconds());
        m_renderer.Render(m_camera, m_textureAnimationTime, m_timer.DeltaSeconds());
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

    int fps = static_cast<int>(1.0f / deltaTime);

    const wchar_t* effect = L"None";
    if (m_renderer.GetPostProcessMode() == PostProcessMode::Grayscale) effect = L"Grayscale";
    else if (m_renderer.GetPostProcessMode() == PostProcessMode::SobelEdges) effect = L"Sobel edges";
    std::wstringstream title;
    title << L"KG is pain / Post FX [1/2/3]: " << effect << L" / culling: Octree"
          << L" / visible " << m_renderer.VisibleObjectCount() << L" / " << m_renderer.TotalObjectCount()
          << L" / FPS " << fps;
    m_window.SetTitle(title.str());
}
