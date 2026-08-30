#!/usr/bin/env bash
# Полный конвейер кросс-сборки EnderForge под Windows (x86_64-msvc) из Linux.
# Запускать из build/:
#   ../scripts/win-cross/build_win.sh
#
# Этапы: инструменты (zig, qtbase, PySide6-win) -> патчи libcxx -> заголовки Qt
# -> импортные библиотеки -> libcxx.a -> объекты -> линк -> dist + zip.
set -u
# Скрипт лежит в <repo>/scripts/win-cross/ — рабочая папка: <repo>/build
cd "$(dirname "$0")/../../build"      # -> build/
SCRIPTS="$(pwd)/../scripts/win-cross"
ROOT="$(pwd)/.."
ZIG=./zig/ziglang
WININC="zig/ziglang/lib/libc/include/any-windows-any"

echo "==> [1/10] инструменты"
if [ ! -x "$ZIG/zig" ]; then
  echo "  установка ziglang (PyPI)…"
  python3 -m pip install --break-system-packages ziglang >/dev/null 2>&1 || pip3 install --break-system-packages ziglang >/dev/null 2>&1
  rm -rf zig && mkdir -p zig
  ZIGPKG=$(python3 -c "import ziglang,os;print(os.path.dirname(ziglang.__file__))")
  ln -sfn "$ZIGPKG" zig/ziglang
fi
"$ZIG/zig" version

if [ ! -d qtbase ]; then
  echo "  скачивание qtbase 6.11.2…"
  curl -sL -o qtbase.tgz https://codeload.github.com/qt/qtbase/tar.gz/refs/tags/v6.11.2
  tar xzf qtbase.tgz && mv qtbase-6.11.2 qtbase
fi

