/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* totem-playlist.c

   Copyright (C) 2002, 2003, 2004, 2005 Bastien Nocera

   SPDX-License-Identifier: GPL-3-or-later

   Author: Bastien Nocera <hadess@hadess.net>
 */

#include "config.h"
#include "totem-playlist.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>

#include "totem-uri.h"
#include "totem-interface.h"

#define PL_LEN (gtk_tree_model_iter_n_children (playlist->model, NULL))

static gboolean totem_playlist_add_one_row_impl (TotemPlaylist *playlist,
					    gint    file_index,
					    const char    *file_name,
						const char *full_path,
					    gboolean       playing);

typedef gboolean (*ClearComparisonFunc) (TotemPlaylist *playlist, GtkTreeIter *iter, gconstpointer data);



/* Callback function for GtkBuilder */


struct _TotemPlaylist {
	GtkBox parent;

	GtkWidget *treeview;
	GtkTreeModel *model;
	GtkTreePath *current;
	// GtkTreeSelection *selection;

	GtkListStore * list_store;

	// TotemPlParser *parser;

	/* Widgets */
	// GtkWidget *remove_button;

	GSettings *settings;
	GSettings *lockdown_settings;

	/* This is a scratch list for when we're removing files */
	GList *list;
	guint current_to_be_removed : 1;

	guint disable_save_to_disk : 1;

	/* Repeat mode */
	guint repeat : 1;
};

/* Signals */
enum {
	CHANGED,
	ITEM_ACTIVATED,
	ACTIVE_NAME_CHANGED,
	CURRENT_REMOVED,
	SUBTITLE_CHANGED,
	ITEM_ADDED,
	ITEM_REMOVED,
	LAST_SIGNAL
};

enum {
	PLAYING_COL,
	FILEINDEX_COL,
	FILENAME_COL,
	FULLPATH_COL,
	
	NUM_COLS
};

enum {
	PROP_0,
	PROP_REPEAT
};


static int totem_playlist_table_signals[LAST_SIGNAL];

static void init_treeview (GtkWidget *treeview, TotemPlaylist *playlist);

#define totem_playlist_unset_playing(x) totem_playlist_set_playing(x, TOTEM_PLAYLIST_STATUS_NONE)

G_DEFINE_TYPE(TotemPlaylist, totem_playlist, GTK_TYPE_BOX)










/* This function checks if the current item is NULL, and try to update it
 * as the first item of the playlist if so. It returns TRUE if there is a
 * current item */
static gboolean
update_current_from_playlist (TotemPlaylist *playlist)
{
	int indice;

	if (playlist->current != NULL)
	{
		return TRUE;
	}

	if (PL_LEN != 0)
	{
		//set at start
		indice = 0;
		playlist->current = gtk_tree_path_new_from_indices (indice, -1);
	} 
	else 
	{
		return FALSE;
	}

	return TRUE;
}





static void
set_playing_icon (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
		  GtkTreeModel *model, GtkTreeIter *iter, TotemPlaylist *playlist)
{
	TotemPlaylistStatus playing;
	const char *icon_name;

	gtk_tree_model_get (model, iter, PLAYING_COL, &playing, -1);

	switch (playing) {
		case TOTEM_PLAYLIST_STATUS_PLAYING:
			icon_name = "media-playback-start-symbolic";
			break;
		case TOTEM_PLAYLIST_STATUS_PAUSED:
			icon_name = "media-playback-pause-symbolic";
			break;
		case TOTEM_PLAYLIST_STATUS_NONE:
		default:
			icon_name = NULL;
	}

	g_object_set (renderer, "icon-name", icon_name, NULL);
}


//only show fileidx, filename, fullpath column
static void
init_columns (GtkTreeView *treeview, TotemPlaylist *playlist)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* Playing pix */
	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new ();
	g_object_set (G_OBJECT (column), "title", "Playlist", NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (column, renderer,
			(GtkTreeCellDataFunc) set_playing_icon, playlist, NULL);
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_MENU, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* Labels */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
			"text", FILENAME_COL, NULL);
}





