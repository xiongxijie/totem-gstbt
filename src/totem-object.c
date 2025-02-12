/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007 Bastien Nocera <hadess@hadess.net>
 *
 * SPDX-License-Identifier: GPL-3-or-later
 *
 */


/**
 * SECTION:totem-object
 * @short_description: main Totem object
 * @stability: Unstable
 * @include: totem.h
 *
 * #TotemObject is the core object of Totem; a singleton which controls all Totem's main functions.
 **/

#include "config.h"

#include <glib-object.h>
#include <gtk/gtk.h>
#include <gtk/gtkcssprovider.h>
#include <gtk/gtkstylecontext.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <libhandy-1/handy.h>

#include <string.h>
#include <stdio.h>

#include "gst/totem-gst-helpers.h"
#include "totem.h"
#include "totem-private.h"
#include "totem-options.h"
#include "totem-plugins-engine.h"
#include "totem-playlist.h"
#include "bacon-video-widget.h"
#include "bacon-time-label.h"
#include "bitfield-scale.h" // for UnfinishedBlockInfo
#include "totem-menu.h"
#include "totem-uri.h"
#include "totem-interface.h"
#include "totem-preferences-dialog.h"
// #include "totem-session.h"

#include "totem-player-toolbar.h"



#define REWIND_OR_PREVIOUS 4000

#define SEEK_FORWARD_SHORT_OFFSET 15
#define SEEK_BACKWARD_SHORT_OFFSET -5

#define SEEK_FORWARD_LONG_OFFSET 10*60
#define SEEK_BACKWARD_LONG_OFFSET -3*60

#define DEFAULT_WINDOW_W 650
#define DEFAULT_WINDOW_H 500

#define POPUP_HIDING_TIMEOUT 2 /* seconds */
#define OVERLAY_OPACITY 0.86

#define TOTEM_SESSION_SAVE_TIMEOUT 10 /* seconds */

#define TOTEM_NULL_STREAMING_FILE_IDX -1
#define TOTEM_CLEAR_STREAMING_FILE_IDX -2


/* casts are to shut gcc up */
static const GtkTargetEntry target_table[] = {
	{ (gchar*) "text/uri-list", 0, 0 },
	{ (gchar*) "_NETSCAPE_URL", 0, 1 }
};


static void update_buttons (TotemObject *totem);
static void playlist_changed_cb (GtkWidget *playlist, TotemObject *totem);
static void play_pause_set_label (TotemObject *totem, TotemStates state);
static void totem_object_set_fileidx_and_play (TotemObject *totem, gint fileidx);
static void mark_popup_busy (TotemObject *totem, const char *reason);
static void unmark_popup_busy (TotemObject *totem, const char *reason);
static void video_widget_create (TotemObject *totem);
static void playlist_widget_setup (TotemObject *totem);
static void totem_callback_connect (TotemObject *totem);
static void totem_setup_window (TotemObject *totem);
static void totem_got_torrent_videos_info (BaconVideoWidget* bvw ,TotemObject * totem);
static void totem_got_piece_block_info (BaconVideoWidget* bvw ,PieceBlockInfoSd *sd, TotemObject * totem);
static void totem_got_finished_piece_info (BaconVideoWidget* bvw ,guint8 *byte_array, TotemObject * totem);
static void totem_got_ppi_info (BaconVideoWidget *bvw, DownloadingBlocksSd *sd, TotemObject *totem);

#define action_set_sensitive(name, state)					\
	{										\
		GAction *__action;							\
		__action = g_action_map_lookup_action (G_ACTION_MAP (totem), name);	\
		g_simple_action_set_enabled (G_SIMPLE_ACTION (__action), state);	\
	}

/* Callback functions for GtkBuilder */
G_MODULE_EXPORT gboolean main_window_destroy_cb (GtkWidget *widget, GdkEvent *event, TotemObject *totem);
G_MODULE_EXPORT gboolean window_state_event_cb (GtkWidget *window, GdkEventWindowState *event, TotemObject *totem);
G_MODULE_EXPORT void seek_slider_changed_cb (GtkAdjustment *adj, TotemObject *totem);
G_MODULE_EXPORT gboolean window_key_press_event_cb (GtkWidget *win, GdkEventKey *event, TotemObject *totem);


/* Popup Menu */
G_MODULE_EXPORT void popup_menu_shown_cb                (GtkToggleButton *button, TotemObject *totem);

/* Seekbar */
G_MODULE_EXPORT gboolean seek_slider_pressed_cb         (GtkWidget *widget, GdkEventButton *event, TotemObject *totem);
G_MODULE_EXPORT gboolean seek_slider_released_cb        (GtkWidget *widget, GdkEventButton *event, TotemObject *totem);
G_MODULE_EXPORT gboolean seek_slider_scroll_event_cb    (GtkWidget *widget, GdkEventScroll *event, gpointer user_data);

/* Volume */
G_MODULE_EXPORT void     volume_button_value_changed_cb (GtkScaleButton *button, gdouble value, TotemObject *totem);
G_MODULE_EXPORT gboolean volume_button_scroll_event_cb  (GtkWidget *widget, GdkEventScroll *event, gpointer data);

/* Bacon Video Widget */
G_MODULE_EXPORT gboolean on_video_button_press_event    (BaconVideoWidget *bvw, GdkEventButton *event, TotemObject *totem);
G_MODULE_EXPORT gboolean on_eos_event                   (GtkWidget *widget, TotemObject *totem);

G_MODULE_EXPORT void     on_channels_change_event       (BaconVideoWidget *bvw, TotemObject *totem);
G_MODULE_EXPORT void     update_current_time            (BaconVideoWidget *bvw,
                                                         gint64            current_time,
                                                         gint64            stream_length,
                                                         double            current_position,
                                                         gboolean          seekable,
                                                         TotemObject      *totem);
G_MODULE_EXPORT void     on_got_metadata_event          (BaconVideoWidget *bvw, TotemObject *totem);
G_MODULE_EXPORT void     on_error_event                 (BaconVideoWidget *bvw, char *message, gboolean playback_stopped, TotemObject *totem);
G_MODULE_EXPORT void     play_starting_cb               (BaconVideoWidget *bvw, TotemObject *totem);
G_MODULE_EXPORT gboolean on_bvw_motion_notify_cb        (BaconVideoWidget *bvw, GdkEventMotion *event, TotemObject *totem);


G_MODULE_EXPORT void     property_notify_cb_volume      (BaconVideoWidget *bvw, GParamSpec *spec, TotemObject *totem);
G_MODULE_EXPORT void     property_notify_cb_seekable    (BaconVideoWidget *bvw, GParamSpec *spec, TotemObject *totem);

enum {
	PROP_0,
	PROP_FULLSCREEN,
	PROP_PLAYING,
	PROP_STREAM_LENGTH,
	PROP_SEEKABLE,
	PROP_CURRENT_FILEIDX,
	PROP_CURRENT_TIME,
	PROP_CURRENT_MRL,
	PROP_CURRENT_CONTENT_TYPE,
	PROP_CURRENT_DISPLAY_NAME,
	PROP_MAIN_PAGE
};

enum {
	FILEIDX_OPENED,
	FILEIDX_CLOSED,
	FILE_HAS_PLAYED,
	METADATA_UPDATED,
	LAST_SIGNAL
};





static void totem_object_get_property		(GObject *object,
						 guint property_id,
						 GValue *value,
						 GParamSpec *pspec);
static void totem_object_finalize (GObject *totem);

static int totem_table_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(TotemObject, totem_object, GTK_TYPE_APPLICATION)

/**
 * totem_object_plugins_init:
 * @totem: a #TotemObject
 *
 * Initialises the plugin engine and activates all the
 * enabled plugins.
 **/
static void
totem_object_plugins_init (TotemObject *totem)
{
				printf("(totem_object_plugins_init) \n");
	if (totem->engine == NULL)
		totem->engine = totem_plugins_engine_get_default (totem);
}

/**
 * totem_object_plugins_shutdown:
 * @totem: a #TotemObject
 *
 * Shuts down the plugin engine and deactivates all the
 * plugins.
 **/
static void
totem_object_plugins_shutdown (TotemObject *totem)
{
	if (totem->engine)
		totem_plugins_engine_shut_down (totem->engine);
	g_clear_object (&totem->engine);
}




static void
totem_object_app_open (GApplication  *application,
		       GFile        **files,
		       gint           n_files,
		       const char    *hint)
{

					printf("(totem_object_app_open) \n");

					for (gint i = 0; i < n_files; i++) 
					{
						GFile *file = files[i];
						gchar *file_path = g_file_get_path(file);

						// Use the file_path as needed
						g_print("File %d: %s\n", i, file_path);

						// Free the allocated file path
						g_free(file_path);
					}


	GSList *slist = NULL;
	Totem *totem = TOTEM_OBJECT (application);
	int i;

	// optionstate.had_filenames = (n_files > 0);

	g_application_activate (application);
	gtk_window_present_with_time (GTK_WINDOW (totem->win),
				      gtk_get_current_event_time ());

	// totem_object_set_main_page (TOTEM_OBJECT (application), "player");

	for (i = 0 ; i < n_files; i++)
		slist = g_slist_prepend (slist, g_file_get_uri (files[i]));

	slist = g_slist_reverse (slist);
	// totem_object_open_files_list (TOTEM_OBJECT (application), slist);
	g_slist_free_full (slist, g_free);
}




static void
totem_object_app_activate (GApplication *app)
{

					printf("(totem-object/totem_object_app_activate)\n");

	Totem *totem;
	GtkStyleContext *style_context;

	totem = TOTEM_OBJECT (app);

	/* Already init'ed? */
	if (totem->xml != NULL)
	{
		return;
	}

	/* Main window */
	totem->xml = gtk_builder_new_from_resource ("/org/gnome/totem/ui/totem.ui");

	gtk_builder_connect_signals (totem->xml, totem);
	/* Bacon-Video-Widget */
	totem->bvw = BACON_VIDEO_WIDGET (gtk_builder_get_object (totem->xml, "bvw"));


				
					// registre signal handler, when `g_signal_emit()` called somewhere, 
					// callabck will update playlist according to message btdemux posted
					g_signal_connect (totem->bvw, "btdemux-videoinfo", G_CALLBACK (totem_got_torrent_videos_info), totem);


					g_signal_connect (totem->bvw, "piece-block-info", G_CALLBACK (totem_got_piece_block_info), totem);


					g_signal_connect (totem->bvw, "finished-piece-info", G_CALLBACK (totem_got_finished_piece_info), totem);


					g_signal_connect (totem->bvw, "ppi-info", G_CALLBACK (totem_got_ppi_info), totem);


	totem->win = GTK_WIDGET (gtk_builder_get_object (totem->xml, "totem_main_window"));
#if DEVELOPMENT_VERSION
	style_context = gtk_widget_get_style_context (GTK_WIDGET (totem->win));
	gtk_style_context_add_class (style_context, "devel");
#endif


	/* The playlist widget */
	playlist_widget_setup (totem);

	totem->state = STATE_STOPPED;
	totem->seek_lock = FALSE;

	/*key shortcut accels setup*/
	totem_app_menu_setup (totem);

	/* totem_callback_connect (totem); XXX we do this later now, so it might look ugly for a short while */

	totem_setup_window (totem);

	/* Show ! (again) the video widget this time. */
	video_widget_create (totem);

	/* Show ! */
	gtk_widget_show (totem->win);
	g_application_mark_busy (G_APPLICATION (totem));

	totem->controls_visibility = TOTEM_CONTROLS_UNDEFINED;

	totem->spinner = GTK_WIDGET (gtk_builder_get_object (totem->xml, "spinner"));
		

	/* Previous / Next */
	action_set_sensitive ("next-chapter", FALSE);
	action_set_sensitive ("previous-chapter", FALSE);


	/*Seek Bar (Derived from GtkScale support display libtorrent's bitfield*/
	totem->seek = GTK_WIDGET (gtk_builder_get_object (totem->xml, "bitfield_progress_bar"));
	totem->seekadj = gtk_range_get_adjustment (GTK_RANGE (totem->seek));
		

	/*Volume*/
	totem->volume = GTK_WIDGET (gtk_builder_get_object (totem->xml, "volume_button"));
	gboolean can_set_vol = bacon_video_widget_can_set_volume (totem->bvw);
	gtk_widget_set_sensitive (totem->volume, can_set_vol);
	totem->volume_sensitive = can_set_vol;


	/*Time Label*/
	totem->time_label = BACON_TIME_LABEL (gtk_builder_get_object (totem->xml, "time_label"));
	totem->time_rem_label = BACON_TIME_LABEL (gtk_builder_get_object (totem->xml, "time_rem_label"));

	totem_callback_connect (totem);

	gtk_widget_grab_focus (GTK_WIDGET (totem->bvw));


	/* The prefs after the video widget is connected */
	totem->prefs = totem_preferences_dialog_new (totem);
	gtk_window_set_transient_for (GTK_WINDOW (totem->prefs), GTK_WINDOW(totem->win));


	/* Initialise all the plugins, and set the default page, in case
	 * it comes from a plugin */
	totem_object_plugins_init (totem);


	/* We're only supposed to be called from totem_object_app_handle_local_options()
	 * and totem_object_app_open() */


	// if (totem_session_try_restore (totem) == FALSE) 
	// {
			action_set_sensitive ("play", TRUE);
			play_pause_set_label (totem, STATE_PLAYING);
			
				printf("(totem_object_app_activate) set fileidx as -1 to start pipeline but not playing yet \n");
			//initially set fileidx as -1, we start the pipeline, so btdemux can start loop , so can it feed videos_info to retrieve playlist
			totem_object_set_fileidx (totem, TOTEM_NULL_STREAMING_FILE_IDX);
			bacon_video_widget_open (totem->bvw, -1);


			//// totem_object_play(totem);

			g_application_unmark_busy (G_APPLICATION (totem));

			gtk_window_set_application (GTK_WINDOW (totem->win), GTK_APPLICATION (totem));
	// }
	gtk_window_present_with_time (GTK_WINDOW (totem->win), GDK_CURRENT_TIME);
}

