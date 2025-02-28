/*
 * Copyright (C) 2003-2007 the GStreamer project
 *      Julien Moutte <julien@moutte.net>
 *      Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2003-2022 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2005-2008 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright © 2009 Christian Persch
 *
 * SPDX-License-Identifier: GPL-3-or-later
 *
 */

/**
 * SECTION:bacon-video-widget
 * @short_description: video playing widget and abstraction
 * @stability: Unstable
 * @include: bacon-video-widget.h
 *
 * #BaconVideoWidget is a widget to play audio or video streams It has a GStreamer
 * backend, and abstracts away the differences to provide a simple interface to the functionality required by Totem. It handles all the low-level
 * audio and video work for Totem (or passes the work off to the backend).
 **/

#include <config.h>

#define GST_USE_UNSTABLE_API 1

#include <gst/gst.h>

#include <gst/gstpad.h> 
#include <gst/gstcaps.h> 

/* GStreamer Interfaces */
// #include <gst/video/navigation.h>
#include <gst/video/colorbalance.h>
/* for detecting sources of errors */
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/audio/streamvolume.h>

/* for missing decoder/demuxer detection */
#include <gst/pbutils/pbutils.h>

/* for the cover metadata info */
#include <gst/tag/tag.h>

/* system */
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* gtk+/gnome */
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gdesktop-enums.h>
#include "totem-gst-helpers.h"
#include "totem-gst-pixbuf-helpers.h"
#include "bacon-video-widget.h"
#include "bacon-video-widget-enums.h"
#include "bacon-video-widget-resources.h"

#include "gst-bt/gst_bt_demux.hpp"

#define DEFAULT_USER_AGENT "Videos/"VERSION

#define DEFAULT_CONTROLS_WIDTH 600             /* In pixels */
#define LOGO_SIZE 64                           /* Maximum size of the logo */
#define REWIND_OR_PREVIOUS 4000

#define MAX_NETWORK_SPEED 10752
#define BUFFERING_LEFT_RATIO 1.1

/* Helper constants */
#define NANOSECS_IN_SEC 1000000000
#define SEEK_TIMEOUT NANOSECS_IN_SEC / 10
#define FORWARD_RATE 1.0
#define REVERSE_RATE -1.0
#define DIRECTION_STR (forward == FALSE ? "reverse" : "forward")

#define BVW_TRACK_NONE -2
#define BVW_TRACK_AUTO -1


#define ULONG_TO_POINTER(number)        ((gpointer) (guintptr) (number))
#define POINTER_TO_ULONG(number)        ((guintptr) (number))


#define is_error(e, d, c) \
  (e->domain == GST_##d##_ERROR && \
   e->code == GST_##d##_ERROR_##c)

#define I_(string) (g_intern_static_string (string))

/* Signals */
enum
{
  SIGNAL_ERROR,
  SIGNAL_EOS,
  SIGNAL_REDIRECT,
  SIGNAL_CHANNELS_CHANGE,
  SIGNAL_BTDEMUX_VIDEOINFO,
  SIGNAL_TICK,
  SIGNAL_GOT_METADATA,
  SIGNAL_VIDEO_CHANGED,
  SIGNAL_AUDIO_CHANGED,
  SIGNAL_VIDEO_TAGS_CHANGED,
  SIGNAL_AUDIO_TAGS_CHANGED,
  SIGNAL_BUFFERING,
  SIGNAL_MISSING_PLUGINS,
  SIGNAL_PPI_INFO,
  SIGNAL_PIECE_BLOCK_INFO,
  SIGNAL_FINISHED_PIECE_INFO,
  SIGNAL_PLAY_STARTING,
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0,
  PROP_POSITION,
  PROP_CURRENT_TIME,
  PROP_STREAM_LENGTH,
  PROP_PLAYING,
  PROP_REFERRER,
  PROP_SEEKABLE,
  PROP_USER_AGENT,
  PROP_VOLUME,
  PROP_DOWNLOAD_FILENAME,
  PROP_DEINTERLACING,
  PROP_BRIGHTNESS,
  PROP_CONTRAST,
  PROP_SATURATION,
  PROP_HUE,
  PROP_AUDIO_OUTPUT_TYPE,
  PROP_AV_OFFSET,
  PROP_SHOW_CURSOR,
  PROP_CUR_AUDIO_TAGS,
  PROP_CUR_VIDEO_TAGS,

};

static const gchar *video_props_str[4] = {
  "brightness",
  "contrast",
  "saturation",
  "hue"
};

struct _BaconVideoWidget
{
  GtkBin                       parent;

  /* widgets */
  GtkWidget                   *stack;
  GtkWidget                   *audio_only;
  GtkWidget                   *broken_video;
  GtkWidget                   *video_widget;

  GtkWindow                   *parent_toplevel;

  GError                      *init_error;

  BvwAspectRatio               ratio_type;

          GstElement                 *pipeline;

          GstElement                  *filesrc;
          
          GstElement                  *btdemux;

          GstElement                  *decodebin;

          GstElement                  *audio_bin;

          GstElement                  *glsinkbin;

          GstElement                  *video_sink;

          GstElement                  *audio_filter_convert;


  //...copied from gstplaybin2
  GPtrArray                   *video_channels; 
  GPtrArray                   *audio_channels; 


  GPtrArray                      *btdemux_srcpads;

  guint                        update_id;
  guint                        ppi_id;


  gboolean                     media_has_video;
  gboolean                     media_has_unsupported_video;
  gboolean                     media_has_audio;
  gint                         seekable; /* -1 = don't know, FALSE = no */
  gint64                       stream_length;
  gint64                       current_time;
  gdouble                      current_position;
  
  gint                         cur_video_fileidx_within_tor;

  gint                         stream_id_seq;

  GstPad*                      video_sinpad_decodebin;
  GstPad*                      audio_sinpad_decodebin;

  /*  Tags of current stream */
  GstTagList                  *tagcache;
  GstTagList                  *audiotags;
  GstTagList                  *videotags;
  GMutex                       get_audiotags_mutex;
  GMutex                       get_videotags_mutex;


  GAsyncQueue                 *tag_update_queue;
  guint                        tag_update_id;



  gboolean                     cursor_shown;


  /* Visual effects */
  GstElement                  *audio_capsfilter;
  GstElement                  *audio_pitchcontrol;

  /* Other stuff */
  gdouble                      volume;

  BvwRotation                  rotation;
  
  gint                         video_width; /* Movie width */
  gint                         video_height; /* Movie height */
  gint                         video_fps_n;
  gint                         video_fps_d;

  BvwAudioOutputType           speakersetup;

  GstBus                       *bus;
  gulong                       sig_bus_async;

  gint                         eos_id;

  /* When seeking, queue up the seeks if they happen before
   * the previous one finished */
  GMutex                       seek_mutex;
  GstClock                     *clock;
  GstClockTime                 seek_req_time;
  gint64                       seek_time;


  /* state we want to be in, as opposed to actual pipeline state
   * which may change asynchronously or during buffering */
  GstState                     target_state;
  

  gboolean                     buffering;


  /* for missing codecs handling */
  GList                       *missing_plugins;   /* GList of GstMessages */

  /* for stepping frame*/
  float                        rate;

  GList                        *videos_info;
};


// define a new type (class) and is part of the type system that provides a way to create objects in a structured and reusable way in C
//args:
//@ the name of your type, which follows the PascalCase convention
//@  owercase version of your type name, used for instance functions and signals
//@  parent type for your new type 
G_DEFINE_TYPE (BaconVideoWidget, bacon_video_widget, GTK_TYPE_BIN)



static void bacon_video_widget_set_property (GObject * object,
                                             guint property_id,
                                             const GValue * value,
                                             GParamSpec * pspec);
static void bacon_video_widget_get_property (GObject * object,
                                             guint property_id,
                                             GValue * value,
                                             GParamSpec * pspec);

static void bacon_video_widget_finalize (GObject * object);

static void bvw_reconfigure_ppi_timeout (BaconVideoWidget *bvw, guint msecs);
static void bvw_stop_play_pipeline (BaconVideoWidget * bvw);
static GError* bvw_error_from_gst_error (BaconVideoWidget *bvw, GstMessage *m);
static gboolean bvw_set_playback_direction (BaconVideoWidget *bvw, gboolean forward);
static gboolean bacon_video_widget_seek_time_no_lock (BaconVideoWidget *bvw,
						      gint64 _time,
						      GstSeekFlags flag,
						      GError **error);

typedef struct {
  GstTagList *tags; /*audio-tags, videotags*/
  const gchar *type; /*audio, video*/
} UpdateTagsDelayedData;

static void update_tags_delayed_data_destroy (UpdateTagsDelayedData *data);

static GtkWidgetClass *parent_class = NULL;

static int bvw_signals[LAST_SIGNAL] = { 0 };

GST_DEBUG_CATEGORY (_totem_gst_debug_cat);
#define GST_CAT_DEFAULT _totem_gst_debug_cat

typedef gchar * (* MsgToStrFunc) (GstMessage * msg);



static gchar **
bvw_get_missing_plugins_foo (const GList * missing_plugins, MsgToStrFunc func)
{
  GPtrArray *arr = g_ptr_array_new ();
  GHashTable *ht;

  ht = g_hash_table_new (g_str_hash, g_str_equal);
  while (missing_plugins != NULL) {
    char *tmp;
    tmp = func (GST_MESSAGE (missing_plugins->data));
    if (!g_hash_table_lookup (ht, tmp)) {
      g_ptr_array_add (arr, tmp);
      g_hash_table_insert (ht, tmp, GINT_TO_POINTER (1));
    } else {
      g_free (tmp);
    }
    missing_plugins = missing_plugins->next;
  }
  g_ptr_array_add (arr, NULL);
  g_hash_table_destroy (ht);
  return (gchar **) g_ptr_array_free (arr, FALSE);
}

static gchar **
bvw_get_missing_plugins_descriptions (const GList * missing_plugins)
{
  return bvw_get_missing_plugins_foo (missing_plugins,
      gst_missing_plugin_message_get_description);
}

static void
bvw_clear_missing_plugins_messages (BaconVideoWidget * bvw)
{
  g_list_free_full (bvw->missing_plugins, (GDestroyNotify) gst_mini_object_unref);
  bvw->missing_plugins = NULL;
}

static void
bvw_check_if_video_decoder_is_missing (BaconVideoWidget * bvw)
{
  GList *l;

  for (l = bvw->missing_plugins; l != NULL; l = l->next) {
    GstMessage *msg = GST_MESSAGE (l->data);
    g_autofree char *d = NULL;
    char *f;

    if ((d = gst_missing_plugin_message_get_installer_detail (msg))) {
      if ((f = strstr (d, "|decoder-")) && strstr (f, "video")) {
        bvw->media_has_unsupported_video = TRUE;
        break;
      }
    }
  }
}


static void
bvw_show_error_if_video_decoder_is_missing (BaconVideoWidget * bvw)
{
  GList *l;

  if (bvw->media_has_video || bvw->missing_plugins == NULL)
    return;

  for (l = bvw->missing_plugins; l != NULL; l = l->next) {
    GstMessage *msg = GST_MESSAGE (l->data);
    gchar *d, *f;

    if ((d = gst_missing_plugin_message_get_installer_detail (msg))) {
      if ((f = strstr (d, "|decoder-")) && strstr (f, "video")) {
        GError *err;

        /* create a fake GStreamer error so we get a nice warning message */
        err = g_error_new (GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN, "x");
        msg = gst_message_new_error (GST_OBJECT (bvw->pipeline), err, NULL);
        g_error_free (err);
        err = bvw_error_from_gst_error (bvw, msg);
        gst_message_unref (msg);
        g_signal_emit (bvw, bvw_signals[SIGNAL_ERROR], 0, err->message, FALSE);
        g_error_free (err);
        g_free (d);
        break;
      }
      g_free (d);
    }
  }
}


static void
update_cursor (BaconVideoWidget *bvw)
{

  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));

  GdkWindow *window;

  window = gtk_widget_get_window (GTK_WIDGET (bvw));

  if (!gtk_window_is_active (bvw->parent_toplevel)) 
  {
    gdk_window_set_cursor (window, NULL);
    return;
  }

  if (bvw->cursor_shown)
  {
    gdk_window_set_cursor (window, NULL);
  }

}

static void
bacon_video_widget_realize (GtkWidget * widget)
{
  BaconVideoWidget *bvw = BACON_VIDEO_WIDGET (widget);
  GdkWindow *window = NULL;
  //// GdkDisplay *display;

  GTK_WIDGET_CLASS (parent_class)->realize (widget);


  if (gtk_widget_get_realized (widget))
  {
    window = gtk_widget_get_window (widget);
  }


  bvw->parent_toplevel = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (bvw)));

  g_signal_connect_swapped (G_OBJECT (bvw->parent_toplevel), "notify::is-active",
			    G_CALLBACK (update_cursor), bvw);

}


static void
bacon_video_widget_unrealize (GtkWidget *widget)
{
  
  BaconVideoWidget *bvw = BACON_VIDEO_WIDGET (widget);

  GTK_WIDGET_CLASS (parent_class)->unrealize (widget);

  if (bvw->parent_toplevel != NULL) {
    g_signal_handlers_disconnect_by_func (bvw->parent_toplevel, update_cursor, bvw);
    bvw->parent_toplevel = NULL;
  }

}


static void
set_current_actor (BaconVideoWidget *bvw)
{
  const char *page;

  if (bvw->media_has_audio && !bvw->media_has_video)
    page = "audio-only";
  else if (bvw->media_has_unsupported_video)
    page = "broken-video";
  else
    page = "video";

  gtk_stack_set_visible_child_name (GTK_STACK (bvw->stack), page);
}








static void
bacon_video_widget_get_preferred_width (GtkWidget *widget,
                                        gint      *minimum,
                                        gint      *natural)
{
  /* We could also make the actor a minimum width, based on its contents */
  *minimum = *natural = DEFAULT_CONTROLS_WIDTH;
}

static void
bacon_video_widget_get_preferred_height (GtkWidget *widget,
                                         gint      *minimum,
                                         gint      *natural)
{
  *minimum = *natural = DEFAULT_CONTROLS_WIDTH / 16 * 9;
}

static gboolean
bvw_boolean_handled_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer foobar)
{
  gboolean continue_emission;
  gboolean signal_handled;
  
  signal_handled = g_value_get_boolean (handler_return);
  g_value_set_boolean (return_accu, signal_handled);
  continue_emission = !signal_handled;
  
  return continue_emission;
}



