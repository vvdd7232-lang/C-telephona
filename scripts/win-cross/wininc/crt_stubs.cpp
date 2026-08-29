// Стабы CRT-функций, которых нет ни в одной системной CRT DLL как экспортов,
// и vftable std::type_info. Сборка: кросс-компиляция для x86_64-windows-msvc.
#include <cstdio>
#include <cstdarg>
#include <cstdint>

// ---------------------------------------------------------------- vfprintf
extern "C" __declspec(dllimport) int __cdecl __stdio_common_vsnprintf_s(
    unsigned __int64 _Options, char *_Buffer, size_t _BufferCount,
    size_t _MaxCount, char const *_Format, _locale_t _Locale, va_list _ArgList);

// libc++ (verbose_abort) вызывает vfprintf для сообщений об ошибках;
// vfprintf не экспортируется из ucrtbase.dll (там только __stdio_common_*).
extern "C" int __cdecl vfprintf(FILE *_Stream, char const *_Format, va_list _ArgList)
{
    char buf[2048];
    va_list ap2;
    va_copy(ap2, _ArgList);
    int n = __stdio_common_vsnprintf_s(0, buf, sizeof buf, (size_t)-1, _Format, nullptr, ap2);
    va_end(ap2);
    if (n < 0)
        return n;
    fwrite(buf, 1, (size_t)n, _Stream);
    return n;
}

// ------------------------------------------------------------- type_info vftable
// vftable std::type_info (`??_7type_info@@6B@`). В MSVC-мире его берут из
// статического vcruntime.lib; ни одна CRT DLL (vcruntime140/msvcp140) его не
// экспортирует, поэтому определяем сами: массив из одного указателя на
// виртуальный деструктор std::type_info (определён в libcxx/typeinfo.cpp).
extern "C" void type_info_dtor_thunk() __asm__("??1type_info@std@@UEAA@XZ");
extern "C" void *const type_info_vftable __asm__("??_7type_info@@6B@") =
    (void *)&type_info_dtor_thunk;

// ------------------------------------------------------------------- _fltused
// Символ-маркер инициализации FPU (обычно его добавляет CRT).
extern "C" int _fltused = 0;

// ---------------------------------------------------------------------- atexit
// В MSVC-мире atexit/_onexit не экспортируются из DLL: их реализуют
// статически (в vcruntime.lib) поверх onexit-таблиц ucrtbase.dll:
//   _initialize_onexit_table / _register_onexit_function / _execute_onexit_table
// (все три — настоящие экспорты ucrtbase.dll).
struct OnexitTable {
    void (**first)(void);
    void (**last)(void);
    void (**end)(void);
};

extern "C" __declspec(dllimport) int __cdecl _initialize_onexit_table(OnexitTable *table);
extern "C" __declspec(dllimport) int __cdecl _register_onexit_function(OnexitTable *table,
                                                                       int (__cdecl *func)(void));
extern "C" __declspec(dllimport) int __cdecl _execute_onexit_table(OnexitTable *table);

namespace {
OnexitTable g_onexitTable;
bool g_onexitInit = false;
}

extern "C" int __cdecl atexit(void (__cdecl *func)(void))
{
    if (!g_onexitInit) {
        if (_initialize_onexit_table(&g_onexitTable) != 0)
            return -1;
        g_onexitInit = true;
    }
    return _register_onexit_function(&g_onexitTable,
                                     reinterpret_cast<int (__cdecl *)(void)>(func));
}

extern "C" void run_atexit_handlers()
{
    if (g_onexitInit)
        _execute_onexit_table(&g_onexitTable);
}
