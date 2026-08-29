#!/usr/bin/env python3
# Воссоздаёт qt6/headers для кросс-сборки:
#  - копирует публичные заголовки qtbase по модулям
#  - создаёт forwarding-файлы <QClass> (их при сборке Qt генерирует cmake)
#  - воссоздаёт сгенерированные заголовки: qconfig.h, *-config.h,
#    qtdeprecationdefinitions.h (+ недостающие макросы), exports-заголовки
import re, glob, os, shutil

BASE = os.path.abspath(os.path.dirname(__file__))  # scripts/win-cross
BUILD = os.path.abspath(os.path.join(BASE, '..', '..', 'build'))
SRC = os.path.abspath(os.path.join(BASE, '..', '..', 'src'))
MODS = {'corelib':'QtCore','gui':'QtGui','network':'QtNetwork','widgets':'QtWidgets'}
H = f'{BUILD}/qt6/headers'

def read(p):
    try:
        return open(p, errors='replace').read()
    except Exception:
        return ''

# 1) копируем заголовки qtbase (без приватных _p.h)
for src, mod in MODS.items():
    d = f'{H}/{mod}'
    os.makedirs(d, exist_ok=True)
    for h in glob.glob(f'{BUILD}/qtbase/src/{src}/**/*.h', recursive=True):
        b = os.path.basename(h)
        if b.endswith('_p.h') or b.endswith('.qdoc.h'):
            continue
        shutil.copy(h, os.path.join(d, b))
    print(mod, len(os.listdir(d)))

# 2) forwarding <Mod/QClass>
need = set()
for d in MODS.values():
    for f in glob.glob(f'{H}/{d}/*.h'):
        for m in re.finditer(r'#include <(QtCore|QtGui|QtNetwork|QtWidgets)/([A-Z][A-Za-z0-9_]+)>', read(f)):
            need.add((m.group(1), m.group(2)))
for f in glob.glob(f'{SRC}/*.cpp') + glob.glob(f'{SRC}/*.h'):
    for m in re.finditer(r'#include <(QtCore|QtGui|QtNetwork|QtWidgets)/([A-Z][A-Za-z0-9_]+)>', read(f)):
        need.add((m.group(1), m.group(2)))
    for m in re.finditer(r'#include\s*<Q([A-Za-z0-9_]+)>', read(f)):
        need.add(('', 'Q' + m.group(1)))

def find_low(low):
    for m2 in MODS.values():
        c = f'{H}/{m2}/q{low}.h'
        if os.path.exists(c):
            return c
    return None

def find_by_class(cls):
    for m2 in MODS.values():
        for h in glob.glob(f'{H}/{m2}/q*.h'):
            if re.search(r'\b(class|struct)\s+(?:[A-Za-z_][A-Za-z0-9_]*_EXPORT\s+)*' + cls + r'\b', read(h)):
                return h
    return None

created = 0
missing = []
for mod, cls in sorted(need):
    out = f'{H}/{mod or "QtCore"}/{cls}'
    if os.path.exists(out):
        continue
    low = cls[1:].lower()
    cand = f'{H}/{mod}/q{low}.h' if mod else None
    if not cand or not os.path.exists(cand):
        cand = find_low(low)
    if not cand:
        cand = find_by_class(cls)
    if cand:
        open(out, 'w').write(f'#include "{os.path.basename(cand)}"\n')
        created += 1
    else:
        missing.append((mod, cls))
print('forwarding создано:', created, '| нет исходников:', missing)

# 3) qconfig.h
open(f'{H}/QtCore/qconfig.h', 'w').write('''// [cross] сгенерированный при сборке Qt заголовок; воссоздан вручную.
#ifndef QT_QCONFIG_H
#define QT_QCONFIG_H
#define QT_VERSION_MAJOR 6
#define QT_VERSION_MINOR 11
#define QT_VERSION_PATCH 2
#define QT_VERSION_STR "6.11.2"
#define QT_VERSION_CHECK(major, minor, patch) ((major<<16)|(minor<<8)|(patch))
#define QT_VERSION QT_VERSION_CHECK(QT_VERSION_MAJOR, QT_VERSION_MINOR, QT_VERSION_PATCH)
#endif
''')

# 4) *-config.h (QT_FEATURE_*)
feats = set()
for d in MODS.values():
    for f in glob.glob(f'{H}/{d}/*.h'):
        s = read(f)
        feats.update(re.findall(r'QT_CONFIG\(([a-z_0-9]+)\)', s))
        feats.update(re.findall(r'QT_REQUIRE_CONFIG\(([a-z_0-9]+)\)', s))
for d, cfg in [('QtCore','qtcore-config.h'),('QtGui','qtgui-config.h'),
               ('QtNetwork','qtnetwork-config.h'),('QtWidgets','qtwidgets-config.h')]:
    lines = [f'// [cross] сгенерированный при сборке Qt заголовок (воссоздан)',
             f'#ifndef QT{d.upper()}_CONFIG_H', f'#define QT{d.upper()}_CONFIG_H']
    if d == 'QtCore':
        lines.append('#define QT_CONFIG(feature) QT_FEATURE_##feature')
    for f in sorted(feats):
        lines.append(f'#define QT_FEATURE_{f} 1')
    lines.append('#endif')
    open(f'{H}/{d}/{cfg}', 'w').write('\n'.join(lines) + '\n')