static void
init_treeview (GtkWidget *treeview, TotemPlaylist *playlist)
{
	// GtkTreeSelection *selection;

	init_columns (GTK_TREE_VIEW (treeview), playlist);

	// selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	// gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	// playlist->selection = selection;


	// gtk_widget_show (treeview);
}

static void
update_repeat_cb (GSettings *settings, const gchar *key, TotemPlaylist *playlist)
{
	playlist->repeat = g_settings_get_boolean (settings, "repeat");

	g_signal_emit (G_OBJECT (playlist),
			totem_playlist_table_signals[CHANGED], 0,
			NULL);
	g_object_notify (G_OBJECT (playlist), "repeat");
}

static void
update_lockdown_cb (GSettings *settings, const gchar *key, TotemPlaylist *playlist)
{
	playlist->disable_save_to_disk = g_settings_get_boolean (settings, "disable-save-to-disk");
}

static void
init_config (TotemPlaylist *playlist)
{
	playlist->settings = g_settings_new (TOTEM_GSETTINGS_SCHEMA);
	playlist->lockdown_settings = g_settings_new ("org.gnome.desktop.lockdown");

	playlist->disable_save_to_disk = g_settings_get_boolean (playlist->lockdown_settings, "disable-save-to-disk");

	g_signal_connect (playlist->lockdown_settings, "changed::disable-save-to-disk",
			  G_CALLBACK (update_lockdown_cb), playlist);

	playlist->repeat = g_settings_get_boolean (playlist->settings, "repeat");

	g_signal_connect (playlist->settings, "changed::repeat", (GCallback) update_repeat_cb, playlist);
}




static void
totem_playlist_dispose (GObject *object)
{
	TotemPlaylist *playlist = TOTEM_PLAYLIST (object);


	g_object_unref (playlist->list_store);

	g_clear_object (&playlist->settings);
	g_clear_object (&playlist->lockdown_settings);
	g_clear_pointer (&playlist->current, gtk_tree_path_free);

	G_OBJECT_CLASS (totem_playlist_parent_class)->dispose (object);
}