static int
totem_object_app_handle_local_options (GApplication *application,
				       GVariantDict *options)
{

									printf("(totem_object_app_handle_local_options) \n");

	g_autoptr(GError) error = NULL;

	if (!g_application_register (application, NULL, &error)) 
	{

							printf("Failed to register application:%s \n", error->message);

		g_warning ("Failed to register application: %s", error->message);
		return 1;
	}

	if (!g_application_get_is_remote (application)) 
	{
							printf("NOT g_application_get_is_remote \n");
		HdyStyleManager *style_manager;
		hdy_init ();
		style_manager = hdy_style_manager_get_default ();
		hdy_style_manager_set_color_scheme (style_manager, HDY_COLOR_SCHEME_FORCE_DARK);
	}

	// totem_options_process_for_server (TOTEM_OBJECT (application), &optionstate);

	return -1;
}





static void
totem_object_class_init (TotemObjectClass *klass)
{

			printf("(totem_object_class_init) \n");

	GObjectClass *object_class;
	GApplicationClass *app_class;

	object_class = (GObjectClass *) klass;
	app_class = (GApplicationClass *) klass;

	object_class->get_property = totem_object_get_property;
	object_class->finalize = totem_object_finalize;

	app_class->activate = totem_object_app_activate;
	app_class->open = totem_object_app_open;
	app_class->handle_local_options = totem_object_app_handle_local_options;

	/**
	 * TotemObject:fullscreen:
	 *
	 * If %TRUE, Totem is in fullscreen mode.
	 **/
	// g_object_class_install_property (object_class, PROP_FULLSCREEN,
	// 				 g_param_spec_boolean ("fullscreen", "Fullscreen?", "Whether Totem is in fullscreen mode.",
	// 						       FALSE, G_PARAM_READABLE));

	/**
	 * TotemObject:playing:
	 *
	 * If %TRUE, Totem is playing an audio or video file.
	 **/
	g_object_class_install_property (object_class, PROP_PLAYING,
					 g_param_spec_boolean ("playing", "Playing?", "Whether Totem is currently playing a file.",
							       FALSE, G_PARAM_READABLE));

	/**
	 * TotemObject:stream-length:
	 *
	 * The length of the current stream, in milliseconds.
	 **/
	g_object_class_install_property (object_class, PROP_STREAM_LENGTH,
					 g_param_spec_int64 ("stream-length", "Stream length", "The length of the current stream.",
							     G_MININT64, G_MAXINT64, 0,
							     G_PARAM_READABLE));

	/**
	 * TotemObject:current-time:
	 *
	 * The player's position (time) in the current stream, in milliseconds.
	 **/
	g_object_class_install_property (object_class, PROP_CURRENT_TIME,
					 g_param_spec_int64 ("current-time", "Current time", "The player's position (time) in the current stream.",
							     G_MININT64, G_MAXINT64, 0,
							     G_PARAM_READABLE));

	/**
	 * TotemObject:seekable:
	 *
	 * If %TRUE, the current stream is seekable.
	 **/
	g_object_class_install_property (object_class, PROP_SEEKABLE,
					 g_param_spec_boolean ("seekable", "Seekable?", "Whether the current stream is seekable.",
							       FALSE, G_PARAM_READABLE));

	/**
	 * TotemObject:current-fileidx:
	 *
	 * The file_index within torrent of the current playing stream.
	 **/
	g_object_class_install_property (object_class, PROP_CURRENT_FILEIDX,
					 g_param_spec_int64 ("current-fileidx", "Current file_index", "The file index of the current stream.",
							      G_MININT64, G_MAXINT64, 0, 
								  G_PARAM_READABLE));

	/**
	 * TotemObject:current-content-type:
	 *
	 * The content-type of the current stream.
	 **/
	g_object_class_install_property (object_class, PROP_CURRENT_CONTENT_TYPE,
					 g_param_spec_string ("current-content-type",
							      "Current stream's content-type",
							      "Current stream's content-type.",
							      NULL, G_PARAM_READABLE));



	/**
	 * TotemObject:current-display-name:
	 *
	 * The display name of the current stream.
	 **/
	g_object_class_install_property (object_class, PROP_CURRENT_DISPLAY_NAME,
					 g_param_spec_string ("current-display-name",
							      "Current stream's display name",
							      "Current stream's display name.",
							      NULL, G_PARAM_READABLE));





	/**
	 * TotemObject::fileidx-opened:
	 * @totem: the #TotemObject which received the signal
	 * @fileidx: the file_index of the opened stream within torrent
	 *
	 * The #TotemObject::fileidx-opened signal is emitted when a new stream within torrent is opened by Totem.
	 */
	totem_table_signals[FILEIDX_OPENED] =
		g_signal_new ("fileidx-opened",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				0, NULL, NULL,
				g_cclosure_marshal_VOID__STRING,
				G_TYPE_NONE, 1, G_TYPE_STRING);




	/**
	 * TotemObject::file-has-played:
	 * @totem: the #TotemObject which received the signal
	 * @fileidx: the file_index of the opened stream within torrent
	 *
	 * The #TotemObject::file-has-played signal is emitted when a new stream wihtin torrent has started playing in Totem.
	 */
	totem_table_signals[FILE_HAS_PLAYED] =
		g_signal_new ("file-has-played",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				0, NULL, NULL,
				g_cclosure_marshal_VOID__INT,
				G_TYPE_NONE, 1, G_TYPE_INT);




	/**
	 * TotemObject::fileidx-closed:
	 * @totem: the #TotemObject which received the signal
	 *
	 * The #TotemObject::fileidx-closed signal is emitted when Totem closes a stream.
	 */
	totem_table_signals[FILEIDX_CLOSED] =
		g_signal_new ("fileidx-closed",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				0, NULL, NULL,
				g_cclosure_marshal_VOID__VOID,
				G_TYPE_NONE, 0, G_TYPE_NONE);



	/**
	 * TotemObject::metadata-updated:
	 * @totem: the #TotemObject which received the signal
	 * @artist: the name of the artist, or %NULL
	 * @title: the stream title, or %NULL
	 * @album: the name of the stream's album, or %NULL
	 * @track_number: the stream's track number
	 *
	 * The #TotemObject::metadata-updated signal is emitted when the metadata of a stream is updated, typically
	 * when it's being loaded.
	 */
	totem_table_signals[METADATA_UPDATED] =
		g_signal_new ("metadata-updated",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				0, NULL, NULL,
	                        g_cclosure_marshal_generic,
				G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);


}







static void
totem_object_init (TotemObject *totem)
{
								printf("(totem-object/totem_object_init) \n");


	totem->streaming_file_idx = TOTEM_NULL_STREAMING_FILE_IDX;

	totem->settings = g_settings_new (TOTEM_GSETTINGS_SCHEMA);

	// g_application_add_main_option_entries (G_APPLICATION (totem), all_options);
	g_application_add_option_group (G_APPLICATION (totem), bacon_video_widget_get_option_group ());
	
						// printf("totem->busy_popup_ht INIT \n");

	totem->busy_popup_ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	totem_app_actions_setup (totem);
}

static void
totem_object_finalize (GObject *object)
{

			printf("~~~~totem_object_finalize \n");
	TotemObject *totem = TOTEM_OBJECT (object);

	g_clear_object (&totem->playlist_signals);
	g_clear_pointer (&totem->busy_popup_ht, g_hash_table_destroy);
	g_clear_pointer (&totem->player_title, g_free);

	G_OBJECT_CLASS (totem_object_parent_class)->finalize (object);
}

