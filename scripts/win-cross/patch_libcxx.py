#!/usr/bin/env python3
# Патчи libc++ zig для кросс-сборки под x86_64-windows-msvc (ABI-совместимость
# с MSVC STL, которой собраны Qt DLL):
#  1. __config: убрать `inline namespace _LIBCPP_ABI_NAMESPACE` (std::__1),
#     чтобы mangle std::X совпадал с MSVC STL (std:: без __1).
#  2. ratio: `class ratio` -> `struct ratio` (MSVC mangle: struct=U, class=V).
#  3. __locale_dir/support/windows.h: объявить _strtold_l (mingw его не даёт).
#  4. zig lib/include/intrin.h: undef _setjmp перед объявлением (mingw setjmp.h
#     делает из него макрос с 2 аргументами).
import os

LIBCXX = 'zig/ziglang/lib/libcxx'
ZIGLIB = 'zig/ziglang/lib'

def patch(path, marker, old, new, what):
    p = os.path.join(os.getcwd(), path)
    s = open(p, encoding='utf-8', errors='replace').read()
    if marker in s:
        print(f'[skip] {what}: уже пропатчен')
        return
    assert old in s, f'{what}: якорь не найден в {p}'
    open(p, 'w').write(s.replace(old, new, 1))
    print(f'[ok]   {what}')

# 1
patch(os.path.join(LIBCXX, 'include/__config'),
      '/* [cross] без ABI-namespace',
      '#  define _LIBCPP_BEGIN_NAMESPACE_STD _LIBCPP_BEGIN_UNVERSIONED_NAMESPACE_STD inline namespace _LIBCPP_ABI_NAMESPACE {',
      '#  define _LIBCPP_BEGIN_NAMESPACE_STD _LIBCPP_BEGIN_UNVERSIONED_NAMESPACE_STD /* [cross] без ABI-namespace __1: std::X совпадает с MSVC STL (Qt DLL) */',
      '__config BEGIN')
patch(os.path.join(LIBCXX, 'include/__config'),
      '/* [cross] без ABI-namespace __1 */',
      '#  define _LIBCPP_END_NAMESPACE_STD } _LIBCPP_END_UNVERSIONED_NAMESPACE_STD',
      '#  define _LIBCPP_END_NAMESPACE_STD _LIBCPP_END_UNVERSIONED_NAMESPACE_STD /* [cross] без ABI-namespace __1 */',
      '__config END')
# 2
patch(os.path.join(LIBCXX, 'include/ratio'),
      'struct ratio {',
      'template <intmax_t N, intmax_t D = 1>\nclass ratio',
      'template <intmax_t N, intmax_t D = 1>\nstruct ratio',
      'ratio #1')
patch(os.path.join(LIBCXX, 'include/ratio'),
      'struct ratio {',
      'template <intmax_t _Num, intmax_t _Den = 1>\nclass ratio {',
      'template <intmax_t _Num, intmax_t _Den = 1>\nstruct ratio {',
      'ratio #2')
# 3
patch(os.path.join(LIBCXX, 'include/__locale_dir/support/windows.h'),
      '[cross-build] mingw-заголовки не объявляют _strtold_l',
      '''inline _LIBCPP_HIDE_FROM_ABI long double __strtold(const char* __nptr, char** __endptr, __locale_t __loc) {
  return ::_strtold_l(__nptr, __endptr, __loc);
}''',
      '''// [cross-build] mingw-заголовки не объявляют _strtold_l (есть в ucrtbase.dll)
extern "C" long double _strtold_l(const char* _Str, char** _EndPtr, ::_locale_t _Locale);
inline _LIBCPP_HIDE_FROM_ABI long double __strtold(const char* __nptr, char** __endptr, __locale_t __loc) {
  return _strtold_l(__nptr, __endptr, __loc);
}''',
      'windows.h _strtold_l')
# 4
patch(os.path.join(ZIGLIB, 'include/intrin.h'),
      '// [cross] mingw setjmp.h определяет _setjmp',
      '''#if __STDC_HOSTED__
int __cdecl _setjmp(jmp_buf);
#endif''',
      '''#if __STDC_HOSTED__
// [cross] mingw setjmp.h определяет _setjmp как макрос на __intrinsic_setjmpex
// (с 2 параметрами) — снимаем макрос, чтобы объявить настоящую _setjmp.
#  undef _setjmp
int __cdecl _setjmp(jmp_buf);
#endif''',
      'intrin.h _setjmp')

print('libcxx пропатчен')
