@echo off
cls
setlocal enabledelayedexpansion
rmdir /s /q %1
set PKG_CONFIG_PATH=%1\conan
conan install . -g pkg_config --build missing --install-folder %1\conan
call meso setup %*
call python ..\meson-ninja-vs\\ninja_vs.py -b %1