static void
totem_object_get_property (GObject *object,
			   guint property_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	TotemObject *totem;

	totem = TOTEM_OBJECT (object);

	switch (property_id)
	{
	case PROP_FULLSCREEN:
		g_value_set_boolean (value, totem_object_is_fullscreen (totem));
		break;
	case PROP_PLAYING:
		g_value_set_boolean (value, totem_object_is_playing (totem));
		break;
	case PROP_STREAM_LENGTH:
		g_value_set_int64 (value, bacon_video_widget_get_stream_length (totem->bvw));
		break;
	// case PROP_CURRENT_TIME:
	// 	g_value_set_int64 (value, bacon_video_widget_get_current_time (totem->bvw));
	// 	break;
	case PROP_SEEKABLE:
		g_value_set_boolean (value, totem_object_is_seekable (totem));
		break;
	// case PROP_CURRENT_FILEIDX
	// 	g_value_set_int64 (value, totem_object_is_seekable (totem));
	// 	break;
	// case PROP_CURRENT_DISPLAY_NAME:
	// 	g_value_take_string (value, totem_playlist_get_current_title (totem->playlist));
	// 	break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

/**
 * totem_object_get_main_window:
 * @totem: a #TotemObject
 *
 * Gets Totem's main window and increments its reference count.
 *
 * Return value: (transfer full): Totem's main window
 **/
GtkWindow *
totem_object_get_main_window (TotemObject *totem)
{
	g_return_val_if_fail (TOTEM_IS_OBJECT (totem), NULL);

	g_object_ref (G_OBJECT (totem->win));

	return GTK_WINDOW (totem->win);
}



//can be used For Properties Menu
/**
 * totem_object_get_menu_section:
 * @totem: a #TotemObject
 * @id: the ID for the menu section to look up
 *
 * Get the #GMenu of the given @id from the main Totem #GtkBuilder file.
 *
 * Return value: (transfer none) (nullable): a #GMenu or %NULL on failure
 **/
GMenu *
totem_object_get_menu_section (TotemObject *totem,
			       const char  *id)
{
	GObject *object;
	g_return_val_if_fail (TOTEM_IS_OBJECT (totem), NULL);

	object = gtk_builder_get_object (totem->xml, id);
	if (object == NULL || !G_IS_MENU (object))
		return NULL;

	return G_MENU (object);
}

/**
 * totem_object_empty_menu_section:
 * @totem: a #TotemObject
 * @id: the ID for the menu section to empty
 *
 * Empty the GMenu section pointed to by @id, and remove any
 * related actions. Note that menu items with specific target
 * will not have the associated action removed.
 **/
void
totem_object_empty_menu_section (TotemObject *totem,
				 const char  *id)
{
	GMenu *menu;

	g_return_if_fail (TOTEM_IS_OBJECT (totem));

	menu = G_MENU (gtk_builder_get_object (totem->xml, id));
	g_return_if_fail (menu != NULL);

	while (g_menu_model_get_n_items (G_MENU_MODEL (menu)) > 0) {
		const char *action;
		g_menu_model_get_item_attribute (G_MENU_MODEL (menu), 0, G_MENU_ATTRIBUTE_ACTION, "s", &action);
		if (g_str_has_prefix (action, "app.")) {
			GVariant *target;

			target = g_menu_model_get_item_attribute_value (G_MENU_MODEL (menu), 0, G_MENU_ATTRIBUTE_TARGET, NULL);

			/* Don't remove actions that have a specific target */
			if (target == NULL)
				g_action_map_remove_action (G_ACTION_MAP (totem), action + strlen ("app."));
			else
				g_variant_unref (target);
		}
		g_menu_remove (G_MENU (menu), 0);
	}
}




//actually used in totem-movie-properties 
/**
 * totem_object_get_video_widget:
 * @totem: a #TotemObject
 *
 * Gets Totem's video widget and increments its reference count.
 *
 * Return value: (transfer full): Totem's video widget
 **/
GtkWidget *
totem_object_get_video_widget (TotemObject *totem)
{
	g_return_val_if_fail (TOTEM_IS_OBJECT (totem), NULL);

	g_object_ref (G_OBJECT (totem->bvw));

	return GTK_WIDGET (totem->bvw);
}




/**
 * totem_object_get_current_time:
 * @totem: a #TotemObject
 *
 * Gets the current position's time in the stream as a gint64.
 *
 * Return value: the current position in the stream
 **/
gint64
totem_object_get_current_time (TotemObject *totem)
{
	g_return_val_if_fail (TOTEM_IS_OBJECT (totem), 0);

	return bacon_video_widget_get_current_time (totem->bvw);
}






static gboolean
totem_object_set_current_fileidx_and_play (TotemObject *totem)
{
	gint file_index_cur = -1;

	file_index_cur = totem_playlist_get_current_fileidx (totem->playlist);

					printf ("(totem_object_set_current_fileidx_and_play) get current fileidx %d from totem-playlist \n", file_index_cur);

	if (file_index_cur != -1)
	{
		totem_object_set_fileidx_and_play (totem, file_index_cur);
	}

	return file_index_cur != -1;
}









//NO NEED TO IT
//save the torrent's streaming playing state. should localte at torrent-info-hash/player_state.xspf.xspf
//we can store the currently playing file_index or stuffs  such as seeking to last time when open 
// static gboolean
// save_session_timeout_cb (Totem *totem)
// {
// 	totem_session_save (totem);
// 	return TRUE;
// }

// static void
// setup_save_timeout_cb (Totem    *totem,
// 		       gboolean  enable)
// {
// 	if (enable && totem->save_timeout_id == 0) 
// 	{
// 		totem->save_timeout_id = g_timeout_add_seconds (TOTEM_SESSION_SAVE_TIMEOUT,
// 								(GSourceFunc) save_session_timeout_cb,
// 								totem);
// 		g_source_set_name_by_id (totem->save_timeout_id, "[totem] save_session_timeout_cb");
// 	} 
// 	else if (totem->save_timeout_id > 0) 
// 	{
// 		g_source_remove (totem->save_timeout_id);
// 		totem->save_timeout_id = 0;
// 	}
// }









/**
 * totem_object_get_current_fileidx:
 * @totem: a #TotemObject
 *
 * Get the fileidx of the current stream, or `-1` if nothing's playing.
 *
 *
 * Return value: an integet containing the fileidx of the current stream
 **/
gint
totem_object_get_current_fileidx (TotemObject *totem)
{
	return totem_playlist_get_current_fileidx (totem->playlist);
}

/**
 * totem_object_get_current_full_path:
 * @totem: a #TotemObject
 *
 * Get the URI of the current stream, or %NULL if nothing's playing.
 * Free with g_free().
 *
 * Return value: a newly-allocated string containing the URI of the current stream
 **/
char *
totem_object_get_current_full_path (TotemObject *totem)
{
	return totem_playlist_get_current_full_path (totem->playlist);
}


/**
 * totem_object_get_playlist_length:
 * @totem: a #TotemObject
 *
 * Returns the length of the current playlist.
 *
 * Return value: the playlist length
 **/
guint
totem_object_get_playlist_length (TotemObject *totem)
{
	int last;

	last = totem_playlist_get_last (totem->playlist);
	if (last == -1)
		return 0;
	return last + 1;
}

/**
 * totem_object_get_playlist_pos:
 * @totem: a #TotemObject
 *
 * Returns the <code class="literal">0</code>-based index of the current entry in the playlist. If
 * there is no current entry in the playlist, <code class="literal">-1</code> is returned.
 *
 * Return value: the index of the current playlist entry, or <code class="literal">-1</code>
 **/
int
totem_object_get_playlist_pos (TotemObject *totem)
{
	return totem_playlist_get_current (totem->playlist);
}

/**
 * totem_object_get_title_at_playlist_pos:
 * @totem: a #TotemObject
 * @playlist_index: the <code class="literal">0</code>-based entry index
 *
 * Gets the title of the playlist entry at @index.
 *
 * Return value: the entry title at @index, or %NULL; free with g_free()
 **/
char *
totem_object_get_title_at_playlist_pos (TotemObject *totem, guint playlist_index)
{
	return totem_playlist_get_title (totem->playlist, playlist_index);
}

/**
 * totem_object_get_short_title:
 * @totem: a #TotemObject
 *
 * Gets the title of the current entry in the playlist.
 *
 * Return value: the current entry's title, or %NULL; free with g_free()
 **/
char *
totem_object_get_short_title (TotemObject *totem)
{
	return totem_playlist_get_current_title (totem->playlist);
}










//FORWARD DECLARATION
static void set_controls_visibility (TotemObject      *totem,
				     gboolean          visible,
				     gboolean          animate);

static void
unschedule_hiding_popup (TotemObject *totem)
{
	if (totem->transition_timeout_id > 0)
		g_source_remove (totem->transition_timeout_id);
	totem->transition_timeout_id = 0;
}


//when no mouse motion for a few seconds , the button toolbar will hide, when mouse reach within the player area, it will pop up again
static gboolean
hide_popup_timeout_cb (TotemObject *totem)
{
	set_controls_visibility (totem, FALSE, TRUE);
	unschedule_hiding_popup (totem);
	return G_SOURCE_REMOVE;
}

static void
schedule_hiding_popup (TotemObject *totem)
{
	unschedule_hiding_popup (totem);
	totem->transition_timeout_id = g_timeout_add_seconds (POPUP_HIDING_TIMEOUT, (GSourceFunc) hide_popup_timeout_cb, totem);
	g_source_set_name_by_id (totem->transition_timeout_id, "[totem] schedule_hiding_popup");
}

static void
show_popup (TotemObject *totem)
{
	set_controls_visibility (totem, TRUE, FALSE);
	schedule_hiding_popup (totem);
}



static void
emit_file_opened (TotemObject *totem
				,const char *fpath)
{
	// NO NEED
	/*
	* THIS is used for save playlist, playing state, all for resume, 
	* so next time you open totem, it will resume playing the video you left last time
	**/
	// totem_session_save (totem);
	// setup_save_timeout_cb (totem, TRUE);

	//actually to tell totem-movie-properties Dialog fileidx has been switched, show its `movie proeprties`
	//also to tell totem-open-directory plugin the full path of the video being played
	g_signal_emit (G_OBJECT (totem),
		       totem_table_signals[FILEIDX_OPENED],
		       0);
}

/*
 * THIS is used for save playlist, playing state, all for resume, 
 * so next time you open totem, it will resume playing the video you left last time
 **/
static void
emit_file_closed (TotemObject *totem)
{
	//NO NEED
	// setup_save_timeout_cb (totem, FALSE);
	// totem_session_save (totem);
	g_signal_emit (G_OBJECT (totem),
		       totem_table_signals[FILEIDX_CLOSED],
		       0);
}



/**
 * totem_file_has_played:
 * @totem: a #TotemObject
 *
 * Emits the #TotemObject::file-played signal on @totem.
 **/
static void
totem_file_has_played (TotemObject *totem, gint fileidx)
{
	g_signal_emit (G_OBJECT (totem),
		       totem_table_signals[FILE_HAS_PLAYED],
		       0, fileidx);
}

/*
 * emit_metadata_updated:
 * @totem: a #TotemObject
 * @artist: the stream's artist, or %NULL
 * @title: the stream's title, or %NULL
 * @album: the stream's album, or %NULL
 * @track_num: the track number of the stream
 *
 * Emits the #TotemObject::metadata-updated signal on @totem,
 * with the specified stream data.
 * First get metadata can also be count as metada update
 **/
static void
emit_metadata_updated (TotemObject *totem,
			const char *artist,
			const char *title,
			const char *album,
			guint track_num)
{
	g_signal_emit (G_OBJECT (totem),
		       totem_table_signals[METADATA_UPDATED],
		       0,
		       artist,
		       title,
		       album,
		       track_num);
}




static void
reset_seek_status (TotemObject *totem)
{
	/* Release the lock and reset everything so that we
	 * avoid being "stuck" seeking on errors */

	if (totem->seek_lock != FALSE) 
	{
		totem->seek_lock = FALSE;
		unmark_popup_busy (totem, "seek started");
		bacon_video_widget_seek (totem->bvw, 0, NULL);
		bacon_video_widget_stop (totem->bvw);
		play_pause_set_label (totem, STATE_STOPPED);
	}
}

/**
 * totem_object_show_error:
 * @totem: a #TotemObject
 * @title: the error dialog title
 * @reason: the error dialog text
 *
 * Displays a non-blocking error dialog with the
 * given @title and @reason.
 **/
void
totem_object_show_error (TotemObject *totem, const char *title, const char *reason)
{
	reset_seek_status (totem);
	totem_interface_error (title, reason,
			GTK_WINDOW (totem->win));
}

static void
totem_object_save_size (TotemObject *totem)
{
	if (totem->bvw == NULL)
		return;

	if (totem_object_is_fullscreen (totem) != FALSE)
		return;

	/* Save the size of the video widget */
	gtk_window_get_size (GTK_WINDOW (totem->win), &totem->window_w, &totem->window_h);
}




//save totem player  window width/height, is maximised.. serialized in state.ini file
static void
totem_object_save_state (TotemObject *totem)
{
	GKeyFile *keyfile;
	g_autofree char *contents = NULL;
	g_autofree char *filename = NULL;

	if (totem->win == NULL)
		return;
	if (totem->window_w == 0
	    || totem->window_h == 0)
		return;

	keyfile = g_key_file_new ();
	g_key_file_set_integer (keyfile, "State",
				"window_w", totem->window_w);
	g_key_file_set_integer (keyfile, "State",
			"window_h", totem->window_h);
	g_key_file_set_boolean (keyfile, "State",
			"maximised", totem->maximised);

	contents = g_key_file_to_data (keyfile, NULL, NULL);
	g_key_file_free (keyfile);
	filename = g_build_filename (totem_dot_dir (), "state.ini", NULL);
	g_file_set_contents (filename, contents, -1, NULL);
}





/**
 * totem_object_exit:
 * @totem: a #TotemObject
 *
 * Closes Totem.
 **/
void
totem_object_exit (TotemObject *totem)
{
						printf("~(totem-object/totem_object_exit) \n");

	GdkDisplay *display = NULL;

	/* Shut down the plugins first, allowing them to display modal dialogues (etc.) without threat of being killed from another thread */
	if (totem != NULL && totem->engine != NULL)
	{
		totem_object_plugins_shutdown (totem);
	}

	if (gtk_main_level () > 0)
		gtk_main_quit ();

	if (totem == NULL)
		exit (0);

	if (totem->bvw)
		totem_object_save_size (totem);

	if (totem->win != NULL) {
		gtk_widget_hide (totem->win);
		display = gtk_widget_get_display (totem->win);
	}

	if (totem->prefs != NULL)
		gtk_widget_hide (totem->prefs);

	if (display != NULL)
		gdk_display_sync (display);

	// setup_save_timeout_cb (totem, FALSE);
	// totem_session_cleanup (totem);

	totem_object_save_state (totem);

	g_clear_object (&totem->settings);

	if (totem->win)
	{
		gtk_widget_destroy (GTK_WIDGET (totem->win));
	}


	g_object_unref (totem);

	exit (0);
}


G_GNUC_NORETURN gboolean
main_window_destroy_cb (GtkWidget *widget, GdkEvent *event, TotemObject *totem)
{

				printf("(totme-object/main_window_destroy_cb) \n");

	totem_object_exit (totem);
}

//when click Play/Pause, update icon
static void
play_pause_set_label (TotemObject *totem, TotemStates state)
{

				// printf("(totem-object/play_pause_set_label) \n");

	GtkWidget *image;
	const char *id, *tip;

	if (state == totem->state)
		return;

	switch (state)
	{
		case STATE_PLAYING:
			id = "media-playback-pause-symbolic";
			tip = N_("Pause");
			bacon_time_label_set_show_msecs (totem->time_label, FALSE);
			totem_playlist_set_playing (totem->playlist, TOTEM_PLAYLIST_STATUS_PLAYING);
			break;
		case STATE_PAUSED:
			id = "media-playback-start-symbolic";
			tip = N_("Play");
			totem_playlist_set_playing (totem->playlist, TOTEM_PLAYLIST_STATUS_PAUSED);
			break;
		case STATE_STOPPED:
			bacon_time_label_reset (totem->time_label);
			bacon_time_label_reset (totem->time_rem_label);
			id = "media-playback-start-symbolic";
			totem_playlist_set_playing (totem->playlist, TOTEM_PLAYLIST_STATUS_NONE);
			tip = N_("Play");
			break;
		default:
			g_assert_not_reached ();
			return;
	}

	gtk_widget_set_tooltip_text (totem->play_button, _(tip));
	image = gtk_button_get_image (GTK_BUTTON (totem->play_button));
	gtk_image_set_from_icon_name (GTK_IMAGE (image), id, GTK_ICON_SIZE_MENU);

	totem->state = state;

	g_object_notify (G_OBJECT (totem), "playing");
}







/**
 * totem_object_play:
 * @totem: a #TotemObject
 *
 * Plays the current file_index of torrent video . If Totem is already playing, it continues
 * to play. If the stream cannot be played, and error dialog is displayed.
 **/
void
totem_object_play (TotemObject *totem)
{			
	g_autoptr(GError) err = NULL;
	int retval;
	g_autofree char *msg = NULL;
	g_autofree char *disp = NULL;

	if (totem->streaming_file_idx == -1)
	{
		return;
	}

					printf("(totem_object_play) \n");

	if (bacon_video_widget_is_playing (totem->bvw) != FALSE)
	{
		return;
	}

	retval = bacon_video_widget_play (totem->bvw,  &err);

	//update Play/Pause icon
	play_pause_set_label (totem, retval ? STATE_PLAYING : STATE_STOPPED);

	if (retval != FALSE) 
	{
		unmark_popup_busy (totem, "paused");
		if (totem->has_played_emitted == FALSE) 
		{
			totem_file_has_played (totem, totem->streaming_file_idx);//emit FILE_HAS_PLAYED, no need 
			totem->has_played_emitted = TRUE;
		}
		return;
	}

	msg = g_strdup_printf(_("Videos could not play."));

	totem_object_show_error (totem, msg, err->message);
	bacon_video_widget_stop (totem->bvw);
	play_pause_set_label (totem, STATE_STOPPED);

	g_free (msg);
}




static void
totem_object_seek (TotemObject *totem, double pos)
{

					printf("(totem_object_seek) %lf \n", pos);

	g_autoptr(GError) err = NULL;
	int retval;

	// if (totem->mrl == NULL)
	// 	return;

	if (bacon_video_widget_is_seekable (totem->bvw) == FALSE)
		return;

	retval = bacon_video_widget_seek (totem->bvw, pos, &err);

	if (retval == FALSE)
	{
		g_autofree char *msg = NULL;
		g_autofree char *disp = NULL;

		// disp = totem_uri_escape_for_display (totem->mrl);
		// msg = g_strdup_printf(_("Videos could not play “%s”."), disp);

					printf("(totem_object_seek)Videos could not play  \n");

		reset_seek_status (totem);

		// totem_object_show_error (totem, msg, err->message);
	}
}



static void
totem_object_set_fileidx_and_play (TotemObject *totem, gint file_index)
{
							printf("totem_object_set_fileidx_and_play, file_index=%d \n", file_index);

	totem_object_set_fileidx (totem, file_index);
	totem_object_play (totem);
}





/**
 * totem_object_play_pause:
 * @totem: a #TotemObject
 *
 * Gets the current fileidx from the playlist and attempts to play it.
 * If the stream is already playing, playback is paused.
 **/
void
totem_object_play_pause (TotemObject *totem)
{
				printf("(totem_object_play_pause) \n");

	if (totem->streaming_file_idx == -1) 
	{
		/* Try to pull an file_idx from the playlist */
		if (!totem_object_set_current_fileidx_and_play (totem))
			play_pause_set_label (totem, STATE_STOPPED);
		return;
	}

	if (bacon_video_widget_is_playing (totem->bvw) == FALSE) 
	{
		if (bacon_video_widget_play (totem->bvw, NULL) != FALSE &&
		    totem->has_played_emitted == FALSE) 
		{
			totem_file_has_played (totem , totem->streaming_file_idx);
			totem->has_played_emitted = TRUE;
		}
		play_pause_set_label (totem, STATE_PLAYING);
	} 
	else 
	{
		bacon_video_widget_pause (totem->bvw);
		play_pause_set_label (totem, STATE_PAUSED);
	}
}


/**
 * totem_object_stop:
 * @totem: a #TotemObject
 *
 * Not same as Pause, it terminates playback, and sets the playlist back at the start.
 */
void
totem_object_stop (TotemObject *totem)
{
	gint fileidx = -1;
	
	totem_playlist_set_at_start (totem->playlist);
	update_buttons (totem);
	bacon_video_widget_stop (totem->bvw);
	mark_popup_busy (totem, "paused");
	play_pause_set_label (totem, STATE_STOPPED);
	fileidx = totem_playlist_get_current_fileidx (totem->playlist);
	if (fileidx != -1)
	{

						printf("totem_object_stop \n");

		totem_object_set_fileidx (totem, fileidx);
		bacon_video_widget_pause (totem->bvw);
	}
}

/**
 * totem_object_pause:
 * @totem: a #TotemObject
 *
 * Pauses the current stream. If Totem is already paused, it continues
 * to be paused.
 **/
void
totem_object_pause (TotemObject *totem)
{

									printf("(totem_object_pause)\n");

	if (bacon_video_widget_is_playing (totem->bvw) != FALSE) {
		bacon_video_widget_pause (totem->bvw);
		mark_popup_busy (totem, "paused");
		play_pause_set_label (totem, STATE_PAUSED);
	}
}


//FullScreen Switch
gboolean
window_state_event_cb (GtkWidget           *window,
		       GdkEventWindowState *event,
		       TotemObject         *totem)
{
	GAction *action;
	gboolean is_fullscreen;

	totem->maximised = !!(event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED);

	if ((event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) == 0)
		return FALSE;

	if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) {
		if (totem->controls_visibility != TOTEM_CONTROLS_UNDEFINED)
			totem_object_save_size (totem);

		totem->controls_visibility = TOTEM_CONTROLS_FULLSCREEN;
	} else {
		totem->controls_visibility = TOTEM_CONTROLS_VISIBLE;
		totem_object_save_size (totem);
	}

	is_fullscreen = (totem->controls_visibility == TOTEM_CONTROLS_FULLSCREEN);
	hdy_flap_set_fold_policy (HDY_FLAP (totem->flap), is_fullscreen ?
				  HDY_FLAP_FOLD_POLICY_ALWAYS : HDY_FLAP_FOLD_POLICY_NEVER);
	
	totem_player_toolbar_set_fullscreen_mode (TOTEM_PLAYER_TOOLBAR (totem->player_header), is_fullscreen);

	action = g_action_map_lookup_action (G_ACTION_MAP (totem), "fullscreen");
	g_simple_action_set_state (G_SIMPLE_ACTION (action),
				   g_variant_new_boolean (is_fullscreen));

	if (totem->transition_timeout_id > 0)
	{
		set_controls_visibility (totem, TRUE, FALSE);
	}

	g_object_notify (G_OBJECT (totem), "fullscreen");

	return FALSE;
}



static void
totem_object_action_fullscreen_toggle (TotemObject *totem)
{
	if (totem_object_is_fullscreen (totem) != FALSE)
		gtk_window_unfullscreen (GTK_WINDOW (totem->win));
	else
		gtk_window_fullscreen (GTK_WINDOW (totem->win));
}

/**
 * totem_object_set_fullscreen:
 * @totem: a #TotemObject
 * @state: %TRUE if Totem should be fullscreened
 *
 * Sets Totem's fullscreen state according to @state.
 **/
void
totem_object_set_fullscreen (TotemObject *totem, gboolean state)
{
	if (totem_object_is_fullscreen (totem) == state)
		return;

	if (state)
		gtk_window_fullscreen (GTK_WINDOW (totem->win));
	else
		gtk_window_unfullscreen (GTK_WINDOW (totem->win));
}




static void
totem_got_ppi_info (BaconVideoWidget *bvw, DownloadingBlocksSd* sd, TotemObject *totem)
{

			printf ("(totem_got_ppi_info) \n");

	bitfield_scale_update_downloading_blocks (totem->seek, sd);

	g_free (sd);
}



//gathered from each piece_finished_alerts just during checking torrent
static void
totem_got_finished_piece_info (BaconVideoWidget* bvw ,guint8 *byte_array, TotemObject * totem)
{
	bitfield_scale_set_whole_piece_finished (totem->seek, byte_array);

	// Remove the data (association) from the GObject to prevent double-free
	bvw_g_object_association_clear (bvw, "piece-finished");

	//// g_free (byte_array);
}


static void
totem_got_piece_block_info (BaconVideoWidget* bvw ,PieceBlockInfoSd *sd, TotemObject * totem)
{

				printf("(totem_got_piece_block_info) entering \n");

    if (!sd) 
	{
        g_warning("Received NULL shared data.");
        return;
    }

	guint total_num_blocks = sd->info.total_num_blocks;
	guint total_num_pieces = sd->info.total_num_pieces;
	guint blocks_per_piece_normal = sd->info.blocks_per_piece_normal;
	guint num_blocks_last_piece = sd->info.num_blocks_last_piece;
	
    bitfield_scale_set_piece_block_info(totem->seek, total_num_blocks, total_num_pieces,
                                        blocks_per_piece_normal, num_blocks_last_piece,
                                        sd->info.finished_pieces, 
                                        sd->info.unfinished_pieces);

	// Remove the data (association) from the GObject to prevent double-free
	bvw_g_object_association_clear (bvw, "piece-block-info");

	//// g_free(sd);
}






/*for free the GList of GHashTable*/
static void 
custom_hash_table_free (gpointer key, gpointer value, gpointer user_data) 
{

							// printf ("(custom_hash_table_free) %s\n", (const gchar*)key);

    // For "idx" key, we don't free the value since it's a GINT_TO_POINTER'd int
    if (g_strcmp0 ((const gchar*)key, "idx") != 0) 
	{
        // Free strings (like "fname" and "fpath")
        g_free (value);
    }
    // No need to free the key because g_hash_table_new_full manages the keys
}
// Function to free each GHashTable in the GList
static void 
free_hash_table_in_list (gpointer data) 
{
    GHashTable *pair = (GHashTable *)data;

    // Free the GHashTable and its contents
    g_hash_table_foreach (pair, custom_hash_table_free, NULL);
    g_hash_table_destroy (pair); // Destroys the GHashTable itself
}

static void 
free_res_list (GList *res_list) 
{
    g_list_foreach (res_list, (GFunc)free_hash_table_in_list, NULL);
    g_list_free (res_list);
}



static void
totem_got_torrent_videos_info (BaconVideoWidget* bvw ,TotemObject * totem)
{

			// printf("(totem_got_torrent_videos_info) entering\n");

	GList *videos_info = NULL;

	bacon_video_widget_retrieve_btdemux_video_info (bvw, &videos_info);
	
	if (videos_info)
	{
		// Iterate over the GList containing GHashTables
		for (GList *l = videos_info; l != NULL; l = l->next) 
		{
			// Each element in the list is a GHashTable
			GHashTable *pair = (GHashTable *)l->data;
        
			if (pair) 
			{
				// Retrieve the "key" and "value" from the GHashTable, tranfser none
				gint key = GPOINTER_TO_INT (g_hash_table_lookup (pair, "idx"));
				const gchar *fname = g_hash_table_lookup (pair, "fname");
				const gchar *fpath = g_hash_table_lookup (pair, "fpath");

              							printf("(totem_got_torrent_videos_info) Loop item, Fidx: %d, FileName: %s, FullPath: %s\n", key, fname, fpath);

				//add one row in playlist
				totem_playlist_add_one_row (totem->playlist, key, fname, fpath);				
			}
		}


		int len = totem_playlist_get_last (totem->playlist) + 1;
		printf ("(totem_got_torrent_videos_info) probe: len of playlist is %d\n", len);

 totem_playlist_set_next (totem->playlist);

		//********then we can set fileidx from playlist and play it
		//if has two videos, get the second; if single video, play it
		totem_object_set_current_fileidx_and_play (totem);

		goto cleanup;

	}
	else
	{
		goto beach;
	}

cleanup:

		//responsible for free it
		// g_list_free_full (videos_info, (GDestroyNotify) g_hash_table_destroy);
	printf ("(totem_got_torrent_videos_info) gotta free the GList* \n");
		free_res_list ( videos_info );

beach:
		printf("(totem_got_torrent_videos_info) EMPTY \n");

}








static char *
totem_get_nice_name_for_stream (TotemObject *totem)
{
	GValue title_value = { 0, };
	GValue album_value = { 0, };
	GValue artist_value = { 0, };
	GValue value = { 0, };
	char *retval;
	int tracknum;



	// Free the #GValue with g_value_unset().
	bacon_video_widget_get_metadata (totem->bvw, BVW_INFO_TITLE, &title_value);
	bacon_video_widget_get_metadata (totem->bvw, BVW_INFO_ARTIST, &artist_value);
	bacon_video_widget_get_metadata (totem->bvw, BVW_INFO_ALBUM, &album_value);
	bacon_video_widget_get_metadata (totem->bvw,
					 BVW_INFO_TRACK_NUMBER,
					 &value);


	tracknum = g_value_get_int (&value);			
	g_value_unset (&value);

	emit_metadata_updated (totem,
	                       g_value_get_string (&artist_value),
	                       g_value_get_string (&title_value),
	                       g_value_get_string (&album_value),
	                       tracknum);

	if (g_value_get_string (&title_value) == NULL) 
	{
		retval = NULL;
		goto bail;
	}
	if (g_value_get_string (&artist_value) == NULL) 
	{
		retval = g_value_dup_string (&title_value);
		goto bail;
	}

	if (tracknum != 0) 
	{
		retval = g_strdup_printf ("%02d. %s - %s",
					  tracknum,
					  g_value_get_string (&artist_value),
					  g_value_get_string (&title_value));
	} 
	else 
	{
		retval = g_strdup_printf ("%s - %s",
					  g_value_get_string (&artist_value),
					  g_value_get_string (&title_value));
	}

	bail:
		g_value_unset (&album_value);
		g_value_unset (&artist_value);
		g_value_unset (&title_value);

		return retval;
}






static void
update_player_header_title (TotemObject *totem, const char *name)
{
	if (name != NULL) 
	{
		/* Update the player header title label */
		g_clear_pointer (&totem->player_title, g_free);
		totem->player_title = g_strdup (name);
	} 
	else 
	{
		bacon_time_label_reset (totem->time_label);
		bacon_time_label_reset (totem->time_rem_label);

		g_object_notify (G_OBJECT (totem), "stream-length");

		/* Update the player title label */
		g_clear_pointer (&totem->player_title, g_free);
	}

	//set totem's player header title
	totem_player_toolbar_set_title (TOTEM_PLAYER_TOOLBAR (totem->player_header), totem->player_title);
}








static void
set_controls_visibility (TotemObject      *totem,
			 gboolean          visible,
			 gboolean          animate)
{
	gtk_widget_set_visible (GTK_WIDGET (gtk_builder_get_object (totem->xml, "toolbar")), visible);
	if (totem->controls_visibility == TOTEM_CONTROLS_FULLSCREEN)
	{
		hdy_flap_set_reveal_flap (HDY_FLAP (totem->flap), visible);
	}
	bacon_video_widget_set_show_cursor (totem->bvw, visible);
	if (visible && animate)
	{
		schedule_hiding_popup (totem);
	}
	totem->reveal_controls = visible;
}




static void
mark_popup_busy (TotemObject      *totem,
		 const char       *reason)
{

				printf("mark_popup_busy %p, %s\n", (void*)totem,reason);
			
	g_hash_table_insert (totem->busy_popup_ht,
			     g_strdup (reason),
			     GINT_TO_POINTER (1));
	g_debug ("Adding popup busy for reason %s", reason);

	set_controls_visibility (totem, TRUE, FALSE);
	unschedule_hiding_popup (totem);
}

static void
unmark_popup_busy (TotemObject      *totem,
		   const char       *reason)
{

				printf("-unmark_popup_busy %s\n", reason);


	g_hash_table_remove (totem->busy_popup_ht, reason);
	g_debug ("Removing popup busy for reason %s", reason);

	if (g_hash_table_size (totem->busy_popup_ht) == 0 &&
	    gtk_widget_get_opacity (GTK_WIDGET (gtk_builder_get_object (totem->xml, "toolbar"))) != 0.0) 
	{
		g_debug ("Will hide popup soon");
		schedule_hiding_popup (totem);
	}
}



/*
	HACK:
	mrl is like url of that video file, but in context of a torrent, if we know file_index of that mp4 file we know which to play
	so libtorrent::file_index functions the same as mrl  

	'Which video file in this torrent to play'

*/
/**
 * totem_object_set_fileidx:
 * @totem: a #TotemObject
 * @fileidx: the file_index within that torrent to streaming
 * 
 * Loads the specified @fileidx 
 *
 * If a stream is already playing, it will be stopped and closed.
 *
 * Errors will be reported asynchronously.
 **/
void
totem_object_set_fileidx (TotemObject *totem, gint file_index)
{

	char *fpath = NULL;
									printf("totem_object_set_fileidx, file_idx=%d \n", file_index);

	//when switch to another item in playlist, we should close the current one
	if (totem->streaming_file_idx != -1) 
	{
									printf("(totem_object_set_fileidx, file_idx=%d) closing the current stream first \n", file_index);

		totem->pause_start = FALSE;
		bacon_video_widget_close (totem->bvw);
		emit_file_closed (totem);
		totem->has_played_emitted = FALSE;
		play_pause_set_label (totem, STATE_STOPPED);
	}

	//Initial case, this is just for starting the pipeline, so wen then can get playlist and choose which video to play
	if (file_index == TOTEM_NULL_STREAMING_FILE_IDX) 
	{
		play_pause_set_label (totem, STATE_PAUSED);
	
		/* Play/Pause */
		action_set_sensitive ("play", FALSE);

		/* Volume */
		gtk_widget_set_sensitive (totem->volume, FALSE);
		totem->volume_sensitive = FALSE;	

		/* Control popup */
		action_set_sensitive ("next-chapter", FALSE);
		action_set_sensitive ("previous-chapter", FALSE);

		/* Set the label */
		update_player_header_title (totem, NULL);

		g_object_notify (G_OBJECT (totem), "playing");

	} 

	//maybe used later, for destory 
	else if (file_index == TOTEM_CLEAR_STREAMING_FILE_IDX)
	{
		play_pause_set_label (totem, STATE_STOPPED);

		/* Play/Pause */
		action_set_sensitive ("play", FALSE);

		/* Volume */
		gtk_widget_set_sensitive (totem->volume, FALSE);
		totem->volume_sensitive = FALSE;

		/* Control popup */
		action_set_sensitive ("next-chapter", FALSE);
		action_set_sensitive ("previous-chapter", FALSE);

		/* Set the label */
		update_player_header_title (totem, NULL);

		g_object_notify (G_OBJECT (totem), "playing");
	}

	else 
	{
		gboolean can_vol_seek;
	
		g_application_mark_busy (G_APPLICATION (totem));
		bacon_video_widget_open (totem->bvw, file_index);
		mark_popup_busy (totem, "opening file");

		g_application_unmark_busy (G_APPLICATION (totem));

		totem->streaming_file_idx = file_index;

		/* Play/Pause */
		action_set_sensitive ("play", TRUE);

		/* Volume */
		can_vol_seek = bacon_video_widget_can_set_volume (totem->bvw);
		gtk_widget_set_sensitive (totem->volume, can_vol_seek);
		totem->volume_sensitive = can_vol_seek;

		/* Set the playlist */
		play_pause_set_label (totem, STATE_PAUSED);

		fpath = totem_object_get_current_full_path (totem);
		emit_file_opened (totem, fpath);
		//dont free `fpath` here, let its user to be responsible for free it after use
		// g_free (fpath);
	}

	g_object_notify (G_OBJECT (totem), "current-fileidx");

	update_buttons (totem);

}





//avoid switch too frequently
static gboolean
totem_time_within_seconds (TotemObject *totem)
{
	gint64 _time;

	_time = bacon_video_widget_get_current_time (totem->bvw);

	return (_time < REWIND_OR_PREVIOUS);
}


static void
totem_object_direction (TotemObject *totem, TotemPlaylistDirection dir)
{
	if (totem_playlist_has_direction (totem->playlist, dir) == FALSE &&
	    totem_playlist_get_repeat (totem->playlist) == FALSE)
	{
		//if playlist repeat-mode is OFF, and this is last movie in playlist, just do nothing
				printf ("(totem_object_direction) repeat-mode is OFF, do nothing \n");
		return;
	}


	if (dir == TOTEM_PLAYLIST_DIRECTION_NEXT ||
	    bacon_video_widget_is_seekable (totem->bvw) == FALSE ||
	    totem_time_within_seconds (totem) != FALSE) 
	{
				printf ("(totem_object_direction) switch to next item in playlist \n");

		// set playlist "cursor" to next for previous item 	
		totem_playlist_set_direction (totem->playlist, dir);
		// perform switch to next movie in playlist
		totem_object_set_current_fileidx_and_play (totem);
	} 
	else
	{
		// don't perform switch to another fileidx in playlist, just seek to the very beginning of the current movie
		totem_object_seek (totem, 0);
	}
}







/**
 * totem_object_can_seek_previous:
 * @totem: a #TotemObject
 *
 * Returns true if totem_object_seek_previous() would have an effect.
 */
gboolean
totem_object_can_seek_previous (TotemObject *totem)
{
	return 
	// bacon_video_widget_has_previous_track (totem->bvw) ||
		totem_playlist_has_previous_item (totem->playlist) ||
		totem_playlist_get_repeat (totem->playlist);
}

/**
 * totem_object_seek_previous:
 * @totem: a #TotemObject
 *
 * If a normal stream
 * is being played, goes to the start of the stream if possible. If seeking is
 * not possible, plays the previous entry in the playlist.
 **/
void
totem_object_seek_previous (TotemObject *totem)
{
	totem_object_direction (totem, TOTEM_PLAYLIST_DIRECTION_PREVIOUS);
}

/**
 * totem_object_can_seek_next:
 * @totem: a #TotemObject
 *
 * Returns true if totem_object_seek_next() would have an effect.
 */
gboolean
totem_object_can_seek_next (TotemObject *totem)
{
	return 
		totem_playlist_has_next_item (totem->playlist) ||
		totem_playlist_get_repeat (totem->playlist);
}



/**
 * totem_object_seek_next:
 * @totem: a #TotemObject
 *
 * plays the next entry in the playlist.
 **/
void
totem_object_seek_next (TotemObject *totem)
{
	totem_object_direction (totem, TOTEM_PLAYLIST_DIRECTION_NEXT);
}





static void
totem_seek_time_rel (TotemObject *totem, gint64 _time, gboolean relative, gboolean accurate)
{
	g_autoptr(GError) err = NULL;
	gint64 sec;

	if (totem->streaming_file_idx == -1)
	{
		return;
	}
	if (bacon_video_widget_is_seekable (totem->bvw) == FALSE)
	{
		return;
	}

	if (relative != FALSE) 
	{
		gint64 oldmsec;
		oldmsec = bacon_video_widget_get_current_time (totem->bvw);
		sec = MAX (0, oldmsec + _time);
	} 
	else 
	{
		sec = _time;
	}

	bacon_video_widget_seek_time (totem->bvw, sec, accurate, &err);

	if (err != NULL)
	{
		g_autofree char *msg = NULL;
		g_autofree char *disp = NULL;

		// disp = totem_uri_escape_for_display (totem->mrl);
		msg = g_strdup_printf(_("Videos could not play."));

		bacon_video_widget_stop (totem->bvw);
		play_pause_set_label (totem, STATE_STOPPED);
		totem_object_show_error (totem, msg, err->message);
	}
}

/**
 * totem_object_seek_relative:
 * @totem: a #TotemObject
 * @offset: the time offset to seek to
 * @accurate: whether to use accurate seek, an accurate seek might be slower for some formats (see GStreamer docs)
 *
 * Seeks to an @offset from the current position in the stream,
 * or displays an error dialog if that's not possible.
 **/
void
totem_object_seek_relative (TotemObject *totem, gint64 offset, gboolean accurate)
{
	totem_seek_time_rel (totem, offset, TRUE, accurate);
}

/**
 * totem_object_seek_time:
 * @totem: a #TotemObject
 * @msec: the time to seek to
 * @accurate: whether to use accurate seek, an accurate seek might be slower for some formats (see GStreamer docs)
 *
 * Seeks to an absolute time in the stream, or displays an
 * error dialog if that's not possible.
 **/
void
totem_object_seek_time (TotemObject *totem, gint64 msec, gboolean accurate)
{
	totem_seek_time_rel (totem, msec, FALSE, accurate);
}

static void
totem_object_set_zoom (TotemObject *totem,
		       gboolean     zoom)
{
	GAction *action;

	action = g_action_map_lookup_action (G_ACTION_MAP (totem), "zoom");
	g_action_change_state (action, g_variant_new_boolean (zoom));
}

/**
 * totem_object_get_volume:
 * @totem: a #TotemObject
 *
 * Gets the current volume level, as a value between <code class="literal">0.0</code> and <code class="literal">1.0</code>.
 *
 * Return value: the volume level
 **/
double
totem_object_get_volume (TotemObject *totem)
{
	return bacon_video_widget_get_volume (totem->bvw);
}

/**
 * totem_object_set_volume:
 * @totem: a #TotemObject
 * @volume: the new absolute volume value
 *
 * Sets the volume, with <code class="literal">1.0</code> being the maximum, and <code class="literal">0.0</code> being the minimum level.
 **/
void
totem_object_set_volume (TotemObject *totem, double volume)
{
	if (bacon_video_widget_can_set_volume (totem->bvw) == FALSE)
		return;

	bacon_video_widget_set_volume (totem->bvw, volume);
}

/**
 * totem_object_get_rate:
 * @totem: a #TotemObject
 *
 * Gets the current playback rate, with `1.0` being the normal playback rate.
 *
 * Return value: the volume level
 **/
float
totem_object_get_rate (TotemObject *totem)
{
	return bacon_video_widget_get_rate (totem->bvw);
}

/**
 * totem_object_set_rate:
 * @totem: a #TotemObject
 * @rate: the new absolute playback rate
 *
 * Sets the playback rate, with `1.0` being the normal playback rate.
 *
 * Return value: %TRUE on success, %FALSE on failure.
 **/
gboolean
totem_object_set_rate (TotemObject *totem, float rate)
{
	return bacon_video_widget_set_rate (totem->bvw, rate);
}

/**
 * totem_object_volume_toggle_mute:
 * @totem: a #TotemObject
 *
 * Toggles the mute status.
 **/
static void
totem_object_volume_toggle_mute (TotemObject *totem)
{

						printf("totem_object_volume_toggle_mute \n");

	if (totem->muted == FALSE) {
		totem->muted = TRUE;
		totem->prev_volume = bacon_video_widget_get_volume (totem->bvw);
		bacon_video_widget_set_volume (totem->bvw, 0.0);
	} else {
		totem->muted = FALSE;
		bacon_video_widget_set_volume (totem->bvw, totem->prev_volume);
	}
}

/**
 * totem_object_set_volume_relative:
 * @totem: a #TotemObject
 * @off_pct: the value by which to increase or decrease the volume
 *
 * Sets the volume relative to its current level, with <code class="literal">1.0</code> being the
 * maximum, and <code class="literal">0.0</code> being the minimum level.
 **/
static void
totem_object_set_volume_relative (TotemObject *totem, double off_pct)
{
						printf("totem_object_set_volume_relative: %lf\n", off_pct);

	double vol;

	if (bacon_video_widget_can_set_volume (totem->bvw) == FALSE)
		return;
	if (totem->muted != FALSE)
		totem_object_volume_toggle_mute (totem);

	vol = bacon_video_widget_get_volume (totem->bvw);
	bacon_video_widget_set_volume (totem->bvw, vol + off_pct);
}

static void
totem_object_toggle_aspect_ratio (TotemObject *totem)
{
	GAction *action;
	int tmp;

	tmp = bacon_video_widget_get_aspect_ratio (totem->bvw);
	tmp++;
	if (tmp > BVW_RATIO_DVB)
		tmp = BVW_RATIO_AUTO;

	action = g_action_map_lookup_action (G_ACTION_MAP (totem), "aspect-ratio");
	g_action_change_state (action, g_variant_new ("i", tmp));
}

void
totem_object_show_help (TotemObject *totem)
{
	g_autoptr(GError) error = NULL;

	if (gtk_show_uri_on_window (GTK_WINDOW (totem->win), "help:totem", gtk_get_current_event_time (), &error) == FALSE)
		totem_object_show_error (totem, _("Videos could not display the help contents."), error->message);
}

void
totem_object_show_keyboard_shortcuts (TotemObject *totem)
{
	g_autoptr(GtkBuilder) builder = NULL;

	if (totem->shortcuts_win) {
		gtk_window_present (totem->shortcuts_win);
		return;
	}

	builder = gtk_builder_new_from_resource ("/org/gnome/totem/ui/shortcuts.ui");
	totem->shortcuts_win = GTK_WINDOW (gtk_builder_get_object (builder, "shortcuts-totem"));
	gtk_window_set_transient_for (totem->shortcuts_win, GTK_WINDOW (totem->win));

	g_signal_connect (totem->shortcuts_win, "destroy",
			  G_CALLBACK (gtk_widget_destroyed), &totem->shortcuts_win);

	gtk_widget_show (GTK_WIDGET (totem->shortcuts_win));
}




////actually update title
//// void
//// on_channels_change_event (BaconVideoWidget *bvw, TotemObject *totem)
//// {
//// 	g_autofree char *name = NULL;

//// 	/* updated stream info (new song) */
//// 	name = totem_get_nice_name_for_stream (totem);

//// 	if (name != NULL) 
//// 	{
//// 		update_player_header_title (totem, name);
//// 		totem_playlist_set_title
//// 			(TOTEM_PLAYLIST (totem->playlist), name);
//// 	}
//// }





//first get (from none to something) is also count as change
static void
on_playlist_change_name (TotemPlaylist *playlist, TotemObject *totem)
{
	g_autofree char *name = NULL;

	name = totem_playlist_get_current_title (playlist);
	if (name != NULL)
	{

					printf ("(totem-object/on_playlist_change_name)  got title to update player header %s\n", name);

		update_player_header_title (totem, name);
	}
}




/*what means got-metadata
it means decodebin srcpad have added (so moov atom surely been parsed before this), start to play
*/
void
on_got_metadata_event (BaconVideoWidget *bvw, TotemObject *totem)
{

						printf ("(on_got_metadata_event) \n");

    g_autofree char *name = NULL;

	name = totem_get_nice_name_for_stream (totem);

	if (name != NULL) 
	{
		totem_playlist_set_title (TOTEM_PLAYLIST (totem->playlist), name);
	}

	update_buttons (totem);
	on_playlist_change_name (TOTEM_PLAYLIST (totem->playlist), totem);
}









void
on_error_event (BaconVideoWidget *bvw, char *message,
                gboolean playback_stopped, TotemObject *totem)
{

									printf("(totem-object/on_error_event) \n");

	/* Clear the seek if it's there, we only want to try and seek
	 * the first file, even if it's not there */
	// totem_playlist_steal_current_starttime (totem->playlist);
	totem->pause_start = FALSE;

	if (playback_stopped)
		play_pause_set_label (totem, STATE_STOPPED);

	totem_object_show_error (totem, _("An error occurred"), message);
}








void
play_starting_cb (BaconVideoWidget *bvw,
		  TotemObject      *totem)
{
				printf("play_starting_cb \n");
	unmark_popup_busy (totem, "opening file");
}



/*when mouse hovering on player area, it will pop up the buttom toolbar of player */
gboolean
on_bvw_motion_notify_cb (BaconVideoWidget *bvw,
			 GdkEventMotion   *event,
			 TotemObject      *totem)
{
	if (!totem->reveal_controls)
		set_controls_visibility (totem, TRUE, TRUE);

	/* FIXME: handle hover
	 * if (hovering)
	 *         unschedule_hiding_popup (bvw);
	 */

	return GDK_EVENT_PROPAGATE;
}



static void
update_seekable (TotemObject *totem)
{

							// printf("(totem-object/update_seekable) \n");

	gboolean seekable;
	gboolean notify;

	seekable = bacon_video_widget_is_seekable (totem->bvw);
	notify = (totem->seekable == seekable);
	totem->seekable = seekable;

	/* Check if the stream is seekable */
	gtk_widget_set_sensitive (totem->seek, seekable);

	if (seekable != FALSE) 
	{
		gint64 starttime;
		starttime = 0;
		if (starttime != 0) 
		{
			bacon_video_widget_seek_time (totem->bvw,
						      starttime * 1000, FALSE, NULL);
			if (totem->pause_start) 
			{
				totem_object_pause (totem);
				totem->pause_start = FALSE;
			}
		}
	}

	if (notify)
	{
		g_object_notify (G_OBJECT (totem), "seekable");
	}
}



static void
update_slider_visibility (TotemObject *totem,
			  gint64 stream_length)
{
	if (totem->stream_length == stream_length)
		return;
	if (totem->stream_length > 0 && stream_length > 0)
		return;
	if (stream_length != 0)
		gtk_range_set_range (GTK_RANGE (totem->seek), 0., 65535.);
	else
		gtk_range_set_range (GTK_RANGE (totem->seek), 0., 0.);
}


//handler of bvw_signals[SIGNAL_TICK], see totem.ui 
void
update_current_time (BaconVideoWidget *bvw,
		     gint64            current_time,
		     gint64            stream_length,
		     double            current_position,
		     gboolean          seekable,
		     TotemObject      *totem)
{

								// printf("(totem-object/update_current_time) \n");

	update_slider_visibility (totem, stream_length);

	//update position of progress bar and time label
	if (totem->seek_lock == FALSE) 
	{
		gtk_adjustment_set_value (totem->seekadj,
					  current_position * 65535);

		if (stream_length == 0 && totem->streaming_file_idx != -1) 
		{
			bacon_time_label_set_time (totem->time_label,
						   current_time, -1);
			bacon_time_label_set_time (totem->time_rem_label,
						   current_time, -1);
		} 
		else 
		{
			bacon_time_label_set_time (totem->time_label,
						   current_time,
						   stream_length);
			bacon_time_label_set_time (totem->time_rem_label,
						   current_time,
						   stream_length);
		}
	}

	if (totem->stream_length != stream_length) 
	{
		g_object_notify (G_OBJECT (totem), "stream-length");
		totem->stream_length = stream_length;
	}


}



void
volume_button_value_changed_cb (GtkScaleButton *button, gdouble value, TotemObject *totem)
{

						printf("volume_button_value_changed_cb: %lf\n", value);

	totem->muted = FALSE;
	bacon_video_widget_set_volume (totem->bvw, value);
}

gboolean
volume_button_scroll_event_cb (GtkWidget      *widget,
			       GdkEventScroll *event,
			       gpointer        user_data)
{
						printf("Mouse Wheel volume_button_scroll_event_cb: \n");

	TotemObject *totem = user_data;
	gboolean increase;

	if (event->direction == GDK_SCROLL_SMOOTH) {
		gdouble delta_y;

		gdk_event_get_scroll_deltas ((GdkEvent *) event, NULL, &delta_y);
		if (delta_y == 0.0)
			return GDK_EVENT_PROPAGATE;

		increase = delta_y < 0.0;
	} else if (event->direction == GDK_SCROLL_UP) {
		increase = TRUE;
	} else if (event->direction == GDK_SCROLL_DOWN) {
		increase = SEEK_BACKWARD_OFFSET * 1000;
	} else {
		return GDK_EVENT_PROPAGATE;
	}

	totem_object_set_volume_relative (totem, increase ? VOLUME_UP_OFFSET : VOLUME_DOWN_OFFSET);
	return GDK_EVENT_STOP;
}

static void
update_volume_sliders (TotemObject *totem)
{
	double volume;

	volume = bacon_video_widget_get_volume (totem->bvw);

	g_signal_handlers_block_by_func (totem->volume, volume_button_value_changed_cb, totem);
	gtk_scale_button_set_value (GTK_SCALE_BUTTON (totem->volume), volume);
	g_signal_handlers_unblock_by_func (totem->volume, volume_button_value_changed_cb, totem);
}

void
property_notify_cb_volume (BaconVideoWidget *bvw, GParamSpec *spec, TotemObject *totem)
{
	update_volume_sliders (totem);
}

void
property_notify_cb_seekable (BaconVideoWidget *bvw, GParamSpec *spec, TotemObject *totem)
{
			// printf("(totem-object) property_notify_cb_seekable \n");
	update_seekable (totem);
}

gboolean
seek_slider_pressed_cb (GtkWidget *widget, GdkEventButton *event, TotemObject *totem)
{
	/* HACK: we want the behaviour you get with the left button, so we
	 * mangle the event.  clicking with other buttons moves the slider in
	 * step increments, clicking with the left button moves the slider to
	 * the location of the click.
	 */
	event->button = GDK_BUTTON_PRIMARY;

	g_object_set (gtk_widget_get_settings (widget),
		      "gtk-primary-button-warps-slider", GINT_TO_POINTER(TRUE),
		      NULL);

	totem->seek_lock = TRUE;
	mark_popup_busy (totem, "seek started");

	return FALSE;
}



void
seek_slider_changed_cb (GtkAdjustment *adj, TotemObject *totem)
{
	double pos;
	gint64 _time;

	if (totem->seek_lock == FALSE)
		return;

	pos = gtk_adjustment_get_value (adj) / 65535;
	// in milliseconds
	_time = bacon_video_widget_get_stream_length (totem->bvw);

	bacon_time_label_set_time (totem->time_label,
				   pos * _time, _time);
	bacon_time_label_set_time (totem->time_rem_label,
				   pos * _time, _time);

					// printf eg. "seek_slider_changed_cb, pos is 0.759259"
					printf("(totem-object/seek_slider_changed_cb), pos is %lf \n", pos);

	// if (bacon_video_widget_can_direct_seek (totem->bvw) != FALSE)
		totem_object_seek (totem, pos);
}



gboolean
seek_slider_released_cb (GtkWidget *widget, GdkEventButton *event, TotemObject *totem)
{


				printf("(totem-object/seek_slider_released_cb) \n");

	GtkAdjustment *adj;
	gdouble val;

	/* HACK: see seek_slider_pressed_cb */
	event->button = GDK_BUTTON_PRIMARY;

	/* set to FALSE here to avoid triggering a final seek when
	 * syncing the adjustments while being in direct seek mode */
	totem->seek_lock = FALSE;
	unmark_popup_busy (totem, "seek started");

	/* sync both adjustments */
	adj = gtk_range_get_adjustment (GTK_RANGE (widget));
	val = gtk_adjustment_get_value (adj);

	if (bacon_video_widget_can_direct_seek (totem->bvw) == FALSE)
		totem_object_seek (totem, val / 65535.0);

	return FALSE;
}




gboolean
seek_slider_scroll_event_cb (GtkWidget      *widget,
			     GdkEventScroll *event,
			     gpointer        user_data)
{
	TotemObject *totem = user_data;
	gint64 offset;

	if (event->direction == GDK_SCROLL_SMOOTH) {
		gdouble delta_y;

		gdk_event_get_scroll_deltas ((GdkEvent *) event, NULL, &delta_y);
		if (delta_y == 0.0)
			return GDK_EVENT_PROPAGATE;

		offset = delta_y >= 0.0 ? SEEK_BACKWARD_OFFSET * 1000 : SEEK_FORWARD_OFFSET * 1000;
	} else if (event->direction == GDK_SCROLL_UP) {
		offset = SEEK_FORWARD_OFFSET * 1000;
	} else if (event->direction == GDK_SCROLL_DOWN) {
		offset = SEEK_BACKWARD_OFFSET * 1000;
	} else {
		return GDK_EVENT_PROPAGATE;
	}
	totem_object_seek_relative (totem, offset, FALSE);
	return GDK_EVENT_STOP;
}






/*called when item added-remove in playlist, although it seems useful, we dont need it now*/
static void
playlist_changed_cb (GtkWidget *playlist, TotemObject *totem)
{
	gint fileidx = -1;;

	update_buttons (totem);

	fileidx = totem_playlist_get_current_fileidx (totem->playlist);
	if (fileidx == -1)
	{
		return;
	}

	if (totem_playlist_get_playing (totem->playlist) == TOTEM_PLAYLIST_STATUS_NONE) {
		if (totem->pause_start)
		{
			totem_object_set_fileidx (totem, fileidx);
		}
		else
		{
			totem_object_set_fileidx_and_play (totem, fileidx);
		}
	}

	totem->pause_start = FALSE;
}





/*called when double-click one row in tree view in playlist*/
static void
item_activated_cb (GtkWidget *playlist, TotemObject *totem)
{
	//seek to the right start
	totem_object_seek (totem, 0);
}





/*got called when playlist repeat-mode changed*/
static void
playlist_repeat_toggle_cb (TotemPlaylist *playlist, GParamSpec *pspec, TotemObject *totem)
{
	GAction *action;
	gboolean repeat;

	repeat = totem_playlist_get_repeat (playlist);
	action = g_action_map_lookup_action (G_ACTION_MAP (totem), "repeat");
	g_simple_action_set_state (G_SIMPLE_ACTION (action),
				   g_variant_new_boolean (repeat));
}

/**
 * totem_object_is_fullscreen:
 * @totem: a #TotemObject
 *
 * Returns %TRUE if Totem is fullscreened.
 *
 * Return value: %TRUE if Totem is fullscreened
 **/
gboolean
totem_object_is_fullscreen (TotemObject *totem)
{
	g_return_val_if_fail (TOTEM_IS_OBJECT (totem), FALSE);

	return (totem->controls_visibility == TOTEM_CONTROLS_FULLSCREEN);
}

/**
 * totem_object_is_playing:
 * @totem: a #TotemObject
 *
 * Returns %TRUE if Totem is playing a stream.
 *
 * Return value: %TRUE if Totem is playing a stream
 **/
gboolean
totem_object_is_playing (TotemObject *totem)
{
	g_return_val_if_fail (TOTEM_IS_OBJECT (totem), FALSE);

	if (totem->bvw == NULL)
		return FALSE;

	return bacon_video_widget_is_playing (totem->bvw) != FALSE;
}

/**
 * totem_object_is_paused:
 * @totem: a #TotemObject
 *
 * Returns %TRUE if playback is paused.
 *
 * Return value: %TRUE if playback is paused, %FALSE otherwise
 **/
gboolean
totem_object_is_paused (TotemObject *totem)
{
	g_return_val_if_fail (TOTEM_IS_OBJECT (totem), FALSE);

	return totem->state == STATE_PAUSED;
}

/**
 * totem_object_is_seekable:
 * @totem: a #TotemObject
 *
 * Returns %TRUE if the current stream is seekable.
 *
 * Return value: %TRUE if the current stream is seekable
 **/
gboolean
totem_object_is_seekable (TotemObject *totem)
{
	g_return_val_if_fail (TOTEM_IS_OBJECT (totem), FALSE);

	if (totem->bvw == NULL)
		return FALSE;

	return bacon_video_widget_is_seekable (totem->bvw) != FALSE;
}

static gboolean
event_is_touch (GdkEventButton *event)
{
	GdkDevice *device;

	device = gdk_event_get_device ((GdkEvent *) event);
	return (gdk_device_get_source (device) == GDK_SOURCE_TOUCHSCREEN);
}


//double-click on video frame to set fullscreen
gboolean
on_video_button_press_event (BaconVideoWidget *bvw, GdkEventButton *event, TotemObject *totem)
{
	if (event->type == GDK_BUTTON_PRESS && event->button == 1) 
	{
		gtk_widget_grab_focus (GTK_WIDGET (bvw));
		return TRUE;
	} 
	else if (event->type == GDK_2BUTTON_PRESS &&
		   event->button == 1 &&
		   event_is_touch (event) == FALSE) 
	{
		totem_object_action_fullscreen_toggle (totem);
		return TRUE;
	} 
	else if (event->type == GDK_BUTTON_PRESS && event->button == 2) 
	{
		totem_object_play_pause (totem);
		return TRUE;
	}

	return FALSE;
}


gboolean
on_eos_event (GtkWidget *widget, TotemObject *totem)
{
	
	gint fidx = -1;

	reset_seek_status (totem);


	//***For current stream is Last item in playlist
	if (
		//if it's last item of playlist, `totem_playlist_has_next_item` return FALSE
		totem_playlist_has_next_item (totem->playlist) == FALSE &&
	    //playlist repeat mode is OFF
	    totem_playlist_get_repeat (totem->playlist) == FALSE &&
		//There is more then one item in playlist OR current stream is NOT seekable
	    (totem_playlist_get_last (totem->playlist) != 0 ||
		totem_object_is_seekable (totem) == FALSE)
	)
	{
								printf("(totem-object/on_eos_event) FIRST\n");

		//go back to start of playlist end pause
		totem_playlist_set_at_start (totem->playlist);
		update_buttons (totem);
		bacon_video_widget_stop (totem->bvw);
		play_pause_set_label (totem, STATE_STOPPED);

		//such as when playing last item in playlist, when reach eos, it will start back at the first item in playlist and pause
		fidx = totem_playlist_get_current_fileidx (totem->playlist);
								printf("(totem-object/on_eos_event) get current fileidx:%d and set it \n", fidx);
		if(fidx != -1)		
		{
			totem_object_set_fileidx (totem, fidx);
		}
								printf("(totem-object/on_eos_event) gonna  call bacon_video_widget_pause \n");
		bacon_video_widget_pause (totem->bvw);
	} 
	//***For current stream is Non-Last item in playlist, will transition to next
	else 
	{
								printf("(totem-object/on_eos_event) SECOND\n");
		//***For playlist has only single item Case
		if (totem_playlist_get_last (totem->playlist) == 0 &&
		    totem_object_is_seekable (totem)) 
		{
			if (totem_playlist_get_repeat (totem->playlist) != FALSE) 
			{

						printf ("(totem-object/on_eos_event) single item in playlist, Repeat-mode is ON \n");

				//play from start
				totem_object_seek_time (totem, 0, FALSE);
				totem_object_play (totem);
			} 
			else 
			{

						printf ("(totem-object/on_eos_event) single item in playlist, Repeat-mode is OFF \n");

				//pause at start
				totem_object_pause (totem);
				totem_object_seek_time (totem, 0, FALSE);
			}
		} 
		//***For playlist has multiple items Case
		else 
		{

						printf ("(totem-object/on_eos_event) since playlist has multiple items, so seek next item\n");

			totem_object_seek_next (totem);
		}
	}

	return FALSE;
}



static void
totem_object_handle_seek (TotemObject *totem, GdkEventKey *event, gboolean is_forward)
{
	if (is_forward != FALSE) {
		if (event->state & GDK_SHIFT_MASK)
			totem_object_seek_relative (totem, SEEK_FORWARD_SHORT_OFFSET * 1000, FALSE);
		else if (event->state & GDK_CONTROL_MASK)
			totem_object_seek_relative (totem, SEEK_FORWARD_LONG_OFFSET * 1000, FALSE);
		else
			totem_object_seek_relative (totem, SEEK_FORWARD_OFFSET * 1000, FALSE);
	} else {
		if (event->state & GDK_SHIFT_MASK)
			totem_object_seek_relative (totem, SEEK_BACKWARD_SHORT_OFFSET * 1000, FALSE);
		else if (event->state & GDK_CONTROL_MASK)
			totem_object_seek_relative (totem, SEEK_BACKWARD_LONG_OFFSET * 1000, FALSE);
		else
			totem_object_seek_relative (totem, SEEK_BACKWARD_OFFSET * 1000, FALSE);
	}
}





static gboolean
totem_object_handle_key_press (TotemObject *totem, GdkEventKey *event)
{
	GdkModifierType mask;
	gboolean retval;
	gboolean switch_rtl = FALSE;

	retval = TRUE;

	mask = event->state & gtk_accelerator_get_default_mod_mask ();

	switch (event->keyval) {
	case GDK_KEY_A:
	case GDK_KEY_a:
		totem_object_toggle_aspect_ratio (totem);
		break;

	case GDK_KEY_AudioPrev:
	case GDK_KEY_Back:
	case GDK_KEY_B:
	case GDK_KEY_b:
		totem_object_seek_previous (totem);
		show_popup (totem);
		break;

	case GDK_KEY_F5:
		/* Start presentation button */
		totem_object_set_fullscreen (totem, TRUE);
		totem_object_play_pause (totem);
		break;
	case GDK_KEY_F11:
	case GDK_KEY_f:
	case GDK_KEY_F:
		totem_object_action_fullscreen_toggle (totem);
		break;
	case GDK_KEY_H:
	case GDK_KEY_h:
		totem_object_show_keyboard_shortcuts (totem);
		break;
	case GDK_KEY_question:
		totem_object_show_keyboard_shortcuts (totem);
		break;
	case GDK_KEY_M:
	case GDK_KEY_m:
			totem_object_volume_toggle_mute (totem);
		break;
	case GDK_KEY_AudioNext:
	case GDK_KEY_Forward:
	case GDK_KEY_N:
	case GDK_KEY_n:
	case GDK_KEY_End:
		totem_object_seek_next (totem);
		show_popup (totem);
		break;
	case GDK_KEY_AudioPlay:
	case GDK_KEY_p:
	case GDK_KEY_P:
	case GDK_KEY_k:
	case GDK_KEY_K:
		totem_object_play_pause (totem);
		break;
	case GDK_KEY_comma:
	case GDK_KEY_FrameBack:
		totem_object_pause (totem);
		bacon_time_label_set_show_msecs (totem->time_label, TRUE);
		bacon_video_widget_step (totem->bvw, FALSE, NULL);
		break;
	case GDK_KEY_period:
	case GDK_KEY_FrameForward:
		totem_object_pause (totem);
		bacon_time_label_set_show_msecs (totem->time_label, TRUE);
		bacon_video_widget_step (totem->bvw, TRUE, NULL);
		break;
	case GDK_KEY_AudioPause:
	case GDK_KEY_Pause:
	case GDK_KEY_AudioStop:
		totem_object_pause (totem);
		break;
	case GDK_KEY_q:
	case GDK_KEY_Q:
		totem_object_exit (totem);
		break;
	case GDK_KEY_r:
	case GDK_KEY_R:
	case GDK_KEY_ZoomIn:
		totem_object_set_zoom (totem, TRUE);
		break;
	case GDK_KEY_t:
	case GDK_KEY_T:
	case GDK_KEY_ZoomOut:
		totem_object_set_zoom (totem, FALSE);
		break;
	case GDK_KEY_Escape:
		totem_object_set_fullscreen (totem, FALSE);
		break;
	case GDK_KEY_space:
	case GDK_KEY_Return:
		if (mask != GDK_CONTROL_MASK) 
		{
			GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (totem->win));
			if (totem_object_is_fullscreen (totem) != FALSE || focus == NULL ||
			    focus == GTK_WIDGET (totem->bvw) || focus == totem->seek) {
				if (event->keyval == GDK_KEY_space) {
					totem_object_play_pause (totem);
				} 
			} else
				retval = FALSE;
		} 
		else 
		{
			if (event->keyval == GDK_KEY_space)
				totem_object_play_pause (totem);
		}
		break;
	case GDK_KEY_Left:
	case GDK_KEY_Right:
		if (event->state & GDK_MOD1_MASK) {
			gboolean is_forward;

			is_forward = (event->keyval == GDK_KEY_Right);
			/* Switch direction in RTL environment */
			if (gtk_widget_get_direction (totem->win) == GTK_TEXT_DIR_RTL)
				is_forward = !is_forward;
			if (is_forward)
				totem_object_seek_next (totem);
			else
				totem_object_seek_previous (totem);
			break;
		}
		switch_rtl = TRUE;
		/* fall through */
	case GDK_KEY_Page_Up:
	case GDK_KEY_Page_Down:
			{
			gboolean is_forward;

			is_forward = (event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_Page_Up);
			/* Switch direction in RTL environment */
			if (switch_rtl && gtk_widget_get_direction (totem->win) == GTK_TEXT_DIR_RTL)
				is_forward = !is_forward;

			if (totem_object_is_seekable (totem)) {
				totem_object_handle_seek (totem, event, is_forward);
				show_popup (totem);
			}
			}
		break;
	case GDK_KEY_Home:
		totem_object_seek (totem, 0);
		show_popup (totem);
		break;
	case GDK_KEY_Up:
		if (mask == GDK_SHIFT_MASK)
			totem_object_set_volume_relative (totem, VOLUME_UP_SHORT_OFFSET);
		else
			totem_object_set_volume_relative (totem, VOLUME_UP_OFFSET);
		break;
	case GDK_KEY_Down:
		if (mask == GDK_SHIFT_MASK)
			totem_object_set_volume_relative (totem, VOLUME_DOWN_SHORT_OFFSET);
		else
			totem_object_set_volume_relative (totem, VOLUME_DOWN_OFFSET);
		break;
	case GDK_KEY_Menu:
	case GDK_KEY_F10:
		show_popup (totem);
		GtkWidget *player_menu = totem_player_toolbar_get_player_button (TOTEM_PLAYER_TOOLBAR (totem->player_header));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (player_menu),
					      !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (player_menu)));
		break;
	case GDK_KEY_Time:
		show_popup (totem);
		break;
	case GDK_KEY_0:
		if (mask == GDK_CONTROL_MASK) 
		{
			totem_object_set_zoom (totem, FALSE);
			break;
		}
		/* fall-through */
	case GDK_KEY_1:
	case GDK_KEY_2:
	case GDK_KEY_3:
	case GDK_KEY_4:
	case GDK_KEY_5:
	case GDK_KEY_6:
	case GDK_KEY_7:
	case GDK_KEY_8:
	case GDK_KEY_9:
		totem_object_seek (totem, (event->keyval - GDK_KEY_0) * 0.1);
		break;

	default:
		retval = FALSE;
	}

	return retval;
}


