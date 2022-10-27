project('test_project', 'cpp', 
    default_options : [
        'b_vscrt=md',
        'cpp_std=c++latest',
        'b_debug=false',
        'buildtype=plain',
    ]
)
add_project_arguments('-Z7', '-DNOMINMAX', language : 'cpp')
add_project_link_arguments('-DEBUG', language : 'cpp')

src = [
    'main.cpp',
    'imgui_impl_opengl3.cpp',
    'imgui_impl_opengl3.h',
    'imgui_impl_glfw.cpp',
    'imgui_impl_glfw.h',
    'imgui.cpp',
    'imgui.h',
    'imgui_demo.cpp',
    'imgui_draw.cpp',
    'imgui_internal.h',
    'imgui_tables.cpp',
    'imgui_widgets.cpp',
    ]
inc = []

python = import('python3').find_python()
cpp = meson.get_compiler('cpp')

deps = [
    dependency('nlohmann_json'),
    dependency('magic_enum'),
    #dependency('imgui'),
    dependency('glfw3'),
    dependency('implot'),
    dependency('kissfft-float'),
]

t0 = executable('test_exe',
    sources : src,
    include_directories : inc,
    dependencies : deps
)
