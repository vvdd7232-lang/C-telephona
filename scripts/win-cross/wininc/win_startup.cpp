// Минимальный CRT-стартап для x86_64-windows-msvc при сборке с -nostdlib.
// lld-link для подсистемы windows ищет WinMainCRTStartup; вызываем main().
extern "C" int main(int, char **);

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned int uExitCode);

// Реализован в crt_stubs.cpp (atexit через onexit-таблицы ucrtbase).
extern "C" void run_atexit_handlers();

extern "C" void __cdecl WinMainCRTStartup(void);
extern "C" void __cdecl wWinMainCRTStartup(void);

extern "C" void __cdecl WinMainCRTStartup(void)
{
    int argc = 1;
    static char progName[] = "EnderForge";
    char *argv[2] = { progName, nullptr };
    const int code = main(argc, argv);
    run_atexit_handlers();
    ExitProcess(static_cast<unsigned int>(code));
}

// lld-link для подсистемы windows может выбирать wWinMainCRTStartup
extern "C" void __cdecl wWinMainCRTStartup(void)
{
    WinMainCRTStartup();
}
