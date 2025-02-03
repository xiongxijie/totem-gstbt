/* totem-menu.c

   Copyright (C) 2004-2005 Bastien Nocera

   SPDX-License-Identifier: GPL-3-or-later

   Author: Bastien Nocera <hadess@hadess.net>
 */

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#define GST_USE_UNSTABLE_API 1
#include <gst/tag/tag.h>
#include <string.h>

#include "totem-menu.h"
#include "totem.h"
#include "totem-interface.h"
#include "totem-private.h"
#include "bacon-video-widget.h"
#include "totem-uri.h"

#include "totem-profile.h"








static void
preferences_action_cb (GSimpleAction *action,
		       GVariant      *parameter,
		       gpointer       user_data)
{
	gtk_widget_show (TOTEM_OBJECT (user_data)->prefs);
}


static void
fullscreen_change_state (GSimpleAction *action,
			 GVariant      *value,
			 gpointer       user_data)
{
	gboolean param;

	param = g_variant_get_boolean (value);
	totem_object_set_fullscreen (TOTEM_OBJECT (user_data), param);

	g_simple_action_set_state (action, value);
}




static void
aspect_ratio_change_state (GSimpleAction *action,
			   GVariant      *value,
			   gpointer       user_data)
{
	BvwAspectRatio ratio;

	ratio = g_variant_get_int32 (value);
	bacon_video_widget_set_aspect_ratio (TOTEM_OBJECT (user_data)->bvw, ratio);

	g_simple_action_set_state (action, value);
}


static void
zoom_action_change_state (GSimpleAction *action,
			  GVariant      *value,
			  gpointer       user_data)
{
	gboolean expand;

	expand = g_variant_get_boolean (value);
	bacon_video_widget_set_zoom (TOTEM_OBJECT (user_data)->bvw,
				     expand ? BVW_ZOOM_EXPAND : BVW_ZOOM_NONE);

	g_simple_action_set_state (action, value);
}


//playlist repeat mode, when last song finished,start back at begining item
static void
repeat_change_state (GSimpleAction *action,
		     GVariant      *value,
		     gpointer       user_data)
{
	gboolean param;

	param = g_variant_get_boolean (value);
	totem_playlist_set_repeat (TOTEM_OBJECT (user_data)->playlist, param);

	g_simple_action_set_state (action, value);
}

// used to enable/disable some features
static void
toggle_action_cb (GSimpleAction *action,
		  GVariant      *parameter,
		  gpointer       user_data)
{
	GVariant *state;

	state = g_action_get_state (G_ACTION (action));
	g_action_change_state (G_ACTION (action), g_variant_new_boolean (!g_variant_get_boolean (state)));
	g_variant_unref (state);
}



static void
list_action_cb (GSimpleAction *action,
		GVariant      *parameter,
		gpointer       user_data)
{
	g_action_change_state (G_ACTION (action), parameter);
}

static void
help_action_cb (GSimpleAction *action,
		GVariant      *parameter,
		gpointer       user_data)
{
	totem_object_show_help (TOTEM_OBJECT (user_data));
}

static void
keyboard_shortcuts_action_cb (GSimpleAction *action,
			      GVariant      *parameter,
			      gpointer       user_data)
{
	totem_object_show_keyboard_shortcuts (TOTEM_OBJECT (user_data));
}

static void
quit_action_cb (GSimpleAction *action,
		GVariant      *parameter,
		gpointer       user_data)
{
				printf("totem-menu:[\"quit\"]quit_action_cb \n");
	totem_object_exit (TOTEM_OBJECT (user_data));
}













static void
play_action_cb (GSimpleAction *action,
		GVariant      *parameter,
		gpointer       user_data)
{
	totem_object_play_pause (TOTEM_OBJECT (user_data));
}

static void
next_chapter_action_cb (GSimpleAction *action,
			GVariant      *parameter,
			gpointer       user_data)
{
	TOTEM_PROFILE (totem_object_seek_next (TOTEM_OBJECT (user_data)));
}

static void
previous_chapter_action_cb (GSimpleAction *action,
			    GVariant      *parameter,
			    gpointer       user_data)
{
	TOTEM_PROFILE (totem_object_seek_previous (TOTEM_OBJECT (user_data)));
}






static GActionEntry app_entries[] = {
	/* Main app menu */
	// { "open", open_action_cb, NULL, NULL, NULL },
	// { "open-location", open_location_action_cb, NULL, NULL, NULL },
	{ "fullscreen", toggle_action_cb, NULL, "false", fullscreen_change_state },
	{ "preferences", preferences_action_cb, NULL, NULL, NULL },
	{ "repeat", toggle_action_cb, NULL, "false", repeat_change_state },
	{ "shortcuts", keyboard_shortcuts_action_cb, NULL, NULL, NULL },
	{ "help", help_action_cb, NULL, NULL, NULL },
	{ "quit", quit_action_cb, NULL, NULL, NULL },





	/* Cogwheel menu */
	{ "aspect-ratio", list_action_cb, "i", "0", aspect_ratio_change_state },
	{ "zoom", toggle_action_cb, NULL, "false", zoom_action_change_state },




	/* Navigation popup */
	{ "play", play_action_cb, NULL, NULL, NULL },
	{ "next-chapter", next_chapter_action_cb, NULL, NULL, NULL },
	{ "previous-chapter", previous_chapter_action_cb, NULL, NULL, NULL },


};

void
totem_app_actions_setup (Totem *totem)
{
	g_action_map_add_action_entries (G_ACTION_MAP (totem), app_entries, G_N_ELEMENTS (app_entries), totem);
}

void
totem_app_menu_setup (Totem *totem)
{
	char *accels[] = { NULL, NULL, NULL };
	const char * const shortcuts_accels[] = {
		"<Ctrl>H",
		"<Ctrl>question",
		"<Ctrl>F1",
		NULL
	};

	/* FIXME: https://gitlab.gnome.org/GNOME/glib/issues/700 */
	accels[0] = "F10";
	gtk_application_set_accels_for_action (GTK_APPLICATION (totem), "app.main-menu", (const char * const *) accels);

	// accels[0] = "<Primary>G";
	// gtk_application_set_accels_for_action (GTK_APPLICATION (totem), "app.next-angle", (const char * const *) accels);
	// accels[0] = "<Primary>M";
	// gtk_application_set_accels_for_action (GTK_APPLICATION (totem), "app.root-menu", (const char * const *) accels);
	// accels[0] = "<Primary>E";
	// gtk_application_set_accels_for_action (GTK_APPLICATION (totem), "app.eject", (const char * const *) accels);
	// gtk_application_set_accels_for_action (GTK_APPLICATION (totem), "app.shortcuts", shortcuts_accels);
	// accels[0] = "F1";
	// gtk_application_set_accels_for_action (GTK_APPLICATION (totem), "app.help", (const char * const *) accels);
	// accels[0] = "<Primary>l";
	
	//Ctrl+L Add web video MRL
	// accels[1] = "OpenURL";
	// gtk_application_set_accels_for_action (GTK_APPLICATION (totem), "app.open-location", (const char * const *) accels);

	//Ctrl+O Add Video FIle
	// accels[0] = "<Primary>o";
	// accels[1] = "Open";
	// gtk_application_set_accels_for_action (GTK_APPLICATION (totem), "app.open", (const char * const *) accels);

	gtk_window_set_application (GTK_WINDOW (totem->win), GTK_APPLICATION (totem));
}