static void
totem_playlist_init (TotemPlaylist *playlist)
{
	gtk_widget_init_template (GTK_WIDGET (playlist));

	// gtk_widget_add_events (GTK_WIDGET (playlist), GDK_KEY_PRESS_MASK);
	// g_signal_connect (G_OBJECT (playlist), "key_press_event",
	// 		  G_CALLBACK (totem_playlist_key_press), playlist);

	init_treeview (playlist->treeview, playlist);

	//Type is GtkListStore *
	playlist->list_store = gtk_list_store_new (4, G_TYPE_INT ,G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
	// signature is `void gtk_tree_view_set_model (GtkTreeView  *tree_view, GtkTreeModel *model)`
	gtk_tree_view_set_model (GTK_TREE_VIEW(playlist->treeview), GTK_TREE_MODEL(playlist->list_store));


	playlist->model = gtk_tree_view_get_model
		(GTK_TREE_VIEW (playlist->treeview));


	if (!GTK_IS_LIST_STORE(playlist->model)) {
		printf ("(totem_playlist_init) The model is not a GtkListStore! \n");
		// return; // Or handle the error appropriately
	}


	/* tooltips */
	// gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(playlist->treeview),
	// 				 FILENAME_ESCAPED_COL);

	/* The configuration */
	init_config (playlist);

	gtk_widget_show_all (GTK_WIDGET (playlist));
}




/*got an instance*/
GtkWidget*
totem_playlist_new (void)
{
	return GTK_WIDGET (g_object_new (TOTEM_TYPE_PLAYLIST, NULL));
}






static gboolean
totem_playlist_add_one_row_impl (TotemPlaylist *playlist,
			    gint  file_index,
			    const char *file_name,
			    const char *full_path,
			    gboolean    playing)
{
	GtkListStore *store;
	GtkTreeIter iter;
	char *filename_for_display, *full_path_for_file;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);
	g_return_val_if_fail (full_path != NULL, FALSE);

	filename_for_display = g_strdup (file_name);
	full_path_for_file = g_strdup (full_path);


	store = GTK_LIST_STORE (playlist->model);

	// g_debug ("totem_playlist_add_one_row_impl (): %s %s %s %s %"G_GINT64_FORMAT " %s", filename_for_display, uri, display_name, subtitle_uri, starttime, playing ? "true" : "false");

// gint row_count = gtk_tree_model_iter_n_children(playlist->model, NULL);
// printf("(totem_playlist_add_one_row_impl) Number of rows: %d\n", row_count);

				printf("(totem_playlist_add_one_row_impl) %d %s %s\n", file_index, filename_for_display, full_path_for_file);


											// if (playlist->current  != NULL)
											// {

											// GtkTreeIter tmp_iter;
											// gchar *title;
													
											// gtk_tree_model_get_iter (playlist->model,
											// 			 &tmp_iter,
											// 			 playlist->current);

											// gtk_tree_model_get (playlist->model,
											// 		    &tmp_iter,
											// 		    FILENAME_COL, &title,
											// 		    -1);	
											// 			printf("(totem_playlist_add_one_row_impl) probe %s \n", title);
											// }



											/*
											* gtk_list_store_insert_with_values:
											* @list_store: A `GtkListStore`
											* @iter: (out) (optional): An unset `GtkTreeIter` to set to the new row
											* @position: position to insert the new row, or -1 to append after existing
											*    rows
											* @...: pairs of column number and value, terminated with -1
											*/
											//transfer none
											// gtk_list_store_insert_with_values (store, &iter, -1,
											// 				   PLAYING_COL, playing ? TOTEM_PLAYLIST_STATUS_PAUSED : TOTEM_PLAYLIST_STATUS_NONE,
											// 				   FILEINDEX_COL, file_index,
											// 				   FILENAME_COL, filename_for_display,
											// 				   FULLPATH_COL, full_path_for_file,
											// 				   -1);




	GtkTreeIter insert_iter;

	// if ( playlist->current )
	// {

	// 	gtk_list_store_append ( store, &insert_iter);
	// 	if (!gtk_list_store_iter_is_valid (store, &insert_iter)) 
	// 	{
	// 			printf ("(totem_playlist_add_one_row_impl) iter is not valid \n");
	// 			return FALSE;
	// 	}

	// 	gtk_list_store_set (store, &insert_iter,
	// 		PLAYING_COL, playing ? TOTEM_PLAYLIST_STATUS_PAUSED : TOTEM_PLAYLIST_STATUS_NONE,
	// 		FILEINDEX_COL, file_index,
	// 		FILENAME_COL, filename_for_display,
	// 		FULLPATH_COL, full_path_for_file,
	// 		-1);

	// 	// printf ("(totem_playlist_add_one_row_impl) use append and set \n");

	// }
	// else
	// {
							printf ("(totem_playlist_add_one_row_impl) use insert_with_values \n");
		// If there is no current item, insert a new row.
    	gtk_list_store_insert_with_values (store, &insert_iter, -1,  // Insert at the end
                                      PLAYING_COL, playing ? TOTEM_PLAYLIST_STATUS_PAUSED : TOTEM_PLAYLIST_STATUS_NONE,
                                      FILEINDEX_COL, file_index,
                                      FILENAME_COL, filename_for_display,
                                      FULLPATH_COL, full_path_for_file,
                                      -1);
	// }

	


	g_signal_emit (playlist,
		       totem_playlist_table_signals[ITEM_ADDED],
		       0, filename_for_display);


	g_free (filename_for_display);
	g_free (full_path_for_file);


	if (playlist->current == NULL)
	{
		playlist->current = gtk_tree_model_get_path (playlist->model, &insert_iter);
	}

	g_signal_emit (G_OBJECT (playlist),
			totem_playlist_table_signals[CHANGED], 0,
			NULL);

	return TRUE;
}




void
totem_playlist_add_one_row (TotemPlaylist *playlist, gint file_index, const gchar *file_name, const gchar* full_path)
{
	totem_playlist_add_one_row_impl (playlist, file_index, file_name, full_path, TRUE);
	
}




