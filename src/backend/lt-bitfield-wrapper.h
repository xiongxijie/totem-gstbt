#ifndef LIBTORRENT_WRAPPER_H
#define LIBTORRENT_WRAPPER_H



#include <stdbool.h>  // For C code, to make 'bool' available

#ifdef __cplusplus
extern "C" {
#endif


    // Define an opaque struct to represent the C++ typed_bitfield in C
    typedef struct {
        void* bitfield;  // This is a pointer to the actual C++ typed_bitfield<libtorrent::piece_index_t>
    } BitfieldWrapper;



    // Function to create a new bitfield (C++ object wrapped in a C-compatible struct)
    BitfieldWrapper* bitfield_create(int num_bits, bool initial_value);

    // Function to destroy the bitfield
    void bitfield_destroy(BitfieldWrapper* wrapper);

    // Function to set a bit in the bitfield
    void bitfield_set(BitfieldWrapper* wrapper, int index);

    // Function to unset a bit in the bitfield
    void bitfield_clear(BitfieldWrapper* wrapper, int index);

    void bitfield_clear_all(BitfieldWrapper* wrapper);

    // Function to get a bit from the bitfield
    bool bitfield_get(BitfieldWrapper* wrapper, int index);

    // Function to get the size of the bitfield (number of bits)
    int bitfield_size(BitfieldWrapper* wrapper);

#ifdef __cplusplus
}
#endif



// C++ specific includes should only be included when compiling as C++ code
#ifdef __cplusplus
#include "libtorrent/bitfield.hpp"
#include "libtorrent/units.hpp"
#endif


#endif // LIBTORRENT_WRAPPER_H