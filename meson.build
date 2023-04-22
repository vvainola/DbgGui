project('DbgGui', ['cpp', 'c'], 
    default_options : [
        'b_vscrt=md',
        'cpp_std=c++latest',
        'c_std=c17',
        'warning_level=3'
    ]
)

project_root = meson.current_source_dir()
add_project_arguments([
        '-DNOMINMAX',
        '-DJSON_DIAGNOSTICS=1',
    ], language : ['c', 'cpp'])

python = import('python3').find_python()
cpp = meson.get_compiler('cpp')

deps = [
    dependency('nlohmann_json'),
    dependency('magic_enum'),
    dependency('glfw3'),
    dependency('kissfft-float'),
    dependency('cxxopts'),
    cpp.find_library('dbghelp'),
]

imgui_src = files([
    'src/imgui/cousine_regular.cpp',
    'src/imgui/imgui_impl_opengl3.cpp',
    'src/imgui/imgui_impl_opengl3.h',
    'src/imgui/imgui_impl_glfw.cpp',
    'src/imgui/imgui_impl_glfw.h',
    'src/imgui/imgui.cpp',
    'src/imgui/imgui.h',
    'src/imgui/imgui_demo.cpp',
    'src/imgui/imgui_draw.cpp',
    'src/imgui/imgui_internal.h',
    'src/imgui/imgui_tables.cpp',
    'src/imgui/imgui_widgets.cpp',
    'src/imgui/implot.cpp',
    'src/imgui/implot.h',
    'src/imgui/implot_demo.cpp',
    'src/imgui/implot_internal.h',
    'src/imgui/implot_items.cpp',
    'src/imgui/implot_items.cpp',
    'src/nativefiledialog/nfd_common.c',
    'src/nativefiledialog/nfd_win.cpp',
])
inc = []
inc += include_directories('src/imgui', 'src/nativefiledialog/include', 'include')

imgui_lib = static_library('imgui',
    sources : imgui_src,
    dependencies : deps,
    include_directories : inc,
)

lib_symbols = static_library('symbols',
    sources : [
        'src/symbols/arithmetic_symbol.cpp',
        'src/symbols/dbghelp_symbols_lookup.cpp',
        'src/symbols/raw_symbol.cpp',
        'src/symbols/symbol_helpers.cpp',
        'src/symbols/variant_symbol.cpp',
        'src/symbols/global_snapshot.cpp'
    ],
    dependencies : deps,
    include_directories : 'include'
)

lib_gui = static_library('dbg_gui',
    sources : [
        'src/dbg_gui_wrapper.cpp',
        'src/dbg_gui.cpp',
        'src/dbg_gui_plots.cpp',
        'src/dbg_gui_windows.cpp',
        'src/dbg_gui.h',
    ],
    include_directories : inc,
    link_with : [lib_symbols, imgui_lib],
    dependencies : deps
)

cpp_gui = executable('test_gui_cpp',
    sources : 'src/test_main.cpp',
    include_directories : 'include',
    link_with : lib_gui,
)

c_gui = executable('test_gui_c',
    sources : 'src/test_main.c',
    include_directories : 'include',
    link_with : lib_gui,
)

csv_plotter = executable('csv_plotter',
    sources : [
        'src/csv_plot/csvplot_main.cpp',
        'src/csv_plot/csvplot.cpp',
        'src/csv_plot/save_image.cpp'
    ],
    include_directories : inc + ['src'],
    link_with : imgui_lib,
    dependencies : deps,
)

lib_dbghelp_symbols_dep = declare_dependency(
    link_with : lib_symbols,
    include_directories : 'include'
)

lib_gui_dep = declare_dependency(
    link_with : [lib_gui, lib_symbols],
    include_directories : 'include'
)


if get_option('tests')
executable('tests',
    sources : ['tests/symbols_test.cpp'],
    dependencies : [lib_dbghelp_symbols_dep, dependency('catch2-with-main')]
)
endif
