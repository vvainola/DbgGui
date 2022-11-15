project('test_project', ['cpp', 'c'], 
    default_options : [
        'b_vscrt=md',
        'cpp_std=c++latest',
        'warning_level=3'
    ]
)

add_project_arguments('-DNOMINMAX', language : ['c', 'cpp'])

src = [
    'dbg_gui_wrapper.cpp',
    'debug_gui.cpp',
    'debug_gui_plots.cpp',
    'debug_gui_windows.cpp',
    'debug_gui.h',
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
inc += include_directories('imgui/bindings', 'include')

python = import('python3').find_python()
cpp = meson.get_compiler('cpp')

deps = [
    dependency('nlohmann_json'),
    dependency('magic_enum'),
    dependency('glfw3'),
    dependency('kissfft-float'),
    cpp.find_library('dbghelp'),
]

lib_gui = static_library('dbg_gui',
    sources : src,
    include_directories : inc,
    dependencies : deps
)

cpp_gui = executable('test_gui_cpp',
    sources : 'main.cpp',
    link_with : lib_gui,
    include_directories : inc
)

c_gui = executable('test_gui_c',
    sources : 'main.c',
    include_directories : 'include',
    link_with : lib_gui
)
