set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]
set ignore-comments := true

default:
    @just --list

[no-cd]
[windows]
setup build *opts:
    #! powershell
    cmd /c rmdir /s /q {{build}}
    $Env:UV_PROJECT_ENVIRONMENT=".venv_win"
    if (!(Test-Path -Path .venv_win)) {
        uv venv .venv_win
    }
    .venv_win/scripts/activate
    $Env:PKG_CONFIG_PATH="{{build}}\\conan"
    uv run conan install conanfile-windows.txt --build missing --output-folder {{build}}\conan --conf tools.env.virtualenv:powershell=powershell.exe
     # Call conanbuild.ps1 to set the environment variables
    .\\{{build}}\\conan\\conanbuild.ps1
    uv run meson setup {{build}} {{opts}} -Dtests=true -Dcpp_std=c++latest
    uv run ..\meson-ninja-vs\ninja_vs.py -b {{build}}

[no-cd]
[windows]
build build_folder="build" *opts="":
    #! powershell
    .venv/scripts/activate
    meson compile -C {{build_folder}}

[no-cd]
[windows]
test build_folder="build" *opts="":
    #! powershell
    .venv/scripts/activate
    meson compile -C {{build_folder}}
    cd {{build_folder}}
    ./tests.exe

[linux]
setup build *opts:
    #!/usr/bin/env bash
    rm -rf {{build}}
    export UV_LINK_MODE="copy"
    export UV_PROJECT_ENVIRONMENT=".venv_linux"
    uv venv --allow-existing .venv_linux
    source .venv_linux/bin/activate
    export PKG_CONFIG_PATH="{{build}}/conan"
    uv run conan install conanfile-linux.txt --build missing --output-folder {{build}}/conan
    source ./{{build}}/conan/conanbuild.sh
    uv run meson setup {{build}} {{opts}} -Dtests=true -Dcpp_std=c++23

[no-cd]
[linux]
build build_folder="build" *opts="":
    #!/usr/bin/env bash
    source .venv_linux/bin/activate
    ninja -C {{build_folder}}

[no-cd]
[linux]
test build_folder="build" *opts="":
    #!/usr/bin/env bash
    source .venv_linux/bin/activate
    ninja -C {{build_folder}}
    cd ./{{build_folder}}
    ./tests