static void
bacon_video_widget_class_init (BaconVideoWidgetClass * klass)
{

              printf("bacon_video_widget_class_init \n");
              
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkIconTheme *default_theme;

  object_class = (GObjectClass *) klass;
  widget_class = (GtkWidgetClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  /* GtkWidget */
  widget_class->get_preferred_width = bacon_video_widget_get_preferred_width;
  widget_class->get_preferred_height = bacon_video_widget_get_preferred_height;
  widget_class->realize = bacon_video_widget_realize;
  widget_class->unrealize = bacon_video_widget_unrealize;



  /* GObject */
  // called when a property of a GObject instance is set, such as other use g_object_set() 
  object_class->set_property = bacon_video_widget_set_property;
  //  called when a property of a GObject instance is retrieved or read
  object_class->get_property = bacon_video_widget_get_property;
  // called during the object's destruction process, free any resources (like memory or handles) that the object may have allocated during its lifetime.
  object_class->finalize = bacon_video_widget_finalize;



  /* Properties */
  /**
   * BaconVideoWidget:position:
   *
   * The current position in the stream, as a percentage between <code class="literal">0</code> and <code class="literal">1</code>.
   **/
  g_object_class_install_property (object_class, PROP_POSITION,
                                   g_param_spec_double ("position", "Position", "The current position in the stream.",
							0, 1.0, 0,
							G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:stream-length:
   *
   * The length of the current stream, in milliseconds.
   **/
  g_object_class_install_property (object_class, PROP_STREAM_LENGTH,
	                           g_param_spec_int64 ("stream-length", "Stream length",
                                                     "The length of the current stream, in milliseconds.", 0, G_MAXINT64, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:playing:
   *
   * Whether a stream is currently playing.
   **/
  g_object_class_install_property (object_class, PROP_PLAYING,
                                   g_param_spec_boolean ("playing", "Playing?",
                                                         "Whether a stream is currently playing.", FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:seekable:
   *
   * Whether the current stream can be seeked.
   **/
  g_object_class_install_property (object_class, PROP_SEEKABLE,
                                   g_param_spec_boolean ("seekable", "Seekable?",
                                                         "Whether the current stream can be seeked.", FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:volume:
   *
   * The current volume level, as a percentage between <code class="literal">0</code> and <code class="literal">1</code>.
   **/
  g_object_class_install_property (object_class, PROP_VOLUME,
	                           g_param_spec_double ("volume", "Volume", "The current volume level.",
	                                                0.0, 1.0, 0.0,
	                                                G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
  

  /**
   * BaconVideoWidget:brightness:
   *
   * The brightness of the video display.
   **/
  g_object_class_install_property (object_class, PROP_BRIGHTNESS,
                                   g_param_spec_int ("brightness", "Brightness",
                                                      "The brightness of the video display.", 0, 65535, 32768,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:contrast:
   *
   * The contrast of the video display.
   **/
  g_object_class_install_property (object_class, PROP_CONTRAST,
                                   g_param_spec_int ("contrast", "Contrast",
                                                      "The contrast of the video display.", 0, 65535, 32768,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:saturation:
   *
   * The saturation of the video display.
   **/
  g_object_class_install_property (object_class, PROP_SATURATION,
                                   g_param_spec_int ("saturation", "Saturation",
                                                      "The saturation of the video display.", 0, 65535, 32768,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:hue:
   *
   * The hue of the video display.
   **/
  g_object_class_install_property (object_class, PROP_HUE,
                                   g_param_spec_int ("hue", "Hue",
                                                      "The hue of the video display.", 0, 65535, 32768,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:audio-output-type:
   *
   * The type of audio output to use (e.g. the number of channels).
   **/
  g_object_class_install_property (object_class, PROP_AUDIO_OUTPUT_TYPE,
                                   g_param_spec_enum ("audio-output-type", "Audio output type",
                                                      "The type of audio output to use.", BVW_TYPE_AUDIO_OUTPUT_TYPE,
                                                      BVW_AUDIO_SOUND_STEREO,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:av-offset:
   *
   * Control the synchronisation offset between the audio and video streams.
   * Positive values make the audio ahead of the video and negative values
   * make the audio go behind the video.
   **/
  g_object_class_install_property (object_class, PROP_AV_OFFSET,
				   g_param_spec_int64 ("av-offset", "Audio/Video offset",
						       "The synchronisation offset between audio and video in nanoseconds.",
						       G_MININT64, G_MAXINT64,
						       0, G_PARAM_READWRITE |
						       G_PARAM_STATIC_STRINGS));

  /**
   * BaconVideoWidget:show-cursor:
   *
   * Whether the mouse cursor is shown.
   **/
  g_object_class_install_property (object_class, PROP_SHOW_CURSOR,
                                   g_param_spec_boolean ("show-cursor", "Show cursor",
                                                         "Whether the mouse cursor is shown.", FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));



    /**
     * BaconVideoWidget:current-audio-tags:
     *
     * Audio Tags of current movie
    **/
    g_object_class_install_property (object_class, PROP_CUR_AUDIO_TAGS,
      g_param_spec_boxed ("cur-audio-tags", "Audio Tags",
          "The currently active audio tags of current movie ", GST_TYPE_TAG_LIST,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));



    /**
     * BaconVideoWidget:current-video-tags:
     *
     * Video Tags of current movie
    **/
    g_object_class_install_property (object_class, PROP_CUR_VIDEO_TAGS,
      g_param_spec_boxed ("cur-video-tags", "Video Tags",
          "The currently active video tags of current movie", GST_TYPE_TAG_LIST,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));





  /******************************************** Signals ***********************************************/

            
    /*
    emit when btdemux give us its video info(file index & file name & full path)
    */
    bvw_signals[SIGNAL_BTDEMUX_VIDEOINFO] =
    g_signal_new (I_("btdemux-videoinfo"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);




  /**
   * BaconVideoWidget::error:
   * @bvw: the #BaconVideoWidget which received the signal
   * @message: the error message
   * @playback_stopped: %TRUE if playback has stopped due to the error, %FALSE otherwise
   * @fatal: %TRUE if the error was fatal to playback, %FALSE otherwise
   *
   * Emitted when the backend wishes to asynchronously report an error. If @fatal is %TRUE,
   * playback of this stream cannot be restarted.
   **/
  bvw_signals[SIGNAL_ERROR] =
    g_signal_new (I_("error"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);

  /**
   * BaconVideoWidget::eos:
   * @bvw: the #BaconVideoWidget which received the signal
   *
   * Emitted when the end of the current stream is reached.
   **/
  bvw_signals[SIGNAL_EOS] =
    g_signal_new (I_("eos"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  // g_cclosure_marshal_VOID__VOID: 
  // convert data types between C and GObject's type system so that they can be used in callbacks and signal emissions.
  // both the signal and the handler return nothing

  /**
   * BaconVideoWidget::got-metadata:
   * @bvw: the #BaconVideoWidget which received the signal
   *
   * Emitted when the widget has updated the metadata of the current stream. This
   * will typically happen after downloading enough piece of that video within torrent.
   * Emit when torrent has download enough piece to start playing/streaming
   * get the most crucial piece contains [moov] atom so we can start playing or get tags
   * Call bacon_video_widget_get_metadata() to query the updated metadata (Tags)
   **/
  bvw_signals[SIGNAL_GOT_METADATA] =
      g_signal_new (I_("got-metadata"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,/*class closure*/
                  NULL/*class closure notify*/, NULL/*signal accumulator*/, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);



  bvw_signals[SIGNAL_VIDEO_CHANGED] =
      g_signal_new (I_("video-changed"), /*signal name*/
                    G_TYPE_FROM_CLASS (object_class), /*type of the object*/
                    G_SIGNAL_RUN_LAST,/*signal flags*/
                    0,/*class closure*/
                    NULL/*class closure notify*/, NULL/*signal accumulator*/, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);



  bvw_signals[SIGNAL_AUDIO_CHANGED] =
      g_signal_new (I_("audio-changed"), 
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_LAST,
                    0,/*class closure*/
                    NULL/*class closure notify*/, NULL/*signal accumulator*/, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);



  bvw_signals[SIGNAL_VIDEO_TAGS_CHANGED] =
      g_signal_new (I_("video-tags-changed"), 
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_LAST,
                    0,/*class closure*/
                    NULL/*class closure notify*/, NULL/*signal accumulator*/, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 1, G_TYPE_INT);



  bvw_signals[SIGNAL_AUDIO_TAGS_CHANGED] =
      g_signal_new (I_("audio-tags-changed"), 
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_LAST,
                    0,/*class closure*/
                    NULL/*class closure notify*/, NULL/*signal accumulator*/, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 1, G_TYPE_INT);




  /**
   * BaconVideoWidget::channels-change:
   * @bvw: the #BaconVideoWidget which received the signal
   *
   * Emitted when the number of audio languages available changes, or when the
   * selected audio language is changed.
   *
   * Query the new list of audio languages with bacon_video_widget_get_languages().
   **/
  //// bvw_signals[SIGNAL_CHANNELS_CHANGE] =
  ////   g_signal_new (I_("channels-change"),
  ////                 G_TYPE_FROM_CLASS (object_class),
  ////                 G_SIGNAL_RUN_LAST,
  ////                 0,
  ////                 NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);




  /**
   * BaconVideoWidget::tick:
   * @bvw: the #BaconVideoWidget which received the signal
   * @current_time: the current position in the stream, in milliseconds since the beginning of the stream
   * @stream_length: the length of the stream, in milliseconds
   * @current_position: the current position in the stream, as a percentage between <code class="literal">0</code> and <code class="literal">1</code>
   * @seekable: %TRUE if the stream can be seeked, %FALSE otherwise
   *
   * Emitted every time an important time event happens, or at regular intervals when playing a stream.
   **/
  bvw_signals[SIGNAL_TICK] =
    g_signal_new (I_("tick"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE, 4, G_TYPE_INT64, G_TYPE_INT64, G_TYPE_DOUBLE,
                  G_TYPE_BOOLEAN);




  bvw_signals[SIGNAL_PPI_INFO] =
    g_signal_new("ppi-info",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__POINTER,  // POINTER for DownloadingBlocksSd*
                 G_TYPE_NONE, 1, G_TYPE_POINTER);    // G_TYPE_POINTER for the DownloadingBlocksSd*


  bvw_signals[SIGNAL_PIECE_BLOCK_INFO] =
    g_signal_new("piece-block-info",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__POINTER,  // POINTER for the PieceBlockInfoSd*
                 G_TYPE_NONE, 1, G_TYPE_POINTER);    // G_TYPE_POINTER for the PieceBlockInfoSd*


  bvw_signals[SIGNAL_FINISHED_PIECE_INFO] =
    g_signal_new("finished-piece-info",
                 G_TYPE_FROM_CLASS(object_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__POINTER,  // POINTER for the guint8*
                 G_TYPE_NONE, 1, G_TYPE_POINTER);    // G_TYPE_POINTER for the guint8*


  /**
   * BaconVideoWidget::missing-plugins:
   * @bvw: the #BaconVideoWidget which received the signal
   * @details: a %NULL-terminated array of missing plugin details for use when installing the plugins with libgimme-codec
   * @descriptions: a %NULL-terminated array of missing plugin descriptions for display to the user
   * @playing: %TRUE if the stream could be played even without these plugins, %FALSE otherwise
   *
   * Emitted when plugins required to play the current stream are not found. This allows the application
   * to request the user install them before proceeding to try and play the stream again.
   *
   * Note that this signal is only available for the GStreamer backend.
   *
   * Return value: %TRUE if the signal was handled and some action was taken, %FALSE otherwise
   **/
  bvw_signals[SIGNAL_MISSING_PLUGINS] =
    g_signal_new (I_("missing-plugins"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  bvw_boolean_handled_accumulator, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_BOOLEAN, 3, G_TYPE_STRV, G_TYPE_STRV, G_TYPE_BOOLEAN);


  /**
   * BaconVideoWidget::play-starting:
   * @bvw: the #BaconVideoWidget which received the signal
   *
   * Emitted when a movie will start playing, meaning it's not buffering, or paused
   *  waiting for plugins to be installed, drives to be mounted or authentication
   *  to succeed.
   *
   * This usually means that OSD popups can be hidden.
   *
   **/
  bvw_signals[SIGNAL_PLAY_STARTING] =
    g_signal_new ("play-starting",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);



  g_resources_register (_bvw_get_resource ());

  default_theme = gtk_icon_theme_get_default ();
  gtk_icon_theme_add_resource_path (default_theme, "/org/gnome/totem/bvw");

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/totem/bvw/bacon-video-widget.ui");
  gtk_widget_class_bind_template_child (widget_class, BaconVideoWidget, stack);
  gtk_widget_class_bind_template_child (widget_class, BaconVideoWidget, audio_only);
  gtk_widget_class_bind_template_child (widget_class, BaconVideoWidget, broken_video);
}



static gboolean bvw_query_timeout (BaconVideoWidget *bvw);
static gboolean bvw_downloading_ppi_timeout (BaconVideoWidget *bvw);
static void parse_stream_info (BaconVideoWidget *bvw);





//when switch in playlist
static void
bvw_update_stream_info (BaconVideoWidget *bvw)
{

              // printf("(bvw_update_stream_info) \n");

  parse_stream_info (bvw);

printf ("(bvw_update_stream_info) emit got-metadata \n");

  g_signal_emit (bvw, bvw_signals[SIGNAL_GOT_METADATA], 0, NULL);

}





/*
let totem-object retrieve videos info got to update playlist
*/
void 
bacon_video_widget_retrieve_btdemux_video_info (BaconVideoWidget *bvw, GList **  videos_info_addr)
{

  if(videos_info_addr != NULL && bvw->videos_info != NULL)
  {
    printf("(bacon_video_widget_retrieve_btdemux_video_info) Got it \n");

    // Pass the ownership of the videos_info to the caller
    *videos_info_addr = bvw->videos_info;

  }
  else
  {
    printf("(bacon_video_widget_retrieve_btdemux_video_info) EMPTY \n");
  }
}





/*********helper**********/
void 
bvw_g_object_association_clear (BaconVideoWidget *bvw, const gchar* name)
{
  g_object_set_data (G_OBJECT(bvw->btdemux), name, NULL);
}






static void
bvw_handle_application_message (BaconVideoWidget *bvw, GstMessage *msg)
{
    const GstStructure *structure;
    const gchar *type_name = NULL;
    gchar *src_name;

              printf("(bvw_handle_application_message) entering \n");
    src_name = gst_object_get_name (GST_OBJECT_CAST(msg->src));

    structure = gst_message_get_structure (msg);
    if (structure)
    {
        type_name = gst_structure_get_name (structure);
                                printf("(bvw_handle_application_message) src_name is:%s,type_name is:%s \n", src_name, type_name);

    }
    
    if (type_name == NULL)
    {
        goto unhandled;
    }



    if (strcmp (type_name, "got-piece-block-info") == 0) 
    {
        PieceBlockInfoSd *shared_data = g_object_get_data (G_OBJECT(bvw->btdemux), "piece-block-info");
        if (shared_data) 
        {
            g_signal_emit (bvw, bvw_signals[SIGNAL_PIECE_BLOCK_INFO], 0, shared_data);     
        }
        goto done;
    } 

    else if (strcmp (type_name, "got-piece-finished-info") == 0) 
    {
        guint8 *finished_pieces = g_object_get_data (G_OBJECT(bvw->btdemux), "piece-finished");
        if(finished_pieces)
        {
            g_signal_emit (bvw, bvw_signals[SIGNAL_FINISHED_PIECE_INFO], 0, finished_pieces);
        }
        goto done;
    }

    else if (strcmp (type_name, "start-ppi") == 0) 
    {
              printf ("(bvw_handle_application_message) Pausing because we're not ready to play the buffer yet, SET bvw->pipeline state to PAUSED\n");

      gst_element_set_state (GST_ELEMENT (bvw->pipeline), GST_STATE_PAUSED);//really need it ?
      bvw_reconfigure_ppi_timeout (bvw, 1000);

      goto done;
    }
    
    // whole torrent finished  downloading
    else if (strcmp (type_name, "stop-ppi") == 0) 
    {
      
              printf ("(bvw_handle_application_message) torrent download finished, stopping the ppi \n");
            
        /* do query for the last time */
        bvw_downloading_ppi_timeout (bvw);
        /* torrent finished, so don't run the timeout anymore */
        bvw_reconfigure_ppi_timeout (bvw, 0);

        goto done;
    }


unhandled:
    GST_WARNING ("Unhandled element message %s from %s: %" GST_PTR_FORMAT,
                  GST_STR_NULL (type_name), GST_STR_NULL (src_name), msg);

done:
    // gst_structure_free (structure);
    g_free (src_name);

}







static void
bvw_handle_element_message (BaconVideoWidget *bvw, GstMessage *msg)
{
  const GstStructure *structure;
  const gchar *type_name = NULL;
  gchar *src_name;

  src_name = gst_object_get_name (GST_OBJECT_CAST(msg->src));

  // Returns: (transfer none) 
  structure = gst_message_get_structure (msg);
  if (structure)
  {
    type_name = gst_structure_get_name (structure);

                                // printf("(bvw_handle_element_message) src_name is:%s,type_name is:%s \n", src_name, type_name);

  }


  // GST_DEBUG ("from %s: %" GST_PTR_FORMAT, src_name, structure);

  if (type_name == NULL)
  {
    goto unhandled;
  }

  if (strcmp (type_name, "btdemux-video-info") == 0) 
  {

                                printf("(bvw_handle_element_message) src_name is:btdemux,type_name is:btdemux-video-info  \n");

    const GValue* pairs = G_VALUE_INIT;
    
    // Returns: (transfer none) 
    pairs = gst_structure_get_value (structure, "pairs");

    GList *res_list = NULL;

    if (GST_VALUE_HOLDS_ARRAY (pairs))
    {

            printf("gst_value_array_get_size = %d \n", gst_value_array_get_size (pairs));

      for (guint i = 0; i < gst_value_array_get_size (pairs); i++) 
      {
          //ret (transfer none)
          const GValue *stru_value = gst_value_array_get_value (pairs, i);

          if (GST_VALUE_HOLDS_STRUCTURE (stru_value))
          {
              gint key = -2;
              const gchar *fname = "";
              const gchar *fpath = "";
              // Retrieve each field 

              const GstStructure *stru = (const GstStructure *) g_value_get_boxed (stru_value);

              const gchar * name =   gst_structure_get_name (stru);
                        // printf ("(bvw) name is %s\n", name);
  
              if (g_strcmp0(name, "pair") == 0)
              {
                if ( gst_structure_has_field (stru, "fidx"))
                {
                  gst_structure_get_int (stru, "fidx", &key);
                            // printf ("(bvw) fidx is %d\n", key);
                            
                }
              
                if ( gst_structure_has_field (stru, "fname"))
                {
                  //transfer none
                  fname = gst_structure_get_string (stru, "fname");
                        printf ("(bvw) fname is %s\n", fname);

                }
    
                if ( gst_structure_has_field (stru, "fpath"))
                {
                  fpath = gst_structure_get_string (stru, "fpath");
                        printf ("(bvw) fpath is %s\n", fpath);

                }
              }

              printf("(btdemux-video-info) Key: %d, FileName: %s, FullPath: %s\n", key, fname, fpath);

              // Create a GHashTable to store the key-value pair
              GHashTable *pair = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,NULL /*no use g_free,we impl our own*/);

              // Insert the key-value pair into the GHashTable, transfer full
              g_hash_table_insert (pair, "idx", GINT_TO_POINTER (key));
              g_hash_table_insert (pair, "fname", g_strdup (fname));
              g_hash_table_insert (pair, "fpath", g_strdup (fpath));

              // Add the GHashTable to the GList
              res_list = g_list_append (res_list, pair);



              ////gst_structure_free (stru);

               g_value_unset (stru_value);
          }            
         
      }
    }



    bvw->videos_info = res_list;

    // dont cleanup it now, for later use
    // g_list_free_full(res_list, (GDestroyNotify)g_hash_table_destroy);

    // g_value_unset (pairs);
  
    //got'em all, then we need to let totem object knows it, so it can update its totem-playlist
    //emit in bacon-video-widget.c, let totem-object.c to receive videos info for construct playlist
    g_signal_emit (bvw, bvw_signals[SIGNAL_BTDEMUX_VIDEOINFO], 0, NULL);
  }

  //missing gstreamer plugin
  else if (gst_is_missing_plugin_message (msg)) 
  {
    bvw->missing_plugins =
      g_list_prepend (bvw->missing_plugins, gst_message_ref (msg));
    goto done;
  } 

unhandled:
  GST_WARNING ("Unhandled element message %s from %s: %" GST_PTR_FORMAT,
      GST_STR_NULL (type_name), GST_STR_NULL (src_name), msg);

done:
  g_free (src_name);


}


/* This is a hack to avoid doing poll_for_state_change() indirectly
 * from the bus message callback (via EOS => totem => close => wait for READY)
 * and deadlocking there. We need something like a
 * gst_bus_set_auto_flushing(bus, FALSE) ... */
static gboolean
bvw_signal_eos_delayed (gpointer user_data)
{
  BaconVideoWidget *bvw = BACON_VIDEO_WIDGET (user_data);

  g_signal_emit (bvw, bvw_signals[SIGNAL_EOS], 0, NULL);
  bvw->eos_id = 0;
  return FALSE;
}



static void
bvw_reconfigure_tick_timeout (BaconVideoWidget *bvw, guint msecs)
{
  if (bvw->update_id != 0) {

              printf ("(bvw_reconfigure_tick_timeout) removing tick timeout \n");

    GST_DEBUG ("removing tick timeout");
    g_source_remove (bvw->update_id);
    bvw->update_id = 0;
  }
  if (msecs > 0) {

              printf ("(bvw_reconfigure_tick_timeout) adding tick timeout (at %ums) \n", msecs);

    GST_DEBUG ("adding tick timeout (at %ums)", msecs);
    bvw->update_id =
      g_timeout_add (msecs, (GSourceFunc) bvw_query_timeout, bvw);
    g_source_set_name_by_id (bvw->update_id, "[totem] bvw_query_timeout");
  }
}



static void
bvw_reconfigure_ppi_timeout (BaconVideoWidget *bvw, guint msecs)
{
  if (bvw->ppi_id != 0) {
              printf("(bvw_reconfigure_ppi_timeout) removing ppi timeout\n");

    GST_DEBUG ("removing ppi timeout");
    g_source_remove (bvw->ppi_id);
    bvw->ppi_id = 0;
  }
  if (msecs > 0) {
              printf("(bvw_reconfigure_ppi_timeout) adding ppi timeout (at %ums)\n", msecs);

    GST_DEBUG ("adding ppi timeout (at %ums)", msecs);
    bvw->ppi_id =
      g_timeout_add (msecs, (GSourceFunc) bvw_downloading_ppi_timeout, bvw);
    g_source_set_name_by_id (bvw->ppi_id, "[totem] bvw_downaloding_ppi_timeout");
  }
}




/* returns TRUE if the error has been handled and should be ignored */
static gboolean
bvw_check_missing_plugins_error (BaconVideoWidget * bvw, GstMessage * err_msg)
{
  gboolean error_src_is_playbin;
  GError *err = NULL;

  if (bvw->missing_plugins == NULL) {
    GST_DEBUG ("no missing-plugin messages");
    return FALSE;
  }

  gst_message_parse_error (err_msg, &err, NULL);

  error_src_is_playbin = (err_msg->src == GST_OBJECT_CAST (bvw->pipeline));

  /* If we get a WRONG_TYPE error from playbin itself it's most likely because
   * there is a subtitle stream we can decode, but no video stream to overlay
   * it on. Since there were missing-plugins messages, we'll assume this is
   * because we cannot decode the video stream (this should probably be fixed
   * in playbin, but for now we'll work around it here) */
  if (is_error (err, CORE, MISSING_PLUGIN) ||
      is_error (err, STREAM, CODEC_NOT_FOUND) ||
      (is_error (err, STREAM, WRONG_TYPE) && error_src_is_playbin)) {
    bvw_check_if_video_decoder_is_missing (bvw);
    set_current_actor (bvw);
  } else {
    GST_DEBUG ("not an error code we are looking for, doing nothing");
  }

  g_error_free (err);
  return FALSE;
}

//maybe no need
static gboolean
bvw_check_mpeg_eos (BaconVideoWidget *bvw, GstMessage *err_msg)
{
      printf ("(bvw_check_mpeg_eos) \n");
  gboolean ret = FALSE;
  g_autoptr(GError) err = NULL;
  g_autofree char *dbg = NULL;

  gst_message_parse_error (err_msg, &err, &dbg);

  /* Error from gst-libs/gst/video/gstvideodecoder.c
   * thrown by mpeg2dec */

  if (err != NULL &&
      dbg != NULL &&
      is_error (err, STREAM, DECODE) &&
      strstr (dbg, "no valid frames found")) {
    if (bvw->eos_id == 0) {
      bvw->eos_id = g_idle_add (bvw_signal_eos_delayed, bvw);
      g_source_set_name_by_id (bvw->eos_id, "[totem] bvw_signal_eos_delayed");
      GST_DEBUG ("Throwing EOS instead of an error when seeking to the end of an MPEG file");
    } else 
    {
      GST_DEBUG ("Not throwing EOS instead of an error when seeking to the end of an MPEG file, EOS already planned");
    }
    ret = TRUE;
  }

  return ret;
}






/***************************************************************************** */
/******************************Tags-Related*************************************/
/*******************************************************************************/
static void
bvw_update_tags (BaconVideoWidget * bvw, GstTagList *tag_list, const gchar *type)
{
  GstTagList **cache = NULL;
  GstTagList *result;
  const gchar* property_name_str = NULL;

  /* all tags (replace previous tags, title/artist/etc. might change
   * in the middle of a stream, e.g. with radio streams) */
  // Returns: (transfer full) (nullable): the new list
  result = gst_tag_list_merge (bvw->tagcache, tag_list,
                                   GST_TAG_MERGE_REPLACE);


  //new tags equals to old ones, nothing interesting  just return
  if (bvw->tagcache && result 
      && gst_tag_list_is_equal (result/*new-tags*/, bvw->tagcache/*old-tags*/)) 
  {
      gst_tag_list_unref (result);
      GST_WARNING ("Pipeline sent %s tags update with no changes", type);

                      printf("(bvw_update_tags) Pipeline sent %s tags update with no changes \n", type);

      return;
  }
  g_clear_pointer (&bvw->tagcache, gst_tag_list_unref);
  //cache tag whether it is audio or video
  bvw->tagcache = result;
  // GST_DEBUG ("Tags: %" GST_PTR_FORMAT, tag_list);


  /* Dispatching to media-type-specific tags */
  if (!strcmp (type, "video")) 
  {
    cache = &bvw->videotags;
    property_name_str = "cur-video-tags";
  } 
  else if (!strcmp (type, "audio")) 
  {
    cache = &bvw->audiotags;
    property_name_str = "cur-audio-tags";

  }
  if (cache) 
  {
    // Returns: (transfer full) (nullable): the new list
    result = gst_tag_list_merge (*cache, tag_list, GST_TAG_MERGE_REPLACE);
    if (*cache)
    {
      gst_tag_list_unref (*cache);
    }
    *cache = result;
  }

                      printf("(bvw_update_tags) gain %s Tags: %" GST_PTR_FORMAT " \n", type, tag_list);

  /* clean up */
  if (tag_list)
  {
    gst_tag_list_unref (tag_list);
  }

  if (property_name_str != NULL)
  {
    // notify others, so notify::tags callback called  for both audio tags and video tags
    g_object_notify (G_OBJECT (bvw), property_name_str);
  }

printf ("(bvw_update_tags) emit got-metadata \n");

  g_signal_emit (bvw, bvw_signals[SIGNAL_GOT_METADATA], 0);

  /*set backgronud for player page, video , audio-only , or broken video alert*/
  set_current_actor (bvw);


}



static void
update_tags_delayed_data_destroy (UpdateTagsDelayedData *data)
{
  /*clean-up*/
  g_slice_free (UpdateTagsDelayedData, data);
}


static gboolean
bvw_update_tags_dispatcher (BaconVideoWidget *bvw)
{

  UpdateTagsDelayedData *data;

  /* If we take the queue's lock for the entire function call, we can use it to protect tag_update_id too */
  g_async_queue_lock (bvw->tag_update_queue);

  /*entering a while loop, pop items from async-queue holding tags data(audio tags or video tags)*/
  while ((data = g_async_queue_try_pop_unlocked (bvw->tag_update_queue)) != NULL) 
  {

        // if (GST_IS_TAG_LIST(data->tags))
        //     printf("bvw_update_tags_dispatcher(valid taglist) \n");
        // else
        //     printf("bvw_update_tags_dispatcher(not valid taglist) \n");

    bvw_update_tags (bvw, data->tags, data->type);

    update_tags_delayed_data_destroy (data);
  }

  bvw->tag_update_id = 0;
  g_async_queue_unlock (bvw->tag_update_queue);

  return FALSE;
}


/* Marshal the changed tags to the main thread for updating the GUI
 * and sending the BVW signals */
static void
bvw_update_tags_delayed (BaconVideoWidget *bvw, GstTagList *tags, const gchar *type) 
{

  UpdateTagsDelayedData *data = g_slice_new0 (UpdateTagsDelayedData);

  // hold on to a reference to the data
  data->tags = gst_tag_list_ref (tags);
  data->type = type;

  g_async_queue_lock (bvw->tag_update_queue);
  /*Push tags data to async-queue*/
  g_async_queue_push_unlocked (bvw->tag_update_queue, data);

  if (bvw->tag_update_id == 0) 
  {
    bvw->tag_update_id = g_idle_add ((GSourceFunc) bvw_update_tags_dispatcher, bvw);
    g_source_set_name_by_id (bvw->tag_update_id, "[totem] bvw_update_tags_dispatcher");
  }

  g_async_queue_unlock (bvw->tag_update_queue);

}

/****************Tags Changed Callback*****************/
//maybe thses two cb are of no use, since we dont implement the audio streams switcher,(i haven't meet multiple video tracks/streams case)
//such as one movie has multiple audio tracks,but we dont implement this, we default to play the first stream
//so these two cb only be called when first get tags, but they unlikely to change, cuz just movie in torrent, not a live stream
static void
video_tags_changed_cb (GstElement *elem, gint stream_id, gpointer user_data)
{

                  printf("In video_tags_changed_cb \n");
        
  BaconVideoWidget *bvw = (BaconVideoWidget *) user_data;
  GstTagList *tags = NULL;
  gint current_stream_id = 0;

  if(bvw->video_channels)
  {
    current_stream_id = bvw->video_channels->len;//current stream id set to the latest idx of video_channels
  }

  /* Only get the updated tags if it's latest stream id inserted in on_pad_added once a new outpad added for gstdecodebin2*/
  if (current_stream_id != stream_id)
  {
    return;
  }
  
  g_object_get (bvw, "cur-video-tags", &tags, NULL);
 
  if (tags)
  {
    bvw_update_tags_delayed (bvw, tags, "video");
  }   
}

static void
audio_tags_changed_cb (GstElement *elem, gint stream_id, gpointer user_data)
{

                  printf("In audio_tags_changed_cb \n");

  BaconVideoWidget *bvw = (BaconVideoWidget *) user_data;
  GstTagList *tags = NULL;
  gint current_stream_id = 0;

  if(bvw->audio_channels)
  {
    current_stream_id = bvw->audio_channels->len;//current stream id set to the latest idx of audio_channels
  }

  /* Only get the updated tags if it's latest stream id inserted in on_pad_added once a new outpad added for gstdecodebin2*/
  if (current_stream_id != stream_id)
  {
    return;
  }

  g_object_get (bvw, "cur-audio-tags", &tags, NULL);

  if (tags)
  {
    bvw_update_tags_delayed (bvw, tags, "audio");
  }
}






/*Handling Three-Piece-Area Buffering msg from btdemux*/
static void
bvw_handle_buffering_message (GstMessage * message, BaconVideoWidget *bvw)
{
  gint percent = 0;

  gst_message_parse_buffering (message, &percent);

  /**********Three-Piece-Area Buffered finished***********/
  if (percent >= 100) 
  {
    /* a 100% message means buffering is done */
    bvw->buffering = FALSE;
    /* if the desired state is playing, go back */
    if (bvw->target_state == GST_STATE_PLAYING) 
    {
      // GST_DEBUG ("Buffering done, setting pipeline back to PLAYING");
                printf("(bvw_handle_buffering_message) Buffering done, setting pipeline back to PLAYING\n");
      bacon_video_widget_play (bvw, NULL);
    } 
    else 
    {
      // GST_DEBUG ("Buffering done, keeping pipeline PAUSED");
                printf("(bvw_handle_buffering_message) Buffering done, keeping pipeline PAUSED\n");
    }
  } 

  /**********Three-Piece-Area Buffered not finished And we are not paused***********/
  else if (bvw->target_state == GST_STATE_PLAYING) 
  {
    GstState cur_state;

    gst_element_get_state (bvw->pipeline, &cur_state, NULL, 0);
    if (cur_state != GST_STATE_PAUSED) 
    {
      // GST_DEBUG ("Buffering ... temporarily pausing playback %d%%", percent);
      
                printf("(bvw_handle_buffering_message) Buffering... temporarily pausing playback %d%% and SET bvw->pipeline state to PAUSED\n", percent);

      gst_element_set_state (bvw->pipeline, GST_STATE_PAUSED);
    } 
    else 
    {
                printf("(bvw_handle_buffering_message) Buffering (already paused) ... %d%%\n", percent);
      // GST_LOG ("Buffering (already paused) ... %d%%", percent);
    }
    bvw->buffering = TRUE;
  } 

  /**********Three-Piece-Area Buffered not finished And we've already let paused , do nothing and just report the progress***********/
  else 
  {
    // GST_LOG ("Buffering ... %d", percent);
                printf("(bvw_handle_buffering_message)Buffering ... %d\n", percent);
    bvw->buffering = TRUE;
  }
}





/* GST MESSAGE HANDLING */
static void
bvw_bus_message_cb (GstBus * bus, GstMessage * message, BaconVideoWidget *bvw)
{
  GstMessageType msg_type;

  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));

  msg_type = GST_MESSAGE_TYPE (message);

  if (msg_type != GST_MESSAGE_STATE_CHANGED) {
    gchar *src_name = gst_object_get_name (GST_OBJECT_CAST(message->src));
    GST_LOG ("Handling %s message from element %s",
        gst_message_type_get_name (msg_type), src_name);

                            // printf("(bvw_bus_message_cb) Handling %s message from element %s \n",
                            //  gst_message_type_get_name (msg_type), src_name );

    g_free (src_name);
  }

  switch (msg_type) 
  {  
    case GST_MESSAGE_ERROR: 
    {
                  printf("(bvw_bus_message_cb) GST_MESSAGE_ERROR: \n");
      // totem_gst_message_print (message, bvw->pipeline, "totem-error");

      // if (!bvw_check_missing_plugins_error (bvw, message) &&
	    //     !bvw_check_mpeg_eos (bvw, message)) {
      //   GError *error;
      //   error = bvw_error_from_gst_error (bvw, message);
      //   bvw->target_state = GST_STATE_NULL;
      //   if (bvw->pipeline)
      //     gst_element_set_state (bvw->pipeline, GST_STATE_NULL);
      //   bvw->buffering = FALSE;
      //   g_signal_emit (bvw, bvw_signals[SIGNAL_ERROR], 0,
      //                  error->message, TRUE);
      //   g_error_free (error);
      // }

      break;
    }

    case GST_MESSAGE_WARNING: 
    {
      GST_WARNING ("Warning message: %" GST_PTR_FORMAT, message);
      break;
    }

    case GST_MESSAGE_TAG: 
      /* Ignore TAG messages, we get updated tags from the
       * GST_EVENT_TAG in bvw_decodebin_video_outpad_event 
       * or bvw_decodebin_audio_outpad_event
       */
      break;

    case GST_MESSAGE_EOS:
    {
      GST_DEBUG ("EOS message");
      
              printf("(bvw_bus_message_cb) Got EOS Message \n");

      /* update slider one last time */
      bvw_query_timeout (bvw);
      if (bvw->eos_id == 0) 
      {
        bvw->eos_id = g_idle_add (bvw_signal_eos_delayed, bvw);
        g_source_set_name_by_id (bvw->eos_id, "[totem] bvw_signal_eos_delayed");
      }
      break;
    }

    case GST_MESSAGE_BUFFERING:
    {
        printf("bvw_bus_message_cb GST_MESSAGE_BUFFERING \n");
      bvw_handle_buffering_message (message, bvw);
      break;
    }



    case GST_MESSAGE_APPLICATION: 
    {
      bvw_handle_application_message (bvw, message);
      break;
    }




    //**************************************might see gstbasesink.c***************************************
    case GST_MESSAGE_STATE_CHANGED: 
    {
      GstState old_state, new_state;
      gchar *src_name;

      gst_message_parse_state_changed (message, &old_state, &new_state, NULL);

      if (old_state == new_state)
        break;

      /* we only care about bvw->pipeline state changes */
      if (GST_MESSAGE_SRC (message) != GST_OBJECT (bvw->pipeline))
      {

        break;
      }

      src_name = gst_object_get_name (GST_OBJECT_CAST(message->src));

                  printf("(bvw_bus_message_cb) %s changed state from %s to %s\n", 
                      src_name,
                      gst_element_state_get_name (old_state),
                      gst_element_state_get_name (new_state)
                      );

      // GST_DEBUG ("%s changed state from %s to %s", src_name,
      //     gst_element_state_get_name (old_state),
      //     gst_element_state_get_name (new_state));
      g_free (src_name);


      /* now do stuff */
      if (new_state <= GST_STATE_PAUSED) 
      {
        bvw_query_timeout (bvw);
        bvw_reconfigure_tick_timeout (bvw, 0);
      } else if (new_state > GST_STATE_PAUSED) 
      {
        bvw_reconfigure_tick_timeout (bvw, 200);
      }

      //state changed from READY to PAUSED, maybe it is start playing
      if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) 
      {
                    //// GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN_CAST (bvw->pipeline),
                    ////     GST_DEBUG_GRAPH_SHOW_ALL ^ GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS,
                    ////     "totem-prerolled");

        gint64 stream_length = bacon_video_widget_get_stream_length (bvw);

                  printf("(bvw_bus_message_cb) In GST_MESSAGE_STATE_CHANGED: stream len = %ld\n", stream_length);

        bvw_update_stream_info (bvw);
        /* show a non-fatal warning message if we can't decode the video */
        bvw_show_error_if_video_decoder_is_missing (bvw);
        /* Now that we have the length, check whether we wanted
        * to pause or to stop the pipeline */
        if (bvw->target_state == GST_STATE_PAUSED)
        {
                  printf("(bvw_bus_message_cb) GST_MESSAGE_STATE_CHANGED, next line call bacon_video_widget_pause\n");

	        bacon_video_widget_pause (bvw);
        }
      } 
      //state changed from PAUSED back to READY, maybe it is played and reach EOS
      else if (old_state == GST_STATE_PAUSED && new_state == GST_STATE_READY) 
      {

                printf ("(bvw_bus_message_cb) pipeline state changed from PAUSED back to READY, clear related tags cache \n");

        bvw->media_has_video = FALSE;
        bvw->media_has_audio = FALSE;
	      bvw->media_has_unsupported_video = FALSE;

        /* clean metadata cache */
        g_clear_pointer (&bvw->tagcache, gst_tag_list_unref);
        g_clear_pointer (&bvw->audiotags, gst_tag_list_unref);
        g_clear_pointer (&bvw->videotags, gst_tag_list_unref);

        bvw->video_width = 0;
        bvw->video_height = 0;
      }
      break;
    }
    /*******************************************************************************/



    case GST_MESSAGE_ELEMENT: {
  
      g_object_notify (G_OBJECT (bvw), "seekable");
      bvw_handle_element_message (bvw, message);
      break;
    }

    case GST_MESSAGE_DURATION_CHANGED: {
      gint64 len = -1;
      if (gst_element_query_duration (bvw->pipeline, GST_FORMAT_TIME, &len) && len != -1) {
        bvw->stream_length = len / GST_MSECOND;
	GST_DEBUG ("got new stream length (through duration message) %" G_GINT64_FORMAT, bvw->stream_length);
      }
      break;
    }

    case GST_MESSAGE_ASYNC_DONE: {
          printf ("(bvw_bus_message_cb) In GST_MESSAGE_ASYNC_DONE case \n");
	gint64 _time;
	/* When a seek has finished, set the playing state again */
	g_mutex_lock (&bvw->seek_mutex);

	bvw->seek_req_time = gst_clock_get_internal_time (bvw->clock);
	_time = bvw->seek_time;
	bvw->seek_time = -1;

	g_mutex_unlock (&bvw->seek_mutex);

	if (_time >= 0) {

          printf("(bvw_bus_message_cb) Have an old seek to schedule, doing it now \n");

	  GST_DEBUG ("Have an old seek to schedule, doing it now");
	  bacon_video_widget_seek_time_no_lock (bvw, _time, 0, NULL);
	} else if (bvw->target_state == GST_STATE_PLAYING) {

          printf("(bvw_bus_message_cb) Maybe starting deferred playback after seek \n");

	  GST_DEBUG ("Maybe starting deferred playback after seek");
	  bacon_video_widget_play (bvw, NULL);
	}

	bacon_video_widget_get_stream_length (bvw);
	bacon_video_widget_is_seekable (bvw);
      break;
    }

    /* FIXME: at some point we might want to handle CLOCK_LOST and set the
     * pipeline back to PAUSED and then PLAYING again to select a different
     * clock (this seems to trip up rtspsrc though so has to wait until
     * rtspsrc gets fixed) */
    case GST_MESSAGE_CLOCK_PROVIDE:
    case GST_MESSAGE_CLOCK_LOST:
    case GST_MESSAGE_NEW_CLOCK:
    case GST_MESSAGE_STATE_DIRTY:
    case GST_MESSAGE_STREAM_STATUS:
      break;

    case GST_MESSAGE_UNKNOWN:
    case GST_MESSAGE_INFO:
    case GST_MESSAGE_STEP_DONE:
    case GST_MESSAGE_STRUCTURE_CHANGE:
    case GST_MESSAGE_SEGMENT_START:
    case GST_MESSAGE_SEGMENT_DONE:
    case GST_MESSAGE_LATENCY:
    case GST_MESSAGE_ASYNC_START:
    case GST_MESSAGE_REQUEST_STATE:
    case GST_MESSAGE_STEP_START:
    case GST_MESSAGE_QOS:
    case GST_MESSAGE_PROGRESS:
    case GST_MESSAGE_ANY:
    case GST_MESSAGE_RESET_TIME:
    case GST_MESSAGE_STREAM_START:
    case GST_MESSAGE_NEED_CONTEXT:
    case GST_MESSAGE_HAVE_CONTEXT:
    default:
      GST_LOG ("Unhandled message: %" GST_PTR_FORMAT, message);
      break;
  }
}

static void
got_time_tick (GstElement * play, gint64 time_nanos, BaconVideoWidget * bvw)
{
  gboolean seekable;

  bvw->current_time = (gint64) time_nanos / GST_MSECOND;

          // printf("(bacon_video_widget/GOT_TIME_TICK) bvw->current_time = %ld \n", bvw->current_time);

  if (bvw->stream_length == 0) 
  {
    bvw->current_position = 0;
  } 
  else 
  {
    bvw->current_position = 
            (gdouble) bvw->current_time / bvw->stream_length;
  }

  if (bvw->stream_length == 0) 
  {
    seekable = bacon_video_widget_is_seekable (bvw);
  } 
  else 
  {
    if (bvw->seekable == -1)
    {
      g_object_notify (G_OBJECT (bvw), "seekable");
    }
    seekable = TRUE;
  }


/*
  GST_DEBUG ("current time: %" GST_TIME_FORMAT ", stream length: %" GST_TIME_FORMAT ", seekable: %s",
      GST_TIME_ARGS (bvw->current_time * GST_MSECOND),
      GST_TIME_ARGS (bvw->stream_length * GST_MSECOND),
      (seekable) ? "TRUE" : "FALSE");
*/

  g_signal_emit (bvw, bvw_signals[SIGNAL_TICK], 0,
                 bvw->current_time, bvw->stream_length,
                 bvw->current_position,
                 seekable);
}





static gboolean
bvw_query_timeout (BaconVideoWidget *bvw)
{
  gint64 pos = -1;

  /* check pos of stream */
  if (gst_element_query_position (bvw->pipeline, GST_FORMAT_TIME, &pos)) {
    if (pos != -1) {
      got_time_tick (GST_ELEMENT (bvw->pipeline), pos, bvw);
    }
  } else {
    GST_DEBUG ("could not get position");
  }
                  //printf("(bvw_query_timeout) pos=%ld \n", pos);

  return TRUE;
}





/**********************************************************************************************************/
/***************************************BUFFERING RELATED**************************************************/
/**********************************************************************************************************/

/*query downloading blocks progress of torrent*/
static gboolean
bvw_downloading_ppi_timeout (BaconVideoWidget *bvw)
{
                          printf("(bvw_downaloding_ppi_timeout) \n");

  DownloadingBlocksSd* ppi = NULL;

  g_signal_emit_by_name (G_OBJECT (bvw->btdemux), "get-ppi", &ppi);

  if(ppi)
  {
      g_signal_emit (bvw, bvw_signals[SIGNAL_PPI_INFO], 0, ppi);
  }

  return TRUE;

}







static void
caps_set (GObject * obj,
    GParamSpec * pspec, BaconVideoWidget * bvw)
{
  GstPad *pad = GST_PAD (obj);
  GstStructure *s;
  GstCaps *caps;

  if (!(caps = gst_pad_get_current_caps (pad)))
  {
    return;
  }

  /* Get video decoder caps */
  s = gst_caps_get_structure (caps, 0);
  if (s) {
    /* We need at least width/height and framerate */
    if (!(gst_structure_get_fraction (s, "framerate", &bvw->video_fps_n,
          &bvw->video_fps_d) &&
          gst_structure_get_int (s, "width", &bvw->video_width) &&
          gst_structure_get_int (s, "height", &bvw->video_height)))
    {
      printf("(caps_set) fps_n: %d, fps_d: %d, width: %d, height: %d \n", 
      bvw->video_fps_n,
      bvw->video_fps_d,
      bvw->video_width,
      bvw->video_height);

      return;

    }
  }

  gst_caps_unref (caps);
}



static void
parse_stream_info (BaconVideoWidget *bvw)
{
  GstPad *video_pad = NULL;
  gint n_video, n_audio;

  n_video = bvw->video_channels ? bvw->video_channels->len : 0;
  n_audio = bvw->audio_channels ? bvw->audio_channels->len : 0;

  bvw->media_has_video = FALSE;

  if (n_video > 0) 
  {
    gint i;

    bvw->media_has_video = TRUE;
    for(i=0; i<n_video && video_pad==NULL; i++)
    {
      video_pad = g_ptr_array_index (bvw->video_channels, i);
    }
  }

  bvw->media_has_audio = (n_audio > 0);

  if (video_pad) 
  {
    GstCaps *caps;

    if ((caps = gst_pad_get_current_caps (video_pad))) 
    {
      caps_set (G_OBJECT (video_pad), NULL, bvw);
      gst_caps_unref (caps);
    }

    //"caps" property defined in #GstPad (see gstpad.c)
    g_signal_connect (video_pad, "notify::caps", G_CALLBACK (caps_set), bvw);

    //dont unref it prematurely, it managed within in gstdecodebin2
    //// gst_object_unref (video_pad);
  }

  set_current_actor (bvw);
}




static void
bvw_stream_changed_cb (BaconVideoWidget *bvw)
{

                printf("(bvw_stream_changed_cb) \n");

  bvw_update_stream_info (bvw);
}





static void
bacon_video_widget_finalize (GObject * object)
{
  
                            printf("(bacon_video_widget_finalize) \n");

  BaconVideoWidget *bvw = (BaconVideoWidget *) object;

  GST_DEBUG ("finalizing");

  g_type_class_unref (g_type_class_peek (BVW_TYPE_METADATA_TYPE));
  g_type_class_unref (g_type_class_peek (BVW_TYPE_ROTATION));

  if (bvw->bus) 
  {
    /* make bus drop all messages to make sure none of our callbacks is ever
     * called again (main loop might be run again to display error dialog) */
    gst_bus_set_flushing (bvw->bus, TRUE);

    if (bvw->sig_bus_async)
      g_signal_handler_disconnect (bvw->bus, bvw->sig_bus_async);

    g_clear_pointer (&bvw->bus, gst_object_unref);
  }

  g_clear_error (&bvw->init_error);
  
  g_clear_object (&bvw->clock);

  if (bvw->pipeline != NULL)
  {
              printf ("(bacon_video_widget_finalize) SET bvw->pipeline state to NULL\n");

    gst_element_set_state (bvw->pipeline, GST_STATE_NULL);
  }

  g_clear_object (&bvw->pipeline);

  if (bvw->update_id) 
  {
    g_source_remove (bvw->update_id);
    bvw->update_id = 0;
  }


  g_clear_pointer (&bvw->tagcache, gst_tag_list_unref);
  g_clear_pointer (&bvw->audiotags, gst_tag_list_unref);
  g_clear_pointer (&bvw->videotags, gst_tag_list_unref);


  if (bvw->tag_update_id != 0)
  {
    g_source_remove (bvw->tag_update_id);
  }
  g_async_queue_unref (bvw->tag_update_queue);

  if (bvw->eos_id != 0) 
  {
    g_source_remove (bvw->eos_id);
    bvw->eos_id = 0;
  }

  //clear video_channels
  for (guint i = 0; i < bvw->video_channels->len; i++) {
    GstPad *pad = g_ptr_array_index (bvw->video_channels, i);
    gst_object_unref (pad);  // Decrement reference count
  }
  g_ptr_array_free (bvw->video_channels, TRUE);


  //clear audio_channels
  for (guint i = 0; i < bvw->audio_channels->len; i++) {
    GstPad *pad = g_ptr_array_index (bvw->audio_channels, i);
    gst_object_unref (pad);  // Decrement reference count
  }
  g_ptr_array_free (bvw->audio_channels, TRUE);


  //btdemux_srcpads
  for (guint i = 0; i < bvw->btdemux_srcpads->len; i++) {
    GstPad *pad = g_ptr_array_index (bvw->btdemux_srcpads, i);
    gst_object_unref (pad);  // Decrement reference count
  }
  g_ptr_array_free (bvw->btdemux_srcpads, TRUE);


  if (bvw->audio_sinpad_decodebin)
  {
    gst_object_unref (bvw->audio_sinpad_decodebin);
  }

  if (bvw->video_sinpad_decodebin)
  {
    gst_object_unref (bvw->video_sinpad_decodebin);
  }



  if(bvw->filesrc)
  {
    gst_object_unref(bvw->filesrc);
  }
  if(bvw->btdemux)
  {
    gst_object_unref(bvw->btdemux);
  }
  if(bvw->decodebin)
  {
    gst_object_unref(bvw->decodebin);
  }
  if(bvw->audio_bin)
  {
   gst_object_unref(bvw->audio_bin);
  }
  if(bvw->glsinkbin)
  {
   gst_object_unref(bvw->glsinkbin);
  }
  if(bvw->video_sink)
  {
   gst_object_unref(bvw->video_sink);
  }

  if(bvw->audio_filter_convert)
  {
   gst_object_unref(bvw->audio_filter_convert);
  }

  if(bvw->audio_capsfilter)
  {
    gst_object_unref(bvw->audio_capsfilter);
  }

  if(bvw->audio_pitchcontrol)
  {
   gst_object_unref(bvw->audio_pitchcontrol);
  }


  g_mutex_clear (&bvw->seek_mutex);
  g_mutex_clear (&bvw->get_audiotags_mutex);
  g_mutex_clear (&bvw->get_videotags_mutex);
  

  G_OBJECT_CLASS (parent_class)->finalize (object);

}





static void
bacon_video_widget_set_property (GObject * object, guint property_id,
                                 const GValue * value, GParamSpec * pspec)
{
  BaconVideoWidget *bvw;

  bvw = BACON_VIDEO_WIDGET (object);

  switch (property_id) {
    case PROP_VOLUME:
      bacon_video_widget_set_volume (bvw, g_value_get_double (value));
      break;
    case PROP_BRIGHTNESS:
      bacon_video_widget_set_video_property (bvw, BVW_VIDEO_BRIGHTNESS, g_value_get_int (value));
      break;
    case PROP_CONTRAST:
      bacon_video_widget_set_video_property (bvw, BVW_VIDEO_CONTRAST, g_value_get_int (value));
      break;
    case PROP_SATURATION:
      bacon_video_widget_set_video_property (bvw, BVW_VIDEO_SATURATION, g_value_get_int (value));
      break;
    case PROP_HUE:
      bacon_video_widget_set_video_property (bvw, BVW_VIDEO_HUE, g_value_get_int (value));
      break;
    case PROP_AUDIO_OUTPUT_TYPE:
      bacon_video_widget_set_audio_output_type (bvw, g_value_get_enum (value));
      break;
    case PROP_AV_OFFSET:
      g_object_set_property (G_OBJECT (bvw->pipeline), "av-offset", value);
      break;
    case PROP_SHOW_CURSOR:
      bacon_video_widget_set_show_cursor (bvw, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
bacon_video_widget_get_property (GObject * object, guint property_id,
                                 GValue * value, GParamSpec * pspec)
{
  BaconVideoWidget *bvw;

  bvw = BACON_VIDEO_WIDGET (object);

  switch (property_id) {
    case PROP_POSITION:
      g_value_set_double (value, bacon_video_widget_get_position (bvw));
      break;
    case PROP_STREAM_LENGTH:
      g_value_set_int64 (value, bacon_video_widget_get_stream_length (bvw));
      break;
    case PROP_PLAYING:
      g_value_set_boolean (value, bacon_video_widget_is_playing (bvw));
      break;
    case PROP_SEEKABLE:
      g_value_set_boolean (value, bacon_video_widget_is_seekable (bvw));
      break;
    case PROP_VOLUME:
      g_value_set_double (value, bvw->volume);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_int (value, bacon_video_widget_get_video_property (bvw, BVW_VIDEO_BRIGHTNESS));
      break;
    case PROP_CONTRAST:
      g_value_set_int (value, bacon_video_widget_get_video_property (bvw, BVW_VIDEO_CONTRAST));
      break;
    case PROP_SATURATION:
      g_value_set_int (value, bacon_video_widget_get_video_property (bvw, BVW_VIDEO_SATURATION));
      break;
    case PROP_HUE:
      g_value_set_int (value, bacon_video_widget_get_video_property (bvw, BVW_VIDEO_HUE));
      break;
    case PROP_AUDIO_OUTPUT_TYPE:
      g_value_set_enum (value, bacon_video_widget_get_audio_output_type (bvw));
      break;
    case PROP_AV_OFFSET:
      g_object_get_property (G_OBJECT (bvw->pipeline), "av-offset", value);
      break;
    case PROP_SHOW_CURSOR:
      g_value_set_boolean (value, bvw->cursor_shown);
      break;
    case PROP_CUR_AUDIO_TAGS: 
    {
          // printf ("(bacon_video_widget_get_property) PROP_CUR_AUDIO_TAGS Locking\n");
      g_mutex_lock (&bvw->get_audiotags_mutex);
      g_value_set_boxed (value, bvw->audiotags);
          // printf ("(bacon_video_widget_get_property) PROP_CUR_AUDIO_TAGS UnLocking\n");
      g_mutex_unlock (&bvw->get_audiotags_mutex);
      break;
    }
    case PROP_CUR_VIDEO_TAGS:
    {
          // printf ("(bacon_video_widget_get_property) PROP_CUR_VIDEO_TAGS Locking\n");
      g_mutex_lock (&bvw->get_videotags_mutex);
      g_value_set_boxed (value, bvw->videotags);
          // printf ("(bacon_video_widget_get_property) PROP_CUR_VIDEO_TAGS UnLocking\n");
      g_mutex_unlock (&bvw->get_videotags_mutex);
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/* ============================================================= */
/*                                                               */
/*                       Public Methods                          */
/*                                                               */
/* ============================================================= */





/******************************************************************************/
/******************** Audio Output Type (audio channels) **********************/
/******************************************************************************/
static gint
get_num_audio_channels (BaconVideoWidget * bvw)
{
  gint channels;

  switch (bvw->speakersetup) {
    case BVW_AUDIO_SOUND_STEREO:
      channels = 2;
      break;
    case BVW_AUDIO_SOUND_4CHANNEL:
      channels = 4;
      break;
    case BVW_AUDIO_SOUND_5CHANNEL:
      channels = 5;
      break;
    case BVW_AUDIO_SOUND_41CHANNEL:
      /* so alsa has this as 5.1, but empty center speaker. We don't really
       * do that yet. ;-). So we'll take the placebo approach. */
    case BVW_AUDIO_SOUND_51CHANNEL:
      channels = 6;
      break;
    case BVW_AUDIO_SOUND_AC3PASSTHRU:
    default:
      g_return_val_if_reached (-1);
  }

  return channels;
}


static GstCaps *
fixate_to_num (const GstCaps * in_caps, gint channels)
{
  gint n, count;
  GstStructure *s;
  const GValue *v;
  GstCaps *out_caps = NULL;

  //init to 0
  count = 0;

  if(in_caps)
  {
    out_caps = gst_caps_copy (in_caps);
    //gst_caps_get_size  -  Gets the number of structures contained in the first arg .
    count = gst_caps_get_size (out_caps);

  }

  for (n = 0; n < count; n++) {
    //GstStructure holds key-value pairs.
    s = gst_caps_get_structure (out_caps, n);
    //the number of audio channels in an audio stream
    v = gst_structure_get_value (s, "channels");
    if (!v)
      continue;
    /* get channel count (or list of ~) */
    //gst_structure_fixate_field_nearest_int not worked,it need channels field to be list or range, not just a integer
    // gboolean fixated = gst_structure_fixate_field_nearest_int (s, "channels", channels);
    gst_structure_set (s, "channels", G_TYPE_INT, channels, NULL);

        printf("(bvw/fixate_to_num) channels value is %d\n", channels);
          
        // Since `s` is just a reference to the internal structure, we should not append it directly.
        // Instead, we should copy it, modify it, and append the new one.
        GstStructure *modified_s = gst_structure_copy(s); // Make a copy of the structure
        gst_caps_remove_structure(out_caps, n);  // Remove the old structure
        gst_caps_append_structure(out_caps, modified_s);  // Append the new structure

  }

  return out_caps;
}



static void
set_audio_filter (BaconVideoWidget *bvw)
{
  gint channels;
  GstCaps *caps, *res;
  GstPad *pad, *peer_pad;

  /* reset old */
  if(bvw->audio_capsfilter)
    g_object_set (bvw->audio_capsfilter, "caps", NULL, NULL);

  /* construct possible caps to filter down to our chosen caps */
  /* Start with what the audio sink supports, but limit the allowed
   * channel count to our speaker output configuration */
  pad = gst_element_get_static_pad (bvw->audio_capsfilter, "src");
  // return the pad that is connected to it,
  peer_pad = gst_pad_get_peer (pad);
  gst_object_unref (pad);
  
  caps = gst_pad_get_current_caps (peer_pad);

      if(caps == NULL)
      {
        printf("(bvw/set_audio_filter) caps is NULL \n");
      }
      else
      {
          gchar *caps_str;
          caps_str = gst_caps_to_string(caps);
          printf("(set_audio_filter): Pad capabilities: %s\n", caps_str);
          g_free(caps_str);
      }

  if(peer_pad){
    gst_object_unref (peer_pad);
  }

  if ((channels = get_num_audio_channels (bvw)) == -1)
  {
    return;
  }

  res = fixate_to_num (caps, channels);
  if(caps){
    gst_caps_unref (caps);
  }

  if(res)
  {
          gchar *caps_str;
          caps_str = gst_caps_to_string(res);
          printf("(set_audio_filter): Pad capabilities of res: %s\n", caps_str);
          g_free(caps_str);
  }

  /* set */
  if (res && gst_caps_is_empty (res)) {
    gst_caps_unref (res);
    res = NULL;
  }
  g_object_set (bvw->audio_capsfilter, "caps", res, NULL);

  if (res) {
    gst_caps_unref (res);
  }

  /* reset */
  // pad = gst_element_get_static_pad (bvw->audio_capsfilter, "src");
  // gst_pad_set_caps (pad, NULL);
  // gst_object_unref (pad);
}



/**
 * bacon_video_widget_get_audio_output_type:
 * @bvw: a #BaconVideoWidget
 *
 * Returns the current audio output type (e.g. how many speaker channels)
 * from #BvwAudioOutputType.
 *
 * Return value: the audio output type, or <code class="literal">-1</code>
 **/
BvwAudioOutputType
bacon_video_widget_get_audio_output_type (BaconVideoWidget *bvw)
{
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), -1);

  return bvw->speakersetup;
}

/**
 * bacon_video_widget_set_audio_output_type:
 * @bvw: a #BaconVideoWidget
 * @type: the new audio output type
 *
 * Sets the audio output type (number of speaker channels) in the video widget.
 **/
void
bacon_video_widget_set_audio_output_type (BaconVideoWidget *bvw,
                                          BvwAudioOutputType type)
{
  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));

  if (type == bvw->speakersetup)
    return;
  else if (type == BVW_AUDIO_SOUND_AC3PASSTHRU)
    return;

  bvw->speakersetup = type;
  g_object_notify (G_OBJECT (bvw), "audio-output-type");

  set_audio_filter (bvw);
}

/* =========================================== */
/*                                             */
/*               Play/Pause, Stop              */
/*                                             */
/* =========================================== */

static GError*
bvw_error_from_gst_error (BaconVideoWidget *bvw, GstMessage * err_msg)
{
  const gchar *src_typename;
  GError *ret = NULL;
  GError *e = NULL;
  char *dbg = NULL;

  GST_LOG ("resolving %" GST_PTR_FORMAT, err_msg);
  src_typename = (err_msg->src) ? G_OBJECT_TYPE_NAME (err_msg->src) : NULL;
  gst_message_parse_error (err_msg, &e, &dbg);
  
  if (src_typename &&
      g_str_equal (src_typename, "GstGtkGLSink") &&
      is_error (e, RESOURCE, NOT_FOUND)) 
  {
    bvw->media_has_unsupported_video = TRUE;
    ret = g_error_new_literal (BVW_ERROR, BVW_ERROR_GENERIC,
			       _("Could not initialise OpenGL support."));
    set_current_actor (bvw);
    goto done;
  }


  if (is_error (e, CORE, MISSING_PLUGIN) ||
      is_error (e, STREAM, CODEC_NOT_FOUND) ||
      is_error (e, STREAM, WRONG_TYPE) ||
      is_error (e, STREAM, NOT_IMPLEMENTED) ||
      (is_error (e, STREAM, FORMAT) && strstr (dbg, "no video pad or visualizations"))) 
  {
    if (bvw->missing_plugins != NULL) {
      gchar **descs, *msg = NULL;
      guint num;

      descs = bvw_get_missing_plugins_descriptions (bvw->missing_plugins);
      num = g_list_length (bvw->missing_plugins);


      if (is_error (e, CORE, MISSING_PLUGIN)) 
      {
        /* should be exactly one missing thing (source or converter) */
        msg = g_strdup_printf (_("The playback of this movie requires a %s "
				 "plugin which is not installed."), descs[0]);
	ret = g_error_new_literal (BVW_ERROR, BVW_ERROR_NO_PLUGIN_FOR_FILE, msg);
	g_free (msg);
      } else {
        gchar *desc_list;

        desc_list = g_strjoinv ("\n", descs);
        msg = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "The playback of this movie "
            "requires a %s plugin which is not installed.", "The playback "
            "of this movie requires the following plugins which are not "
            "installed:\n\n%s", num), (num == 1) ? descs[0] : desc_list);
        g_free (desc_list);
	ret = g_error_new_literal (BVW_ERROR, BVW_ERROR_CODEC_NOT_HANDLED, msg);
	g_free (msg);
      }
      g_strfreev (descs);
    } else {

	      ret = g_error_new_literal (BVW_ERROR, BVW_ERROR_CODEC_NOT_HANDLED,
				   _("An audio or video stream is not handled due to missing codecs. "
				     "You might need to install additional plugins to be able to play some types of movies"));
      
    }
    goto done;
  }

  if (is_error (e, STREAM, FAILED) &&
	     src_typename &&
	     strncmp (src_typename, "GstTypeFind", 11) == 0) {
    ret = g_error_new_literal (BVW_ERROR, BVW_ERROR_READ_ERROR,
			       _("This file cannot be played over the network. Try downloading it locally first."));
    goto done;
  }

  /* generic error, no code; take message */
  ret = g_error_new_literal (BVW_ERROR, BVW_ERROR_GENERIC,
			     e->message);

done:
  g_error_free (e);
  g_free (dbg);
  bvw_clear_missing_plugins_messages (bvw);

  return ret;
}





/**
 * bacon_video_widget_open:
 * @bvw: a #BaconVideoWidget
 * @fileidx: an file_idx within torrent
 *
 * Opens the given file_index in @bvw for playing.
 *
 * The file_index is loaded and waiting to be played with bacon_video_widget_play().
 **/
void
bacon_video_widget_open (BaconVideoWidget *bvw, gint fileidx)
{
  
  // g_return_if_fail (fileidx != -1);
  if(fileidx == -1)
  {
    
    //initially fileidx is -1, we start the pipeline, so btdemux can feed videos_info to retrieve playlist

              printf("(bacon_video_widget_open) initial case: fileidx is -1 ,progressively SET bvw->pipeline state to READY and then PAUSED\n");

      bvw->target_state = GST_STATE_READY;
      //propagates the state change to all the elements inside the pipeline
      gst_element_set_state (bvw->pipeline, GST_STATE_READY);

      bvw->target_state = GST_STATE_PAUSED;
      gst_element_set_state (bvw->pipeline, GST_STATE_PAUSED);

      return;
  }

  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));
  g_return_if_fail (bvw->pipeline != NULL);
  
                printf("(bacon_video_widget_open) set cur_video_fileidx_within_tor to %d\n", fileidx);

  bvw->cur_video_fileidx_within_tor = fileidx;

  /* So we aren't closed yet... */
  if (bvw->cur_video_fileidx_within_tor) 
  {

                printf("(bacon_video_widget_open) So we aren't closed yet...\n");

    bacon_video_widget_close (bvw);
  }
  

  bvw->media_has_video = FALSE;
  bvw->media_has_unsupported_video = FALSE;
  bvw->media_has_audio = FALSE;


  /* Flush the bus to make sure we don't get any messages
   * from the previous file index torrent pieces pushed by btdemux
   */
  gst_bus_set_flushing (bvw->bus, TRUE);

  gst_bus_set_flushing (bvw->bus, FALSE);

/******************************************************************/
  // set target fileindex of that video within torrent, 
  // so it willpushes pieces resides in that video file
  g_object_set (bvw->btdemux, "current-video-file-index", fileidx, NULL);
/*******************************************************************/

  bvw->seekable = -1;

  bvw_clear_missing_plugins_messages (bvw);


}



/**
 * bacon_video_widget_play:
 * @bvw: a #BaconVideoWidget
 * @error: a #GError, or %NULL
 *
 * Plays the currently-pushed pieces of data corresponding to that file_index in @bvw.
 *
 * Errors from the GStreamer backend will be returned asynchronously via the
 * #BaconVideoWidget::error signal, even if this function returns %TRUE.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 **/
gboolean
bacon_video_widget_play (BaconVideoWidget * bvw, GError ** error)
{
                        printf("(bacon_video_widget_play) \n");

  GstState cur_state;

  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), FALSE);
  g_return_val_if_fail (bvw->cur_video_fileidx_within_tor != -1, FALSE);


  bvw->target_state = GST_STATE_PLAYING;

  /* Don't try to play if we're already doing that */
  gst_element_get_state (bvw->pipeline, &cur_state, NULL, 0);

                        printf("(bacon_video_widget_play) cur_state:%s\n", gst_element_state_get_name(cur_state));

  if (cur_state == GST_STATE_PLAYING)
  {
    return TRUE;
  }

  /* Or when we're buffering (the pieces data needed to streaming is downloading)*/
  if (bvw->buffering != FALSE) 
  {
        printf ("(bacon_video_widget_play) buffering in progress, not playing \n");
    // GST_DEBUG ("buffering in progress, not playing");
    /* just lie and do nothing in this case */
    return TRUE;
  }

  /* Set direction to forward */
  if (bvw_set_playback_direction (bvw, TRUE) == FALSE) 
  {
    GST_DEBUG ("Failed to reset direction back to forward to play");
    g_set_error_literal (error, BVW_ERROR, BVW_ERROR_GENERIC,
        _("This file could not be played. Try restarting playback."));
    return FALSE;
  }

  g_signal_emit (bvw, bvw_signals[SIGNAL_PLAY_STARTING], 0);

  GST_DEBUG ("play");

              printf ("(bacon_video_widget_play) SET bvw->pipeline state to PLAYING\n");

  gst_element_set_state (bvw->pipeline, GST_STATE_PLAYING);

  /* will handle all errors asynchroneously */
  return TRUE;
}




/**
 * bacon_video_widget_can_direct_seek:
 * @bvw: a #BaconVideoWidget
 *
 * Determines whether direct seeking is possible for the current stream.
 *
 * Return value: %TRUE if direct seeking is possible, %FALSE otherwise
 **/
gboolean
bacon_video_widget_can_direct_seek (BaconVideoWidget *bvw)
{
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), FALSE);

  if (bvw->cur_video_fileidx_within_tor == -1)
    return FALSE;


  return FALSE;
}

static gboolean
bacon_video_widget_seek_time_no_lock (BaconVideoWidget *bvw,
				      gint64 _time,
				      GstSeekFlags flag,
				      GError **error)
{
  if (bvw_set_playback_direction (bvw, TRUE) == FALSE)
    return FALSE;

  bvw->seek_time = -1;

                  printf ("(bacon_video_widget_seek_time_no_lock) SET bvw->pipeline state to PAUSED\n");

  gst_element_set_state (bvw->pipeline, GST_STATE_PAUSED);

                  printf("(bacon_video_widget_seek_time_no_lock) Call gst_element_seek on pipeline \n");

  gst_element_seek (bvw->pipeline, bvw->rate,
		    GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | flag,
		    GST_SEEK_TYPE_SET, _time * GST_MSECOND,
		    GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);


  return TRUE;
}


/**
 * bacon_video_widget_seek_time:
 * @bvw: a #BaconVideoWidget
 * @_time: the time to which to seek, in milliseconds
 * @accurate: whether to use accurate seek, an accurate seek might be slower for some formats (see GStreamer docs)
 * @error: a #GError, or %NULL
 *
 * Seeks the currently-playing stream to the absolute position @time, in milliseconds.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 **/
gboolean
bacon_video_widget_seek_time (BaconVideoWidget *bvw, gint64 _time, gboolean accurate, GError **error)
{
  GstClockTime cur_time;
  GstSeekFlags  flag;

  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), FALSE);

  GST_LOG ("Seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS (_time * GST_MSECOND));

                          printf("(bacon_video_widget_seek_time) Seeking to %" GST_TIME_FORMAT " \n", GST_TIME_ARGS (_time * GST_MSECOND));

  /* Don't say we'll seek past the end */
  _time = MIN (_time, bvw->stream_length);

  /* Emit a time tick of where we are going, we are paused */
  got_time_tick (bvw->pipeline, _time * GST_MSECOND, bvw);

  /* Is there a pending seek? */
  g_mutex_lock (&bvw->seek_mutex);

  /* If there's no pending seek, or
   * it's been too long since the seek,
   * or we don't have an accurate seek requested */
  cur_time = gst_clock_get_internal_time (bvw->clock);
  if (bvw->seek_req_time == GST_CLOCK_TIME_NONE ||
      cur_time > bvw->seek_req_time + SEEK_TIMEOUT ||
      accurate) 
  {
    bvw->seek_time = -1;
    bvw->seek_req_time = cur_time;
    g_mutex_unlock (&bvw->seek_mutex);
  } 
  else 
  {
    GST_LOG ("Not long enough since last seek, queuing it");

              printf("(bacon_video_widget_seek_time) Not long enough since last seek, queuing it \n");

    bvw->seek_time = _time;
    g_mutex_unlock (&bvw->seek_mutex);
    return TRUE;
  }

  flag = (accurate ? GST_SEEK_FLAG_ACCURATE : GST_SEEK_FLAG_NONE);
  bacon_video_widget_seek_time_no_lock (bvw, _time, flag, error);

  return TRUE;
}


/**
 * bacon_video_widget_seek:
 * @bvw: a #BaconVideoWidget
 * @position: the percentage of the way through the stream to which to seek
 * @error: a #GError, or %NULL
 *
 * Seeks the currently-playing stream to @position as a percentage of the total
 * stream length.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 **/
gboolean
bacon_video_widget_seek (BaconVideoWidget *bvw, double position, GError **error)
{
      gint64 seek_time, length_nanos;


      g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);
      g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), FALSE);

      length_nanos = (gint64) (bvw->stream_length * GST_MSECOND);
      seek_time = (gint64) (length_nanos * position);

      GST_LOG ("Seeking to %3.2f%% %" GST_TIME_FORMAT, position,
          GST_TIME_ARGS (seek_time));

                      printf("(bacon_video_widget_seek) Seeking to %3.2f%% %" GST_TIME_FORMAT "\n", position, GST_TIME_ARGS (seek_time));

      return bacon_video_widget_seek_time (bvw, seek_time / GST_MSECOND, FALSE, error);
}


/**
 * bacon_video_widget_step:
 * @bvw: a #BaconVideoWidget
 * @forward: the direction of the frame step
 * @error: a #GError, or %NULL
 *
 * Step one frame forward, if @forward is %TRUE, or backwards, if @forward is %FALSE
 *
 * Return value: %TRUE on success, %FALSE otherwise
 **/
gboolean
bacon_video_widget_step (BaconVideoWidget *bvw, gboolean forward, GError **error)
{
  GstEvent *event;
  gboolean retval;

  if (bvw_set_playback_direction (bvw, forward) == FALSE)
    return FALSE;

  event = gst_event_new_step (GST_FORMAT_BUFFERS, 1, 1.0, TRUE, FALSE);

  retval = gst_element_send_event (bvw->pipeline, event);

  if (retval != FALSE)
    bvw_query_timeout (bvw);
  else
    GST_WARNING ("Failed to step %s", DIRECTION_STR);

  return retval;
}




static void
bvw_stop_play_pipeline (BaconVideoWidget * bvw)
{

    GstState cur_state;

    gst_element_get_state (bvw->pipeline, &cur_state, NULL, 0);
    //if cur_state is GST_STATE_PAUSED or GST_STATE_PLAYING
    if (cur_state > GST_STATE_READY) 
    {
      GstMessage *msg;

      GST_DEBUG ("stopping");

              printf ("(bvw_stop_play_pipeline) SET bvw->pipeline state to READY\n");

      gst_element_set_state (bvw->pipeline, GST_STATE_READY);

      /* process all remaining state-change messages so everything gets
      * cleaned up properly (before the state change to NULL flushes them) */
      // GST_DEBUG ("processing pending state-change messages");
      while ((msg = gst_bus_pop_filtered (bvw->bus, GST_MESSAGE_STATE_CHANGED))) 
      {
        gst_bus_async_signal_func (bvw->bus, msg, NULL);
        gst_message_unref (msg);
      }
    }

    /* and now drop all following messages until we start again. The
     * bus is set to flush=false again in bacon_video_widget_open()
    */
    if (bvw->bus)
    {
      gst_bus_set_flushing (bvw->bus, TRUE);
    }

    /* Now in READY or lower */
    bvw->target_state = GST_STATE_READY;

    bvw->buffering = FALSE;
   
    bvw_reconfigure_ppi_timeout (bvw, 0);

    g_object_set (bvw->video_sink,
                  "rotate-method", GST_VIDEO_ORIENTATION_AUTO,
                  NULL);

    GST_DEBUG ("stopped");
}





/**
 * bacon_video_widget_pause:
 * @bvw: a #BaconVideoWidget
 *
 * Pauses the current stream in the video widget.
 *
 **/
void
bacon_video_widget_pause (BaconVideoWidget * bvw)
{

  GstStateChangeReturn ret;
  GstState state;

  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));
  g_return_if_fail (GST_IS_ELEMENT (bvw->pipeline));

  /* Get the current state */
  ret = gst_element_get_state (GST_ELEMENT (bvw->pipeline), &state, NULL, 0);

              if (ret != GST_STATE_CHANGE_NO_PREROLL &&
                  ret != GST_STATE_CHANGE_SUCCESS &&
                  state > GST_STATE_READY) 
              {

                      printf("(bacon_video_widget_pause) Stopping because we have a live stream\n");

                GST_LOG ("Stopping because we have a live stream");

                bacon_video_widget_stop (bvw);
                return;
              }

        printf("(bacon_video_widget_pause) Pausing\n");

  // GST_LOG ("Pausing");

  bvw->target_state = GST_STATE_PAUSED;
  

              printf ("(bacon_video_widget_pause) SET bvw->pipeline state to PAUSED\n");

  gst_element_set_state (GST_ELEMENT (bvw->pipeline), GST_STATE_PAUSED);
}





/**
 * bacon_video_widget_stop:
 * @bvw: a #BaconVideoWidget
 *
 * Terminates playing the current stream and resets to the first position in the stream.
 * Be mindful that Stop differs Pause
 **/
void
bacon_video_widget_stop (BaconVideoWidget * bvw)
{

  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));
  g_return_if_fail (GST_IS_ELEMENT (bvw->pipeline));

                    printf ("(bacon_video_widget_stop) \n");

  // GST_LOG ("Stopping");
  bvw_stop_play_pipeline (bvw);

  /* Reset time position (aka, bvw->current_time) to 0 when stopping */
  got_time_tick (GST_ELEMENT (bvw->pipeline), 0, bvw);
}




/**
 * bacon_video_widget_close:
 * @bvw: a #BaconVideoWidget
 *
 * Closes the current stream and frees the resources associated with it.
 **/
void
bacon_video_widget_close (BaconVideoWidget * bvw)
{
  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));
  g_return_if_fail (GST_IS_ELEMENT (bvw->pipeline));
  
  GST_LOG ("Closing");
  bvw_stop_play_pipeline (bvw);

  bvw->rate = FORWARD_RATE;

  bvw->current_time = 0;
  bvw->seek_req_time = GST_CLOCK_TIME_NONE;
  bvw->seek_time = -1;
  bvw->stream_length = 0;

  if (bvw->eos_id != 0)
  {
    g_source_remove (bvw->eos_id);
    bvw->eos_id = 0;
  }


  g_clear_pointer (&bvw->tagcache, gst_tag_list_unref);
  g_clear_pointer (&bvw->audiotags, gst_tag_list_unref);
  g_clear_pointer (&bvw->videotags, gst_tag_list_unref);

  g_object_notify (G_OBJECT (bvw), "seekable");

  got_time_tick (GST_ELEMENT (bvw->pipeline), 0, bvw);

}






/**
 * bacon_video_widget_can_set_volume:
 * @bvw: a #BaconVideoWidget
 *
 * Returns whether the volume level can be set, given the current settings.
 *
 * The volume cannot be set if the audio output type is set to
 * %BVW_AUDIO_SOUND_AC3PASSTHRU.
 *
 * Return value: %TRUE if the volume can be set, %FALSE otherwise
//  **/
gboolean
bacon_video_widget_can_set_volume (BaconVideoWidget * bvw)
{
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), FALSE);

  if (bvw->speakersetup == BVW_AUDIO_SOUND_AC3PASSTHRU)
    return FALSE;

  return TRUE;
}



/**
 * bacon_video_widget_set_volume:
 * @bvw: a #BaconVideoWidget
 * @volume: the new volume level, as a percentage between <code class="literal">0</code> and <code class="literal">1</code>
 *
 * Sets the volume level of the stream as a percentage between <code class="literal">0</code> and <code class="literal">1</code>.
 *
 * If bacon_video_widget_can_set_volume() returns %FALSE, this is a no-op.
 **/
void
bacon_video_widget_set_volume (BaconVideoWidget * bvw, double volume)
{     

                printf("(bacon_video_widget_set_volume) %lf \n", volume);

  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));
  g_return_if_fail (GST_IS_ELEMENT (bvw->pipeline));

  if (bacon_video_widget_can_set_volume (bvw) != FALSE) {
    volume = CLAMP (volume, 0.0, 1.0);
    gst_stream_volume_set_volume (GST_STREAM_VOLUME (bvw->pipeline),
                                  GST_STREAM_VOLUME_FORMAT_CUBIC,
                                  volume);

    bvw->volume = volume;
    g_object_notify (G_OBJECT (bvw), "volume");
  }
}

/**
 * bacon_video_widget_get_volume:
 * @bvw: a #BaconVideoWidget
 *
 * Returns the current volume level, as a percentage between <code class="literal">0</code> and <code class="literal">1</code>.
 *
 * Return value: the volume as a percentage between <code class="literal">0</code> and <code class="literal">1</code>
 **/
double
bacon_video_widget_get_volume (BaconVideoWidget * bvw)
{

                printf("(bacon_video_widget_get_volume)\n" );

  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), 0.0);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), 0.0);

  return bvw->volume;
}

/**
 * bacon_video_widget_set_show_cursor:
 * @bvw: a #BaconVideoWidget
 * @show_cursor: %TRUE to show the cursor, %FALSE otherwise
 *
 * Sets whether the cursor should be shown when it is over the video
 * widget. If @show_cursor is %FALSE, the cursor will be invisible
 * when it is moved over the video widget.
 **/
void
bacon_video_widget_set_show_cursor (BaconVideoWidget * bvw,
                                    gboolean show_cursor)
{
  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));

  if (bvw->cursor_shown == show_cursor)
  {
    return;
  }
  bvw->cursor_shown = show_cursor;
  update_cursor (bvw);
}

