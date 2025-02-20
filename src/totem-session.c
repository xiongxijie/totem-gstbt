/* totem-session.c

   Copyright (C) 2004 Bastien Nocera

   SPDX-License-Identifier: GPL-3-or-later

   Author: Bastien Nocera <hadess@hadess.net>
 */

#include "config.h"

#include "totem.h"
#include "totem-private.h"
#include "totem-session.h"
#include "totem-uri.h"

#include <stdio.h>
static GFile *session_file = NULL;

static GFile *
get_session_file (void)
{
	char *path;

	if (session_file)
		return session_file;

	path = g_build_filename (totem_dot_dir (), "player_state.xspf", NULL);

					// printf("session file locate in : %s \n", path);

	session_file = g_file_new_for_path (path);
	g_free (path);

	return session_file;
}

static char *
get_session_filename (void)
{
	return g_file_get_uri (get_session_file ());
}

gboolean
totem_session_try_restore (Totem *totem)
{

						printf("totem_session_try_restore \n");
	char *uri;
	gint fileidx;

	g_signal_group_block (totem->playlist_signals);
	totem->pause_start = TRUE;

	/* Possibly the only place in Totem where it makes sense to add an MRL to the playlist synchronously, since we haven't yet entered
	 * the GTK+ main loop, and thus can't freeze the application. */
	uri = get_session_filename ();
	if (totem_playlist_add_mrl_sync (totem->playlist, uri) == FALSE) {
		totem->pause_start = FALSE;
		g_signal_group_unblock (totem->playlist_signals);
		totem_object_set_fileidx (totem, NULL, NULL);
		g_free (uri);
		return FALSE;
	}
	g_free (uri);

	g_signal_group_unblock (totem->playlist_signals);


	fileidx = totem_playlist_get_current_fileidx (totem->playlist, &subtitle);

	// if (fileidx != -1)
	// 	totem_object_set_main_page (totem, "player");

	totem_object_set_fileidx (totem, mrl, subtitle);

	/* We do the seeking after being told that the stream is seekable,
	 * not straight away */



	return TRUE;
}

void
totem_session_save (Totem *totem)
{
	GFile *file;
	gint64 curr = -1;

	if (totem->bvw == NULL)
		return;

	file = get_session_file ();

							printf("Save session file locate in : %s \n", g_file_get_uri (get_session_file ()));

	if (!totem_playing_dvd (totem->mrl))
		curr = bacon_video_widget_get_current_time (totem->bvw) / 1000;
	totem_playlist_save_session_playlist (totem->playlist, file, curr);
}

void
totem_session_cleanup (Totem *totem)
{

							printf("Clean session file  \n");


	g_file_delete (get_session_file (), NULL, NULL);
	g_clear_object (&session_file);
}
