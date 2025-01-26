set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]
set ignore-comments := true

default:
    @just --list

[no-cd]
[windows]
setup build *opts:
    #! powershell
    cmd /c rmdir /s /q {{build}}
    if (!(Test-Path -Path .venv)) {
        uv venv
    }
    .venv/scripts/activate
    $Env:PKG_CONFIG_PATH="{{build}}\\conan"
    uv run conan install . --build missing --output-folder {{build}}\conan --conf tools.env.virtualenv:powershell=powershell.exe
     # Call conanbuild.ps1 to set the environment variables
    .\\{{build}}\\conan\\conanbuild.ps1
    uv run meson setup {{build}} {{opts}} -Dtests=true
    uv run ..\meson-ninja-vs\ninja_vs.py -b {{build}}