/**
 * bacon_video_widget_set_aspect_ratio:
 * @bvw: a #BaconVideoWidget
 * @ratio: the new aspect ratio
 *
 * Sets the aspect ratio used by the widget, from #BvwAspectRatio.
 *
 * Changes to this take effect immediately.
 **/
void
bacon_video_widget_set_aspect_ratio (BaconVideoWidget *bvw,
                                     BvwAspectRatio ratio)
{
  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));

  bvw->ratio_type = ratio;

  switch (bvw->ratio_type) {
  case BVW_RATIO_SQUARE:
    g_object_set (bvw->video_sink,
		  "video-aspect-ratio-override", 1, 1,
		  NULL);
    break;
  case BVW_RATIO_FOURBYTHREE:
    g_object_set (bvw->video_sink,
		  "video-aspect-ratio-override", 4, 3,
		  NULL);
    break;
  case BVW_RATIO_ANAMORPHIC:
    g_object_set (bvw->video_sink,
		  "video-aspect-ratio-override", 16, 9,
		  NULL);
    break;
  case BVW_RATIO_DVB:
    g_object_set (bvw->video_sink,
		  "video-aspect-ratio-override", 20, 9,
		  NULL);
    break;
    /* handle these to avoid compiler warnings */
  case BVW_RATIO_AUTO:
  default:
    g_object_set (bvw->video_sink,
		  "video-aspect-ratio-override", 0, 1,
		  NULL);
    break;
  }
}

