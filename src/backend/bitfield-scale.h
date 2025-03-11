/*
 * progress bar also diplaying libtorrent::typed_bitfield
 * derived from GtkScale widget
 *
 *
 * SPDX-License-Identifier: GPL-3-or-later
 *
 */

#pragma once




#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "gst-bt/gst_bt_demux.hpp"

#define BITFIELD_SCALE_TYPE (bitfield_scale_get_type())
G_DECLARE_FINAL_TYPE (BitfieldScale, bitfield_scale, BITFIELD, SCALE, GtkScale)



G_MODULE_EXPORT GType bitfield_scale_get_type (void);
GtkWidget *bitfield_scale_new                 (void);



// typedef struct {
//     gint piece_index;     // Key (integer)
//     guint8 *progress; // Value (GByteArray) a bitfield
// } UnfinishedBlockInfo;


void
bitfield_scale_update_downloading_blocks (BitfieldScale *self, DownloadingBlocksSd* sd);

void
bitfield_scale_set_piece_block_info (GtkWidget *widget, gint num_blocks, gint num_pieces, 
                                    gint block_per_piece_normal, gint block_last_piece,
                                    guint8 * finished_pieces, 
                                    UnfinishedPieceInfo * unfinished_pieces);

void 
bitfield_scale_set_whole_piece_finished (GtkWidget *widget, guint8 * finished_pieces);


void 
bitfield_scale_update_piece_matrix_fallback (GtkWidget *widget, guint8 * piece_matrix);