print('фич:', len(feats))

# 5) ранние QT_BEGIN/END_NAMESPACE (цикл через qtdeprecationmarkers.h)
p = f'{H}/QtCore/qtconfigmacros.h'
s = read(p)
anchor = '#define QTCONFIGMACROS_H\n'
add = anchor + '''
// [cross] ранние определения: qtclasshelpermacros.h включается из
// qtdeprecationmarkers.h во время обработки этого файла (циклическая
// зависимость), когда поздние определения ещё не выполнены.
#if !defined(QT_NAMESPACE)
#  define QT_BEGIN_NAMESPACE
#  define QT_END_NAMESPACE
#  define QT_USE_NAMESPACE
#  define QT_PREPEND_NAMESPACE(name) ::name
#  define QT_FORWARD_DECLARE_CLASS(name) class name;
#  define QT_FORWARD_DECLARE_STRUCT(name) struct name;
#endif
'''
if 'ранние определения' not in s:
    open(p, 'w').write(s.replace(anchor, add, 1))
    print('qtconfigmacros.h пропатчен')

# 6) qtdeprecationdefinitions.h + недостающие макросы
tpl = read(f'{BUILD}/qtbase/src/corelib/global/qtdeprecationdefinitions.h.in')
tpl = tpl.replace('@QT_DISABLE_DEPRECATED_UP_TO@', '0x060B02').replace('@QT_WARN_DEPRECATED_UP_TO@', '0x060B02')
blocks = ['''// [cross] QT_DEPRECATED_SINCE/QT_*_REMOVED_SINCE — обычно из
// qtdeprecationmarkers.h, но qlogging.h использует их раньше.
#ifndef QT_DEPRECATED_SINCE
#  ifdef QT_DEPRECATED
#    define QT_DEPRECATED_SINCE(major, minor) (QT_VERSION_CHECK(major, minor, 0) > QT_DISABLE_DEPRECATED_UP_TO)
#  else
#    define QT_DEPRECATED_SINCE(major, minor) 0
#  endif
#endif''']
for n in ['QT_CORE_REMOVED_SINCE','QT_GUI_REMOVED_SINCE','QT_NETWORK_REMOVED_SINCE','QT_WIDGETS_REMOVED_SINCE',
          'QT_CORE_INLINE_SINCE','QT_GUI_INLINE_SINCE','QT_NETWORK_INLINE_SINCE','QT_WIDGETS_INLINE_SINCE']:
    kind = 'inline' if 'INLINE' in n else '(QT_VERSION_CHECK(major, minor, 0) > QT_DISABLE_DEPRECATED_UP_TO)'
    blocks.append(f'#ifndef {n}\n# define {n}(major, minor) {kind}\n#endif')
for n in ['QT_CORE_CONSTEXPR_INLINE_SINCE']:
    blocks.append(f'#ifndef {n}\n# define {n}(major, minor) constexpr\n#endif')
for n in ['QT_CORE_INLINE_IMPL_SINCE','QT_GUI_INLINE_IMPL_SINCE','QT_NETWORK_INLINE_IMPL_SINCE']:
    blocks.append(f'#ifndef {n}\n# define {n}(major, minor) inline\n#endif')
vers = set()
for d in MODS.values():
    for f in glob.glob(f'{H}/{d}/*.h'):
        s = read(f)
        vers.update(re.findall(r'QT_DEPRECATED_VERSION_X_(\d+_\d+)', s))
        vers.update(re.findall(r'QT_DEPRECATED_VERSION_(\d+_\d+)\b', s))
for v in sorted(vers):
    blocks.append(f'#ifndef QT_DEPRECATED_VERSION_X_{v}\n# define QT_DEPRECATED_VERSION_X_{v}\n#endif')
    blocks.append(f'#ifndef QT_DEPRECATED_VERSION_{v}\n# define QT_DEPRECATED_VERSION_{v}\n#endif')
extra = '\n'.join(blocks) + '\n'
if 'QT_CORE_REMOVED_SINCE' not in tpl:
    tpl = tpl.replace('#endif // QTDEPRECATIONDEFINITIONS_H', extra + '#endif // QTDEPRECATIONDEFINITIONS_H')
open(f'{H}/QtCore/qtdeprecationdefinitions.h', 'w').write(tpl)
print('qtdeprecationdefinitions.h готов')

# 7) exports-заголовки
for name, mod, exp in [('qtcoreexports.h','QtCore','Q_CORE_EXPORT'),
                       ('qtguiexports.h','QtGui','Q_GUI_EXPORT'),
                       ('qtnetworkexports.h','QtNetwork','Q_NETWORK_EXPORT'),
                       ('qtwidgetsexports.h','QtWidgets','Q_WIDGETS_EXPORT')]:
    p = f'{H}/{mod}/{name}'
    if not os.path.exists(p):
        open(p, 'w').write(f'#ifndef {name.upper().replace(".","_")}\n#define {name.upper().replace(".","_")}\n#define {exp} __declspec(dllimport)\n#endif\n')
print('exports-заголовки готовы')
