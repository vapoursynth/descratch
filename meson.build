project('DeScratch', 'cpp',
    default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++17'],
    license: 'GPL-2.0',
    license_files: 'LICENSE',
    meson_version: '>=1.2.3',
    version: '4.0',
)

incdir = include_directories('vapoursynth/include', 'AviSynthPlus/avs_core/include')

py = import('python').find_installation(pure: false)

shared_module('descratch',
    files('src/descratch.cpp'),
    gnu_symbol_visibility: 'hidden',
    include_directories: incdir,
    install: true,
    install_dir: py.get_install_dir() / 'vapoursynth/plugins',
    name_prefix: '',
)