#!/usr/bin/env bash
# ============================================================
# Сборка EnderForge в песочнице/CI без системного Qt SDK.
#
# Что делает:
#   1. Скачивает Qt 6 (библиотеки) из колеса PySide6-Essentials (PyPI)
#   2. Клонирует заголовки qtbase из GitHub (та же версия)
#   3. Генерирует слой заголовков (scripts/gen_qt_headers.py)
#   4. Собирает заглушки недостающих системных библиотек
#   5. Компилирует лаунчер и делает скриншот (docs/preview.png)
#
# На реальной машине с установленным Qt используйте CMake:
#   cmake -B build && cmake --build build
# ============================================================
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/build"
QT6_VER="6.11.2"

mkdir -p "$BUILD"
cd "$BUILD"

echo "==> [1/5] Скачиваем Qt $QT6_VER (PySide6-Essentials, manylinux)"
mkdir -p qtpkg
pip3 download PySide6-Essentials --no-deps --only-binary=:all: -d qtpkg
WHEEL="$(ls qtpkg/*.whl 2>/dev/null | head -1 || true)"
if [ -z "$WHEEL" ]; then
    echo "Ошибка: не удалось скачать PySide6-Essentials (нужен доступ к PyPI)" >&2
    exit 1
fi
python3 -m zipfile -e "$WHEEL" qt6
rm -rf qtpkg

# dev-симлинки для линковки
cd "$BUILD/qt6/PySide6/Qt/lib"
for f in libQt6Core libQt6Gui libQt6Widgets libQt6Network; do
    [ -e "$f.so" ] || ln -s "$f.so.6" "$f.so"
done

echo "==> [2/5] Заголовки qtbase (git, tag v$QT6_VER)"
cd "$BUILD"
[ -d qtbase/.git ] || \
    git clone --depth 1 --branch "v$QT6_VER" --filter=blob:none --sparse \
        https://github.com/qt/qtbase.git qtbase 2>/dev/null
cd qtbase
git sparse-checkout set include src/corelib src/gui src/widgets src/network 2>/dev/null || true

echo "==> [3/5] Генерация заголовков"
cd "$BUILD"
python3 "$REPO/scripts/gen_qt_headers.py"

echo "==> [4/5] Заглушки недостающих системных библиотек"
mkdir -p runtime-libs
python3 - "$BUILD" <<'EOF'
import subprocess, re, os, sys
build = sys.argv[1]
QT = os.path.join(build, 'qt6', 'PySide6', 'Qt')
def undef(path):
    out = subprocess.check_output(['readelf','-Ws',path], text=True)
    syms = set()
    for line in out.splitlines():
        m = re.match(r'\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+\S+\s+GLOBAL\s+\S+\s+UND\s+(\S+)', line)
        if m: syms.add(m.group(1).split('@')[0])
    return syms
files = []
for root, dirs, names in os.walk(QT):
    for n in names:
        if n.endswith('.so') or re.search(r'\.so\.\d+$', n):
            files.append(os.path.join(root, n))
allund = set()
for f in files:
    allund |= undef(f)
def gen(pat, outname):
    syms = sorted({s for s in allund if pat.fullmatch(s)})
    open(os.path.join(build, 'runtime-libs', outname + '.c'), 'w').write(
        ''.join(f'int {s}(void) {{ return 0; }}\n' for s in syms))
    return syms
egl = gen(re.compile(r'^egl[A-Za-z0-9_]*$'), 'libEGL.so.1')
gl = gen(re.compile(r'^(gl|glX)[A-Za-z0-9_]*$'), 'libGL.so.1')
xkb = gen(re.compile(r'^xkb_[A-Za-z0-9_]*$'), 'libxkbcommon.so.0')
dbus = gen(re.compile(r'^dbus_[A-Za-z0-9_]*$'), 'libdbus-1.so.3')
icu = sorted(s for s in allund if s.endswith('_73'))
body = '#include <stdint.h>\ntypedef uint16_t UChar;\ntypedef int32_t UErrorCode;\n'
body += 'static int32_t ascii_case(UChar *d, int32_t cap, const UChar *s, int32_t len, int lower) {\n'
body += '  int32_t n = len < 0 ? 0 : len, i;\n'
body += '  for (i = 0; i < n && i < cap; ++i) {\n    UChar c = s[i];\n'
body += '    if (lower) { if (c >= 65 && c <= 90) c += 32; }\n'
body += '    else { if (c >= 97 && c <= 122) c -= 32; }\n    d[i] = c;\n  }\n  return n;\n}\n'
body += 'int32_t u_strToLower_73(UChar *d, int32_t dc, const UChar *s, int32_t sl, const char *l, UErrorCode *e) { (void)l;(void)e; return ascii_case(d,dc,s,sl,1); }\n'
body += 'int32_t u_strToUpper_73(UChar *d, int32_t dc, const UChar *s, int32_t sl, const char *l, UErrorCode *e) { (void)l;(void)e; return ascii_case(d,dc,s,sl,0); }\n'
for s in icu:
    if s not in ('u_strToLower_73', 'u_strToUpper_73'):
        body += f'int {s}(void) {{ return 0; }}\n'