/*******************GET CURRENT FULLPATH*****************/
char *
totem_playlist_get_current_full_path (TotemPlaylist *playlist)
{
	GtkTreeIter iter;
	char *fpath;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), NULL);

	if (update_current_from_playlist (playlist) == FALSE)
		return NULL;

	gtk_tree_model_get_iter (playlist->model,
				 &iter,
				 playlist->current);

	gtk_tree_model_get (playlist->model,
			    &iter,
			    FULLPATH_COL, &fpath,
			    -1);

				printf ("(totem_playlist_get_current_full_path) %s \n", fpath);
	//fpath needs to freed using g_free
	return fpath;
}




/************************GET CURRENT FILEIDX*************************/
gint 
totem_playlist_get_current_fileidx (TotemPlaylist *playlist)
{
	GtkTreeIter iter;
	gint file_idx;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), -1);

	// checks if the current item is NULL, and try to update it
	if (update_current_from_playlist (playlist) == FALSE)
	{
		return -1;
	}

	if (gtk_tree_model_get_iter (playlist->model, &iter, playlist->current) == FALSE)
	{
		return -1;
	}

	gtk_tree_model_get (playlist->model, &iter,
				FILEINDEX_COL, &file_idx,
				-1);

	return file_idx;
}



/*******************GET CURRENT TITEL/FILENAME***************/
char *
totem_playlist_get_current_title (TotemPlaylist *playlist)
{
	GtkTreeIter iter;
	char *title;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), NULL);

	if (update_current_from_playlist (playlist) == FALSE)
		return NULL;

	gtk_tree_model_get_iter (playlist->model,
				 &iter,
				 playlist->current);

	gtk_tree_model_get (playlist->model,
			    &iter,
			    FILENAME_COL, &title,
			    -1);
	return title;
}





// gint64
// totem_playlist_steal_current_starttime (TotemPlaylist *playlist)
// {
// 	GtkTreeIter iter;
// 	gint64 starttime;

// 	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), 0);

// 	if (update_current_from_playlist (playlist) == FALSE)
// 		return 0;

// 	gtk_tree_model_get_iter (playlist->model,
// 				 &iter,
// 				 playlist->current);

// 	gtk_tree_model_get (playlist->model,
// 			    &iter,
// 			    STARTTIME_COL, &starttime,
// 			    -1);

// 	/* And reset the starttime so it's only used once,
// 	 * hence the "steal" in the API name */
// 	gtk_list_store_set (GTK_LIST_STORE (playlist->model),
// 			    &iter,
// 			    STARTTIME_COL, 0,
// 			    -1);

// 	return starttime;
// }



char *
totem_playlist_get_title (TotemPlaylist *playlist, guint title_index)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	char *title;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), NULL);

	path = gtk_tree_path_new_from_indices (title_index, -1);

	gtk_tree_model_get_iter (playlist->model,
				 &iter, path);

	gtk_tree_path_free (path);

	gtk_tree_model_get (playlist->model,
			    &iter,
			    FILENAME_COL, &title,
			    -1);

	return title;
}






gboolean
totem_playlist_has_previous_item (TotemPlaylist *playlist)
{
	GtkTreeIter iter;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), FALSE);

	if (update_current_from_playlist (playlist) == FALSE)
	{
		return FALSE;
	}

	/**
	 * gtk_tree_model_get_iter:
	 * @tree_model: a `GtkTreeModel`
	 * @iter: (out): the uninitialized `GtkTreeIter`
	 * @path: the `GtkTreePath`
	 *
	 * Sets @iter to a valid iterator pointing to @path.
	 *
	 * If @path does not exist, @iter is set to an invalid
	 * iterator and %FALSE is returned.
	 *
	 * Returns: %TRUE, if @iter was set
	 *
	 */
	gtk_tree_model_get_iter (playlist->model,
				 &iter,
				 playlist->current);

	return gtk_tree_model_iter_previous (playlist->model, &iter);
}



