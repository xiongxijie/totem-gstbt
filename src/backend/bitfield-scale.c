/*
 * SPDX-License-Identifier: GPL-3-or-later
 */


#include "bitfield-scale.h"




struct _BitfieldScale
{
    GtkScale parent_instance;

    DownloadingBlocksSd* downloading_blocks; /* now-downloading blocks, including piece_index, block_index and block_download_progress*/

    gint num_blocks;/*total number of blocks in torrent */
    gint num_pieces;/*total number of pieces in torrent*/
    gint block_per_piece_normal;/*num of blocks per piece for non-last pieces*/
    gint block_in_last_piece;/*num of blocks for the last piece*/
    guint8 *have_bitfield;/*bitfield represent what pieces we already have*/
    UnfinishedPieceInfo*  unfinished_block_bitfield;/*bitfield of blocks within the piece for partial-downloaded pieces*/
    GMutex lock;  // Mutex for synchronizing access to downloading_blocks
};



G_DEFINE_TYPE (BitfieldScale, bitfield_scale, GTK_TYPE_SCALE)



	

/***************************helper******************************/
static gboolean 
is_bit_set(guint8 *byte_array, gint byte_array_len, gint bit_index) 
{
    g_return_val_if_fail(byte_array != NULL, FALSE);

    // Check that the bit_index is valid
    if (bit_index >= byte_array_len * 8) {
        g_warning("bit_index out of bounds");
        return FALSE;  // or return FALSE to indicate invalid bit index
    }

    // Calculate the byte index and the bit position within the byte
    gsize byte_index = bit_index / 8;
    gint bit_position = bit_index % 8;

    // Get the byte at the calculated index
    guint8 byte = byte_array[byte_index];  

    // Check if the bit at the given position is set
    if (byte & (1 << bit_position)) 
    {
        return TRUE;  // The bit is set
    } else {
        return FALSE; // The bit is unset
    }
}

static void 
print_bit_status(guint8 *byte_array, gsize bitfield_len) 
{
    g_return_if_fail (byte_array != NULL);
    
    // Total number of bits in the GByteArray
    gsize total_bits = bitfield_len * 8;

    // Iterate through every bit from start to end
    for (gsize bit_index = 0; bit_index < total_bits; ++bit_index) 
    {
        // Calculate the byte index and the bit position within the byte
        gsize byte_index = bit_index / 8;
        gint bit_position = bit_index % 8;

        // Get the byte at the calculated index
        guint8 byte = byte_array[byte_index];

        // Check if the bit at the given position is set
        if (byte & (1 << bit_position)) 
        {
            printf("Bit at index %zu: SET\n", bit_index);  // Bit is set
        } 
        else 
        {
            printf("Bit at index %zu: UNSET\n", bit_index);  // Bit is unset
        }
    }
}

static void
set_bit_in_bitfield(guint8 *bitfield, gint bitfield_len, gint bit_index)
{
    g_return_if_fail(bitfield != NULL);

    // Calculate which byte this bit belongs to
    gint byte_index = bit_index / 8;

    // Calculate the position of the bit within the byte (0-7)
    gint bit_position = bit_index % 8;

    // Check if the byte_index is within the valid range
    if (byte_index >= bitfield_len) 
    {
        g_warning("Bit index out of bounds! bitfield length: %d, requested bit index: %d", bitfield_len, bit_index);
        return;
    }

    // Get the byte at the byte_index
    guint8 *byte = &bitfield[byte_index];

    // Set the bit using bitwise OR with a mask
    *byte |= (1 << bit_position);  // This sets the bit at `bit_position` to 1
}




