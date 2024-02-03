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
        '-Zc:preprocessor',
    ], language : ['c', 'cpp'])

python = import('python3').find_python()
cpp = meson.get_compiler('cpp')
windows = import('windows')

if get_option('force_optimizations')
    override_options = ['optimization=3']
    # Override options does not work in VS backend so use Directory.Build.Targets to
    # override to optimization flag set in the VS project.
    configure_file(
        input : 'Directory.Build.Targets.in',
        output : 'Directory.Build.Targets',
        copy : true
    )
else
    override_options = []
endif

deps = [
    dependency('nlohmann_json'),
    dependency('magic_enum'),
    dependency('glfw3'),
    dependency('kissfft-float'),
    dependency('cxxopts'),
    cpp.find_library('dbghelp'),
]

imgui_src = files([
    'src/imgui/calibri.cpp',
    'src/imgui/cousine_regular.cpp',
    'src/imgui/backend/imgui_impl_opengl3.cpp',
    'src/imgui/backend/imgui_impl_opengl3.h',
    'src/imgui/backend/imgui_impl_glfw.cpp',
    'src/imgui/backend/imgui_impl_glfw.h',
    'src/imgui/imgui.cpp',
    'src/imgui/imgui.h',
    'src/imgui/imgui_demo.cpp',
    'src/imgui/imgui_draw.cpp',
    'src/imgui/imgui_internal.h',
    'src/imgui/imgui_tables.cpp',
    'src/imgui/imgui_widgets.cpp',
    'src/imgui/implot/implot.cpp',
    'src/imgui/implot/implot.h',
    'src/imgui/implot/implot_demo.cpp',
    'src/imgui/implot/implot_internal.h',
    'src/imgui/implot/implot_items.cpp',
    'src/imgui/implot/implot_items.cpp',
    'src/nativefiledialog/nfd_common.c',
    'src/nativefiledialog/nfd_win.cpp',
])
inc = []
inc += include_directories(
    'src/imgui',
    'src/imgui/implot',
    'src/imgui/backend',
    'src/nativefiledialog/include',
    'include'
)

imgui_lib = static_library('imgui',
    sources : imgui_src,
    dependencies : deps,
    include_directories : inc,
    override_options : override_options,
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
    include_directories : 'include',
    override_options : override_options,
)

lib_gui = static_library('dbg_gui',
    sources : [
        'src/dark_theme.cpp',
        'src/dbg_gui_wrapper.cpp',
        'src/dbg_gui.cpp',
        'src/dbg_gui_plots.cpp',
        'src/dbg_gui_windows.cpp',
        'src/dbg_gui.h',
    ],
    include_directories : inc,
    link_with : [lib_symbols, imgui_lib],
    dependencies : deps,
    override_options : override_options,
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

version_header = vcs_tag(
    command : ['git', 'rev-parse', '--short', 'HEAD'],
    input : 'src/version.h.in',
    output: 'version.h'
)


csv_plotter_icon = windows.compile_resources('src/csv_plot/icon.rc',
    depend_files : [
        'src/csv_plot/csv_plotter.png',
        'src/csv_plot/csv_plotter.ico',
        'src/csv_plot/resource.h'
    ],
)

csv_plotter = executable('csv_plotter',
    sources : [
        'src/csv_plot/csv_helpers.cpp',
        'src/csv_plot/csvplot_main.cpp',
        'src/csv_plot/csvplot.cpp',
        'src/csv_plot/save_image.cpp',
        'src/dark_theme.cpp',
        version_header,
        csv_plotter_icon
    ],
    include_directories : inc + ['src'],
    link_with : imgui_lib,
    dependencies : deps,
    override_options : override_options,
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