gboolean
window_key_press_event_cb (GtkWidget *win, GdkEventKey *event, TotemObject *totem)
{
	/* Shortcuts disabled? */
	if (totem->disable_kbd_shortcuts != FALSE)
	{
		return FALSE;
	}

	/* Handle Quit */
	if ((event->state & GDK_CONTROL_MASK) &&
	    event->type == GDK_KEY_PRESS &&
	    (event->keyval == GDK_KEY_Q ||
	     event->keyval == GDK_KEY_q)) 
	{
		return totem_object_handle_key_press (totem, event);
	}

	/* Handle back/quit */
	if ((event->state & GDK_CONTROL_MASK) &&
	    event->type == GDK_KEY_PRESS &&
	    (event->keyval == GDK_KEY_W ||
	     event->keyval == GDK_KEY_w)) 
	{
			totem_object_exit (totem);
		return FALSE;
	}

	if (event->type == GDK_KEY_PRESS)
		return totem_object_handle_key_press (totem, event);

	return FALSE;
}



static void
update_buttons (TotemObject *totem)
{
	action_set_sensitive ("previous-chapter",
				       totem_object_can_seek_previous (totem));
	action_set_sensitive ("next-chapter",
				       totem_object_can_seek_next (totem));
}



static void
totem_setup_window (TotemObject *totem)
{
								printf("(totem_setup_window) \n");

	GtkWidget *menu_button;
	GKeyFile *keyfile;
	int w, h;
	g_autofree char *filename = NULL;

	/*save serialized file*/
	filename = g_build_filename (totem_dot_dir (), "state.ini", NULL);
	// filename is /home/pal/onfig/totem/state.ini 	
	keyfile = g_key_file_new ();
	if (g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, NULL) == FALSE) 
	{
		w = DEFAULT_WINDOW_W;
		h = DEFAULT_WINDOW_H;
		totem->maximised = TRUE;
	} 
	else 
	{
		GError *err = NULL;
		w = g_key_file_get_integer (keyfile, "State", "window_w", &err);
		if (err != NULL) 
		{
			w = 0;
			g_clear_error (&err);
		}
		h = g_key_file_get_integer (keyfile, "State", "window_h", &err);
		if (err != NULL) 
		{
			h = 0;
			g_clear_error (&err);
		}
		totem->maximised = g_key_file_get_boolean (keyfile, "State",
				"maximised", NULL);
	}
	if (w > 0 && h > 0 && totem->maximised == FALSE) 
	{
		gtk_window_set_default_size (GTK_WINDOW (totem->win), w, h);
		totem->window_w = w;
		totem->window_h = h;
	} 
	else if (totem->maximised != FALSE) 
	{
		gtk_window_maximize (GTK_WINDOW (totem->win));
	}

	/* flap window */
	totem->flap = GTK_WIDGET (gtk_builder_get_object (totem->xml, "flap"));

	/* player Headerbar */
	totem->player_header = GTK_WIDGET (gtk_builder_get_object (totem->xml, "player_header"));


	menu_button = totem_player_toolbar_get_player_button (TOTEM_PLAYER_TOOLBAR (totem->player_header));
	g_signal_connect (menu_button, "toggled", G_CALLBACK (popup_menu_shown_cb), totem);

	return;
}



