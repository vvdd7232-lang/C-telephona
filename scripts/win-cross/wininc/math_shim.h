// Шим для кросс-компиляции под Windows.
//
// 1. mingw'шный math.h не определяет классификаторы FP_*, когда компилятор
//    сообщает о себе __GNUC__ (мы это форсируем для mingw-заголовков).
//    Задаём сами значения, совпадающие с <cmath> (0..4).
// 2. libc++ (libcxx/math.h) после включения C-math.h делает #undef
//    fpclassify/signbit/isfinite/isinf/isnan/... и ждёт их как ГЛОБАЛЬНЫЕ
//    ФУНКЦИИ (cmath: using ::isinf _LIBCPP_USING_IF_EXISTS;), а mingw
//    math.h объявляет их только макросами. Объявляем функции сами через
//    __builtin_* (в clang они всегда доступны).
#ifndef ENDERFORGE_MATH_SHIM_H
#define ENDERFORGE_MATH_SHIM_H

#ifndef FP_NAN
#  define FP_NAN       0
#  define FP_INFINITE  1
#  define FP_ZERO      2
#  define FP_SUBNORMAL 3
#  define FP_NORMAL    4
#endif

#include <math.h>

// --- глобальные функции вместо макросов C-math.h ---
inline int fpclassify(float x)       { return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x); }
inline int fpclassify(double x)      { return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x); }
inline int fpclassify(long double x) { return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x); }
inline int isfinite(float x)         { return __builtin_isfinite(x); }
inline int isfinite(double x)        { return __builtin_isfinite(x); }
inline int isfinite(long double x)   { return __builtin_isfinite(x); }
inline int isinf(float x)            { return __builtin_isinf(x); }
inline int isinf(double x)           { return __builtin_isinf(x); }
inline int isinf(long double x)      { return __builtin_isinf(x); }
inline int isnan(float x)            { return __builtin_isnan(x); }
inline int isnan(double x)           { return __builtin_isnan(x); }
inline int isnan(long double x)      { return __builtin_isnan(x); }
inline int isnormal(float x)         { return __builtin_isnormal(x); }
inline int isnormal(double x)        { return __builtin_isnormal(x); }
inline int isnormal(long double x)   { return __builtin_isnormal(x); }
inline int signbit(float x)          { return __builtin_signbit(x); }
inline int signbit(double x)         { return __builtin_signbit(x); }
inline int signbit(long double x)    { return __builtin_signbit(x); }
inline int isgreater(float x, float y)       { return __builtin_isgreater(x, y); }
inline int isgreater(double x, double y)     { return __builtin_isgreater(x, y); }
inline int isgreater(long double x, long double y) { return __builtin_isgreater(x, y); }
inline int isgreaterequal(float x, float y)       { return __builtin_isgreaterequal(x, y); }
inline int isgreaterequal(double x, double y)     { return __builtin_isgreaterequal(x, y); }
inline int isgreaterequal(long double x, long double y) { return __builtin_isgreaterequal(x, y); }
inline int isless(float x, float y)       { return __builtin_isless(x, y); }
inline int isless(double x, double y)     { return __builtin_isless(x, y); }
inline int isless(long double x, long double y) { return __builtin_isless(x, y); }
inline int islessequal(float x, float y)       { return __builtin_islessequal(x, y); }
inline int islessequal(double x, double y)     { return __builtin_islessequal(x, y); }
inline int islessequal(long double x, long double y) { return __builtin_islessequal(x, y); }
inline int islessgreater(float x, float y)       { return __builtin_islessgreater(x, y); }
inline int islessgreater(double x, double y)     { return __builtin_islessgreater(x, y); }
inline int islessgreater(long double x, long double y) { return __builtin_islessgreater(x, y); }
inline int isunordered(float x, float y)       { return __builtin_isunordered(x, y); }
inline int isunordered(double x, double y)     { return __builtin_isunordered(x, y); }
inline int isunordered(long double x, long double y) { return __builtin_isunordered(x, y); }

#endif // ENDERFORGE_MATH_SHIM_H