/**
 * bacon_video_widget_get_aspect_ratio:
 * @bvw: a #BaconVideoWidget
 *
 * Returns the current aspect ratio used by the widget, from
 * #BvwAspectRatio.
 *
 * Return value: the aspect ratio
 **/
BvwAspectRatio
bacon_video_widget_get_aspect_ratio (BaconVideoWidget *bvw)
{
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), 0);

  return bvw->ratio_type;
}

/**
 * bacon_video_widget_set_zoom:
 * @bvw: a #BaconVideoWidget
 * @mode: the #BvwZoomMode
 *
 * Sets the zoom type applied to the video when it is displayed.
 **/
void
bacon_video_widget_set_zoom (BaconVideoWidget *bvw,
                             BvwZoomMode       mode)
{
  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));

  g_debug ("%s not implemented", G_STRFUNC);
}

/**
 * bacon_video_widget_get_zoom:
 * @bvw: a #BaconVideoWidget
 *
 * Returns the zoom mode applied to videos displayed by the widget.
 *
 * Return value: a #BvwZoomMode
 **/
BvwZoomMode
bacon_video_widget_get_zoom (BaconVideoWidget *bvw)
{
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), BVW_ZOOM_NONE);

  g_debug ("%s not implemented", G_STRFUNC);
  return BVW_ZOOM_NONE;
}

