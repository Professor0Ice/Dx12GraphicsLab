#include "Framework.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    const bool smokeTest = wcsstr(GetCommandLineW(), L"--smoke-test") != nullptr;
    
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
