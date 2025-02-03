src_inc = include_directories('.')
plugins_inc = include_directories('plugins')

subdir('gst')
subdir('backend')

enum_headers = files(
  'backend/bacon-time-label.h',
  'backend/bacon-video-widget.h',
  'totem-interface.h',
  'totem-menu.h',
  # 'totem-open-location.h',
  # 'totem-options.h',
  'totem-player-toolbar.h',
  'totem-playlist.h',
  'totem-preferences-dialog.h',
  'totem-private.h',
  'totem-profile.h',
  'totem-resources.h',
  # 'totem-session.h',
  'totem-uri.h',
  'totem.h'
)

gnome.mkenums(
  'org.gnome.totem.enums.xml',
  sources: enum_headers,
  comments: '<!-- @comment@ -->',
  fhead: '<schemalist>',
  vhead: '  <@type@ id="org.gnome.totem.@EnumName@">',
  vprod: '    <value nick="@valuenick@" value="@valuenum@"/>',
  vtail: '  </@type@>',
  ftail: '</schemalist>',
  install_header: true,
  install_dir: totem_schemadir
)

totem_common_incs = [
  top_inc,
  plugins_inc
]

totem_common_deps = libbacon_video_widget_deps +  [
  glib_dep,
  gio_dep,
  gmodule_dep,
  gtk_dep,
  hdy_dep,
  gir_dep,
  peas_dep,
  dependency('gnome-desktop-3.0')
]

totem_common_cflags = common_flags + warn_flags + [
  '-DDBUS_API_SUBJECT_TO_CHANGE',
  '-DGNOMELOCALEDIR="@0@"'.format(totem_localedir),
  '-DDATADIR="@0@"'.format(totem_datadir),
  '-DLIBEXECDIR="@0@"'.format(totem_libexecdir),
  '-DTOTEM_PLUGIN_DIR="@0@"'.format(totem_pluginsdir),
  '-DBINDIR="@0@"'.format(totem_bindir),
  '-DGTKBUILDERDIR=""'
]

headers = files(
  'plugins/totem-plugin.h',
  'plugins/totem-dirs.h',
  'totem-interface.h',
  'totem.h'
)

install_headers(
  headers,
  subdir: totem_api_path
)

libtotem_player_sources = files(
  'totem-interface.c',
)

libtotem_player_deps = totem_common_deps + [
  libtotem_time_helpers_dep
]

libtotem_player = static_library(
  'totem_player',
  sources: libtotem_player_sources,
  include_directories: top_inc,
  dependencies: libtotem_player_deps,
  c_args: totem_common_cflags + [
    '-DG_LOG_DOMAIN="Totem"'
  ]
)

libtotem_player_dep = declare_dependency(
  link_with: libtotem_player,
  include_directories: src_inc,
  dependencies: libtotem_player_deps
)

libtotem_sources = files(
  'plugins/totem-dirs.c',
  'plugins/totem-plugins-engine.c',
  'totem-menu.c',
  'totem-object.c',
  # 'totem-open-location.c',
  # 'totem-options.c',
  'totem-player-toolbar.c',
  'totem-playlist.c',
  'totem-preferences-dialog.c',
  'totem-preferences-plugin-row.c',
  # 'totem-session.c',
  'totem-uri.c'
)

libtotem_sources += ui_resources

libtotem_deps = [
  libtotem_player_dep,
  libbacon_video_widget_dep,
]

libtotem = shared_library(
  'totem',
  sources: libtotem_sources + gen_sources,
  version: libversion,
  soversion: soversion,
  include_directories: totem_common_incs,
  dependencies: libtotem_deps,
  c_args: totem_common_cflags + [
    '-DG_LOG_DOMAIN="Totem"'
  ],
  install: true,
  install_dir: totem_libdir
)

libtotem_dep = declare_dependency(
  link_with: libtotem,
  include_directories: src_inc,
  dependencies: libtotem_deps
)

pkg.generate(
  libraries: libtotem,
  version: totem_version,
  name: 'Totem',
  description: 'Totem Movie Player plugin API',
  filebase: meson.project_name(),
  subdirs: totem_api_path,
  requires: [
    'gio-2.0 ' + glib_req_version,
    'gtk+-3.0 ' + gtk_req_version,
  ],
  variables: [
    'exec_prefix=' + totem_libexecdir,
    'pluginsdir=' + totem_pluginsdir
  ],
  install_dir: join_paths(totem_libdir, 'pkgconfig')
)

totem = executable(
  meson.project_name(),
  'totem.c',
  include_directories: totem_common_incs,
  dependencies: [ libtotem_dep ],
  c_args: totem_common_cflags + [
    '-DG_LOG_DOMAIN="Totem"'
  ],
  install: true,
  install_dir: totem_bindir
)



gir_sources = libtotem_sources + libtotem_player_sources + headers

gir_incs = [
  'Gio-2.0',
  'GLib-2.0',
  'GObject-2.0',
  'Gtk-3.0',
]

gir_extra_args = [
  '--c-include=totem.h',
  '--pkg-export=totem'
]

gir_dir = join_paths(totem_datadir, 'gir-1.0')
typelib_dir = join_paths(totem_libdir, 'girepository-1.0')

libtotem_gir = gnome.generate_gir(
  libtotem,
  sources: gir_sources,
  nsversion: totem_api_version,
  namespace: totem_gir_ns,
  symbol_prefix: totem_gir_ns.to_lower(),
  identifier_prefix: totem_gir_ns,
  includes: gir_incs,
  include_directories: src_inc,
  install: true,
  install_dir_gir: gir_dir,
  install_dir_typelib: typelib_dir,
  extra_args: gir_extra_args
)



subdir('plugins')