gboolean
totem_playlist_has_next_item (TotemPlaylist *playlist)
{
	GtkTreeIter iter;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), FALSE);

	if (update_current_from_playlist (playlist) == FALSE)
	{
		return FALSE;
	}

	/**
	 * gtk_tree_model_get_iter:
	 * @tree_model: a `GtkTreeModel`
	 * @iter: (out): the uninitialized `GtkTreeIter`
	 * @path: the `GtkTreePath`
	 *
	 * Sets @iter to a valid iterator pointing to @path.
	 *
	 * If @path does not exist, @iter is set to an invalid
	 * iterator and %FALSE is returned.
	 *
	 * Returns: %TRUE, if @iter was set
	 *
	 */
	gtk_tree_model_get_iter (playlist->model,
				 &iter,
				 playlist->current);

	return gtk_tree_model_iter_next (playlist->model, &iter);
}


/*
set title of a row in tree_view 
 such as 
 ##row0 playlist
 #row1 Yellowstone - Paramount Network
 #row2 The Walking Dead - AMC
 #row3 Avengers4:Endgame - Marvel Studio
 #row4 Superman - Warnar Brother.
*/
gboolean
totem_playlist_set_title (TotemPlaylist *playlist, const char *title)
{
	GtkListStore *store;
	GtkTreeIter iter;
	char *escaped_title;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), FALSE);

	if (update_current_from_playlist (playlist) == FALSE)
		return FALSE;

	store = GTK_LIST_STORE (playlist->model);
	gtk_tree_model_get_iter (playlist->model,
			&iter,
			playlist->current);

	escaped_title = g_markup_escape_text (title, -1);
	gtk_list_store_set (store, &iter,
			FILENAME_COL, title,
			-1);
	g_free (escaped_title);

	g_signal_emit (playlist,
		       totem_playlist_table_signals[ACTIVE_NAME_CHANGED], 0);

	return TRUE;
}


/* update PLAYING_COL */
gboolean
totem_playlist_set_playing (TotemPlaylist *playlist, TotemPlaylistStatus state)
{
	GtkListStore *store;
	GtkTreeIter iter;
	GtkTreePath *path;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), FALSE);

	if (update_current_from_playlist (playlist) == FALSE)
		return FALSE;

	store = GTK_LIST_STORE (playlist->model);
	gtk_tree_model_get_iter (playlist->model,
			&iter,
			playlist->current);

	gtk_list_store_set (store, &iter,
			PLAYING_COL, state,
			-1);

	if (state == FALSE)
		return TRUE;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
	gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (playlist->treeview),
				      path, NULL,
				      TRUE, 0.5, 0);
	gtk_tree_path_free (path);

	return TRUE;
}


/*whether item in playlist is being playing */
TotemPlaylistStatus
totem_playlist_get_playing (TotemPlaylist *playlist)
{
	GtkTreeIter iter;
	TotemPlaylistStatus status;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), TOTEM_PLAYLIST_STATUS_NONE);

	//playlist is empty, no item is playing
	if (gtk_tree_model_get_iter (playlist->model, &iter, playlist->current) == FALSE)
	{
		return TOTEM_PLAYLIST_STATUS_NONE;
	}

	//get bool playing field  
	gtk_tree_model_get (playlist->model,
			    &iter,
			    PLAYING_COL, &status,
			    -1);

	return status;
}


//*****************PREVIOUS*********************
void
totem_playlist_set_previous (TotemPlaylist *playlist)
{
	GtkTreeIter iter;
	char *path;

	g_return_if_fail (TOTEM_IS_PLAYLIST (playlist));

	if (totem_playlist_has_previous_item (playlist) == FALSE)
	{
		return;
	}

	totem_playlist_unset_playing (playlist);

	path = gtk_tree_path_to_string (playlist->current);
	//if already at start, PREVIOUS means go to last
	if (g_str_equal (path, "0")) 
	{
		totem_playlist_set_at_end (playlist);
		g_free (path);
		return;
	}
	g_free (path);

	gtk_tree_model_get_iter (playlist->model,
				 &iter,
				 playlist->current);

	/**
	 * gtk_tree_model_iter_previous:
	 * @tree_model: a `GtkTreeModel`
	 * @iter: (in): the `GtkTreeIter`
	 *
	 * Sets @iter to point to the previous node at the current level.
	 *
	 * If there is no previous @iter, %FALSE is returned and @iter is
	 * set to be invalid.
	 *
	 * Returns: %TRUE if @iter has been changed to the previous node
	 */
	if (!gtk_tree_model_iter_previous (playlist->model, &iter))
	{
		g_assert_not_reached ();
	}
	gtk_tree_path_free (playlist->current);
	/**
	 * gtk_tree_model_get_path:
	 * @tree_model: a `GtkTreeModel`
	 * @iter: the `GtkTreeIter`
	 *
	 * Returns a newly-created `GtkTreePath` referenced by @iter.
	 *
	 * This path should be freed with gtk_tree_path_free().
	 *
	 * Returns: a newly-created `GtkTreePath`
	 *
	 */
	playlist->current = gtk_tree_model_get_path(playlist->model, &iter);
}