/**
 * bacon_video_widget_set_rotation:
 * @bvw: a #BaconVideoWidget
 * @rotation: the #BvwRotation of the video in degrees
 *
 * Sets the rotation to be applied to the video when it is displayed.
 **/
void
bacon_video_widget_set_rotation (BaconVideoWidget *bvw,
				 BvwRotation       rotation)
{
  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));

  GST_DEBUG ("Rotating to %s (%f degrees) from %s",
	     g_enum_to_string (BVW_TYPE_ROTATION, rotation),
	     rotation * 90.0,
	     g_enum_to_string (BVW_TYPE_ROTATION, bvw->rotation));

  bvw->rotation = rotation;
  g_object_set (bvw->video_sink, "rotate-method", rotation, NULL);
}

/**
 * bacon_video_widget_get_rotation:
 * @bvw: a #BaconVideoWidget
 *
 * Returns the angle of rotation of the video, in degrees.
 *
 * Return value: a #BvwRotation.
 **/
BvwRotation
bacon_video_widget_get_rotation (BaconVideoWidget *bvw)
{
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), BVW_ROTATION_R_ZERO);

  return bvw->rotation;
}

/* Search for the color balance channel corresponding to type and return it. */
static GstColorBalanceChannel *
bvw_get_color_balance_channel (GstColorBalance * color_balance,
    BvwVideoProperty type)
{
  const GList *channels;

  channels = gst_color_balance_list_channels (color_balance);

  for (; channels != NULL; channels = channels->next) {
    GstColorBalanceChannel *c = channels->data;

    if (type == BVW_VIDEO_BRIGHTNESS && g_strrstr (c->label, "BRIGHTNESS"))
      return g_object_ref (c);
    else if (type == BVW_VIDEO_CONTRAST && g_strrstr (c->label, "CONTRAST"))
      return g_object_ref (c);
    else if (type == BVW_VIDEO_SATURATION && g_strrstr (c->label, "SATURATION"))
      return g_object_ref (c);
    else if (type == BVW_VIDEO_HUE && g_strrstr (c->label, "HUE"))
      return g_object_ref (c);
  }

  return NULL;
}

/**
 * bacon_video_widget_get_video_property:
 * @bvw: a #BaconVideoWidget
 * @type: the type of property
 *
 * Returns the given property of the video display, such as its brightness or saturation.
 *
 * It is returned as a percentage in the full range of integer values; from <code class="literal">0</code>
 * to <code class="literal">65535</code> (inclusive), where <code class="literal">32768</code> is the default.
 *
 * Return value: the property's value, in the range <code class="literal">0</code> to <code class="literal">65535</code>
 **/
int
bacon_video_widget_get_video_property (BaconVideoWidget *bvw,
                                       BvwVideoProperty type)
{
  GstColorBalanceChannel *found_channel = NULL;
  int ret, cur;

  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), 65535/2);
  g_return_val_if_fail (bvw->glsinkbin != NULL, 65535/2);

  ret = 0;

  found_channel = bvw_get_color_balance_channel (GST_COLOR_BALANCE (bvw->glsinkbin), type);
  cur = gst_color_balance_get_value (GST_COLOR_BALANCE (bvw->glsinkbin), found_channel);

  GST_DEBUG ("channel %s: cur=%d, min=%d, max=%d", found_channel->label,
	     cur, found_channel->min_value, found_channel->max_value);

  ret = floor (0.5 +
	       ((double) cur - found_channel->min_value) * 65535 /
	       ((double) found_channel->max_value - found_channel->min_value));

  GST_DEBUG ("channel %s: returning value %d", found_channel->label, ret);
  g_object_unref (found_channel);
  return ret;
}


static gboolean
notify_volume_idle_cb (BaconVideoWidget *bvw)
{
  gdouble vol;

  vol = gst_stream_volume_get_volume (GST_STREAM_VOLUME (bvw->pipeline),
                                      GST_STREAM_VOLUME_FORMAT_CUBIC);
  bvw->volume = vol;

  //g_object_notify is to inform the GObject system that a specific property of a GObject has changed
  g_object_notify (G_OBJECT (bvw), "volume");

  return FALSE;
}

static void
notify_volume_cb (GObject             *object,
		  GParamSpec          *pspec,
		  BaconVideoWidget    *bvw)
{
  guint id;

  id = g_idle_add ((GSourceFunc) notify_volume_idle_cb, bvw);
  g_source_set_name_by_id (id, "[totem] notify_volume_idle_cb");
}







/** 
 * bacon_video_widget_set_video_property:
 * @bvw: a #BaconVideoWidget
 * @type: the type of property
 * @value: the property's value, in the range <code class="literal">0</code> to <code class="literal">65535</code>
 *
 * Sets the given property of the video display, such as its brightness or saturation.
 *
 * It should be given as a percentage in the full range of integer values; from <code class="literal">0</code>
 * to <code class="literal">65535</code> (inclusive), where <code class="literal">32768</code> is the default.
 **/
void
bacon_video_widget_set_video_property (BaconVideoWidget *bvw,
                                       BvwVideoProperty type,
                                       int value)
{
  GstColorBalanceChannel *found_channel = NULL;
  int i_value;

  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));
  g_return_if_fail (bvw->glsinkbin != NULL);

  GST_DEBUG ("set video property type %d to value %d", type, value);

  if ( !(value <= 65535 && value >= 0) )
  {
    return;
  }

  found_channel = bvw_get_color_balance_channel (GST_COLOR_BALANCE (bvw->glsinkbin), type);
  i_value = floor (0.5 + value * ((double) found_channel->max_value -
				  found_channel->min_value) / 65535 + found_channel->min_value);

  GST_DEBUG ("channel %s: set to %d/65535", found_channel->label, value);

  gst_color_balance_set_value (GST_COLOR_BALANCE (bvw->glsinkbin), found_channel, i_value);

  GST_DEBUG ("channel %s: val=%d, min=%d, max=%d", found_channel->label,
	     i_value, found_channel->min_value, found_channel->max_value);

  g_object_unref (found_channel);

  /* Notify of the property change */
  g_object_notify (G_OBJECT (bvw), video_props_str[type]);

  GST_DEBUG ("setting value %d", value);
}






