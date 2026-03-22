project('DbgGui', ['cpp', 'c'],
    meson_version: '>=1.1.0',
    default_options : [
        'b_vscrt=md',
        'c_std=c17',
        'warning_level=3'
    ]
)

WINDOWS = (host_machine.system() == 'windows')
LINUX = (host_machine.system() == 'linux')

project_root = meson.current_source_dir()
if WINDOWS
    add_project_arguments([
            '-DWINDOWS',
            '-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR',
            '-DNOMINMAX',
            '-DJSON_DIAGNOSTICS=1',
            '-Zc:preprocessor',
        ], language : ['c', 'cpp']
    )
else
    add_project_arguments([
            '-DLINUX',
            '-DJSON_DIAGNOSTICS=1',
        ], language : ['c', 'cpp']
    )
endif

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
]

if WINDOWS
    deps += [cpp.find_library('dbghelp')]
else
    deps += [dependency('libdwarf'), dependency('gtk+-3.0')]
endif

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
])

if WINDOWS
    imgui_src += files(['src/nativefiledialog/nfd_win.cpp'])
else
    imgui_src += files(['src/nativefiledialog/nfd_gtk.c'])
endif
inc = []
inc += include_directories(
    'src/imgui',
    'src/imgui/implot',
    'src/imgui/backend',
    'src/nativefiledialog/include',
    'src/symbols',
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
        'src/str_helpers.cpp',
        'src/symbols/arithmetic_symbol.cpp',
        'src/symbols/dbghelp_symbols_lookup.cpp',
        'src/symbols/global_snapshot.cpp',
        'src/symbols/raw_symbol.cpp',
        'src/symbols/symbol_helpers.cpp',
        'src/symbols/variant_symbol.cpp',
    ],
    dependencies : deps,
    include_directories : ['include', 'src'],
    override_options : override_options,
)

lib_gui = static_library('dbg_gui',
    sources : [
        'src/custom_signal.cpp',
        'src/data_structures.cpp',
        'src/dbg_gui_custom_signal.cpp',
        'src/dbg_gui_plots.cpp',
        'src/dbg_gui_popups.cpp',
        'src/dbg_gui_windows.cpp',
        'src/dbg_gui_wrapper.cpp',
        'src/dbg_gui.cpp',
        'src/dbg_gui.h',
        'src/script_window.cpp',
        'src/spectrum.cpp',
        'src/str_helpers.cpp',
        'src/themes.cpp',
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

if WINDOWS
    csv_plotter_icon = [windows.compile_resources('src/csv_plot/icon.rc',
        depend_files : [
            'src/csv_plot/csv_plotter.png',
            'src/csv_plot/csv_plotter.ico',
            'src/csv_plot/resource.h'
        ],
    )]
else
    csv_plotter_icon = []
endif

csv_plotter = executable('csv_plotter',
    sources : [
        'src/csv_plot/csv_helpers.cpp',
        'src/csv_plot/csvplot_custom_signal.cpp',
        'src/csv_plot/csvplot_main.cpp',
        'src/csv_plot/csvplot_spectrum.cpp',
        'src/csv_plot/csvplot_vector.cpp',
        'src/csv_plot/csvplot.cpp',
        'src/csv_plot/save_image.cpp',
        'src/custom_signal.cpp',
        'src/spectrum.cpp',
        'src/str_helpers.cpp',
        'src/themes.cpp',
        version_header,
    ] + csv_plotter_icon,
    include_directories : inc + ['src'],
    link_with : imgui_lib,
    dependencies : deps,
    override_options : override_options,
)

inf2csv = executable('inf2csv',
    sources : [
        'src/csv_plot/csv_helpers.cpp',
        'src/csv_plot/inf2csv.cpp',
        'src/str_helpers.cpp',
    ],
    include_directories : ['src'],
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
