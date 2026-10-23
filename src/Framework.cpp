#include "Framework.hpp"

Framework::Framework(HINSTANCE instance) : m_window(instance, 1600, 900, L"Dx12GraphicsLab"), m_input(m_window.Handle()), m_camera(static_cast<float>(m_window.Width()) / m_window.Height()), m_renderer(m_window.Handle(), m_window.Width(), m_window.Height())
{
    m_window.SetMessageHandler([this](UINT message, WPARAM wParam, LPARAM lParam) {
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
        if (m_input.WasPressed('T')) m_textureAnimationEnabled = !m_textureAnimationEnabled;
        float density = m_renderer.AtmosphereDensity();
        float pollution = m_renderer.AtmospherePollution();
        if (m_input.WasPressed(VK_UP)) pollution += 0.05f;
        if (m_input.WasPressed(VK_DOWN)) pollution -= 0.05f;
        if (m_input.WasPressed(VK_RIGHT)) density += 0.1f;
        if (m_input.WasPressed(VK_LEFT)) density -= 0.1f;
        m_renderer.SetAtmosphere(density, pollution);

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

    std::wstringstream title;
    title << L"KG is pain " << L" / Vosduh " << std::lround(m_renderer.AtmosphereDensity() * 100.0f) << L" / Pil " << std::lround(m_renderer.AtmospherePollution() * 100.0f);
    m_window.SetTitle(title.str());
}