static gboolean 
is_block_bitfield_complete_cur_piece (guint8 *bitfield, gint total_bits_normal, gint bits_last_piece, gboolean is_last_piece)
{
    g_return_val_if_fail(bitfield != NULL, FALSE);

    gint total_bits = is_last_piece ? bits_last_piece : total_bits_normal;

    gsize full_bytes = total_bits / 8;
    gsize remaining_bits = total_bits % 8;

    // Check all full bytes
    for (gint i = 0; i < full_bytes; i++) 
    {
        guint8 byte = bitfield[i];

        // If any byte is not fully set to 1 (0xFF), return FALSE
        if (byte != 0xFF) 
        {
            return FALSE;  // Not all bits are set to TRUE
        }
    }

    // Check the remaining bits in the last byte, if there are any
    if (remaining_bits > 0) 
    {
        guint8 last_byte = bitfield[full_bytes];

        // Create a mask for the relevant bits in the last byte
        guint8 mask = (1 << remaining_bits) - 1;

        // If the relevant bits are not all set, return FALSE
        if ((last_byte & mask) != mask) 
        {
            return FALSE;
        }
    }

    // If we reach here, all bits are set to TRUE
    return TRUE;
}



static gboolean
bitfield_scale_draw (GtkWidget* widget, cairo_t *cr)
{
    //Draw GtkScale 
    GTK_WIDGET_CLASS (bitfield_scale_parent_class)->draw (widget, cr);

    //Draw Bitfield
    BitfieldScale *self = BITFIELD_SCALE (widget);


    /*g_return_val_if_fail*/if (self->num_blocks <= 0 || self->have_bitfield==NULL || self->unfinished_block_bitfield==NULL);
                            {
                                return FALSE;
                            }


    GtkAllocation allocation;
    gint width, height;
    double segment_width;
    gtk_widget_get_allocation (widget, &allocation);
    width = allocation.width;
    height = allocation.height;

 g_mutex_lock(&self->lock);/****************************************************************************/

    //the little segment represent a block
    segment_width = (double) width / self->num_blocks;

    //Iterate each piece (Sequentially)
    for (int i = 0; i < self->num_pieces; i++) 
    {
        gint block_index;
        
        // if this piece is the one we're downloading
        // if downloading_blocks is NULL, Loop will not entered
        if(self->downloading_blocks)
        {
            for (gint d_idx=0; d_idx<self->downloading_blocks->size; ++d_idx) 
            {
                gdouble block_progress = 0;

                gint current_piece_index = self->downloading_blocks->array[d_idx].piece_index;

                // If this is the piece we're currently downloading, get its block info.
                if (current_piece_index == i) 
                {
                    UnfinishedPieceInfo info = self->unfinished_block_bitfield[i];

                    guint8* prog = self->downloading_blocks->array[d_idx].blocks_progress;

                    gdouble is_last_piece = (info.piece_index==self->num_pieces-1);

                    gint blocks_bitfield_len = is_last_piece ? self->block_in_last_piece : self->block_per_piece_normal;



                    if(!prog)
                    {
                        continue;
                    }

                    //if it is last piece
                    guint b_idx_end = self->block_per_piece_normal;
                    if(current_piece_index ==self->num_pieces-1)
                    {
                        b_idx_end = self->block_in_last_piece;
                    }


                    //loop every block within this piece
                    for (gint b_idx=0; b_idx<b_idx_end; ++b_idx)
                    {
                        if(prog[b_idx])
                            block_progress = prog[b_idx]/100;

                        // Calculate the filled width based on progress.
                        double filled_width = block_progress * segment_width;
                        cairo_set_source_rgb(cr, 1.0, 0.647, 0.0); // Orange color
                        // Draw the block's progress.
                        cairo_rectangle(cr, i*self->block_per_piece_normal*segment_width+b_idx*segment_width, 0, 
                        filled_width, height);
                        cairo_fill(cr);

                        if (block_progress == 1.0) 
                        {   
                            // Update the unfinished_block_bitfield to mark this block as downloaded.
                            set_bit_in_bitfield (info.blocks_bitfield, blocks_bitfield_len, b_idx);
                        }

                    }

                    // Check if the entire piece is downloaded by checking all blocks in the piece.
                    if (is_block_bitfield_complete_cur_piece (info.blocks_bitfield,  self->block_per_piece_normal, self->block_in_last_piece, is_last_piece))
                    {
                        // Mark the entire piece as downloaded in the have_bitfield.
                        set_bit_in_bitfield(self->have_bitfield, self->num_pieces,  i);
                    } 
                }
        
            }
        }


        //resume data
        if (is_bit_set(self->have_bitfield, self->num_pieces, i)) 
        {
            cairo_set_source_rgb(cr, 0.0, 1.0, 0.0); // Green color
            gint block_width = (i < self->num_pieces - 1) ? self->block_per_piece_normal : self->block_in_last_piece;

            cairo_rectangle(cr, i * self->block_per_piece_normal * segment_width, 0, 
                block_width * segment_width, height);

            cairo_fill(cr);
        }
        else 
        { 
            //non-last piece case
            if (i < self->num_pieces - 1) 
            {
                // For non-last pieces, we know the block count is normal.
                for (int j = 0; j < self->block_per_piece_normal; j++) 
                {
                    UnfinishedPieceInfo info = self->unfinished_block_bitfield[j];
                    // `j` is whthin [0, block_per_piece_normal]
                    if (is_bit_set (info.blocks_bitfield, self->block_per_piece_normal, j))
                    {
                        cairo_set_source_rgb(cr, 0.0, 1.0, 0.0); // Green color
                        cairo_rectangle(cr, i * self->block_per_piece_normal * segment_width + j * segment_width, 0, 
                        segment_width, height);
                        cairo_fill(cr);
                    }
                }
            } 
            //last piece case
            else 
            {
                // For the last piece, use the block_in_last_piece count.
                for (int j = 0; j < self->block_in_last_piece; j++) 
                {
                    UnfinishedPieceInfo info = self->unfinished_block_bitfield[j];
                    // `j` is whthin [0, block_in_last_piece]
                    if (is_bit_set (info.blocks_bitfield, self->block_per_piece_normal, j)) 
                    {
                        cairo_set_source_rgb (cr, 0.0, 1.0, 0.0); // Green color
                        cairo_rectangle (cr, i*self->block_per_piece_normal*segment_width+j*segment_width, 0, 
                        segment_width, height);
                        cairo_fill (cr);
                    }                    
                }
            }
        }
    }

g_mutex_unlock(&self->lock);/**************************************************************************************/

    return FALSE;
}




