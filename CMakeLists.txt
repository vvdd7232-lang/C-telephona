cmake_minimum_required(VERSION 3.16)

project(EnderForge VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# AUTOMOC уже включён — понадобится для Q_OBJECT-классов
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(enderforge
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/VersionManager.h
    src/VersionManager.cpp
    src/versionmodel.h
    src/versionmodel.cpp
    src/ProfileStore.h
    src/ProfileStore.cpp
    src/GameDownloader.h
    src/GameDownloader.cpp
    src/MinecraftInstaller.h
    src/MinecraftInstaller.cpp
    src/AddProfileDialog.h
    src/AddProfileDialog.cpp
    src/pixelart.h
    src/pixelart.cpp
)

target_link_libraries(enderforge PRIVATE Qt6::Widgets Qt6::Network)

# Путь к ресурсам (шрифт, тема) на время разработки
target_compile_definitions(enderforge PRIVATE
    ENDERFORGE_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)