void
popup_menu_shown_cb (GtkToggleButton *button,
		     TotemObject     *totem)
{

	if (gtk_toggle_button_get_active (button))
		mark_popup_busy (totem, "toolbar/go menu visible");
	else
		unmark_popup_busy (totem, "toolbar/go menu visible");
}

static void
volume_button_menu_shown_cb (GObject     *popover,
			     GParamSpec  *pspec,
			     TotemObject *totem)
{

						printf("(volume_button_menu_shown_cb) \n");

	if (gtk_widget_is_visible (GTK_WIDGET (popover)))
		mark_popup_busy (totem, "volume menu visible");
	else
		unmark_popup_busy (totem, "volume menu visible");
}


static void
totem_callback_connect (TotemObject *totem)
{
							printf("(totem_callback_connect) \n");
	GtkWidget *item;
	GtkBox *box;
	GAction *gaction;
	GtkPopover *popover;

	/* Menu items */
	gaction = g_action_map_lookup_action (G_ACTION_MAP (totem), "repeat");
	g_simple_action_set_state (G_SIMPLE_ACTION (gaction),
				   g_variant_new_boolean (totem_playlist_get_repeat (totem->playlist)));



	/* Controls */
	box = GTK_BOX (gtk_builder_get_object (totem->xml, "controls_box"));
	gtk_widget_insert_action_group (GTK_WIDGET (box), "app", G_ACTION_GROUP (totem));


	/* Play/Pause */
	totem->play_button = GTK_WIDGET (gtk_builder_get_object (totem->xml, "play_button"));


	/* Seekbar */
	g_signal_connect (totem->seekadj, "value-changed",
			  G_CALLBACK (seek_slider_changed_cb), totem);


	/* Volume */
	item = gtk_scale_button_get_popup (GTK_SCALE_BUTTON (totem->volume));
	g_signal_connect (G_OBJECT (item), "notify::visible",
			  G_CALLBACK (volume_button_menu_shown_cb), totem);


	/* Go button ,such as whether repeat after finished, skip to seconds,adjust playback speed */
	item = GTK_WIDGET (gtk_builder_get_object (totem->xml, "go_button"));
	popover = gtk_menu_button_get_popover (GTK_MENU_BUTTON (item));
	gtk_widget_set_size_request (GTK_WIDGET (popover), 175, -1);

				
	/* Set sensitivity of the toolbar buttons */
	action_set_sensitive ("play", FALSE);
	action_set_sensitive ("next-chapter", FALSE);
	// action_set_sensitive ("previous-chapter", FALSE);

	/* Volume */
	update_volume_sliders (totem);
}