static gboolean bitfield_scale_draw(GtkWidget *widget, cairo_t *cr);

static void
bitfield_scale_finalize(GObject *object);

static void 
bitfield_scale_class_init (BitfieldScaleClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    widget_class->draw = bitfield_scale_draw;


    // Set the custom finalize method
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = bitfield_scale_finalize;
}



static void
bitfield_scale_init (BitfieldScale *self)
{   
    self->downloading_blocks = NULL;

    self->num_blocks = -1;
    self->num_pieces = -1;
    self->block_per_piece_normal = -1;
    self->block_in_last_piece = -1;
    
    self->have_bitfield = NULL;
    self->unfinished_block_bitfield = NULL;

    // Initialize the mutex when the object is created
    g_mutex_init(&self->lock);
}



static void 
bitfield_scale_finalize(GObject *object) 
{
    BitfieldScale *self = BITFIELD_SCALE(object);


    if (self->downloading_blocks != NULL) 
    {
        if (self->downloading_blocks->array) 
        {
            for(gint i=0;i<self->downloading_blocks->size;i++)
            {
                g_free (self->downloading_blocks->array[i].blocks_progress);

            }
            g_free (self->downloading_blocks->array);
        }
        g_free (self->downloading_blocks);  
    }

    // Free the have_bitfield (GByteArray) if it exists
    if (self->have_bitfield != NULL) 
    {
        g_free (self->have_bitfield); 
        self->have_bitfield = NULL;
    }

    if (self->unfinished_block_bitfield != NULL) 
    {
        for (guint i = 0; i < self->num_pieces; i++) 
        {
            guint8* tmp = self->unfinished_block_bitfield[i].blocks_bitfield;           
            g_free(tmp);
        }
  
        g_free(self->unfinished_block_bitfield); 
        self->unfinished_block_bitfield = NULL;
    }


    // Free any resources used by downloading_blocks and finalize mutex
    g_mutex_clear(&self->lock);
    // Call the parent class's finalize method to clean up the rest of the object
    G_OBJECT_CLASS(bitfield_scale_parent_class)->finalize(object);
}




