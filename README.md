# EnderForge — Minecraft Launcher

Десктопный лаунчер Minecraft на **C++ / Qt 6 (Widgets)**.

> **Статус:** ранняя версия. Интерфейс + **актуальный список версий Minecraft**
> (из официального манифеста Mojang). Запуск игры — в следующем обновлении.

![Интерфейс лаунчера](docs/preview.png)

## Возможности (текущие)

- 📦 **Актуальный список версий** из официального манифеста Mojang
  (`launchermeta.mojang.com`, запасной адрес — `piston-meta.mojang.com`)
- 🗂 Группировка: последние версии (релиз ★ и снапшот), релизы, снапшоты,
  старые беты и альфы (сортировка: новые сверху)
- 🔄 Кнопка обновления списка, авто-загрузка при старте
- 🚀 Кнопка «Запустить» — включается только когда выбрана версия
  (сам запуск клиента появится позже)
- 🎨 Тёмная тема в стиле Minecraft с пиксельным шрифтом Press Start 2P
  и собственным пиксель-артом (травяной блок, Стив)
- 👤 Карточка аккаунта («Не авторизован» — заглушка)
- 🖥 CLI для CI: `--screenshot`, `--manifest`, `--list-versions`

## Сборка на своей машине

Нужны: **CMake ≥ 3.16**, компилятор с C++17, **Qt 6** (пакет `qt6-base-dev`, включает Network).

### Linux (Ubuntu/Debian)

```bash
sudo apt install cmake g++ qt6-base-dev
cmake -B build
cmake --build build
./build/enderforge
```

### Windows

1. Установите Qt 6 через [онлайн-инсталлятор Qt](https://www.qt.io/download-qt-installer)
   (компоненты *Qt → Qt Widgets*, *Qt → Qt Network*, компилятор MSVC).
2. Откройте проект в Qt Creator (откройте `CMakeLists.txt`) и соберите.

### macOS

```bash
brew install qt
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
./build/enderforge
```

## Сборка без системного Qt (песочница/CI)

В окружениях без Qt SDK, но с доступом к PyPI и GitHub, можно собрать так:

```bash
./scripts/build_sandbox.sh
```

Скрипт скачивает Qt 6 (библиотеки из колеса PySide6-Essentials), берёт
заголовки qtbase из GitHub, генерирует недостающие и собирает лаунчер.
Результат — `build/bin/enderforge` и скриншот `docs/preview.png`
(со списком версий из зеркала манифеста Mojang на GitHub).

## CLI-режимы

```bash
# Показать загруженные версии (источник — сеть или --manifest)
./build/enderforge --list-versions

# Офлайн-тест: список версий из локального файла манифеста
./build/enderforge --manifest version_manifest_v2.json

# Скриншот окна (для CI) — ждёт загрузки списка версий
./build/enderforge --screenshot preview.png
```

## Структура проекта

```
├── CMakeLists.txt            # сборка через CMake (обычная)
├── scripts/
│   ├── build_sandbox.sh      # сборка без системного Qt
│   └── gen_qt_headers.py     # генератор слоя заголовков Qt
├── src/
│   ├── main.cpp              # точка входа, CLI, шрифт, тема
│   ├── MainWindow.h/.cpp     # главное окно и интерфейс
│   ├── VersionManager.h/.cpp # загрузка/парсинг манифеста Mojang
│   └── pixelart.h/.cpp       # отрисовка пиксель-арта (Стив, травяной блок)
└── resources/
    ├── theme.qss             # стили (тёмная тема в стиле Minecraft)
    └── fonts/                # Press Start 2P
```

## Планы

- [x] Загрузка списка версий Minecraft (манифест Mojang)
- [ ] Скачивание и запуск клиента
- [ ] Авторизация (Minecraft / Microsoft)
- [ ] Окно настроек (память, путь к игре, учётная запись)
- [ ] Список модов и сборок
