/* Gst-Bt - BitTorrent related GStreamer elements
 * Copyright (C) 2015 Jorge Luis Zapata
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GST_BT_DEMUX_H
#define GST_BT_DEMUX_H


#include <gst/gst.h>
#include <gst/base/gstadapter.h>



G_BEGIN_DECLS

#define GST_TYPE_BT_DEMUX            (gst_bt_demux_get_type())
#define GST_BT_DEMUX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_BT_DEMUX, GstBtDemux))
#define GST_BT_DEMUX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_BT_DEMUX, GstBtDemuxClass))
#define GST_BT_DEMUX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_BT_DEMUX, GstBtDemuxClass))
#define GST_IS_BT_DEMUX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_BT_DEMUX))
#define GST_IS_BT_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_BT_DEMUX))

#define GST_TYPE_BT_DEMUX_STREAM            (gst_bt_demux_stream_get_type())
#define GST_BT_DEMUX_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                                GST_TYPE_BT_DEMUX_STREAM, GstBtDemuxStream))
#define GST_BT_DEMUX_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                                GST_TYPE_BT_DEMUX_STREAM, GstBtDemuxStreamClass))
#define GST_BT_DEMUX_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                                GST_TYPE_BT_DEMUX_STREAM, GstBtDemuxStreamClass))
#define GST_IS_BT_DEMUX_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                               GST_TYPE_BT_DEMUX_STREAM))
#define GST_IS_BT_DEMUX_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                               GST_TYPE_BT_DEMUX_STREAM))

/* TODO add the FIRST policy */
// typedef enum _GstBtDemuxSelectorPolicy {
//   GST_BT_DEMUX_SELECTOR_POLICY_ALL,
//   GST_BT_DEMUX_SELECTOR_POLICY_LARGER,
// } GstBtDemuxSelectorPolicy;



/***************some defs *********************/
typedef struct 
{
    guint32 piece_index;
    guint8  *blocks_bitfield; // Bitfield for blocks of this piece
} UnfinishedPieceInfo;

typedef struct 
{
    guint32 total_num_blocks;
    guint32 total_num_pieces;
    guint32 blocks_per_piece_normal;
    guint32 num_blocks_last_piece;

    guint8 *finished_pieces;  // Pointer to dynamically allocated array

    UnfinishedPieceInfo *unfinished_pieces;  // Pointer to dynamically allocated array

} PieceBlockInfo;


typedef struct {
    PieceBlockInfo info;
} PieceBlockInfoSd;





typedef struct 
{
  guint32 piece_index;
  guint8*  blocks_progress;
} DownloadingBlocks;

typedef struct 
{
    gint size;
    DownloadingBlocks *array;
} DownloadingBlocksSd;








typedef struct _GstBtDemuxStream
{
  
  GstPad pad;


  gchar *path;

  gint current_piece;
  gint start_offset;
  gint start_piece;
  gint end_offset;
  gint end_piece;
  gint last_piece;

  gint file_idx;

  /*seeking segment range */
  gint64 start_byte;
  gint64 end_byte;

  /*pending segment flag*/
  gboolean pending_segment;
  gboolean flush_start_sent;
  gboolean is_user_seek;
  gboolean moov_after_mdat;

  gboolean requested;
  gboolean finished;
  gboolean buffering;
  gint buffering_level;
  gint buffering_count;

  GStaticRecMutex *lock;
  GAsyncQueue *ipc;
} GstBtDemuxStream;

typedef struct _GstBtDemuxStreamClass {
  GstPadClass parent_class;
} GstBtDemuxStreamClass;

GType gst_bt_demux_stream_get_type (void);

typedef struct _GstBtDemux
{
  GstElement parent;
  GstAdapter *adapter;

  // GstBtDemuxSelectorPolicy policy;
  GMutex *streams_lock;
  GSList *streams;
  gchar *requested_streams;
  gboolean typefind;

  /*path of .torrent file*/
  gchar *tor_path;

  //current playing/streaming file_index of video within that torrent
  gint cur_streaming_fileidx;

  gchar *temp_location;
  gboolean temp_remove;

  gboolean finished;
  gboolean buffering;
  gint buffer_pieces;

  //piece related info 
  gint num_video_file;
  gint total_num_blocks;
  gint total_num_pieces;
  gint num_blocks_last_piece;
  gint blocks_per_piece_normal;
  gpointer session;

  GstTask *task;
#if HAVE_GST_1
  GRecMutex task_lock;
#else
  GStaticRecMutex task_lock;
#endif

  
  // gpointer ppi;

  gboolean completes_checking;

  guint8 *piece_finished_barray;

  GAsyncQueue *ppi_queue;
  //// std::vector<libtorrent::partial_piece_info> ppi;
  

} GstBtDemux;


typedef struct _GstBtDemuxClass
{
  GstElementClass parent_class;

  /* inform that the streams metadata have changed */
  void (*streams_changed) (GstBtDemux * demux);

  /* get stream tags for current stream, there may be multiple stream in one torrent */
  GstTagList *(*get_stream_tags) (GstBtDemux * demux, gint stream);

  DownloadingBlocksSd *(*get_ppi) (GstBtDemux * demux);


} GstBtDemuxClass;


GType gst_bt_demux_get_type (void);

G_END_DECLS





// Function declarations (if needed)
gboolean init_piece_block_info_sd(guint32 num_pieces, guint32 blocks_per_piece_normal);
void free_piece_block_info_sd(void);
PieceBlockInfoSd* get_piece_block_info_sd();

#endif