/**
 * bacon_video_widget_get_position:
 * @bvw: a #BaconVideoWidget
 *
 * Returns the current position in the stream, as a value between
 * <code class="literal">0</code> and <code class="literal">1</code>.
 *
 * Return value: the current position, or <code class="literal">-1</code>
 **/
double
bacon_video_widget_get_position (BaconVideoWidget * bvw)
{
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), -1);
  return bvw->current_position;
}






/**
 * bacon_video_widget_get_current_time:
 * @bvw: a #BaconVideoWidget
 *
 * Returns the current position in the stream, as the time (in milliseconds)
 * since the beginning of the stream.
 *
 * Return value: time since the beginning of the stream, in milliseconds, or <code class="literal">-1</code>
 **/
gint64
bacon_video_widget_get_current_time (BaconVideoWidget * bvw)
{
  //get the current time posistion of playing stream, used for serialization,saving playling stat for resume next time 
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), -1);
  return bvw->current_time;
}






/**
 * bacon_video_widget_get_stream_length:
 * @bvw: a #BaconVideoWidget
 *
 * Returns the total length of the stream, in milliseconds.
 *
 * Return value: the stream length, in milliseconds, or <code class="literal">-1</code>
 **/
gint64
bacon_video_widget_get_stream_length (BaconVideoWidget * bvw)
{
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), -1);

  if (bvw->stream_length == 0 && bvw->pipeline != NULL) {
    gint64 len = -1;

    if (gst_element_query_duration (bvw->decodebin, GST_FORMAT_TIME, &len) && len != -1) {
      bvw->stream_length = len / GST_MSECOND;
    }
  }
  //Be careful! it is in milliseconds !!!
  return bvw->stream_length;
}






/**
 * bacon_video_widget_is_playing:
 * @bvw: a #BaconVideoWidget
 *
 * Returns whether the widget is currently playing a stream.
 *
 * Return value: %TRUE if a stream is playing, %FALSE otherwise
 **/
gboolean
bacon_video_widget_is_playing (BaconVideoWidget * bvw)
{
  gboolean ret;

  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), FALSE);

  ret = (bvw->target_state == GST_STATE_PLAYING);
  GST_LOG ("%splaying", (ret) ? "" : "not ");

  return ret;
}




/**
 * bacon_video_widget_is_seekable:
 * @bvw: a #BaconVideoWidget
 *
 * Returns whether seeking is possible in the current stream.
 *
 * If no stream is loaded, %FALSE is returned.
 *
 * Return value: %TRUE if the stream is seekable, %FALSE otherwise
 **/
gboolean
bacon_video_widget_is_seekable (BaconVideoWidget * bvw)
{
  gboolean res;
  gint old_seekable;

  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), FALSE);

  if (bvw->cur_video_fileidx_within_tor == -1)
  {
    return FALSE;
  }

  old_seekable = bvw->seekable;

  if (bvw->seekable == -1) 
  {
    GstQuery *query;

    query = gst_query_new_seeking (GST_FORMAT_TIME);
    if (gst_element_query (bvw->pipeline, query)) 
    {
        gst_query_parse_seeking (query, NULL, &res, NULL, NULL);

                            printf ("(bacon_video_widget_is_seekable) seeking query says the stream is %s seekable \n", (res) ? "" : " not");

        bvw->seekable = (res) ? 1 : 0;
    } 
    else 
    {
                            // printf("(bacon_video_widget_is_seekable) seeking query failed \n");
    }
    gst_query_unref (query);
  }

  if (bvw->seekable != -1) {
    res = (bvw->seekable != 0);
    goto done;
  }

  /* Try to guess from duration. This is very unreliable
   * though so don't save it */
  if (bvw->stream_length == 0) {
    res = (bacon_video_widget_get_stream_length (bvw) > 0);
  } else {
    res = (bvw->stream_length > 0);
  }

done:

  if (old_seekable != bvw->seekable)
    g_object_notify (G_OBJECT (bvw), "seekable");

                            // printf("(bacon_video_widget_is_seekable) stream is%s seekable \n", res ? "" : " not");

  GST_DEBUG ("stream is%s seekable", (res) ? "" : " not");
  return res;
}






//idx of current-video/audio 
static gint
bvw_get_current_stream_num (BaconVideoWidget * bvw,
    const gchar *stream_type)
{
  gint stream_num = -1;

  if (bvw->pipeline == NULL)
  {
    return stream_num;
  }

  //get stream num
  if(stream_type == "audio")
  {
      stream_num = bvw->audio_channels ? bvw->audio_channels->len : 0;
  }
  else if(stream_type == "video")
  {
      stream_num = bvw->video_channels ? bvw->video_channels->len : 0;
  }


  // GST_LOG ("current %s stream: %d", stream_type, stream_num);
  return stream_num;

}



//stream_type - "video" or "audio"
static GstTagList *
bvw_get_tags_of_current_stream (BaconVideoWidget * bvw,
    const gchar *stream_type)
{
  GstTagList *tags = NULL;
  gint stream_num = -1;
  GstPad *pad;

  stream_num = bvw_get_current_stream_num (bvw, stream_type);

  if (stream_num < 0)
  {
    return NULL;
  }
  
  //get tags
  if(stream_type == "audio")
  {
      g_object_get (bvw, "cur-audio-tags", &tags, NULL);
  }
  else if (stream_type == "video")
  {
      g_object_get (bvw, "cur-video-tags", &tags, NULL);
  }

  GST_LOG ("current %s stream tags %" GST_PTR_FORMAT, stream_type, tags);
  return tags;
}




static GstCaps *
bvw_get_caps_of_current_stream (BaconVideoWidget * bvw,
    const gchar *stream_type)
{
  GstCaps *caps = NULL;
  gint stream_num = -1;
  GstPad *current = NULL;

  stream_num = bvw_get_current_stream_num (bvw, stream_type);
  if (stream_num < 0)
  {
    return NULL;
  }

  //index is num minue one
  if(stream_type == "audio")
  {
    current =  g_ptr_array_index (bvw->audio_channels, stream_num-1);
  }
  else if (stream_type == "video")
  {
    current =  g_ptr_array_index (bvw->video_channels, stream_num-1);
  }

  if (current != NULL) 
  {
    // Returns: (transfer full) (nullable)
    caps = gst_pad_get_current_caps (current);
        // printf ("(bvw_get_caps_of_current_stream) Get caps ok  %d %d %d\n",stream_num, bvw->audio_channels->len,bvw->video_channels->len);
  }else
  {
        // printf ("(bvw_get_caps_of_current_stream) Failed get pad from channels %d %d %d\n",stream_num, bvw->audio_channels->len,bvw->video_channels->len);
  }

  // GST_LOG ("current %s stream caps: %" GST_PTR_FORMAT, stream_type, caps);

  return caps;
}




//for audio caps
static gboolean
audio_caps_have_LFE (GstStructure * s)
{
  guint64 mask;
  int channels;

  if (!gst_structure_get_int (s, "channels", &channels) ||
      channels == 0)
    return FALSE;

  if (!gst_structure_get (s, "channel-mask", GST_TYPE_BITMASK, &mask, NULL))
    return FALSE;

  if (mask & GST_AUDIO_CHANNEL_POSITION_LFE1 ||
      mask & GST_AUDIO_CHANNEL_POSITION_LFE2)
    return TRUE;

  return FALSE;
}











/****************Metadata-Related***************** */
static void
bacon_video_widget_get_metadata_string (BaconVideoWidget * bvw,
                                        BvwMetadataType type,
                                        GValue * value)
{
  char *string = NULL;
  gboolean res = FALSE;

  g_value_init (value, G_TYPE_STRING);

  if (bvw->pipeline == NULL) {
    g_value_set_string (value, NULL);
    return;
  }

  switch (type) {
    case BVW_INFO_TITLE:
      if (bvw->tagcache != NULL) {
        res = gst_tag_list_get_string_index (bvw->tagcache,
                                             GST_TAG_TITLE, 0, &string);
      }
      break;
    case BVW_INFO_ARTIST:
      if (bvw->tagcache != NULL) {
        res = gst_tag_list_get_string_index (bvw->tagcache,
                                             GST_TAG_ARTIST, 0, &string);
      }
      break;
    case BVW_INFO_YEAR:
      if (bvw->tagcache != NULL) {
        GDate *date;
        GstDateTime *datetime;

        if ((res = gst_tag_list_get_date (bvw->tagcache,
                                          GST_TAG_DATE, &date))) {
          string = g_strdup_printf ("%d", g_date_get_year (date));
          g_date_free (date);
        } else if ((res = gst_tag_list_get_date_time (bvw->tagcache,
                                                      GST_TAG_DATE_TIME, &datetime))) {
          string = g_strdup_printf ("%d", gst_date_time_get_year (datetime));
          gst_date_time_unref (datetime);
        }
      }
      break;
    case BVW_INFO_COMMENT:
      if (bvw->tagcache != NULL) {
        res = gst_tag_list_get_string_index (bvw->tagcache,
                                             GST_TAG_COMMENT, 0, &string);

        /* Use the Comment; if that fails, use Description as specified by:
         * http://xiph.org/vorbis/doc/v-comment.html */
        if (!res) {
          res = gst_tag_list_get_string_index (bvw->tagcache,
                                               GST_TAG_DESCRIPTION, 0, &string);
        }
      }
      break;
    case BVW_INFO_ALBUM:
      if (bvw->tagcache != NULL) {
        res = gst_tag_list_get_string_index (bvw->tagcache,
                                             GST_TAG_ALBUM, 0, &string);
      }
      break;
    case BVW_INFO_CONTAINER:
      if (bvw->tagcache != NULL) {
        res = gst_tag_list_get_string_index (bvw->tagcache,
                                             GST_TAG_CONTAINER_FORMAT, 0, &string);
      }
      break;
    case BVW_INFO_VIDEO_CODEC: {
      GstTagList *tags;

      /* try to get this from the stream info first */
      if ((tags = bvw_get_tags_of_current_stream (bvw, "video"))) {
        res = gst_tag_list_get_string (tags, GST_TAG_CODEC, &string);
	gst_tag_list_unref (tags);
      }

      /* if that didn't work, try the aggregated tags */
      if (!res && bvw->tagcache != NULL) {
        res = gst_tag_list_get_string (bvw->tagcache,
            GST_TAG_VIDEO_CODEC, &string);
      }
      break;
    }
    case BVW_INFO_AUDIO_CODEC: {
      GstTagList *tags;

      /* try to get this from the stream info first */
      if ((tags = bvw_get_tags_of_current_stream (bvw, "audio"))) {
        res = gst_tag_list_get_string (tags, GST_TAG_CODEC, &string);
	gst_tag_list_unref (tags);
      }

      /* if that didn't work, try the aggregated tags */
      if (!res && bvw->tagcache != NULL) {
        res = gst_tag_list_get_string (bvw->tagcache,
            GST_TAG_AUDIO_CODEC, &string);
      }
      break;
    }
    case BVW_INFO_AUDIO_CHANNELS: {
      GstStructure *s;
      GstCaps *caps;

      /*get audio channel, so query audio bin in pipeline*/
      caps = bvw_get_caps_of_current_stream (bvw, "audio");
      if (caps) 
      {
              // printf ("(bvw_get_metadata_string) Ok to get BVW_INFO_AUDIO_CHANNELS\n");
        gint channels = 0;

        s = gst_caps_get_structure (caps, 0);
        if ((res = gst_structure_get_int (s, "channels", &channels))) {
          /* FIXME: do something more sophisticated - but what? */
          if (channels > 2 && audio_caps_have_LFE (s)) {
            string = g_strdup_printf ("%s %d.1", _("Surround"), channels - 1);
          } else if (channels == 1) {
            string = g_strdup (_("Mono"));
          } else if (channels == 2) {
            string = g_strdup (_("Stereo"));
          } else {
            string = g_strdup_printf ("%d", channels);
          }
        }
        gst_caps_unref (caps);
      }else{
              // printf ("(bvw_get_metadata_string) Failed to get BVW_INFO_AUDIO_CHANNELS\n");

      }
      break;
    }

    case BVW_INFO_DURATION:
    case BVW_INFO_TRACK_NUMBER:
    case BVW_INFO_HAS_VIDEO:
    case BVW_INFO_DIMENSION_X:
    case BVW_INFO_DIMENSION_Y:
    case BVW_INFO_VIDEO_BITRATE:
    case BVW_INFO_FPS:
    case BVW_INFO_HAS_AUDIO:
    case BVW_INFO_AUDIO_BITRATE:
    case BVW_INFO_AUDIO_SAMPLE_RATE:
      /* Not strings */
    default:
      g_assert_not_reached ();
    }

  /* Remove line feeds */
  if (string && strstr (string, "\n") != NULL)
    g_strdelimit (string, "\n", ' ');
  if (string != NULL)
    string = g_strstrip (string);

  if (res && string && *string != '\0' && g_utf8_validate (string, -1, NULL)) {
    g_value_take_string (value, string);
    GST_DEBUG ("%s = '%s'", g_enum_to_string (BVW_TYPE_METADATA_TYPE, type), string);

                //Comment it out
                //let log shorter and clearer, 
                //since update_properties_from_bvw() in totem-movie-properties.c  will called many times, 
                //if there are many update tags log lines, we no need to care about the details of which tag metadata is which,
                // this will making log a little confusing, 
                
                // printf("xxxxxxx %s = %s\n", g_enum_to_string (BVW_TYPE_METADATA_TYPE, type), string);

  } else {
    g_value_set_string (value, NULL);
    g_free (string);
  }

  return;
}

static void
bacon_video_widget_get_metadata_int (BaconVideoWidget * bvw,
                                     BvwMetadataType type,
                                     GValue * value)
{
  int integer = 0;

  g_value_init (value, G_TYPE_INT);

  if (bvw->pipeline == NULL) {
    g_value_set_int (value, 0);
    return;
  }

  switch (type) {
    case BVW_INFO_DURATION:
      integer = bacon_video_widget_get_stream_length (bvw) / 1000;
      break;
    case BVW_INFO_TRACK_NUMBER:
      if (bvw->tagcache == NULL)
        break;
      if (!gst_tag_list_get_uint (bvw->tagcache,
                                  GST_TAG_TRACK_NUMBER, (guint *) &integer))
        integer = 0;
      break;
    case BVW_INFO_DIMENSION_X:
      integer = bvw->video_width;
      break;
    case BVW_INFO_DIMENSION_Y:
      integer = bvw->video_height;
      break;
    case BVW_INFO_AUDIO_BITRATE:
      if (bvw->audiotags == NULL)
        break;
      if (gst_tag_list_get_uint (bvw->audiotags, GST_TAG_BITRATE,
          (guint *)&integer) ||
          gst_tag_list_get_uint (bvw->audiotags, GST_TAG_NOMINAL_BITRATE,
          (guint *)&integer)) {
        integer /= 1000;
      }
      break;
    case BVW_INFO_VIDEO_BITRATE:
      if (bvw->videotags == NULL)
        break;
      if (gst_tag_list_get_uint (bvw->videotags, GST_TAG_BITRATE,
          (guint *)&integer) ||
          gst_tag_list_get_uint (bvw->videotags, GST_TAG_NOMINAL_BITRATE,
          (guint *)&integer)) {
        integer /= 1000;
      }
      break;
    case BVW_INFO_AUDIO_SAMPLE_RATE: {
      GstStructure *s;
      GstCaps *caps;

      caps = bvw_get_caps_of_current_stream (bvw, "audio");
      if (caps) {
              // printf ("(bvw_get_metadata_int) Ok to get BVW_INFO_AUDIO_SAMPLE_RATE\n");

        s = gst_caps_get_structure (caps, 0);
        gst_structure_get_int (s, "rate", &integer);
        gst_caps_unref (caps);
      }else {
              // printf ("(bvw_get_metadata_int) Failed to get BVW_INFO_AUDIO_SAMPLE_RATE\n");
      }
      break;
    }

    case BVW_INFO_TITLE:
    case BVW_INFO_ARTIST:
    case BVW_INFO_YEAR:
    case BVW_INFO_COMMENT:
    case BVW_INFO_ALBUM:
    case BVW_INFO_CONTAINER:
    case BVW_INFO_HAS_VIDEO:
    case BVW_INFO_VIDEO_CODEC:
    case BVW_INFO_HAS_AUDIO:
    case BVW_INFO_AUDIO_CODEC:
    case BVW_INFO_AUDIO_CHANNELS:
      /* Not ints */
    default:
      g_assert_not_reached ();
    }

  g_value_set_int (value, integer);
  GST_DEBUG ("%s = %d", g_enum_to_string (BVW_TYPE_METADATA_TYPE, type), integer);

  return;
}

static void
bacon_video_widget_get_metadata_bool (BaconVideoWidget * bvw,
                                      BvwMetadataType type,
                                      GValue * value)
{
  gboolean boolean = FALSE;

  g_value_init (value, G_TYPE_BOOLEAN);

  if (bvw->pipeline == NULL) {
    g_value_set_boolean (value, FALSE);
    return;
  }

  GST_DEBUG ("tagcache  = %" GST_PTR_FORMAT, bvw->tagcache);
  GST_DEBUG ("videotags = %" GST_PTR_FORMAT, bvw->videotags);
  GST_DEBUG ("audiotags = %" GST_PTR_FORMAT, bvw->audiotags);

  switch (type)
  {
    case BVW_INFO_HAS_VIDEO:
      boolean = bvw->media_has_video;
      break;
    case BVW_INFO_HAS_AUDIO:
      boolean = bvw->media_has_audio;
      break;

    case BVW_INFO_TITLE:
    case BVW_INFO_ARTIST:
    case BVW_INFO_YEAR:
    case BVW_INFO_COMMENT:
    case BVW_INFO_ALBUM:
    case BVW_INFO_DURATION:
    case BVW_INFO_TRACK_NUMBER:
    case BVW_INFO_CONTAINER:
    case BVW_INFO_DIMENSION_X:
    case BVW_INFO_DIMENSION_Y:
    case BVW_INFO_VIDEO_BITRATE:
    case BVW_INFO_VIDEO_CODEC:
    case BVW_INFO_FPS:
    case BVW_INFO_AUDIO_BITRATE:
    case BVW_INFO_AUDIO_CODEC:
    case BVW_INFO_AUDIO_SAMPLE_RATE:
    case BVW_INFO_AUDIO_CHANNELS:
      /* Not bools */
    default:
      g_assert_not_reached ();
  }

  g_value_set_boolean (value, boolean);
  GST_DEBUG ("%s = %s", g_enum_to_string (BVW_TYPE_METADATA_TYPE, type), (boolean) ? "yes" : "no");

  return;
}



/**
 * bacon_video_widget_get_metadata:
 * @bvw: a #BaconVideoWidget
 * @type: the type of metadata to return
 * @value: a #GValue
 *
 * Provides metadata of the given @type about the current stream in @value.
 *
 * Free the #GValue with g_value_unset().
 **/
void
bacon_video_widget_get_metadata (BaconVideoWidget * bvw,
                                 BvwMetadataType type,
                                 GValue * value)
{

                  // printf("In bacon_video_widget_get_metadata \n");

  g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));
  g_return_if_fail (GST_IS_ELEMENT (bvw->pipeline));

  switch (type)
  {
    case BVW_INFO_TITLE:
    case BVW_INFO_ARTIST:
    case BVW_INFO_YEAR:
    case BVW_INFO_COMMENT:
    case BVW_INFO_ALBUM:
    case BVW_INFO_CONTAINER:
    case BVW_INFO_VIDEO_CODEC:
    case BVW_INFO_AUDIO_CODEC:
    case BVW_INFO_AUDIO_CHANNELS:
      bacon_video_widget_get_metadata_string (bvw, type, value);
      break;
    case BVW_INFO_DURATION:
    case BVW_INFO_DIMENSION_X:
    case BVW_INFO_DIMENSION_Y:
    case BVW_INFO_AUDIO_BITRATE:
    case BVW_INFO_VIDEO_BITRATE:
    case BVW_INFO_TRACK_NUMBER:
    case BVW_INFO_AUDIO_SAMPLE_RATE:
      bacon_video_widget_get_metadata_int (bvw, type, value);
      break;
    case BVW_INFO_HAS_VIDEO:
    case BVW_INFO_HAS_AUDIO:
      bacon_video_widget_get_metadata_bool (bvw, type, value);
      break;
    case BVW_INFO_FPS:
      {
	float fps = 0.0;

	if (bvw->video_fps_d > 0)
	  fps = (float) bvw->video_fps_n / (float) bvw->video_fps_d;

	g_value_init (value, G_TYPE_FLOAT);
	g_value_set_float (value, fps);
      }
      break;
    default:
      g_return_if_reached ();
  }

  return;
}









/* Screenshot functions */

/**
 * bacon_video_widget_can_get_frames:
 * @bvw: a #BaconVideoWidget
 * @error: a #GError, or %NULL
 *
 * Determines whether individual frames from the current stream can
 * be returned using bacon_video_widget_get_current_frame().
 *
 * Frames cannot be returned for audio-only streams.
 *
 * Return value: %TRUE if frames can be captured, %FALSE otherwise
 **/