//**********************NEXT***************************
void
totem_playlist_set_next (TotemPlaylist *playlist)
{
	GtkTreeIter iter;

	g_return_if_fail (TOTEM_IS_PLAYLIST (playlist));

	//if no next item, set at start
	if (totem_playlist_has_next_item (playlist) == FALSE) 
	{
		totem_playlist_set_at_start (playlist);
		return;
	}

	totem_playlist_unset_playing (playlist);

	/**
	 * gtk_tree_model_get_iter:
	 * @tree_model: a `GtkTreeModel`
	 * @iter: (out): the uninitialized `GtkTreeIter`
	 * @path: the `GtkTreePath`
	 *
	 * Sets @iter to a valid iterator pointing to @path.
	 *
	 * If @path does not exist, @iter is set to an invalid
	 * iterator and %FALSE is returned.
	 *
	 * Returns: %TRUE, if @iter was set
	 */
	gtk_tree_model_get_iter (playlist->model,
				 &iter,
				 playlist->current);

	/**
	 * gtk_tree_model_iter_next:
	 * @tree_model: a `GtkTreeModel`
	 * @iter: (in): the `GtkTreeIter`
	 *
	 * Sets @iter to point to the node following it at the current level.
	 *
	 * If there is no next @iter, %FALSE is returned and @iter is set
	 * to be invalid.
	 *
	 * Returns: %TRUE if @iter has been changed to the next node
	 */
	if (!gtk_tree_model_iter_next (playlist->model, &iter))
	{
		g_assert_not_reached ();
	}
	gtk_tree_path_free (playlist->current);
	/**
	 * gtk_tree_model_get_path:
	 * @tree_model: a `GtkTreeModel`
	 * @iter: the `GtkTreeIter`
	 *
	 * Returns a newly-created `GtkTreePath` referenced by @iter.
	 *
	 * This path should be freed with gtk_tree_path_free().
	 *
	 * Returns: a newly-created `GtkTreePath`
	 *
	 * Deprecated: 4.10
	 */
	playlist->current = gtk_tree_model_get_path (playlist->model, &iter);
}

gboolean
totem_playlist_get_repeat (TotemPlaylist *playlist)
{
	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), FALSE);

	return playlist->repeat;
}

void
totem_playlist_set_repeat (TotemPlaylist *playlist, gboolean repeat)
{
	g_return_if_fail (TOTEM_IS_PLAYLIST (playlist));

	g_settings_set_boolean (playlist->settings, "repeat", repeat);
}

void
totem_playlist_set_at_start (TotemPlaylist *playlist)
{
	g_return_if_fail (TOTEM_IS_PLAYLIST (playlist));

	totem_playlist_unset_playing (playlist);
	g_clear_pointer (&playlist->current, gtk_tree_path_free);
	update_current_from_playlist (playlist);
}

void
totem_playlist_set_at_end (TotemPlaylist *playlist)
{
	int indice;

	g_return_if_fail (TOTEM_IS_PLAYLIST (playlist));

	totem_playlist_unset_playing (playlist);
	g_clear_pointer (&playlist->current, gtk_tree_path_free);

	if (PL_LEN) 
	{
		//set to last item idx
		indice = PL_LEN - 1;
		playlist->current = gtk_tree_path_new_from_indices
			(indice, -1);
	}
}

