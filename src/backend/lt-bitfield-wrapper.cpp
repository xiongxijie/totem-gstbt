
#include "lt-bitfield-wrapper.h"



extern "C" {

    // Create a new bitfield
    BitfieldWrapper* bitfield_create(int num_bits, bool initial_value) {
        BitfieldWrapper* wrapper = new BitfieldWrapper;

        wrapper->bitfield = new libtorrent::typed_bitfield<libtorrent::piece_index_t>(num_bits, initial_value);

        return wrapper;
    }




    // Destroy the bitfield
    void bitfield_destroy(BitfieldWrapper* wrapper) {
        // delete wrapper->bitfield;
        delete static_cast<libtorrent::typed_bitfield<libtorrent::piece_index_t>*>(wrapper->bitfield);
        delete wrapper;
    }



    // Set a bit in the bitfield
    void bitfield_set(BitfieldWrapper* wrapper, int index) {
        if (wrapper && wrapper->bitfield) {
            auto* bitfield = static_cast<libtorrent::typed_bitfield<libtorrent::piece_index_t>*>(wrapper->bitfield);
            bitfield->set_bit(index);
        }
    }


    // Unset/Clear a bit in the bitfield
    void bitfield_clear(BitfieldWrapper* wrapper, int index) {
        if (wrapper && wrapper->bitfield) {
            auto* bitfield = static_cast<libtorrent::typed_bitfield<libtorrent::piece_index_t>*>(wrapper->bitfield);
            bitfield->clear_bit(index);
        }
    }



    // Get a bit from the bitfield
    bool bitfield_get(BitfieldWrapper* wrapper, int index) {
        if (wrapper && wrapper->bitfield) {
            auto* bitfield = static_cast<libtorrent::typed_bitfield<libtorrent::piece_index_t>*>(wrapper->bitfield);
            return bitfield->get_bit(index);
        }
        return false;
    }



    // Get the size of the bitfield (number of bits)
    int bitfield_size(BitfieldWrapper* wrapper) {
        if (wrapper && wrapper->bitfield) {
            auto* bitfield = static_cast<libtorrent::typed_bitfield<libtorrent::piece_index_t>*>(wrapper->bitfield);
            return bitfield->size();
        }
        return 0;
    }


    //clear all bit state, all zero (FALSE)
    void bitfield_clear_all(BitfieldWrapper* wrapper) {
        if (wrapper && wrapper->bitfield){
            auto* bitfield = static_cast<libtorrent::typed_bitfield<libtorrent::piece_index_t>*>(wrapper->bitfield);
            bitfield->clear_all();
        }
    }

}

