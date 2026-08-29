#!/usr/bin/env python3
"""Генерирует слой перенаправляющих заголовков Qt (аналог syncqt) из исходников qtbase.

Используется только для сборки в песочнице, где нет системного Qt SDK.
Заголовки из git-дерева qtbase ссылаются на реальные файлы через относительные пути,
а отсутствующие сгенерированные заголовки (qconfig.h, *exports.h) заменяются стабами.
"""
import os, sys, re, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QTBASE = os.path.join(ROOT, 'build', 'qtbase')
HEADERS = os.path.join(ROOT, 'build', 'qt6', 'headers')

MODULES = {
    'corelib': 'QtCore',
    'gui': 'QtGui',
    'widgets': 'QtWidgets',
}

SKIP_DIRS = {'doc', 'examples', 'private', '3rdparty', 'unix', 'win', 'wasm', 'android', 'qnx', 'integrity', 'vxworks'}

def main():
    if os.path.exists(HEADERS):
        shutil.rmtree(HEADERS)

    count = 0
    for srcmod, qtmod in MODULES.items():
        srcroot = os.path.join(QTBASE, 'src', srcmod)
        outroot = os.path.join(HEADERS, qtmod)
        for dirpath, dirnames, filenames in os.walk(srcroot):
            dirnames[:] = [d for d in dirnames if d.lower() not in SKIP_DIRS]
            for fn in filenames:
                if not fn.endswith('.h'):
                    continue
                src = os.path.join(dirpath, fn)
                out = os.path.join(outroot, fn)  # плоская структура, как в Qt SDK
                os.makedirs(outroot, exist_ok=True)
                rel_to_qtbase = os.path.relpath(src, os.path.dirname(out))
                body = f'// forwarding header (generated)\n#pragma once\n#include "{rel_to_qtbase}"\n'
                with open(out, 'w') as f:
                    f.write(body)
                count += 1
                # заголовок с именем класса: qapplication.h -> QApplication
                with open(src, 'r', errors='ignore') as fh:
                    content = fh.read()
                # только определения классов с телом { ... }, без прямых объявлений
                # пропускаем макросы-атрибуты (Q_WIDGETS_EXPORT, QT_DEPRECATED_X(...) и т.п.)
                class_re = re.compile(
                    r'\bclass\s+(?:(?:Q[A-Z0-9]*(?:_[A-Z0-9_]+)+(?:\([^)]*\))?)\s+)*(Q[A-Za-z0-9_]+)\b')
                for m in class_re.finditer(content):
                    tail = content[m.end():m.end() + 400]
                    s_brace = tail.find('{')
                    s_semi = tail.find(';')
                    if s_brace == -1:
                        continue
                    if s_semi != -1 and s_semi < s_brace:
                        continue  # прямое объявление
                    alias = os.path.join(outroot, m.group(1))
                    if not os.path.exists(alias):
                        with open(alias, 'w') as f:
                            f.write(body)
                        count += 1
        print(f'{qtmod}: {count} headers (cumulative)')
    print('Total headers:', count)

    # --- стабы сгенерированных заголовков ---
    # собираем все имена фич и SINCE-макросы из заголовков qtbase
    feature_names = set()
    since_macros = set()
    for srcmod in MODULES:
        srcroot = os.path.join(QTBASE, 'src', srcmod)
        for dirpath, dirnames, filenames in os.walk(srcroot):
            dirnames[:] = [d for d in dirnames if d.lower() not in SKIP_DIRS]
            for fn in filenames:
                if not fn.endswith('.h'):
                    continue
                with open(os.path.join(dirpath, fn), 'r', errors='ignore') as fh:
                    content = fh.read()
                feature_names.update(re.findall(r'QT_CONFIG\(([a-z0-9_]+)\)', content))
                feature_names.update(re.findall(r'QT_FEATURE_([a-z0-9_]+)', content))
                since_macros.update(re.findall(r'\b(QT_[A-Z_]*_SINCE)\s*\(', content))
    qconfig_body = '// stub qconfig.h (consumer build, все фичи включены)\n'
    qconfig_body += '#pragma once\n#define QT_VERSION_MAJOR 6\n#define QT_VERSION_MINOR 11\n#define QT_VERSION_PATCH 2\n'
    for fn in sorted(feature_names):
        qconfig_body += f'#define QT_FEATURE_{fn} 1\n'

    # макросы, которые уже определены в qtdeprecationmarkers.h — не генерируем
    markers_path = os.path.join(QTBASE, 'src', 'corelib', 'global', 'qtdeprecationmarkers.h')
    with open(markers_path, 'r', errors='ignore') as fh:
        markers_content = fh.read()
    markers_macros = set(re.findall(r'^\s*#\s*define\s+([A-Z_]+)\b', markers_content, re.M))
    since_macros = since_macros - markers_macros - {'QT_DEPRECATED_SINCE'}

    stubs = {
        os.path.join(HEADERS, 'QtCore', 'qconfig.h'): qconfig_body,
        os.path.join(HEADERS, 'QtCore', 'qtcore-config.h'):
            '// stub qtcore-config.h\n#pragma once\n',
        os.path.join(HEADERS, 'QtCore', 'qtcoreexports.h'):
            '#pragma once\n#define Q_CORE_EXPORT\n',
        os.path.join(HEADERS, 'QtGui', 'qtguiexports.h'):
            '#pragma once\n#define Q_GUI_EXPORT\n',
        os.path.join(HEADERS, 'QtGui', 'qtgui-config.h'):
            '// stub qtgui-config.h\n#pragma once\n',
        os.path.join(HEADERS, 'QtWidgets', 'qtwidgets-config.h'):
            '// stub qtwidgets-config.h\n#pragma once\n',
        os.path.join(HEADERS, 'QtWidgets', 'qtwidgetsexports.h'):
            '#pragma once\n#define Q_WIDGETS_EXPORT\n',
        os.path.join(HEADERS, 'QtCore', 'qtdeprecationdefinitions.h'):
            '// stub qtdeprecationdefinitions.h (сгенерирован из .in)\n'
            '#pragma once\n#include <QtCore/qcompilerdetection.h>\n'
            '#ifndef QT_DISABLE_DEPRECATED_UP_TO\n'
            '#  ifdef QT_DISABLE_DEPRECATED_BEFORE\n'
            '#    define QT_DISABLE_DEPRECATED_UP_TO QT_DISABLE_DEPRECATED_BEFORE\n'
            '#  else\n'
            '#    define QT_DISABLE_DEPRECATED_UP_TO 0x060B02\n'
            '#  endif\n#endif\n'
            '#ifndef QT_WARN_DEPRECATED_UP_TO\n'
            '#  ifdef QT_DEPRECATED_WARNINGS_SINCE\n'
            '#    define QT_WARN_DEPRECATED_UP_TO QT_DEPRECATED_WARNINGS_SINCE\n'
            '#  else\n'
            '#    define QT_WARN_DEPRECATED_UP_TO 0x060B02\n'
            '#  endif\n#endif\n'
            '#ifndef QT_DEPRECATED\n'
            '#  define QT_DEPRECATED Q_DECL_DEPRECATED\n'
            '#endif\n'
            '#ifdef QT_DEPRECATED\n'
            '#  define QT_DEPRECATED_SINCE(major, minor) (QT_VERSION_CHECK(major, minor, 0) > QT_DISABLE_DEPRECATED_UP_TO)\n'
            '#else\n'
            '#  define QT_DEPRECATED_SINCE(major, minor) 0\n'
            '#endif\n'
            ''.join(
                f'#define {m}(major, minor) 0\n'
                if 'REMOVED' in m else
                f'#define {m}(major, minor) (QT_VERSION_CHECK(major, minor, 0) <= QT_VERSION_CHECK(QT_VERSION_MAJOR, QT_VERSION_MINOR, QT_VERSION_PATCH))\n'
                if 'INLINE_IMPL' in m else
                f'#define {m}(major, minor) constexpr\n'
                if 'CONSTEXPR_INLINE_SINCE' in m else
                f'#define {m}(major, minor) inline\n'
                if 'INLINE_SINCE' in m else
                f'#define {m}(major, minor) (QT_VERSION_CHECK(major, minor, 0) > QT_DISABLE_DEPRECATED_UP_TO)\n'
                for m in sorted(since_macros)
            ),
    }
    for path, content in stubs.items():
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            f.write(content)
        print('stub:', os.path.relpath(path, HEADERS))

if __name__ == '__main__':
    main()