/*setup the totem-playlist widget, Both shown in GUI and not shown is support*/
static void
playlist_widget_setup (TotemObject *totem)
{
	//Below code can make playlist show in GUI, see totem.ui
	// totem->playlist = GTK_WIDGET (gtk_builder_get_object (totem->xml, "playlist_header")); /*TOTEM_PLAYLIST (totem_playlist_new ());*/
	//Below code can cannot let playlist show in GUI
	totem->playlist = TOTEM_PLAYLIST (totem_playlist_new ());

	totem->playlist_signals = g_signal_group_new (TOTEM_TYPE_PLAYLIST);


	//emitted in `totem_playlist_set_title()`, got video metadata atom (such as album, author, title)and update title
	g_signal_group_connect (totem->playlist_signals, "active-name-changed",
				G_CALLBACK (on_playlist_change_name), totem);


	//emitted when double-click a tree-view row in playlist GUI, 
	// see `treeview_row_changed` in totem-playlic.c,
	g_signal_group_connect (totem->playlist_signals, "item-activated",
				G_CALLBACK (item_activated_cb), totem);
	

	//emitted when items added/removed from playlist, NO Need For now
	g_signal_group_connect (totem->playlist_signals, "changed",
				G_CALLBACK (playlist_changed_cb), totem);


	//emitted when repeat property modified, whether enable repeat-mode
	//repeat means: switch to next in playlist, if reach last item, go back at start (the first video)
	g_signal_group_connect (totem->playlist_signals, "notify::repeat",
				G_CALLBACK (playlist_repeat_toggle_cb), totem);


	//Totally
	g_signal_group_set_target (totem->playlist_signals, totem->playlist);
}






void
video_widget_create (TotemObject *totem)
{
					printf("(totem-object/video_widget_create) \n");
	g_autoptr(GError) err = NULL;

	if (g_settings_get_boolean (totem->settings, "force-software-decoders"))
	{
		totem_gst_disable_hardware_decoders ();
	}
	else
	{
		totem_gst_ensure_newer_hardware_decoders ();
	}

	if (!bacon_video_widget_check_init (totem->bvw, &err)) 
	{
		totem_interface_error_blocking (_("Videos could not startup."),
						err != NULL ? err->message : _("No reason."),
						GTK_WINDOW (totem->win));
		totem_object_exit (totem);
	}

	gtk_drag_dest_set (GTK_WIDGET (totem->bvw), GTK_DEST_DEFAULT_ALL,
			   target_table, G_N_ELEMENTS (target_table),
			   GDK_ACTION_MOVE);

	gtk_widget_realize (GTK_WIDGET (totem->bvw));
}