gboolean
bacon_video_widget_can_get_frames (BaconVideoWidget * bvw, GError ** error)
{
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), FALSE);

  /* check for video */
  if (!bvw->media_has_video) {
    g_set_error_literal (error, BVW_ERROR, BVW_ERROR_CANNOT_CAPTURE,
        _("Media contains no supported video streams."));
    return FALSE;
  }

  return TRUE;
}

/**
 * bacon_video_widget_get_current_frame:
 * @bvw: a #BaconVideoWidget
 *
 * Returns a #GdkPixbuf containing the current frame from the playing
 * stream. This will wait for any pending seeks to complete before
 * capturing the frame.
 *
 * Return value: the current frame, or %NULL; unref with g_object_unref()
 **/
GdkPixbuf *
bacon_video_widget_get_current_frame (BaconVideoWidget * bvw)
{
  GdkPixbuf *ret = NULL;
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), NULL);

  /* no video info */
  if (!bvw->video_width || !bvw->video_height) {
    GST_DEBUG ("Could not take screenshot: %s", "no video info");
    g_warning ("Could not take screenshot: %s", "no video info");
    return NULL;
  }

  ret = totem_gst_playbin_get_frame (bvw->pipeline, &error);
  if (!ret) {
    GST_DEBUG ("Could not take screenshot: %s", error->message);
    g_warning ("Could not take screenshot: %s", error->message);
  }
  return ret;
}

/* =========================================== */
/*                                             */
/*          Widget typing & Creation           */
/*                                             */
/* =========================================== */

/**
 * bacon_video_widget_get_option_group:
 *
 * Returns the #GOptionGroup containing command-line options for
 * #BaconVideoWidget.
 *
 * Applications must call either this exactly once.
 *
 * Return value: a #GOptionGroup giving command-line options for #BaconVideoWidget
 **/
GOptionGroup*
bacon_video_widget_get_option_group (void)
{
  return gst_init_get_option_group ();
}


GQuark
bacon_video_widget_error_quark (void)
{
  static GQuark q; /* 0 */

  if (G_UNLIKELY (q == 0)) {
    q = g_quark_from_static_string ("bvw-error-quark");
  }
  return q;
}


static gboolean
bvw_set_playback_direction (BaconVideoWidget *bvw, gboolean forward)
{

  gboolean is_forward;
  gboolean retval;
  float target_rate;
  GstEvent *event;
  gint64 cur = 0;

  is_forward = (bvw->rate > 0.0);
  if (forward == is_forward)
  {
    return TRUE;
  }

  retval = FALSE;
  target_rate = (forward ? FORWARD_RATE : REVERSE_RATE);

  if (gst_element_query_position (bvw->pipeline, GST_FORMAT_TIME, &cur)) 
  {

            printf("(bvw_set_playback_direction) Setting playback direction to %s at %"G_GINT64_FORMAT" \n",
                DIRECTION_STR, cur);

    GST_DEBUG ("Setting playback direction to %s at %"G_GINT64_FORMAT"", DIRECTION_STR, cur);
    event = gst_event_new_seek (target_rate,
                GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                GST_SEEK_TYPE_SET, forward ? cur : G_GINT64_CONSTANT (0),
                GST_SEEK_TYPE_SET, forward ? G_GINT64_CONSTANT (0) : cur);
    if (gst_element_send_event (bvw->pipeline, event) == FALSE) 
    {
      GST_WARNING ("Failed to set playback direction to %s", DIRECTION_STR);
    } 
    else 
    {
      gst_element_get_state (bvw->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
      bvw->rate = target_rate;
      retval = TRUE;
    }
  } 
  else 
  {
    GST_LOG ("Failed to query position to set playback to %s", DIRECTION_STR);
  }

  return retval;
}


static GstElement *
element_make_or_warn (const char *plugin,
		      const char *name)
{
  GstElement *element;
  element = gst_element_factory_make (plugin, name);
  if (!element)
    g_warning ("Element '%s' is missing, verify your installation", plugin);
  return element;
}

static gboolean
is_feature_enabled (const char *env)
{
  const char *value;

  g_return_val_if_fail (env != NULL, FALSE);
  value = g_getenv (env);
  return g_strcmp0 (value, "1") == 0;
}




static gboolean
bvw_decodebin_video_outpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstTagList *taglist;
  gboolean res = TRUE;

  //fetch the `bvw` instance
  gpointer    *instance =
          ULONG_TO_POINTER (g_object_get_data (G_OBJECT (pad),
              "bvw.instance"));
  BaconVideoWidget *bvw = BACON_VIDEO_WIDGET (instance);

          // printf("(bvw_decodebin_video_outpad_event): %s \n", GST_EVENT_TYPE_NAME (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_TAG)
  {
      GstTagList *tags = NULL;
      tags = gst_tag_list_new_empty ();
      //@taglist: (out) (optional) (transfer none):
      gst_event_parse_tag (event, &tags);
      if (tags)
      {

// if (GST_IS_TAG_LIST(tags))
//       printf("(yes taglist) \n");
//   else
//       printf("(not taglist) \n");

          //got audio tags means tags change from null to non-null, pass to tags-changed-related callbacks, even if tags actually not change for our torrent streaming
          bvw_update_tags_delayed (bvw, tags, "video");
      }
  }

  return gst_pad_event_default (pad, parent, event);
}




static gboolean
bvw_decodebin_audio_outpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstTagList *taglist;
  gboolean res = TRUE;
  
  //fetch the `bvw` instance
  gpointer    *instance =
          ULONG_TO_POINTER (g_object_get_data (G_OBJECT (pad),
              "bvw.instance"));
  BaconVideoWidget *bvw = BACON_VIDEO_WIDGET (instance);

          // printf("(bvw_decodebin_audio_outpad_event): %s \n", GST_EVENT_TYPE_NAME (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_TAG)
  {
      GstTagList *tags;
      //@taglist: (out) (optional) (transfer none):
      gst_event_parse_tag (event, &tags);
      if (tags)
      {

// if (GST_IS_TAG_LIST(tags))
//       printf("(yes taglist) \n");
//   else
//       printf("(not taglist) \n");

          //got audio tags means tags change from null to non-null, pass to tags-changed-relcated callbacks, even if tags actually not change for our torrent streaming
          bvw_update_tags_delayed (bvw, tags, "audio");
      }
  }

  return gst_pad_event_default (pad, parent, event);
}






enum
{
  BVW_PAD_TYPE_AUDIO,
  BVW_PAD_TYPE_VIDEO
};
typedef struct
{
  BaconVideoWidget *bvw;
  gint stream_id;
  gint type;
} NotifyTagsData;
//when new deocdebin out pad added (audio or video), notify xx-tags-changed
static void
notify_tags_cb (GObject * object, GParamSpec * pspec, gpointer user_data)
{

        printf ("(bvw/notify_tags_cb) \n");

  NotifyTagsData *ntdata = (NotifyTagsData *) user_data;
  gint signal;

  // GST_DEBUG_OBJECT (ntdata->bvw, "Tags on pad %" GST_PTR_FORMAT
  //     " with stream id %d and type %d have changed",
  //     object, ntdata->stream_id, ntdata->type);

  switch (ntdata->type) {
    case BVW_PAD_TYPE_AUDIO:
      signal = SIGNAL_AUDIO_TAGS_CHANGED;
      break;
    case BVW_PAD_TYPE_VIDEO:
      signal = SIGNAL_VIDEO_TAGS_CHANGED;
      break;
    default:
      signal = -1;
      break;
  }

  if (signal > 0)
  {
    g_signal_emit (G_OBJECT (ntdata->bvw), bvw_signals[signal], 0, ntdata->stream_id);
  }
}





static void 
on_pad_removed (GstElement *element, GstPad *pad, BaconVideoWidget *bvw) 
{
    if (!GST_IS_PAD (pad))
    {
      printf ("(bvw/on_pad_removed) not pad, return \n");
      return;
    }

    //the pad that link to gstdeocdebin2's output pad (eg. src_0, src_1)
    GstPad *sink_pad = NULL;

    GstStructure *structure;
    const gchar *media_type;
    gulong notify_tags_handler = 0;

    //caps is empty ,cant use it to distinguish audio or video
    const gchar *pad_type = gst_pad_get_element_private (pad);  // Retrieve stored type

    const gchar *pad_name = GST_PAD_NAME (pad);
        printf("(bvw/on_pad_removed for decodebin) :%s, pad_type is %s\n", pad_name, pad_type);

    if (g_strcmp0 (pad_type, "audio") == 0) 
    { 
        guint x=0;
        if (g_ptr_array_find (bvw->audio_channels, pad, &x))
        {
          printf ("(bvw/on_pad_removed) existed in audio channels at %d\n", x);
          
          //unlinking pad 
          sink_pad = bvw->audio_sinpad_decodebin;
          if (!sink_pad) {
              g_warning("Failed to get the ghost 'sink' pad from audio_bin");
              goto beach;
          }

          if (sink_pad && gst_pad_is_linked (bvw->audio_sinpad_decodebin)) 
          {
            if (gst_pad_unlink (pad, sink_pad)){
                printf ("(bvw/on_pad_removed for decodebin) unlink audio srcpad with audio_bin success \n");
            } else {
                printf ("(bvw/on_pad_removed for decodebin) unlink audio srcpad with audio_bin failed \n");
            }
          }
          else
          {
            printf ("(bvw/on_pad_removed for decodebin) audio_bin sinkpad is not linked with any pad, no need to unlink \n");
          }


          gst_object_unref (g_ptr_array_index (bvw->audio_channels, x));
          if (g_ptr_array_remove (bvw->audio_channels, pad)){
            printf ("Removed decodebin's Audio pad from audio_channels, len after remove is %d\n",bvw->audio_channels->len);
          }else{
            printf ("Failed Removing decodebin's Audio pad from audio_channels\n");
          }
    

        }
    }
    else if (g_strcmp0 (pad_type, "video") == 0) 
    {
      guint x=0;
      if (g_ptr_array_find (bvw->video_channels, pad, &x))
      {
        printf ("(bvw/on_pad_removed) existed in video channels at %d\n", x);
      
        //unlinking pad
        sink_pad = bvw->video_sinpad_decodebin;
        if (!sink_pad) {
            g_warning("Failed to get 'sink' pad of bvw->glsinkbin");
            goto beach;
        }

        if (sink_pad && gst_pad_is_linked (bvw->video_sinpad_decodebin)) 
        {
            if (gst_pad_unlink (pad, sink_pad)){
                printf ("(bvw/on_pad_removed for decodebin) unlink video srcpad with glsinkbin success \n");
            }
            else{
                printf ("(bvw/on_pad_removed for decodebin) unlink video srcpad with glsinkbin failed \n");
            }
        }
        else
        {
            printf ("(bvw/on_pad_removed for decodebin)  glsinkbin sinkpad is not linked with any pad, no need to unlink \n");
        }

        gst_object_unref (g_ptr_array_index (bvw->video_channels, x));
        if(g_ptr_array_remove (bvw->video_channels, pad)){
          printf ("Removed decodebin's Video pad from video_channels, len after remove is %d\n",bvw->video_channels->len);
        }else{
          printf ("Failed Removing decodebin's Video pad from video_channels\n");
        }

      }
    }
    else
    {
      printf ("(bvw/on_pad_removed for decodebin) %s not found \n", pad_type);
      goto beach;
    }

    notify_tags_handler =
          POINTER_TO_ULONG (g_object_get_data (G_OBJECT (pad),
              "bvw.notify_tags_handler"));
    if (notify_tags_handler != 0)
        g_signal_handler_disconnect (G_OBJECT (pad), notify_tags_handler);

    g_object_set_data (G_OBJECT (pad), "bvw.notify_tags_handler", NULL);

    // printf ("(on_pad_removed for decodebin) END \n");


beach:
;

}




// docodebin -- audio,video output
// dynamically check the type of the pad from decodebin (whether it's video or audio) 
// and connect it to the appropriate sink elements
//NOTE:when moov after mdat,qtdemux will initiate a seek, after btdemux push the piece contaning moov and reset 
//pushing from first piece, after these done, can gstdecodebin2 srcpad added, so on_pad_added below will be called
static void 
on_pad_added (GstElement *element, GstPad *pad, BaconVideoWidget *bvw) 
{
    GstPad *sink_pad = NULL;
    GstCaps *pad_caps;
    GstStructure *structure;
    const gchar *media_type;
    gulong notify_tags_handler = 0;
    NotifyTagsData *ntdata;

    const gchar *pad_name = GST_PAD_NAME (pad);
    printf("(bvw/on_pad_added for decodebin) :%s\n", pad_name);

    pad_caps = gst_pad_query_caps (pad, NULL);

            // guint caps_sz =  gst_caps_get_size (pad_caps);
            // printf ("(decodebin2/on_pad_added) caps size is %d \n", caps_sz);

                      // Optionally, print the capabilities to debug
                      // gchar *caps_str;
                      // caps_str = gst_caps_to_string(pad_caps);
                      // printf("Pad capabilities: %s\n", caps_str);
                      // g_free(caps_str);


    // Extract the structure from the first (top-level) capability
    structure = gst_caps_get_structure (pad_caps, 0);
    media_type = gst_structure_get_name (structure);  // Get the media type (e.g., "audio/x-raw" or "video/x-raw")

    /****************AUDIO********************** */
    if (g_str_has_prefix (media_type, "audio/x-raw")) 
    {

      gst_pad_set_element_private(pad, "audio");  // Store as audio

      g_print("decodebin's Audio pad detected.\n");

      // Get the audio sink pad and link it
      sink_pad = bvw->audio_sinpad_decodebin;
      if (!sink_pad) 
      {
          g_warning("Failed to get the ghost 'sink' pad from audio_bin");
          goto beach;
      }
      if (sink_pad && !gst_pad_is_linked(sink_pad)) 
      {
                    // GstCaps *pad_caps_after_set;
                    // gchar *caps_str_after_set;
                    // pad_caps_after_set = gst_pad_query_caps(sink_pad, NULL);
                    // // Optionally, print the capabilities to debug
                    // caps_str_after_set = gst_caps_to_string(pad_caps_after_set);
                    // printf(" audio_bin sink pad caps: %s\n", caps_str_after_set);
                    // g_free(caps_str_after_set);

          GstPadLinkReturn link_result = gst_pad_link (pad, sink_pad);

                    // printf("(on_pad_added) gst_pad_link ret:%d \n", (int)link_result);

          if (link_result != GST_PAD_LINK_OK) 
          {
              g_warning("Failed to link audio pad to audio sink");
              goto beach;
          }

          //save `bvw` instance for later use
          g_object_set_data (G_OBJECT (bvw->audio_sinpad_decodebin), "bvw.instance", POINTER_TO_ULONG (bvw));

          //for get tags event
          gst_pad_set_event_function (bvw->audio_sinpad_decodebin,
              GST_DEBUG_FUNCPTR (bvw_decodebin_audio_outpad_event));
      }


      //***cache the decodebiin's audio ouutput pad for later use
      g_ptr_array_add(bvw->audio_channels, gst_object_ref (pad));


      //***set callback when 'audio' tags changed
      ntdata = g_new0 (NotifyTagsData, 1);
      ntdata->bvw = bvw;
      ntdata->stream_id = bvw->audio_channels->len;
      ntdata->type = BVW_PAD_TYPE_AUDIO;

      notify_tags_handler =
        g_signal_connect_data (G_OBJECT (bvw), "notify::tags",
        G_CALLBACK (notify_tags_cb), ntdata, (GClosureNotify) g_free,
        (GConnectFlags) 0);

      g_object_set_data (G_OBJECT (bvw), "bvw.notify_tags_handler",
            ULONG_TO_POINTER (notify_tags_handler));


      //***when decodebin output pad added(such as when start playing or switch to next in playlist), we emit a audio-changed signal on bvw
      g_signal_emit (bvw, bvw_signals[SIGNAL_AUDIO_CHANGED], 0, NULL);

    }
    /****************VIDEO********************/
    else if (g_str_has_prefix(media_type, "video/x-raw")) 
    {

        gst_pad_set_element_private(pad, "video");  // Store as video

        g_print("decodebin's Video pad detected.\n");
        // Get the video sink pad and link it
        sink_pad = bvw->video_sinpad_decodebin;
        if (!sink_pad) 
        {
            g_warning("Failed to get 'sink' pad of bvw->glsinkbin");
            goto beach;
        }

        if (sink_pad && !gst_pad_is_linked(sink_pad)) 
        {
            if (gst_pad_link(pad, sink_pad) != GST_PAD_LINK_OK) 
            {
                g_warning("Failed to link video pad to video sink");
                goto beach;
            }

            //save `bvw` instance for later use
            g_object_set_data (G_OBJECT (bvw->video_sinpad_decodebin), "bvw.instance", POINTER_TO_ULONG (bvw));

            //for get tags event
            gst_pad_set_event_function (bvw->video_sinpad_decodebin,
                GST_DEBUG_FUNCPTR (bvw_decodebin_video_outpad_event));
        }

        //***cache the decodebiin's video output pad for later use
        g_ptr_array_add (bvw->video_channels, gst_object_ref (pad));


        //***set callback when 'video' tags changed
        ntdata = g_new0 (NotifyTagsData, 1);
        ntdata->bvw = bvw;
        ntdata->stream_id = bvw->video_channels->len;
        ntdata->type = BVW_PAD_TYPE_VIDEO;

        notify_tags_handler =
          g_signal_connect_data (G_OBJECT (pad), "notify::tags",
          G_CALLBACK (notify_tags_cb), ntdata, (GClosureNotify) g_free,
          (GConnectFlags) 0);

        g_object_set_data (G_OBJECT (pad), "bvw.notify_tags_handler",
              ULONG_TO_POINTER (notify_tags_handler));


        //***when decodebin output pad added(such as when start playing or switch to next in playlist), we emit a video-changed signal on bvw
        g_signal_emit (bvw, bvw_signals[SIGNAL_VIDEO_CHANGED], 0, NULL);
    }

beach:
  gst_caps_unref (pad_caps);  // Unref the capabilities object
}





//Callback once `gst_element_add_pad` called 
// Callback function for pad-added signal from btdemux (to handle dynamic src pad)
// dont send stream-start event once you got an srcpadd on btdemux added, totem-playlsit choose the desired fileidx,
// the undesired pads will be removed, afther that, it is the final one we really to link to decodebin and plays it
static void 
on_btdemux_pad_added (GstElement *element, GstPad *pad, BaconVideoWidget *bvw) 
{

    // Get the name of the pad and check if it's a source pad (the ones we're interested in)
    const gchar *pad_name = GST_PAD_NAME (pad);
    g_print("(on_btdemux_pad_added) btdemux src pad added: %s\n", pad_name);

    if (g_str_has_prefix(pad_name, "src_")) 
    {
        // This is a source pad, so we need to link it to decodebin
        GstPad *decodebin_sink_pad = gst_element_get_static_pad (bvw->decodebin, "sink");

          //only link when we already set fileidx desired
         if (bvw->cur_video_fileidx_within_tor != -1)
         {
            //before linking, check whether there are existing link ,if any, unlink it
            if (gst_pad_is_linked (decodebin_sink_pad)
                && bvw->btdemux_srcpads->len > 0)
            {
              printf ("(on_btdemux_pad_added) Existing link between btdemux and decodebin2 checked, try to unlink \n");

              GstPad* old_pad = g_ptr_array_index (bvw->btdemux_srcpads, 0);
              if (old_pad && !gst_pad_unlink (old_pad, decodebin_sink_pad))
              {
                  printf ("(on_btdemux_pad_added) unlink the old links failed \n");
              }
            }

            //and then link the new pad to gstdecodebin2
            GstPadLinkReturn ret = gst_pad_link (pad, decodebin_sink_pad);

                          // int is_flushing = (int) GST_PAD_IS_FLUSHING (decodebin_sink_pad);
                          // printf ("(on_btdemux_pad_added) the decodebin_sink_pad is_flushing:%d \n", is_flushing);

            if (ret != GST_PAD_LINK_OK) 
            {
                printf ("(on_btdemux_pad_added) Failed to link btdemux src pad (%s) to decodebin sink pad, ret(%d)\n", pad_name, (int)ret);
            } 
            else 
            {
                printf ("(on_btdemux_pad_added) Successfully linked btdemux src pad (%s) to decodebin sink pad \n", pad_name);

                //store it , we will use it later, access still by fileidx (0,1,2,3...)
                g_ptr_array_add (bvw->btdemux_srcpads, gst_object_ref (pad));

                //if it is our desired srcpad of btdemux, then send stream-start event
            
                // dont forget to free it after use
                gchar *desired_pad_name = g_strdup_printf ("src_%02d", bvw->cur_video_fileidx_within_tor) ;

                //check if the added pad's name match our desired_pad_name, (such as src_00. src_01...) if it is ,send stream-start event
                if (g_strcmp0(pad_name, desired_pad_name) == 0) {

                  // printf ("(on_btdemux_pad_added) GST_PAD_MODE (pad) is %d \n",(int)GST_PAD_MODE (pad));
                  printf ("(on_btdemux_pad_added) gst_pad_is_active (pad) is %d \n",(int)gst_pad_is_active (pad));

                  //if the pad previously added and removed ,and here we add it again, gst_pad_is_active may fail before gst_element_add_pad called 
                  //activate it again
                  if( !gst_pad_is_active (pad) ){
                    if( !gst_pad_set_active (pad, TRUE) ){
                        printf ("(on_btdemux_pad_added) ENABLE gst_pad_set_active failed \n");
                    }else{  
                        printf ("(on_btdemux_pad_added) ENABLE gst_pad_set_active ok \n");
                    }
                  }

                  gchar *stream_id;
                  stream_id =
                      gst_pad_create_stream_id_printf (pad, bvw->btdemux,
                      "%02x", bvw->stream_id_seq);

                    // printf ("(on_btdemux_pad_added) Stream-start event has stream id %s on pad '%s'\n", stream_id, pad_name);
            
                  GstEvent *stream_start_event = gst_event_new_stream_start (stream_id);
                  if ( !gst_pad_push_event (pad, stream_start_event) ) 
                  {
                    //failed cuz the pad is flushing
                    g_warning ("(on_btdemux_pad_added) Failed to push stream-start event on pad '%s'", pad_name);
                  } 
                  else 
                  {
                      bvw->stream_id_seq++;
                      printf ("(on_btdemux_pad_added) Stream-start event pushed on pad '%s'\n", pad_name);
                  }

                              // // g_object_set (dec_elem, "sink-caps", caps, NULL);
                              // gboolean caps_set = gst_pad_has_current_caps (pad);
                              // if (caps_set) {
                              //   GstCaps* format = gst_pad_get_current_caps (pad);
                              //   if (format) {
                              //     gchar* capstr = gst_caps_to_string (format);
                              //     printf ("(on_btdemux_pad_added) Got caps set on pad is %s",capstr);
                              //     g_free (capstr);
                              //     gst_object_unref (format);
                              //   } else {
                              //     printf ("(on_btdemux_pad_added) Failed Got caps set on pad\n");
                              //   }
                              // }else{
                              //     printf ("(on_btdemux_pad_added) No caps set on pad\n");
                              // }
                }

                g_free (desired_pad_name);
            }
         }   
        gst_object_unref (decodebin_sink_pad);
    }
}



