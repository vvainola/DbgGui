project('test_project', 'cpp', 
    default_options : [
        'b_vscrt=md',
        'cpp_std=c++latest',
        'b_debug=false',
        'buildtype=plain',
        'warning_level=3'
    ]
)
add_project_arguments('-Z7', '-DNOMINMAX', language : 'cpp')
add_project_link_arguments('-DEBUG', language : 'cpp')

src = [
    'debug_gui.cpp',
    'debug_gui_plots.cpp',
    'debug_gui_windows.cpp',
    'debug_gui.h',
    'main.cpp',
    'imgui/bindings/imgui_impl_opengl3.cpp',
    'imgui/bindings/imgui_impl_opengl3.h',
    'imgui/bindings/imgui_impl_glfw.cpp',
    'imgui/bindings/imgui_impl_glfw.h',
    'imgui/bindings/imgui.cpp',
    'imgui/bindings/imgui.h',
    'imgui/bindings/imgui_demo.cpp',
    'imgui/bindings/imgui_draw.cpp',
    'imgui/bindings/imgui_internal.h',
    'imgui/bindings/imgui_tables.cpp',
    'imgui/bindings/imgui_widgets.cpp',
    'imgui/bindings/implot.cpp',
    'imgui/bindings/implot.h',
    'imgui/bindings/implot_demo.cpp',
    'imgui/bindings/implot_internal.h',
    'imgui/bindings/implot_items.cpp',
    'symbols/arithmetic_symbol.cpp',
    'symbols/dbghelp_symbols_lookup.cpp',
    'symbols/symbol_helpers.cpp',
    'symbols/variant_symbol.cpp',
    ]
inc = []
inc += include_directories('imgui/bindings')

python = import('python3').find_python()
cpp = meson.get_compiler('cpp')

deps = [
    dependency('nlohmann_json'),
    dependency('magic_enum'),
    dependency('glfw3'),
    dependency('kissfft-float'),
    cpp.find_library('dbghelp'),
]

t0 = executable('test_exe',
    sources : src,
    include_directories : inc,
    dependencies : deps
)
