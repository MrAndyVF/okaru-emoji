# okaru-emoji: selector de emojis, GIF (Klipy), kaomojis y símbolos de OKARU. Se compila contra el núcleo instalado
# (paquete okaru-core).
project('okaru-emoji', 'cpp',
  version: files('VERSION'),
  meson_version: '>=1.1',
  default_options: ['cpp_std=c++23', 'cpp_extensions=false', 'warning_level=0', 'b_ndebug=if-release'],
)

# El núcleo y sus bibliotecas son privados (/usr/lib/okaru): se enlazan con su ruta en el binario.
okaru_core = dependency('okaru-core')
_okaru_libdir = get_option('prefix') / get_option('libdir') / 'okaru'
_deps = [okaru_core, dependency('nlohmann_json'), dependency('tomlplusplus')]
_inc = include_directories('src')
_args = ['-Wall', '-Wextra', '-Wno-missing-field-initializers', '-Wpedantic', '-Wconversion', '-Wshadow']

# La clave no vive en el repo: llega por la opción (en la publicación, del secreto KLIPY_API_KEY).
_config = configuration_data()
_config.set_quoted('OKARU_KLIPY_KEY', get_option('klipy_key'))
_build_config = configure_file(output: 'emoji_build_config.h', configuration: _config)

_data = files('src/apps/emoji/emoji_catalog.cpp', 'src/apps/emoji/klipy.cpp')

executable('okaru-emoji',
  sources: files('src/apps/emoji/emoji_window.cpp', 'src/apps/emoji/main.cpp') + _data + [_build_config],
  include_directories: _inc,
  dependencies: _deps,
  cpp_args: _args,
  install_rpath: _okaru_libdir,
  install: true,
)

# Sus datos y textos, que el núcleo busca en /usr/share/okaru/apps/emoji.
install_data('apps/emoji/emoji.json', 'apps/emoji/kaomoji.json', 'apps/emoji/symbols.json',
  install_dir: get_option('datadir') / 'okaru' / 'apps' / 'emoji')
install_subdir('apps/emoji/translations', install_dir: get_option('datadir') / 'okaru' / 'apps' / 'emoji')
install_data('assets/dev.okaru.Emoji.desktop', install_dir: get_option('datadir') / 'applications')

if get_option('tests').allowed()
  test('okaru_emoji_data', executable('okaru_emoji_data_test',
    sources: files('tests/okaru_emoji_data_test.cpp') + _data,
    include_directories: _inc,
    dependencies: _deps,
    build_rpath: _okaru_libdir,
    cpp_args: ['-DEMOJI_DATA_DIR="' + meson.current_source_dir() / 'apps' / 'emoji' + '"'],
    override_options: ['b_ndebug=false'],
  ))
endif
