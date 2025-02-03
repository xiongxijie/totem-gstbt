/*
 * Copyright (C) 2001,2002,2003,2004,2005 Bastien Nocera <hadess@hadess.net>
 *
 * SPDX-License-Identifier: GPL-3-or-later
 *
 */

#pragma once

#include <glib-object.h>
#include <gtk/gtk.h>

/**
 * TOTEM_GSETTINGS_SCHEMA:
 *
 * The GSettings schema under which all Totem settings are stored.
 **/
#define TOTEM_GSETTINGS_SCHEMA "org.gnome.totem"



/**
 * Totem:
 *
 * The #Totem object is a handy synonym for #TotemObject, and the two can be used interchangably.
 **/
typedef struct _TotemObject Totem;

/**
 * TotemObject:
 *
 * All the fields in the #TotemObject structure are private and should never be accessed directly.
 **/
#define TOTEM_TYPE_OBJECT              (totem_object_get_type ())
G_DECLARE_FINAL_TYPE(TotemObject, totem_object, TOTEM, OBJECT, GtkApplication)

void	totem_object_exit			(TotemObject *totem) G_GNUC_NORETURN;
void	totem_object_play			(TotemObject *totem);
void	totem_object_stop			(TotemObject *totem);
void	totem_object_play_pause			(TotemObject *totem);
void	totem_object_pause			(TotemObject *totem);
gboolean totem_object_can_seek_next		(TotemObject *totem);
void	totem_object_seek_next			(TotemObject *totem);
gboolean totem_object_can_seek_previous		(TotemObject *totem);
void	totem_object_seek_previous		(TotemObject *totem);
void	totem_object_seek_time			(TotemObject *totem, gint64 msec, gboolean accurate);
void	totem_object_seek_relative		(TotemObject *totem, gint64 offset, gboolean accurate);
double	totem_object_get_volume			(TotemObject *totem);
void	totem_object_set_volume			(TotemObject *totem, double volume);


void    totem_object_show_error			(TotemObject *totem,
						 const char *title,
						 const char *reason);

gboolean totem_object_is_fullscreen		(TotemObject *totem);
gboolean totem_object_is_playing		(TotemObject *totem);
gboolean totem_object_is_paused			(TotemObject *totem);
gboolean totem_object_is_seekable		(TotemObject *totem);
GtkWindow *totem_object_get_main_window		(TotemObject *totem);
GMenu *totem_object_get_menu_section		(TotemObject *totem,
						 const char  *id);
void totem_object_empty_menu_section		(TotemObject *totem,
						 const char  *id);

float		totem_object_get_rate		(TotemObject *totem);
gboolean	totem_object_set_rate		(TotemObject *totem, float rate);

GtkWidget *totem_object_get_video_widget	(TotemObject *totem);


/* Current media information */
char *	totem_object_get_short_title		(TotemObject *totem);
gint64	totem_object_get_current_time		(TotemObject *totem);

gint   totem_object_get_current_fileidx		(TotemObject *totem);
char *	totem_object_get_current_full_path  (TotemObject *totem);

/* Playlist handling */
guint	totem_object_get_playlist_length	(TotemObject *totem);
int	totem_object_get_playlist_pos		(TotemObject *totem);
char *	totem_object_get_title_at_playlist_pos	(TotemObject *totem,
						 guint playlist_index);

