plugins_incs = [
  top_inc,
  plugins_inc
]

plugins_deps = [libtotem_dep]

plugins_cflags = common_flags + warn_flags + [
  '-DG_LOG_DOMAIN="Totem"'
]


plugins_conf = configuration_data()
plugins_conf.set('GETTEXT_PACKAGE', meson.project_name())


allowed_plugins = [
  'open-directory',
  'properties',
  'rotation',
  'screensaver',
  'screenshot',
  'variable-rate',
]


plugins = []


plugins_option = get_option('with-plugins')
if plugins_option != 'none'
  plugins += [
    'open-directory',
    'properties',
    'rotation',
    'screensaver',
    'screenshot',
    'variable-rate',
  ]

  plugin_error = (plugins_option == 'all')

endif



foreach plugin: plugins
  subdir(plugin)
endforeach
