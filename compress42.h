#ifndef COMPRESS42_H
#define COMPRESS42_H

#include <stdint.h>

//======================================================================

/*  This API is based on the zlib API to make it easier to slip
    this library in along-side it.
*/
typedef unsigned char Byte;

struct cmp_internal_state;

typedef struct cmp_stream
{
    const Byte *next_in;        /* next input byte */
    size_t      avail_in;       /* number of bytes available at next_in */
    size_t      total_in;       /* total number of input bytes read so far */

    Byte        *next_out;      /* next output byte should be put there */
    size_t      avail_out;      /* remaining free space at next_out */
    size_t      total_out;      /* total number of bytes output so far */

    const char *msg;          /* last error message, NULL if no error */
    struct cmp_internal_state *state; /* not visible to applications */

} cmp_stream;


typedef enum cmp_flush_code_enum
{
    CMP_NO_FLUSH,
    CMP_PARTIAL_FLUSH,
    CMP_SYNC_FLUSH,             // ignored
    CMP_FULL_FLUSH,             // ignored
    CMP_FINISH
} cmp_flush_code;


typedef enum cmp_error_code_enum
{
    CMP_OK,
    CMP_STREAM_END,
    CMP_STREAM_ERROR,           // fault when using the API
    CMP_DATA_ERROR              // e.g. a bad header, not in the compressed format
} cmp_error_code;


/** This initialises the state ready for compression.
*/
cmp_error_code
compress_init(
    cmp_stream* strm,
    int         level
    );


/** This initialises the state ready to decompress.
*/
cmp_error_code
decompress_init(
    cmp_stream* strm,
    int         level
    );


extern cmp_error_code
compress(
    cmp_stream*     strm,
    cmp_flush_code  flush
    );


extern cmp_error_code
decompress(
    cmp_stream*     strm,
    cmp_flush_code  flush
    );

extern cmp_error_code   compressEnd(cmp_stream* strm);
extern cmp_error_code   decompressEnd(cmp_stream* strm);


/** Free any memory in the state.
*/
void    compress_free(cmp_stream* strm);

//======================================================================
#endif //COMPRESS42_H