if [ ! -f winqt/wx/PySide6/Qt6Core.dll ]; then
  echo "  скачивание PySide6-Essentials (win_amd64)…"
  pip3 download --no-deps --only-binary=:all: --platform win_amd64 --python-version 310 \
    --dest /tmp/pydl6 PySide6-Essentials >/dev/null 2>&1
  mkdir -p winqt/wx
  python3 -c "import zipfile; zipfile.ZipFile('$(ls /tmp/pydl6/pyside6_essentials-*.whl)').extractall('winqt/wx/PySide6')"
  if [ -d winqt/wx/PySide6/PySide6 ]; then mv winqt/wx/PySide6/PySide6/* winqt/wx/PySide6/ && rmdir winqt/wx/PySide6/PySide6; fi
fi
ls winqt/wx/PySide6/Qt6Core.dll winqt/wx/PySide6/plugins/platforms/qwindows.dll >/dev/null

echo "==> [2/10] wininc"
rm -rf wininc && cp -r "$SCRIPTS/wininc" wininc

echo "==> [3/10] патчи libcxx"
python3 "$SCRIPTS/patch_libcxx.py"

echo "==> [4/10] заголовки Qt"
python3 "$SCRIPTS/setup_qt_headers.py" 2>&1 | tail -3

echo "==> [5/10] импортные библиотеки"
mkdir -p winsyslibs/defs
export DEF_INCLUDE_PATH="$ROOT/build/zig/ziglang/lib/libc/mingw/def-include"
for d in kernel32 user32 gdi32 advapi32 shell32 ole32 oleaut32 ws2_32 winmm version shlwapi \
         comdlg32 comctl32 dwmapi crypt32 secur32 netapi32 psapi dbghelp wininet urlmon \
         rpcrt4 setupapi dnsapi iphlpapi; do
  f="zig/ziglang/lib/libc/mingw/lib-common/$d.def.in"; [ -f "$f" ] || f="zig/ziglang/lib/libc/mingw/lib-common/$d.def"
  python3 "$SCRIPTS/expand_def.py" "$f" > winsyslibs/defs/$d.def
done
python3 "$SCRIPTS/expand_def.py" zig/ziglang/lib/libc/mingw/lib-common/ucrtbase.def.in > winsyslibs/defs/ucrtbase.def

python3 - <<'EOF'
import subprocess, re, os
def exports(dll):
    out = subprocess.check_output(['objdump','-p',f'winqt/wx/PySide6/{dll}'], text=True, errors='replace')
    return set(re.findall(r'^\s*\[ *\d+\] +(\S+)', out, re.M))
need = ['__CxxFrameHandler3','_CxxThrowException','__RTDynamicCast','__std_terminate',
        '__std_type_info_destroy_list','_purecall','__current_exception','__current_exception_context',
        'memcpy','memmove','memset','strchr','strrchr','strstr','wcschr',
        '_get_unexpected','set_unexpected','__uncaught_exceptions']
ex = set(exports('vcruntime140.dll'))
have = [s for s in need if s in ex]
open('winsyslibs/defs/vcruntime140.def','w').write('LIBRARY VCRUNTIME140.dll\nEXPORTS\n' + '\n'.join(have) + '\n')
print('vcruntime140.def:', len(have), '/', len(need))
open('winsyslibs/defs/vcruntime140_1.def','w').write('LIBRARY VCRUNTIME140_1.dll\nEXPORTS\n__CxxFrameHandler4\n')
ex2 = set(exports('msvcp140.dll'))
ep = sorted(s for s in ex2 if s.startswith('?__ExceptionPtr'))
open('winsyslibs/defs/msvcp140.def','w').write('LIBRARY MSVCP140.dll\nEXPORTS\n' + '\n'.join(ep) + '\n')
print('msvcp140.def:', len(ep), 'ExceptionPtr')
EOF

for n in kernel32 user32 gdi32 advapi32 shell32 ole32 oleaut32 ws2_32 winmm version shlwapi \
         comdlg32 comctl32 dwmapi crypt32 secur32 netapi32 psapi dbghelp wininet urlmon \
         rpcrt4 setupapi dnsapi iphlpapi ucrtbase vcruntime140 vcruntime140_1 msvcp140; do
  "$ZIG/zig" dlltool -d winsyslibs/defs/$n.def -l winsyslibs/$n.lib || echo "FAIL $n"
done

mkdir -p winimplibs
python3 - <<'EOF'
import subprocess, re
for dll in ['Qt6Core','Qt6Gui','Qt6Widgets','Qt6Network']:
    out = subprocess.check_output(['objdump','-p',f'winqt/wx/PySide6/{dll}.dll'], text=True, errors='replace')
    names = sorted(set(re.findall(r'^\s*\[ *\d+\] +(\S+)', out, re.M)))
    open(f'winimplibs/{dll}.def','w').write(f'LIBRARY {dll}.dll\nEXPORTS\n' + '\n'.join(names) + '\n')
    print(dll, len(names))
EOF
for d in Qt6Core Qt6Gui Qt6Widgets Qt6Network; do
  "$ZIG/zig" dlltool -d winimplibs/$d.def -l winimplibs/$d.lib || echo "FAIL $d"
done
echo "  системных lib: $(ls winsyslibs/*.lib | wc -l), qt lib: $(ls winimplibs/*.lib | wc -l)"

echo "==> [6/10] libcxx"
rm -rf winlibcxx && mkdir -p winlibcxx
ls zig/ziglang/lib/libcxx/src/*.cpp | xargs -P 8 -I{} bash -c '
f="$1"; base=$(basename "$f" .cpp)
cd "'"$(pwd)"'"
./zig/ziglang/zig c++ -target x86_64-windows-msvc -std=c++23 -O2 -fno-ident -fno-sanitize=all -fno-stack-protector \
  -D_LIBCPP_NO_VCRUNTIME -D_LIBCPP_BUILDING_LIBRARY -DNDEBUG \
  -D__GNUC__=4 -D__GNUC_MINOR__=2 -D__GNUC_PATCHLEVEL__=1 -D__ALTIVEC__ \
  -include wininc/math_shim.h -isystem "zig/ziglang/lib/libc/include/any-windows-any" -I wininc -I zig/ziglang/lib/libcxx/libc \
  -c "$f" -o "winlibcxx/${base}.obj" 2>"winlibcxx/${base}.err"
echo "$base: $?"' _ {} | grep -v ": 0$" | head -8
N=$(ls winlibcxx/*.obj 2>/dev/null | wc -l)
echo "  объектов: $N/45"
[ "$N" != "45" ] && { echo "libcxx не собрался"; exit 1; }
"$ZIG/zig" ar rcs winlibcxx/libcxx.a winlibcxx/*.obj
echo "  libcxx.a: $(stat -c%s winlibcxx/libcxx.a) байт"

echo "==> [7/10] объекты приложения"
rm -rf winobj && mkdir -p winobj
FAIL=0
for f in ../src/*.cpp wininc/crt_stubs.cpp wininc/win_startup.cpp; do
  base=$(basename "$f" .cpp)
  ./zig/ziglang/zig c++ -target x86_64-windows-msvc -std=c++17 -O2 -fno-ident -nostdinc -fno-sanitize=all -fno-stack-protector \
    -D_LIBCPP_NO_VCRUNTIME \
    -D__GNUC__=4 -D__GNUC_MINOR__=2 -D__GNUC_PATCHLEVEL__=1 -D__ALTIVEC__ \
    -include wininc/math_shim.h -isystem zig/ziglang/lib/libcxx/include -isystem $WININC -isystem wininc -isystem zig/ziglang/lib/include \
    -I qt6/headers -I qt6/headers/QtCore -I qt6/headers/QtGui -I qt6/headers/QtWidgets -I qt6/headers/QtNetwork \
    -c "$f" -o "winobj/${base}.obj" 2>"winobj/${base}.err"
  if [ $? -ne 0 ]; then echo "$base: FAIL"; grep -E "fatal error|error:" "winobj/${base}.err" | head -5; FAIL=1; fi
done
[ "$FAIL" != "0" ] && { echo "исходники не собрались"; exit 1; }
echo "  объекты: $(ls winobj/*.obj | wc -l)"

echo "==> [8/10] линк"
./zig/ziglang/zig c++ -target x86_64-windows-msvc -nostdlib -Wl,/subsystem:windows \
  winobj/*.obj \
  winlibcxx/libcxx.a \
  -L winsyslibs -lkernel32 -luser32 -lgdi32 -ladvapi32 -lshell32 -lole32 -loleaut32 -lws2_32 -lwinmm -lversion -lshlwapi -lcomdlg32 -lcomctl32 -ldwmapi -lcrypt32 -lsecur32 -lnetapi32 -lpsapi -ldbghelp -lwininet -lurlmon -lrpcrt4 -lsetupapi -ldnsapi -liphlpapi -lucrtbase -lvcruntime140 -lvcruntime140_1 -lmsvcp140 \
  -L winimplibs -lQt6Widgets -lQt6Gui -lQt6Core -lQt6Network \
  -o /tmp/enderforge.exe 2>&1 | grep -E "error" | head -12
[ -f /tmp/enderforge.exe ] || { echo "линк не удался"; exit 1; }
echo "  exe: $(stat -c%s /tmp/enderforge.exe) байт"

echo "==> [9/10] dist"
rm -rf dist && mkdir -p dist/EnderForge/{platforms,styles,imageformats,tls,networkinformation,iconengines}
cp /tmp/enderforge.exe dist/EnderForge/EnderForge.exe
cp -r "$ROOT/resources" dist/EnderForge/
for d in Qt6Core Qt6Gui Qt6Network Qt6Widgets; do cp winqt/wx/PySide6/$d.dll dist/EnderForge/; done
for d in msvcp140 msvcp140_1 msvcp140_2 msvcp140_codecvt_ids vcruntime140 vcruntime140_1; do cp winqt/wx/PySide6/$d.dll dist/EnderForge/; done
cp winqt/wx/PySide6/plugins/platforms/qwindows.dll dist/EnderForge/platforms/
cp winqt/wx/PySide6/plugins/styles/*.dll dist/EnderForge/styles/
cp winqt/wx/PySide6/plugins/imageformats/*.dll dist/EnderForge/imageformats/
cp winqt/wx/PySide6/plugins/tls/qschannelbackend.dll winqt/wx/PySide6/plugins/tls/qcertonlybackend.dll dist/EnderForge/tls/
cp winqt/wx/PySide6/plugins/networkinformation/*.dll dist/EnderForge/networkinformation/
cp winqt/wx/PySide6/plugins/iconengines/*.dll dist/EnderForge/iconengines/

echo "==> [10/10] zip"
python3 - <<'EOF'
import zipfile, os
if os.path.exists('dist/EnderForge-win64.zip'): os.remove('dist/EnderForge-win64.zip')
with zipfile.ZipFile('dist/EnderForge-win64.zip','w',zipfile.ZIP_DEFLATED) as z:
    for root,_,files in os.walk('dist/EnderForge'):
        for f in files:
            p=os.path.join(root,f); z.write(p, p.replace(os.sep,'/'))
print('zip готов')
EOF
ls -la dist/EnderForge/EnderForge.exe dist/EnderForge-win64.zip
echo "ГОТОВО"