//Callback once `gst_element_remove_pad` called 
static void 
on_btdemux_pad_removed (GstElement *element, GstPad *pad, BaconVideoWidget *bvw) 
{
    const gchar *pad_name = GST_PAD_NAME(pad);
    
    printf ("(on_btdemux_pad_removed) btdemux src pad removed: %s\n", pad_name);


    //unlink if it has been linked to the pad that will be removed
    if ( gst_pad_is_linked (pad) )
    {
      GstPad *decodebin_sink_pad = gst_element_get_static_pad (bvw->decodebin, "sink");

      if (gst_pad_unlink (pad, decodebin_sink_pad))
      {
           printf ("(on_btdemux_pad_removed) unlink success %s\n", pad_name);

      }
      else{
           printf ("(on_btdemux_pad_removed) unlink failed %s\n", pad_name);

      }

      gst_object_unref (decodebin_sink_pad);
    }

    if (bvw->btdemux_srcpads->len)
    {
      guint x=0;
      if (g_ptr_array_find (bvw->btdemux_srcpads, pad, &x))
      {
          if (g_ptr_array_remove_index (bvw->btdemux_srcpads, x) != NULL)
          {
              printf ("(on_btdemux_pad_removed) btdemux src pad removed successfully: %s\n", pad_name);
          }
          else
          {
              printf ("(on_btdemux_pad_removed) btdemux src pad removed failed Not Found %s\n", pad_name);
          }
      }

     
    }


}




static void 
on_btdemux_no_more_pads (GstElement *element, gpointer user_data) 
{
    BaconVideoWidget *bvw = BACON_VIDEO_WIDGET (user_data);

    g_return_if_fail (BACON_IS_VIDEO_WIDGET (bvw));
    
    //bvw->btdemux_srcpads are expected to only have one or zero element
    if (bvw->btdemux_srcpads->len == 1)
    {
        GstPad *pad = g_ptr_array_index (bvw->btdemux_srcpads, 0);
        if(pad == NULL)
        {
          printf ("(on_btdemux_no_more_pads) on no more pads: Not found\n");
          return;
        }

        const gchar *pad_name = GST_PAD_NAME(pad);
    
        printf ("(on_btdemux_no_more_pads) on no more pads: %s\n", pad_name);

        // GstEvent *stream_start_event = gst_event_new_stream_start ("totem");
        // if (!gst_pad_push_event (pad, stream_start_event)) 
        // {
        //     g_warning ("Failed to push stream-start event on pad '%s'", pad_name);
        // } 
        // else 
        // {
        //     g_print ("(on_btdemux_no_more_pads) Stream-start event pushed on pad '%s'\n", pad_name);
        // }
    }
    else
    {
        printf ("(on_btdemux_no_more_pads) Not Found btdemux src pad stored in bvw->btdemux_srcpads \n");
    }
}
















/*****************************************************************/
static void
bacon_video_widget_init (BaconVideoWidget *bvw)
{

                            printf("(bacon_video_widget_init) \n");
        
  GstElement *audio_sink = NULL;
  gchar *version_str;


  GstPad *audio_convert_pad;
  char *template;

  gtk_widget_set_can_focus (GTK_WIDGET (bvw), TRUE);

  g_type_class_ref (BVW_TYPE_METADATA_TYPE);
  g_type_class_ref (BVW_TYPE_ROTATION);

  bvw->cur_video_fileidx_within_tor = -1;
  bvw->volume = -1.0;
  bvw->rate = FORWARD_RATE;
  bvw->tag_update_queue = g_async_queue_new_full ((GDestroyNotify) update_tags_delayed_data_destroy);


  g_mutex_init (&bvw->seek_mutex);
  g_mutex_init (&bvw->get_audiotags_mutex);
  g_mutex_init (&bvw->get_videotags_mutex);


  bvw->clock = gst_system_clock_obtain ();
  bvw->seek_req_time = GST_CLOCK_TIME_NONE;
  bvw->seek_time = -1;

#ifndef GST_DISABLE_GST_DEBUG
  if (_totem_gst_debug_cat == NULL) 
  {
    GST_DEBUG_CATEGORY_INIT (_totem_gst_debug_cat, "totem", 0,
        "Totem GStreamer Backend");
  }
#endif

  version_str = gst_version_string ();
  GST_DEBUG ("Initialised %s", version_str);
  g_free (version_str);


  //used for generating missing plugin 
  gst_pb_utils_init ();

  gtk_widget_set_events (GTK_WIDGET (bvw),
			 gtk_widget_get_events (GTK_WIDGET (bvw)) |
			 GDK_SCROLL_MASK |
			 GDK_POINTER_MOTION_MASK |
			 GDK_BUTTON_MOTION_MASK |
			 GDK_BUTTON_PRESS_MASK |
			 GDK_BUTTON_RELEASE_MASK |
			 GDK_KEY_PRESS_MASK);
  gtk_widget_init_template (GTK_WIDGET (bvw));


   bvw->video_channels = g_ptr_array_new ();
   bvw->audio_channels = g_ptr_array_new ();

  bvw->btdemux_srcpads = g_ptr_array_new();
  /* Instantiate all the fallible plugins */

  bvw->stream_id_seq = 0;

/****we dont use playbin here, so playbin-related code is nonsense.just comment them ******/


              // Create the pipeline element
              bvw->pipeline = gst_pipeline_new("bvw-pipeline");


              //for read .torrent file  , accept .torrent file
              bvw->filesrc = element_make_or_warn("filesrc", "filesrc");


              // torrent "demuxer", push torrent piece to decodebin. a bit like filesrc
              bvw->btdemux = element_make_or_warn("btdemux", "btdemux");


              // a high-level element used for demuxing and deocding, such as qtdemux, decoders , etc...
              bvw->decodebin = element_make_or_warn ("decodebin", "decodebin");


              //adjusts the playback rate of a media stream
              bvw->audio_pitchcontrol = element_make_or_warn ("scaletempo", "scaletempo");


              // render video using OpenGL within a GTK application, taking advantage of OpenGL for enhanced visual effects
              bvw->video_sink = element_make_or_warn ("gtkglsink", "video-sink");


              //serves as a container for OpenGL-based rendering, providing a bridge between multimedia streaming and graphics rendering.
              bvw->glsinkbin = element_make_or_warn ("glsinkbin", "glsinkbin");


              audio_sink = element_make_or_warn ("autoaudiosink", "audio-sink");



  if (!bvw->pipeline ||
      !bvw->filesrc ||
      !bvw->btdemux ||
      !bvw->decodebin ||
      !bvw->audio_pitchcontrol ||
      !bvw->video_sink ||
      !audio_sink ||
      !bvw->glsinkbin) {
    if (bvw->video_sink)
      g_object_ref_sink (bvw->video_sink);
    if (audio_sink)
      g_object_ref_sink (audio_sink);
    g_clear_object (&audio_sink);
    if (bvw->glsinkbin)
      g_object_ref_sink (bvw->glsinkbin);
    g_clear_object (&bvw->glsinkbin);
    bvw->init_error = g_error_new_literal (BVW_ERROR, BVW_ERROR_PLUGIN_LOAD,
					   _("Some necessary plug-ins are missing. "
					     "Make sure that the program is correctly installed."));
    return;
  }


  bvw->speakersetup = BVW_AUDIO_SOUND_STEREO;
  bvw->ratio_type = BVW_RATIO_AUTO;
  bvw->cursor_shown = TRUE;


            // Set location for the filesrc (assuming you have a torrent or file URL)

            // g_object_set(bvw->filesrc, "location", "/media/pal/E/FreeDsktop/gst-bt/install/lib/gstreamer-1.0/Japan.torrent", NULL);
            g_object_set(bvw->filesrc, "location", "/media/pal/E/FreeDsktop/gst-bt/install/lib/gstreamer-1.0/Korea.torrent", NULL);

            // g_object_set(bvw->filesrc, "location", "/media/pal/E/FreeDsktop/gst-bt/install/lib/gstreamer-1.0/ForBiggerBlazes.mp4.torrent", NULL);

            g_object_set(bvw->btdemux, "temp-location", "/home/pal/Documents", NULL);
            // g_object_set(bvw->btdemux, "temp-location", "/home/pal/Downloads", NULL);

            // g_object_set(bvw->btdemux, "temp-location", "/home/pal/Pictures", NULL);

              /*******  Create video output widget ********/
/*
                _______________________glsinkbin________________________
                |          gtkglsink                                    |
                |_______________________________________________________|
*/
              /*video-sink*/
              // control the display of frames per second (FPS) statistics  ,used for performance monitoring
              if (is_feature_enabled ("FPS_DISPLAY")) {
                GstElement *fps;
                fps = gst_element_factory_make ("fpsdisplaysink", "fpsdisplaysink");
                g_object_set (bvw->glsinkbin, "sink", fps, NULL);
                g_object_set (fps, "video-sink", bvw->video_sink, NULL);
              } 
              else 
              {
                g_object_set (bvw->glsinkbin, "sink", bvw->video_sink, NULL);
              }
    
              // obtaining the widget property from the gtkglsink element, to render the video to a GTK widget
              g_object_get (bvw->video_sink, "widget", &bvw->video_widget, NULL);
              gtk_widget_show (bvw->video_widget);
              gtk_stack_add_named (GTK_STACK (bvw->stack), bvw->video_widget, "video");
              g_object_unref (bvw->video_widget);
              gtk_stack_set_visible_child_name (GTK_STACK (bvw->stack), "video");

              //how to interpret the orientation of video frames control how the video is oriented and displayed.
              //eg. Horizontal :where the width of the video is greater than its height.
              //    Vertical :where the height is greater than the width
              g_object_set (bvw->video_sink,
                            "rotate-method", GST_VIDEO_ORIENTATION_AUTO,
                            NULL);

              //store as field for getting tags event
              bvw->video_sinpad_decodebin = gst_element_get_static_pad (bvw->glsinkbin, "sink");
   





/* Audio Processing Chain 

                       -------------------[audiosinkbin] ------------------------------------------------------------------------------
                       |                                                                                                              |
     (ghost pad)       |        (sink)[audioconvert](src)  (sink)[capsfilter] (src) ---- (sink)[scaletempo](src) ---- (sink)[autoaudiosink](src)          |  (src)
              |        ---------------|--------------------------------------------------------------------------------|------------  |
              |_______________________|                                                                                |______________|

  
*/
              /* Link the audiopitch element */
              // filter the capabilities (caps) of the media data being processed in a pipeline
              bvw->audio_capsfilter = gst_element_factory_make ("capsfilter", "audiofilter");

              bvw->audio_filter_convert = gst_element_factory_make("audioconvert", "audioconvert");


              // Container that can hold other GStreamer elements
              bvw->audio_bin = gst_bin_new ("audiosinkbin");
              //used to add multiple GstElement instances to a GstBin
              gst_bin_add_many (GST_BIN (bvw->audio_bin),
                                bvw->audio_filter_convert,
                                bvw->audio_capsfilter,
                                bvw->audio_pitchcontrol,
                                audio_sink, 
                                NULL);


              //link multiple GstElement instances together in sequence
              //connecting the output pad of one element to the input pad of the next
              gst_element_link_many (bvw->audio_filter_convert,
                                     bvw->audio_capsfilter,
                                     bvw->audio_pitchcontrol,
                                     audio_sink,
                                     NULL);

               
              // GstBin does not inherently have a "sink" or "src" pad because it's a container for other elements
              // it uses ghost pads to expose pads from the elements inside it to the outside world
              // created a ghost pad for audio_bin that points to the "sink" pad of audio_capsfilte
              // Ghost pads are used to make the pads of internal elements accessible from the outside
              audio_convert_pad = gst_element_get_static_pad (bvw->audio_filter_convert, "sink");
            
              
                      // GstCaps *pad_caps;
                      // gchar *caps_str;
                      // pad_caps = gst_pad_query_caps(audio_convert_pad, NULL);
                      // Optionally, print the capabilities to debug
                      // caps_str = gst_caps_to_string(pad_caps);
                      // printf(" audio_convert sink pad capabilities: %s\n", caps_str);
                      // g_free(caps_str);
               

              // bvw->audio_bin now has a "sink" ghost pad that maps to the "sink" pad of audio_capsfilter
              gst_element_add_pad (bvw->audio_bin, gst_ghost_pad_new ("sink", audio_convert_pad));
              gst_object_unref (audio_convert_pad);

              //store as field for getting tags event
              bvw->audio_sinpad_decodebin  = gst_element_get_static_pad (bvw->audio_bin, "sink");


                      // GstPad* audio_bin_sinkpad = gst_element_get_static_pad (bvw->audio_bin, "sink");
                      // GstCaps *audio_bin_sinkpad_caps;
                      // gchar *audio_bin_sinkpad_caps_str;
                      // audio_bin_sinkpad_caps = gst_pad_query_caps(audio_bin_sinkpad, NULL);
                      // // Optionally, print the capabilities to debug
                      // audio_bin_sinkpad_caps_str = gst_caps_to_string(audio_bin_sinkpad_caps);
                      // printf(" audio_bin sink pad capabilities: %s\n", audio_bin_sinkpad_caps_str);
                      // g_free(audio_bin_sinkpad_caps_str);
           

              /* Now, Construct the Pipeline */
              printf("(bacon_video_widget_can_direct_seek \n");

//  Adds a %NULL-terminated list of elements to a bin
              gst_bin_add_many (GST_BIN(bvw->pipeline), bvw->filesrc, bvw->btdemux, bvw->decodebin, bvw->glsinkbin, bvw->audio_bin, NULL);

       
              // First link filesrc -> btdemux
              if (!gst_element_link(bvw->filesrc, bvw->btdemux)) {
                  g_warning("Failed to link filesrc and btdemux");
              }


              // Then link btdemux -> decodebin thru 'on_btdemux_pad_added' callback (dynamically), once got src pad of btdemux, link it to decodebin
              g_signal_connect(bvw->btdemux, "pad-added", G_CALLBACK(on_btdemux_pad_added), bvw);

              //switch item in playlist ,remove old btmux pad
              g_signal_connect(bvw->btdemux, "pad-removed", G_CALLBACK(on_btdemux_pad_removed), bvw);

              g_signal_connect(bvw->btdemux, "no-more-pads", G_CALLBACK(on_btdemux_no_more_pads), bvw);


              // link the video pad from decodebin to the video sink (inside bvw->glsinkbin)
              // also link the audio pad from decodebin to the audio sink (inside bvw->audio_bin)
              g_signal_connect(bvw->decodebin, "pad-added", G_CALLBACK(on_pad_added), bvw);

          
              //Also add pad-removed cb 
              g_signal_connect(bvw->decodebin, "pad-removed", G_CALLBACK(on_pad_removed), bvw);
            

              /* pipeline bus managemenet */
              // Retrieve the bus from the pipeline and store it in the BaconVideoWidget struct
              // only a GstPipeline will provide a bus for the application.
              bvw->bus = gst_element_get_bus(bvw->pipeline);


              gst_bus_add_signal_watch (bvw->bus);


              bvw->sig_bus_async = g_signal_connect (bvw->bus, "message", 
                    G_CALLBACK (bvw_bus_message_cb), bvw);


  // The notify::volume signal is emitted whenever the volume property changes
  // For example, if a user adjusts the volume via a slider in a user interface, 
  // your application can capture this change and update the playback accordingly.
  g_signal_connect (bvw, "notify::volume",
      G_CALLBACK (notify_volume_cb), bvw);


  // signal that the video output has changed such as switch between video tracks.
  g_signal_connect (bvw, "video-changed",
      G_CALLBACK (bvw_stream_changed_cb), NULL);
  // signal that the audio output has changed such as switch between audio tracks.
  g_signal_connect (bvw, "audio-changed",
      G_CALLBACK (bvw_stream_changed_cb), NULL);


  // For example, if a user is watching a movie and the movie's tags are updated 
  // (e.g., the title or genre is added or changed), your application can react accordingly
  g_signal_connect (bvw, "video-tags-changed",
      G_CALLBACK (video_tags_changed_cb), bvw);
  g_signal_connect (bvw, "audio-tags-changed",
      G_CALLBACK (audio_tags_changed_cb), bvw);


}







/**
 * bacon_video_widget_new:
 *
 * Creates a new #BaconVideoWidget.
 *
 * Return value: a new #BaconVideoWidget; destroy with gtk_widget_destroy()
 **/
GtkWidget *
bacon_video_widget_new (void)
{
  return GTK_WIDGET (g_object_new (BACON_TYPE_VIDEO_WIDGET, NULL));
}


/**
 * bacon_video_widget_check_init:
 * @error: a #GError, or %NULL.
 *
 * Return value: if an error occured during initialisation, %FALSE is returned
 *   and @error is set. Otherwise, %TRUE is returned.
 **/
gboolean
bacon_video_widget_check_init (BaconVideoWidget  *bvw,
			       GError           **error)
{
  g_return_val_if_fail (bvw != NULL, FALSE);
  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);

  if (!bvw->init_error)
    return TRUE;

  g_propagate_error (error, bvw->init_error);
  bvw->init_error = NULL;
  return FALSE;
}


/**
 * bacon_video_widget_get_rate:
 * @bvw: a #BaconVideoWidget
 *
 * Get the current playback rate, with 1.0 being normal rate.
 *
 * Returns: the current playback rate
 **/
gfloat
bacon_video_widget_get_rate (BaconVideoWidget *bvw)
{
  return bvw->rate;
}

/**
 * bacon_video_widget_set_rate:
 * @bvw: a #BaconVideoWidget
 * @new_rate: the new playback rate
 *
 * Sets the current playback rate.
 *
 * Returns: %TRUE on success, %FALSE on failure.
 **/
gboolean
bacon_video_widget_set_rate (BaconVideoWidget *bvw,
			     gfloat            new_rate)
{
  GstEvent *event;
  gboolean retval = FALSE;
  gint64 cur;

  g_return_val_if_fail (BACON_IS_VIDEO_WIDGET (bvw), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (bvw->pipeline), FALSE);

  if (new_rate == bvw->rate)
    return TRUE;

  /* set upper and lower bound for rate */
  if (new_rate < BVW_MIN_RATE)
  {
    return retval;
  }
  if (new_rate > BVW_MAX_RATE)
  {
    return retval;
  }

  if (gst_element_query_position (bvw->pipeline, GST_FORMAT_TIME, &cur)) 
  {
    GST_DEBUG ("Setting new rate at %"G_GINT64_FORMAT"", cur);
    event = gst_event_new_seek (new_rate,
				GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
				GST_SEEK_TYPE_SET, cur,
				GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
    if (gst_element_send_event (bvw->pipeline, event) == FALSE) 
    {
      GST_DEBUG ("Failed to change rate");
    } 
    else 
    {
      gst_element_get_state (bvw->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
      bvw->rate = new_rate;
      retval = TRUE;
    }
  } 
  else 
  {
    GST_DEBUG ("failed to query position");
  }

  return retval;
}

/*
 * vim: sw=2 ts=8 cindent noai bs=2
 */
