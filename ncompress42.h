#ifndef NCOMPRESS42_H
#define NCOMPRESS42_H

/*  This is free and unencumbered software released into the public domain.

    Anyone is free to copy, modify, publish, use, compile, sell, or
    distribute this software, either in source code form or as a compiled
    binary, for any purpose, commercial or non-commercial, and by any
    means.

    In jurisdictions that recognize copyright laws, the author or authors
    of this software dedicate any and all copyright interest in the
    software to the public domain. We make this dedication for the benefit
    of the public at large and to the detriment of our heirs and
    successors. We intend this dedication to be an overt act of
    relinquishment in perpetuity of all present and future rights to this
    software under copyright law.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
    OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
    OTHER DEALINGS IN THE SOFTWARE.

    For more information, please refer to <http://unlicense.org/>

    Author:
        Anthony L. Shipman
*/

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//======================================================================

/*  This API differs from the zlib API.  The original compress code
    assumes it is pulling from one file and pushing to another. We
    emulate this behaviour to make adapting the code easier.
*/

typedef unsigned char Byte;


/*  This reads bytes from an input stream into the buffer. The available
    room is numBytes.  It must return the number of bytes read or 0 for
    end of file or -1 for an error.
*/
typedef int (*NCmpStreamReader)(Byte* bytes, size_t numBytes, void* rwCtxt);


/*  This is a function to push bytes to an output stream.
    It must return the number of bytes pushed or -1 for an error.
*/
typedef int (*NCmpStreamWriter)(const Byte* bytes, size_t numBytes, void* rwCtxt);


typedef struct NCompressCtxt
{
    NCmpStreamReader    reader;
    NCmpStreamWriter    writer;
    void*               rwCtxt;         // context for the reader and writer

    void*               priv;
} NCompressCtxt;


/*  Error codes, mainly from decompression.
*/
typedef enum NCompressError
{
    NCMP_OK = 0,

    NCMP_READ_ERROR,     // an error from the reader
    NCMP_WRITE_ERROR,    // an error from the writer

    NCMP_DATA_ERROR,     // invalid compressed data format
    NCMP_BITS_ERROR,     // compressed with too large a bits parameter
    NCMP_OTHER_ERROR,    // some other internal error

} NCompressError;


/** Initialise for compression.

    Set the reader, writer and read-write context in
    the CompressCtxt struct. Then call initCompress()

    The bits parameter is only used when compressing. It sets the maximum
    size of a code word. The value must be in the range 9 to 16 or else
    zero to select the default of 16.
*/
void    nInitCompress(NCompressCtxt* ctxt, int bits);

/*  Initialise for decompression.

    Set the reader, writer and read-write context in
    the CompressCtxt struct. Then call initDecompress()
*/
void    nInitDecompress(NCompressCtxt* ctxt);

void    nFreeCompress(NCompressCtxt* ctxt);

NCompressError nCompress(NCompressCtxt* ctxt);

NCompressError nDecompress(NCompressCtxt* ctxt);

//======================================================================

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // NCOMPRESS42_H
