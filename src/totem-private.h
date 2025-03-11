/*
 * Copyright (C) 2001-2002 Bastien Nocera <hadess@hadess.net>
 *
 * SPDX-License-Identifier: GPL-3-or-later
 *
 */

#pragma once

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "totem-playlist.h"
#include "backend/bacon-video-widget.h"
#include "backend/bacon-time-label.h"


// #include "totem-open-location.h"
#include "totem-plugins-engine.h"

typedef enum {
	TOTEM_CONTROLS_UNDEFINED,
	TOTEM_CONTROLS_VISIBLE,
	TOTEM_CONTROLS_FULLSCREEN
} ControlsVisibility;

typedef enum {
	STATE_PLAYING,
	STATE_PAUSED,
	STATE_STOPPED
} TotemStates;

struct _TotemObject {

	//As single GTK application
	GtkApplication parent;

	

	/* Control window */
	GtkBuilder *xml;
	GtkWidget *win;
	GtkWidget *flap;
	GtkWidget *stack;
	GtkWidget *player_header;
	BaconVideoWidget *bvw;
	GtkWidget *prefs;
	GtkWindow *shortcuts_win;
	GtkWidget *spinner;



	GtkWidget *play_button;
	BaconTimeLabel *time_label;
	BaconTimeLabel *time_rem_label;

	/* Plugins */
	GtkWidget *plugins;
	TotemPluginsEngine *engine;

	/* Seek */
	GtkWidget *seek;

	GtkAdjustment *seekadj;
	gboolean seek_lock;/*a bool can also func as a lock*/
	gboolean seekable;

	/* Volume */
	GtkWidget *volume;
	gboolean volume_sensitive;
	gboolean muted;
	double prev_volume;



	/* controls management */
	ControlsVisibility controls_visibility;
	gboolean reveal_controls;
	guint transition_timeout_id;
	GHashTable *busy_popup_ht; /* key=reason string, value=gboolean */

	/* Stream info */
	gint64 stream_length;



	/* session */
	gboolean pause_start;
	guint save_timeout_id;

	/* Window Configuration */
	int window_w, window_h;
	gboolean maximised;


	char *player_title;

	/* Playlist */
	TotemPlaylist *playlist;
	GSignalGroup *playlist_signals;


	//the file_index of currently playing stream within torrent
	gint 	streaming_file_idx;

	GSettings *settings;
	TotemStates state;

	gboolean disable_kbd_shortcuts;
	gboolean has_played_emitted;

	#if 2
	gboolean is_stream_length_set;
	#endif
};

#define SEEK_FORWARD_OFFSET 60
#define SEEK_BACKWARD_OFFSET -15

#define VOLUME_DOWN_OFFSET (-0.08)
#define VOLUME_UP_OFFSET (0.08)

#define VOLUME_DOWN_SHORT_OFFSET (-0.02)
#define VOLUME_UP_SHORT_OFFSET (0.02)

/* Header */

// void	totem_object_open			(Totem *totem);
// void	totem_object_open_location		(Totem *totem);
// void	totem_object_eject			(Totem *totem);

void	totem_object_show_help			(Totem *totem);
void	totem_object_show_keyboard_shortcuts	(Totem *totem);
void    totem_object_set_fileidx			(TotemObject *totem,
						 						gint fileidx);

void	totem_object_set_fullscreen		(TotemObject *totem, gboolean state);

// void	totem_object_set_main_page		(TotemObject *totem,
// 						 const char  *page_id);
// const char * totem_object_get_main_page		(Totem *totem);

void	totem_object_add_items_to_playlist	(TotemObject *totem,
						 GList       *items);