GtkWidget *
bitfield_scale_new (void)
{
	return GTK_WIDGET (g_object_new (BITFIELD_SCALE_TYPE, NULL));
}




/*******************************************************************************************************/
/****************************************PUBLIC FUNCs***************************************************/
/*******************************************************************************************************/
void
bitfield_scale_update_downloading_blocks (BitfieldScale *self, DownloadingBlocksSd* sd)
{
    g_return_if_fail(BITFIELD_IS_SCALE (self));
    g_return_if_fail (self->num_blocks > 0);
    g_return_if_fail (sd!=NULL);

g_mutex_lock(&self->lock);/**************************************/



    // Ensure that we take ownership of the list
    if (self->downloading_blocks != sd) 
    {
        //free previous
        if (self->downloading_blocks != NULL) 
        {
            if (self->downloading_blocks->array) 
            {
                for(gint i=0;i<self->downloading_blocks->size;i++)
                {
                    g_free (self->downloading_blocks->array[i].blocks_progress);

                }
                g_free (self->downloading_blocks->array);
            }
            g_free (self->downloading_blocks);  
         }
         self->downloading_blocks = sd;

        // Mark widget to be redrawn
        gtk_widget_queue_draw(GTK_WIDGET(self));
    }

g_mutex_unlock(&self->lock);/**************************************/

}






//this function expected to called once, in initial time
void 
bitfield_scale_set_piece_block_info (GtkWidget *widget, 
                                    gint num_blocks, gint num_pieces, 
                                    gint block_per_piece_normal, 
                                    gint block_last_piece,
                                    guint8                 *finished_pieces,
                                    UnfinishedPieceInfo    *unfinished_pieces)
{
    BitfieldScale *self = BITFIELD_SCALE (widget);

    g_return_if_fail(BITFIELD_IS_SCALE (self));

    //Mandatory
    if(self->num_blocks < 0)
    {
        self->num_blocks = num_blocks;
    }
    if(self->num_pieces < 0)
    {
        self->num_pieces = num_pieces;
    }
    if(self->block_per_piece_normal < 0)
    {
        self->block_per_piece_normal = block_per_piece_normal;
    }
    if(self->block_in_last_piece < 0)
    {
        self->block_in_last_piece = block_last_piece;
    }

    if(self->have_bitfield == NULL)
    {
        if( finished_pieces != NULL )
        {
                //Take Ownership
            self->have_bitfield = finished_pieces;
        }
        else
        {
            g_warning("have_bitfield is NULL\n");
        }
    }

    if (self->unfinished_block_bitfield == NULL)
    {   
        if (unfinished_pieces != NULL)
        {
                //Take Ownership
            self->unfinished_block_bitfield = unfinished_pieces;
        }
        else
        {
            g_warning("unfinished_block_bitfield is NULL\n");
        }
    }
}



// this function expected to called once, in initial time
// on piece_finished_alert, means this whole piece at `piece_index` marked as `Have`
void 
bitfield_scale_set_whole_piece_finished (GtkWidget *widget, guint8 * finished_pieces)
{
    BitfieldScale *self = BITFIELD_SCALE (widget);

    g_return_if_fail(BITFIELD_IS_SCALE (self));
    g_return_if_fail (finished_pieces != NULL);
    g_return_if_fail (self->num_pieces != -1);

    if(self->have_bitfield != NULL)
    {
        for (gint i=0; i<self->num_pieces; ++i)
        {
            gboolean bit_status = is_bit_set (self->have_bitfield, self->num_pieces, i);
            gboolean bit_status_new = is_bit_set (finished_pieces, self->num_pieces, i);
            if (bit_status_new == TRUE && bit_status == FALSE)
            {
                set_bit_in_bitfield (self->have_bitfield, self->num_pieces, i);
            }
        }
    }
}







