#include "Framework.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    const bool smokeTest = wcsstr(GetCommandLineW(), L"--smoke-test") != nullptr;
    try
    {
        if (smokeTest)
        {
            Window hiddenWindow(instance, 640, 360, L"DX12 smoke test", false);
            Camera camera(640.0f / 360.0f);
            RenderingSystem renderer(hiddenWindow.Handle(), 640, 360);
            renderer.Render(camera, 0.0f);
            return EXIT_SUCCESS;
        }
        Framework application(instance);
        return application.Run();
    }
    catch (const std::exception& error)
    {
        if (smokeTest)
        {
            std::ofstream log("smoke-error.txt", std::ios::trunc);
            log << error.what();
            return EXIT_FAILURE;
        }
        MessageBoxA(nullptr, error.what(), "DX12 Laboratory - fatal error", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}