int
totem_playlist_get_current (TotemPlaylist *playlist)
{
	char *path;
	double current_index;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), -1);

	if (playlist->current == NULL)
		return -1;
	path = gtk_tree_path_to_string (playlist->current);
	if (path == NULL)
		return -1;

	current_index = g_ascii_strtod (path, NULL);
	g_free (path);

	return current_index;
}


//the last item's index in playlist that is `length minus 1`
int
totem_playlist_get_last (TotemPlaylist *playlist)
{
	guint len = PL_LEN;

	g_return_val_if_fail (TOTEM_IS_PLAYLIST (playlist), -1);

	if (len == 0)
		return -1;

	return len - 1;
}

void
totem_playlist_set_current (TotemPlaylist *playlist, guint current_index)
{
	g_return_if_fail (TOTEM_IS_PLAYLIST (playlist));

	if (current_index >= (guint) PL_LEN){
		return;
	}

	totem_playlist_unset_playing (playlist);
	gtk_tree_path_free (playlist->current);
	playlist->current = gtk_tree_path_new_from_indices (current_index, -1);
}

static void
totem_playlist_set_property (GObject      *object,
			     guint         property_id,
			     const GValue *value,
			     GParamSpec   *pspec)
{
	TotemPlaylist *playlist;

	playlist = TOTEM_PLAYLIST (object);

	switch (property_id) {
	case PROP_REPEAT:
		g_settings_set_boolean (playlist->settings, "repeat", g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
totem_playlist_get_property (GObject    *object,
			     guint       property_id,
			     GValue     *value,
			     GParamSpec *pspec)
{
	TotemPlaylist *playlist;

	playlist = TOTEM_PLAYLIST (object);

	switch (property_id) {
	case PROP_REPEAT:
		g_value_set_boolean (value, playlist->repeat);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
totem_playlist_class_init (TotemPlaylistClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->set_property = totem_playlist_set_property;
	object_class->get_property = totem_playlist_get_property;
	object_class->dispose = totem_playlist_dispose;

	/* Signals */
	totem_playlist_table_signals[CHANGED] =
		g_signal_new ("changed",
				G_TYPE_FROM_CLASS (klass),
				G_SIGNAL_RUN_LAST,
				0,
				NULL, NULL,
				g_cclosure_marshal_VOID__VOID,
				G_TYPE_NONE, 0);



	totem_playlist_table_signals[ITEM_ACTIVATED] =
		g_signal_new ("item-activated",
				G_TYPE_FROM_CLASS (klass),
				G_SIGNAL_RUN_LAST,
				0,
				NULL, NULL,
				g_cclosure_marshal_VOID__VOID,
				G_TYPE_NONE, 0);


	totem_playlist_table_signals[ACTIVE_NAME_CHANGED] =
		g_signal_new ("active-name-changed",
				G_TYPE_FROM_CLASS (klass),
				G_SIGNAL_RUN_LAST,
				0,
				NULL, NULL,
				g_cclosure_marshal_VOID__VOID,
				G_TYPE_NONE, 0);


	totem_playlist_table_signals[ITEM_ADDED] =
		g_signal_new ("item-added",
				G_TYPE_FROM_CLASS (klass),
				G_SIGNAL_RUN_LAST,
				0,
				NULL, NULL,
				g_cclosure_marshal_generic,
				G_TYPE_NONE, 1, G_TYPE_STRING);


	totem_playlist_table_signals[ITEM_REMOVED] =
		g_signal_new ("item-removed",
				G_TYPE_FROM_CLASS (klass),
				G_SIGNAL_RUN_LAST,
				0,
				NULL, NULL,
				g_cclosure_marshal_generic,
				G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);


	g_object_class_install_property (object_class, PROP_REPEAT,
					 g_param_spec_boolean ("repeat", "Repeat",
							       "Whether repeat mode is enabled.", FALSE,
							       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/totem/ui/playlist.ui");
	gtk_widget_class_bind_template_child (widget_class, TotemPlaylist, treeview);
}
