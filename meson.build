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

if not WINDOWS
    deps += [dependency('libdwarf'), dependency('gtk+-3.0')]
else
    deps += [cpp.find_library('dbghelp')]
endif

imgui_src = files([
    'src/imgui/backend/imgui_impl_glfw.cpp',
    'src/imgui/backend/imgui_impl_glfw.h',
    'src/imgui/backend/imgui_impl_opengl3.cpp',
    'src/imgui/backend/imgui_impl_opengl3.h',
    'src/imgui/calibri.cpp',
    'src/imgui/cousine_regular.cpp',
    'src/imgui/imgui_demo.cpp',
    'src/imgui/imgui_draw.cpp',
    'src/imgui/imgui_internal.h',
    'src/imgui/imgui_tables.cpp',
    'src/imgui/imgui_widgets.cpp',
    'src/imgui/imgui.cpp',
    'src/imgui/imgui.h',
    'src/imgui/implot/implot_demo.cpp',
    'src/imgui/implot/implot_internal.h',
    'src/imgui/implot/implot_items.cpp',
    'src/imgui/implot/implot_items.cpp',
    'src/imgui/implot/implot.cpp',
    'src/imgui/implot/implot.h',
    'src/imgui/misc/cpp/imgui_stdlib.cpp',
    'src/imgui/misc/cpp/imgui_stdlib.h',
    'src/nativefiledialog/nfd_common.c',
])

if WINDOWS
    imgui_src += files(['src/nativefiledialog/nfd_win.cpp'])
else
    imgui_src += files(['src/nativefiledialog/nfd_gtk.c'])
endif
inc = []
inc += include_directories(
    'include',
    'src/imgui',
    'src/imgui/backend',
    'src/imgui/implot',
    'src/imgui/misc/cpp',
    'src/nativefiledialog/include',
    'src/symbols',
)

imgui_lib = static_library('imgui',
    sources : imgui_src,
    dependencies : deps,
    include_directories : inc,
    override_options : override_options,
)

platform_src = []
if WINDOWS
    platform_src += [
        'src/raw_pdb/PDB_CoalescedMSFStream.cpp',
        'src/raw_pdb/PDB_DBIStream.cpp',
        'src/raw_pdb/PDB_DBITypes.cpp',
        'src/raw_pdb/PDB_DirectMSFStream.cpp',
        'src/raw_pdb/PDB_GlobalSymbolStream.cpp',
        'src/raw_pdb/PDB_ImageSectionStream.cpp',
        'src/raw_pdb/PDB_InfoStream.cpp',
        'src/raw_pdb/PDB_IPIStream.cpp',
        'src/raw_pdb/PDB_ModuleInfoStream.cpp',
        'src/raw_pdb/PDB_ModuleLineStream.cpp',
        'src/raw_pdb/PDB_ModuleSymbolStream.cpp',
        'src/raw_pdb/PDB_NamesStream.cpp',
        'src/raw_pdb/PDB_PCH.cpp',
        'src/raw_pdb/PDB_PublicSymbolStream.cpp',
        'src/raw_pdb/PDB_RawFile.cpp',
        'src/raw_pdb/PDB_SectionContributionStream.cpp',
        'src/raw_pdb/PDB_SourceFileStream.cpp',
        'src/raw_pdb/PDB_TPIStream.cpp',
        'src/raw_pdb/PDB_Types.cpp',
        'src/raw_pdb/PDB.cpp',
        'src/symbols/dbghelp_helpers.cpp',
        'src/symbols/dbg_symbols_win.cpp',
    ]
else
    platform_src += ['src/symbols/elf_helpers.cpp']
    platform_src += ['src/symbols/dbg_symbols_linux.cpp']
endif

lib_symbols = static_library('symbols',
    sources : [
        'src/str_helpers.cpp',
        'src/symbols/arithmetic_symbol.cpp',
        'src/symbols/dbg_symbols.cpp',
        'src/symbols/global_snapshot.cpp',
        'src/symbols/symbol_descriptor.cpp',
        'src/symbols/variant_symbol.cpp',
    ] + platform_src,
    dependencies : deps,
    include_directories : ['include', 'src', 'src/raw_pdb'],
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
        'src/imgui_settings_migration.cpp',
        'src/dbg_gui_internal.h',
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

test_library = shared_library('test_library',
    sources : ['src/test_library.cpp'],
    include_directories : 'tests',
    override_options : override_options,
)

cpp_gui = executable('test_gui_cpp',
    sources : ['src/test_main.cpp', 'src/test_library_loader.cpp'],
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
        'src/imgui_settings_migration.cpp',
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

lib_symbols_dep = declare_dependency(
    link_with : lib_symbols,
    include_directories : 'include'
)

lib_gui_dep = declare_dependency(
    link_with : [lib_gui, lib_symbols],
    include_directories : 'include'
)


if get_option('tests')
    executable('tests',
        sources : [
            'src/csv_plot/csv_helpers.cpp',
            'src/imgui_settings_migration.cpp',
            'src/test_library_loader.cpp',
            'tests/csv_helpers_test.cpp',
            'tests/imgui_settings_migration_test.cpp',
            'tests/signal_cleanup_test.cpp',
            'tests/fwd_decl_types.cpp',
            'tests/symbols_test.cpp',
            'tests/test_types.c',
        ],
        dependencies : [
            lib_symbols_dep,
            dependency('catch2-with-main'),
            deps
        ],
        link_depends : test_library,
        include_directories: inc + ['src', 'tests'],
    )
endif
