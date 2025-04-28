# totem-gstbt
A very poor torrent streaming player Demo(Without torrent client) (mainly working on top of others' code)

## Based on (Clone&Copy&Paste oriented programming)
**[gst-bt]** (https://github.com/turran/gst-bt)
**[Totem]**  (https://gitlab.gnome.org/GNOME/totem)  

## Dependencies
**[libtorrent]** (https://github.com/arvidn/libtorrent)
**[GStreamer]** (https://gitlab.freedesktop.org/gstreamer/gstreamer)


## Description
- construct a gstreamer pipeline for video playing
- use our own written btdemux (a gstreamer plugin equipped with libtorrent) for push downloaded torrent pieces to other elements
- GUI copied form GNOME totem video player
- can streaming video torrent in both torrent downloading or seeder mode
- add playlist feature switching video when torrenting

## Environment 
Ubuntu 24.04 LTS

# test video
https://youtu.be/filNB6CZq_0

![](https://github.com/xiongxijie/totem-gstbt/blob/main/test-video/Screenshot%20from%202025-03-15%2008-18-12.png)
