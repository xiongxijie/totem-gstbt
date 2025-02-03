/* totem-playlist.h: Simple playlist dialog

   Copyright (C) 2002, 2003, 2004, 2005 Bastien Nocera <hadess@hadess.net>

   SPDX-License-Identifier: GPL-3-or-later

   Author: Bastien Nocera <hadess@hadess.net>
 */

#pragma once

#include <gtk/gtk.h>
#include <gio/gio.h>

#define TOTEM_TYPE_PLAYLIST            (totem_playlist_get_type ())
G_DECLARE_FINAL_TYPE(TotemPlaylist, totem_playlist, TOTEM, PLAYLIST, GtkBox)

typedef enum {
	TOTEM_PLAYLIST_STATUS_NONE,
	TOTEM_PLAYLIST_STATUS_PLAYING,
	TOTEM_PLAYLIST_STATUS_PAUSED
} TotemPlaylistStatus;

typedef enum {
	TOTEM_PLAYLIST_DIRECTION_NEXT,
	TOTEM_PLAYLIST_DIRECTION_PREVIOUS
} TotemPlaylistDirection;

typedef enum {
	TOTEM_PLAYLIST_DIALOG_SELECTED,
	TOTEM_PLAYLIST_DIALOG_PLAYING
} TotemPlaylistSelectDialog;


typedef void (*TotemPlaylistForeachFunc) (TotemPlaylist *playlist,
					  const gchar   *filename,
					  const gchar   *uri,
					  gpointer       user_data);

GType    totem_playlist_get_type (void);
GtkWidget *totem_playlist_new      (void);

/* The application is responsible for checking that the mrl is correct
 * @display_name is if you have a preferred display string for the mrl,
 * NULL otherwise
 */
void totem_playlist_add_one_row (TotemPlaylist *playlist, 
								gint file_index, 
								const gchar *file_name, 
								const gchar* full_path);


// void totem_playlist_save_session_playlist (TotemPlaylist *playlist,
// 					   GFile         *output,
// 					   gint64         starttime);



gint      totem_playlist_get_current_fileidx (TotemPlaylist *playlist);
char      *totem_playlist_get_current_full_path (TotemPlaylist *playlist);
char      *totem_playlist_get_current_title (TotemPlaylist *playlist);
// gint64     totem_playlist_steal_current_starttime (TotemPlaylist *playlist);
char      *totem_playlist_get_title (TotemPlaylist *playlist,
				     guint title_index);

gboolean   totem_playlist_set_title (TotemPlaylist *playlist,
				     const char *title);
// void       totem_playlist_set_current_subtitle (TotemPlaylist *playlist,
// 						const char *subtitle_uri);

#define    totem_playlist_has_direction(playlist, direction) (direction == TOTEM_PLAYLIST_DIRECTION_NEXT ? totem_playlist_has_next_item (playlist) : totem_playlist_has_previous_item (playlist))
gboolean   totem_playlist_has_previous_item (TotemPlaylist *playlist);
gboolean   totem_playlist_has_next_item (TotemPlaylist *playlist);

#define    totem_playlist_set_direction(playlist, direction) (direction == TOTEM_PLAYLIST_DIRECTION_NEXT ? totem_playlist_set_next (playlist) : totem_playlist_set_previous (playlist))
void       totem_playlist_set_previous (TotemPlaylist *playlist);
void       totem_playlist_set_next (TotemPlaylist *playlist);

gboolean   totem_playlist_get_repeat (TotemPlaylist *playlist);
void       totem_playlist_set_repeat (TotemPlaylist *playlist, gboolean repeat);

gboolean   totem_playlist_set_playing (TotemPlaylist *playlist, TotemPlaylistStatus state);
TotemPlaylistStatus totem_playlist_get_playing (TotemPlaylist *playlist);

void       totem_playlist_set_at_start (TotemPlaylist *playlist);
void       totem_playlist_set_at_end (TotemPlaylist *playlist);

int        totem_playlist_get_current (TotemPlaylist *playlist);
int        totem_playlist_get_last (TotemPlaylist *playlist);
void       totem_playlist_set_current (TotemPlaylist *playlist, guint current_index);
