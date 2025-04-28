
gstbt_inc = include_directories('.')




libgstbtdeps = [
    gst_dep,
    dependency('gstreamer-base-1.0', version: gst_req_version),
    dependency('libtorrent-rasterbar', version: '>= 2.0.11')
]



libgstbt_sources = files(
  'gst_bt_type.c',
  'gst_bt.c',
  'gst_bt_demux.cpp'
)



# we should Install our wrote gst-bt plugin to the our GStreamer plugin directory
# in my computer it is: `/usr/lib/x86_64-linux-gnu/gstreamer-1.0`
# but i want to use my own-built Gstreamer instead of the system-intalled one
# it's '/media/pal/E/FreeDsktop/gstreamer/install/lib/x86_64-linux-gnu/gstreamer-1.0'

#no, just install it in out project install dir, not our own built gstreamer dir
gstbt_install_dir = '/media/pal/E/GnomeApp/totem-gstbt/install/lib/x86_64-linux-gnu'


# cc = meson.get_compiler('cpp')
# message('C++ compiler version: ' + cc.get_version())


libgstbt = shared_library(
  'gstbt',
  sources: libgstbt_sources,
  version: '0.0.1',
  include_directories: gstbt_inc,
  dependencies: libgstbtdeps,
  c_args: [
    '-DHAVE_GST_1',
    '-DPACKAGE="gst-bt"'
  ],
  cpp_args: [
    '-DHAVE_GST_1'
  ],
  install: true,
  install_dir: gstbt_install_dir
)