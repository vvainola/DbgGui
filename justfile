set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]
set ignore-comments := true

default:
    @just --list

[no-cd]
setup build *opts:
    #! powershell
    cmd /c rmdir /s /q {{build}}
    uv venv
    .venv/scripts/activate
    $Env:PKG_CONFIG_PATH="{{build}}\\conan"
    uv run conan install . --build missing --install-folder {{build}}\conan
    uv run meson setup {{build}} {{opts}} -Dtests=true
    uv run ..\meson-ninja-vs\ninja_vs.py -b {{build}}
