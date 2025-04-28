#!/bin/bash



# our own libtotem.so
export LD_LIBRARY_PATH=/media/pal/E/GnomeApp/totem-gstbt/install/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH


#our own gstreamer
#specify libgstbt.so location which is in our project install dir
export GST_PLUGIN_PATH=/media/pal/E/GnomeApp/totem-gstbt/install/lib/x86_64-linux-gnu:$GST_PLUGIN_PATH
export LD_LIBRARY_PATH=/media/pal/E/FreeDsktop/gstreamer/install/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/media/pal/E/FreeDsktop/gstreamer/install/lib/x86_64-linux-gnu/gstreamer-1.0:$LD_LIBRARY_PATH


# our own libtorrent-rasterbar.so
export LD_LIBRARY_PATH=/media/pal/E/Documents/libtorrent-RC_2_0/install/lib:$LD_LIBRARY_PATH

export PKG_CONFIG_PATH=/media/pal/E/Documents/libtorrent-RC_2_0/install/lib/pkgconfig:$PKG_CONFIG_PATH