open(os.path.join(build, 'runtime-libs', 'libicuuc.so.73.c'), 'w').write(body)
open(os.path.join(build, 'runtime-libs', 'libicui18n.so.73.c'), 'w').write('int qt_stub(void){return 0;}\n')
open(os.path.join(build, 'runtime-libs', 'libicudata.so.73.c'), 'w').write('int qt_stub(void){return 0;}\n')
print(f'   ICU stub: {len(icu)} символов, GL: {len(gl)}, EGL: {len(egl)}, xkb: {len(xkb)}, dbus: {len(dbus)}')
EOF
cd "$BUILD/runtime-libs"
for entry in "libEGL.so.1:" "libGL.so.1:" "libxkbcommon.so.0:V_0.5.0" "libdbus-1.so.3:LIBDBUS_1_3" "libicuuc.so.73:" "libicui18n.so.73:" "libicudata.so.73:"; do
    f="${entry%%:*}"; ver="${entry##*:}"
    extra=()
    if [ -n "$ver" ]; then
        printf '%s { global: *; };\n' "$ver" > "$f.map"
        extra=(-Wl,--version-script="$f.map")
    fi
    gcc -shared -fPIC -O1 -o "$f" "$f.c" "${extra[@]}" -Wl,-soname,"$f"
done

echo "==> [5/5] Компиляция лаунчера"
mkdir -p "$BUILD/bin"
g++ -std=c++17 -fPIC -O2 \
    -DENDERFORGE_RESOURCE_DIR="\"$REPO/resources\"" \
    "$REPO/src/main.cpp" "$REPO/src/MainWindow.cpp" "$REPO/src/VersionManager.cpp" "$REPO/src/pixelart.cpp" \
    -I "$BUILD/qt6/headers" \
    -I "$BUILD/qt6/headers/QtCore" -I "$BUILD/qt6/headers/QtGui" -I "$BUILD/qt6/headers/QtWidgets" -I "$BUILD/qt6/headers/QtNetwork" \
    -L "$BUILD/qt6/PySide6/Qt/lib" -lQt6Widgets -lQt6Gui -lQt6Core -lQt6Network \
    -Wl,--allow-shlib-undefined -Wl,-rpath,'$ORIGIN/../qt6/PySide6/Qt/lib' \
    -o "$BUILD/bin/enderforge"

echo "==> [5.5/5] Зеркало манифеста Mojang (для скриншота со списком версий)"
cd "$BUILD"
if [ ! -f "$BUILD/piston-meta/mc/game/version_manifest_v2.json" ]; then
    git clone --depth 1 --filter=blob:none --sparse \
        https://github.com/theofficialgman/piston-meta-arm64.git piston-meta 2>/dev/null || true
    cd "$BUILD/piston-meta"
    git sparse-checkout set mc/game 2>/dev/null || true
    cd "$BUILD"
fi

echo "==> Готово: $BUILD/bin/enderforge"
echo "Скриншот (со списком версий из зеркала манифеста):"
mkdir -p "$REPO/docs"
cd "$REPO"
# Зеркало актуального манифеста Mojang (для офлайн-теста; сам лаунчер качает с mojang.com)
MANIFEST="$BUILD/piston-meta/mc/game/version_manifest_v2.json"
if [ ! -f "$MANIFEST" ]; then
    echo "Внимание: зеркало манифеста не найдено, скриншот без списка версий"
    MANIFEST=""
fi
ARGS=(--screenshot "$REPO/docs/preview.png")
[ -n "$MANIFEST" ] && ARGS+=(--manifest "$MANIFEST")
LD_LIBRARY_PATH="$BUILD/runtime-libs:$BUILD/qt6/PySide6/Qt/lib" \
QT_QPA_PLATFORM=offscreen \
QT_PLUGIN_PATH="$BUILD/qt6/PySide6/Qt/plugins" \
LC_ALL=C.UTF-8 \
"$BUILD/bin/enderforge" "${ARGS[@]}"
echo "OK: docs/preview.png"
