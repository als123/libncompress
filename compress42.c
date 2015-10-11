/* (N)compress42.c - File compression ala IEEE Computer, Mar 1992.
 *
 * Authors:
 *   Spencer W. Thomas   (decvax!harpo!utah-cs!utah-gr!thomas)
 *   Jim McKie           (decvax!mcvax!jim)
 *   Steve Davies        (decvax!vax135!petsd!peora!srd)
 *   Ken Turkowski       (decvax!decwrl!turtlevax!ken)
 *   James A. Woods      (decvax!ihnp4!ames!jaw)
 *   Joe Orost           (decvax!vax135!petsd!joe)
 *   Dave Mack           (csu@alembic.acs.com)
 *   Peter Jannesen, Network Communication Systems
 *                       (peter@ncs.nl)
 *
 * Revision 4.2.3  92/03/14 peter@ncs.nl
 *   Optimise compress and decompress function and a lot of cleanups.
 *   New fast hash algoritme added (if more than 800Kb available).

    ... and many more revisions have been elided
 */
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>
#include    <ctype.h>
#include    <errno.h>

#include    <sys/types.h>
#include    <sys/stat.h>
#include    <fcntl.h>
#include    <signal.h>
#include    <stdio.h>
#include    <utime.h>

#include    "compress42.h"

#define DEBUG 1

//======================================================================

typedef long int    code_int;
typedef long int    count_int;
typedef long int    cmp_code_int;

/* Defines for third byte of header                     */
static const Byte MAGIC_1 = '\037';        /* First byte of compressed file */
static const Byte MAGIC_2 = '\235';        /* Second byte of compressed file */

/*  Mask for 'number of compression bits'. Masks 0x20 and 0x40 are free.
    I think 0x20 should mean that there is a fourth header byte (for expansion).
*/
static const Byte BIT_MASK = 0x1f;

/*  Block compresssion if table is full and compression rate is dropping
    flush tables.
*/
static const Byte BLOCK_MODE = 0x80;

/*  The next two codes should not be changed lightly, as they must not
    lie within the contiguous general code space.
*/
static const size_t FIRST = 257;        // first free entry
static const size_t CLEAR = 256;        // table clear output code
static const int    INIT_BITS = 9;      // initial number of bits/code

#define IBUFSIZ 8192
#define OBUFSIZ 8192

// Extra room at the end for diddling with bits
#define IBUFSIZ_TOTAL (IBUFSIZ + 64)
#define OBUFSIZ_TOTAL (OBUFSIZ + 2048)

/*  The original code had a FAST variant. The code that
    follows is just the FAST variant.
*/
#define  HBITS       17          /* 50% occupancy */
#define  HSIZE      (1<<HBITS)
#define  HMASK      (HSIZE-1)
#define  HPRIME       9941
#define  BITS           16

static const int CHECK_GAP = 10000;

#define MAXCODE(n)  (1L << (n))


typedef struct compr_state
{
    long        hp;
    int         rpos;           // read position
    int         rlop;           // handle bytes from rpos up to this index
    long        fc;
    int         outbits;        // the number of bits written into outbuf
    int         rsize;
    int         codefull;       // the code table is full
    code_int    free_ent;       // next free entry in codetab below
    int         boff;           // whole bytes, counted in bits?
    int         n_bits;         // number of bits per code
    float       ratio;          // luxuriate in the floating point
    long        checkpoint;
    code_int    extcode;        // one more than the greatests code for n_bits

    /*  This is the pairing of the code (ent) that represents
        the prefix in the table along with the next byte (c).
    */
    union
    {
        long      code;
        struct
        {
            Byte  c;
            unsigned short  ent;
        } e;
    } fcode;

} compr_state;


typedef struct decompr_state
{
    Byte        *stackp;
    Byte        finchar;
    code_int    oldcode;
    int         posbits;
    int         outpos;         // offset into outbuf
    int         outwritten;     // offset into outbuf when flushing
    int         insize;
    int         bitmask;
    code_int    free_ent;
    code_int    maxcode;
    code_int    maxmaxcode;
    int         n_bits;
    int         rsize;
} decompr_state;

/*
    To save much memory, we overlay the table used by compress() with those
    used by decompress().  The tab_prefix table is the same size and type
    as the codetab.  The tab_suffix table needs 2**BITS characters.  We
    get this from the beginning of htab.  The output stack uses the rest
    of htab, and contains characters.  There is plenty of room for any
    possible stack (stack used to be 8000 characters).
*/

typedef struct cmp_internal_state
{
    int     decompress;
    int     block_mode;     // Block compress mode -C compatible with 2.0
    int     maxbits;        // user settable max # bits/code

    cmp_error_code  in_error;   // the state is in error, don't continue

    count_int       htab[HSIZE];
    unsigned short  codetab[HSIZE];

    /*  For reasonable compatibility with the old code we operate
        in these buffers and then transfer to or from the client's
        buffers.
    */
    Byte    inbuf[IBUFSIZ_TOTAL];
    Byte    outbuf[OBUFSIZ_TOTAL];
    int     flushing;       // we are trying to flush

    struct compr_state   cstate;
    struct decompr_state dstate;

} cmp_internal_state;


#define  htabof(st, i)             st->htab[i]
#define  codetabof(st, i)          st->codetab[i]
#define  tab_prefixof(st, i)       codetabof(st, i)
#define  tab_suffixof(st, i)       ((Byte *)(st->htab))[i]
#define  de_stack(st)              ((Byte *)&(st->htab[HSIZE-1]))
#define  clear_htab(st)            memset(st->htab, -1, HSIZE * sizeof(count_int))
#define  clear_tab_prefixof(st)    memset(st->codetab, 0, 256);


static void init_compr_state(cmp_internal_state* istate);



static void
init(cmp_stream* strm, int decompress)
{
    memset(strm, 0, sizeof(cmp_stream));

    strm->state = (cmp_internal_state*)malloc(sizeof(cmp_internal_state));
    memset(strm->state, 0, sizeof(cmp_internal_state));

    strm->state->in_error   = CMP_OK;
    strm->state->block_mode = BLOCK_MODE;
    strm->state->maxbits    = BITS;
    strm->state->decompress = decompress;

    if (!decompress)
    {
        init_compr_state(strm->state);
    }
}



cmp_error_code
compress_init(cmp_stream* strm, int level)
{
    // REVISIT level is not used
    init(strm, 0);
    return CMP_OK;
}



cmp_error_code
decompress_init(cmp_stream* strm, int level)
{
    init(strm, 1);
    return CMP_OK;
}



void
compress_free(cmp_stream* strm)
{
    if (strm->state)
    {
        free(strm->state);
        strm->state = 0;
    }
}


static int
primetab[256] =     /* Special secondary hash table.     */
{
     1013, -1061, 1109, -1181, 1231, -1291, 1361, -1429,
     1481, -1531, 1583, -1627, 1699, -1759, 1831, -1889,
     1973, -2017, 2083, -2137, 2213, -2273, 2339, -2383,
     2441, -2531, 2593, -2663, 2707, -2753, 2819, -2887,
     2957, -3023, 3089, -3181, 3251, -3313, 3361, -3449,
     3511, -3557, 3617, -3677, 3739, -3821, 3881, -3931,
     4013, -4079, 4139, -4219, 4271, -4349, 4423, -4493,
     4561, -4639, 4691, -4783, 4831, -4931, 4973, -5023,
     5101, -5179, 5261, -5333, 5413, -5471, 5521, -5591,
     5659, -5737, 5807, -5857, 5923, -6029, 6089, -6151,
     6221, -6287, 6343, -6397, 6491, -6571, 6659, -6709,
     6791, -6857, 6917, -6983, 7043, -7129, 7213, -7297,
     7369, -7477, 7529, -7577, 7643, -7703, 7789, -7873,
     7933, -8017, 8093, -8171, 8237, -8297, 8387, -8461,
     8543, -8627, 8689, -8741, 8819, -8867, 8963, -9029,
     9109, -9181, 9241, -9323, 9397, -9439, 9511, -9613,
     9677, -9743, 9811, -9871, 9941,-10061,10111,-10177,
    10259,-10321,10399,-10477,10567,-10639,10711,-10789,
    10867,-10949,11047,-11113,11173,-11261,11329,-11423,
    11491,-11587,11681,-11777,11827,-11903,11959,-12041,
    12109,-12197,12263,-12343,12413,-12487,12541,-12611,
    12671,-12757,12829,-12917,12979,-13043,13127,-13187,
    13291,-13367,13451,-13523,13619,-13691,13751,-13829,
    13901,-13967,14057,-14153,14249,-14341,14419,-14489,
    14557,-14633,14717,-14767,14831,-14897,14983,-15083,
    15149,-15233,15289,-15359,15427,-15497,15583,-15649,
    15733,-15791,15881,-15937,16057,-16097,16189,-16267,
    16363,-16447,16529,-16619,16691,-16763,16879,-16937,
    17021,-17093,17183,-17257,17341,-17401,17477,-17551,
    17623,-17713,17791,-17891,17957,-18041,18097,-18169,
    18233,-18307,18379,-18451,18523,-18637,18731,-18803,
    18919,-19031,19121,-19211,19273,-19381,19429,-19477
} ;

//======================================================================
// Compression

/*
    Algorithm from "A Technique for High Performance Data Compression",
    Terry A. Welch, IEEE Computer Vol 17, No 6 (June 1984), pp 8-19.

    Algorithm:
    Modified Lempel-Ziv method (LZW).  Basically finds common substrings
    and replaces them with a variable size code.  This is deterministic,
    and can be done on the fly.  Thus, the decompression procedure needs
    no input table, but tracks the way the table was built.
*/


/*
    Algorithm:  use open addressing double hashing (no chaining) on the
    prefix code / next character combination.

    We do a variant of Knuth's algorithm D (vol. 3, sec. 6.4) along
    with G. Knott's relatively-prime secondary probe.  Here, the
    modular division first probe is gives way to a faster exclusive-or
    manipulation.

    Also do block compression with an adaptive reset, whereby the code
    table is cleared when the compression ratio decreases, but after
    the table fills.

    The variable-length output codes are re-sized at this point, and a
    special CLEAR code is generated for the decompressor.

    Late addition:  construct the table according to file size for
    noticeable speed improvement on small files.  Please direct questions
    about this implementation to ames!jaw.

*/


static void
output(cmp_internal_state* istate, code_int code)
{
    /*  Write the code updating the outbuf and compression state.
        We are writing bit-wise so the code must be merged with
        the partial byte at the end of the buffer.

        The code is up to 16 bits wide.
    */
    compr_state *cs = &istate->cstate;

    Byte  *p = &istate->outbuf[cs->outbits >> 3];
    long   i = ((long)code) << (cs->outbits & 0x7);

    p[0] |= (Byte)(i);
    p[1] |= (Byte)(i >> 8);
    p[2] |= (Byte)(i >> 16);    // these might not be used

    cs->outbits += cs->n_bits;
}



static int
flush_compress(cmp_stream* strm, cmp_flush_code flush)
{
    /*  Flush some bytes to the client.
        This will set the flushing = true if there is still more to flush.
        It returns the flushing flag.

        If flush == CMP_FINISH then we flush the last bits out.
    */
    cmp_internal_state* istate = strm->state;
    compr_state*        cs     = &istate->cstate;
    size_t              num;

    // The number of whole bytes to write
    num = cs->outbits >> 3;

    if (flush == CMP_FINISH && cs->outbits & 0x7)
    {
        // Round up to a byte
        ++num;
    }

    if (num > strm->avail_out)
    {
        num = strm->avail_out;
    }

    if (num > 0)
    {
        memcpy(strm->next_out, istate->outbuf, num);

        cs->outbits -= num << 3;
        cs->boff     = -( ((num<<3) - cs->boff) % (cs->n_bits<<3));

        if (cs->outbits > 0)
        {
            // Preserve (cs->outbits>>3) + 1 bytes which may include a fragment of bits
            memcpy(istate->outbuf, istate->outbuf + num, (cs->outbits>>3) + 1);
            memset(istate->outbuf + (cs->outbits>>3) + 1, '\0', num);
        }
        else
        if (cs->outbits < 0)
        {
            // we must have flushed everything
            cs->outbits = 0;
        }

        strm->next_out  += num;
        strm->avail_out -= num;
        strm->total_out += num;
    }

    istate->flushing = (num > 0);
    return istate->flushing;
}



static void
init_compr_state(cmp_internal_state* istate)
{
    compr_state* cs = &istate->cstate;

    cs->ratio      = 0.0;
    cs->checkpoint = CHECK_GAP;
    cs->n_bits     = INIT_BITS;
    cs->extcode    = MAXCODE(cs->n_bits) + 1;
    cs->codefull   = 0;
    cs->free_ent   = FIRST;
    cs->fcode.code = 0;

    memset(istate->outbuf, 0, OBUFSIZ_TOTAL);

    // Put a header
    istate->outbuf[0] = MAGIC_1;
    istate->outbuf[1] = MAGIC_2;
    istate->outbuf[2] = (char)(istate->maxbits | istate->block_mode);

    // We've generated 3 bytes.
    cs->boff = cs->outbits = (3<<3);

    clear_htab(istate);
}



cmp_error_code
compress(
    cmp_stream*     strm,
    cmp_flush_code  flush
    )
{
    cmp_internal_state* istate = strm->state;

    compr_state* cs = &istate->cstate;
    long         hp;

    if (istate->in_error != CMP_OK)
    {
        return istate->in_error;
    }

    if (istate->flushing)
    {
        // continue flushing
        if (flush_compress(strm, flush) > 0)
        {
            return CMP_OK;
        }
    }

    if (strm->avail_in == 0)
    {
        if (cs->outbits > 0)
        {
            // We still have something to flush
            flush_compress(strm, flush);
        }

        return istate->flushing? CMP_OK : CMP_STREAM_END;
    }

    /*  In the following, rpos will be relative to the section of the
        buffer starting at where we are now. rlop points further along to
        the end of the section that can be processed in this run.
    */
    if (cs->rpos >= cs->rlop)
    {
        // We need more
        cs->rsize = strm->avail_in;

        if (strm->total_in == 0)
        {
            // Start off the prefix string with the first byte
            cs->fcode.e.ent = *strm->next_in++;
            strm->avail_in -= 1;
            cs->rpos = 1;
        }
        else
            cs->rpos = 0;

        cs->rlop = 0;
    }

    do
    {
        if (cs->free_ent >= cs->extcode && cs->fcode.e.ent < FIRST)
        {
            if (cs->n_bits < istate->maxbits)
            {
                // Automatically increase the number of bits per code
                int nb = cs->n_bits << 3;

                cs->boff = cs->outbits =
                    cs->outbits - 1 + nb - (cs->outbits - cs->boff - 1 + nb) % nb;

                if (++cs->n_bits < istate->maxbits)
                    cs->extcode = MAXCODE(cs->n_bits)+1;
                else
                    cs->extcode = MAXCODE(cs->n_bits);
            }
            else
            {
                // Can't be increased. Don't store new codes.
                cs->extcode  = MAXCODE(16) + OBUFSIZ;
                cs->codefull = 1;
            }
        }

        if (cs->codefull && strm->total_in >= cs->checkpoint && cs->fcode.e.ent < FIRST)
        {
            // The string table is full. We empty it if the compression ratio goes down.
            float rat = strm->total_in;

            cs->checkpoint = strm->total_in + CHECK_GAP;

            if (strm->total_out == 0)
            {
                rat = 1.0;
            }
            else
            {
                rat /= strm->total_out;
            }

            if (rat >= cs->ratio)
            {
                cs->ratio = rat;
            }
            else
            {
                cs->ratio = 0.0;
                clear_htab(istate);

                output(istate, CLEAR);

                {
                    int nb = cs->n_bits << 3;

                    cs->boff = cs->outbits =
                        cs->outbits - 1 + nb - ((cs->outbits - cs->boff - 1 + nb) % nb);
                }

                cs->extcode  = MAXCODE(cs->n_bits = INIT_BITS)+1;
                cs->free_ent = FIRST;
                cs->codefull = 0;
            }
        }

        if (cs->outbits >= (OBUFSIZ<<3))
        {
            if (flush_compress(strm, flush))
            {
                return CMP_OK;
            }
        }

        {
            /*  Find out how many input bytes we can handle
                without overflowing the output buffer.
            */
            int i;

            i = cs->rsize - cs->rlop;

            if ((code_int)i > cs->extcode - cs->free_ent)
            {
                i = (int)(cs->extcode - cs->free_ent);
            }

            if (i > ((OBUFSIZ_TOTAL - 32)*8 - cs->outbits) / cs->n_bits)
            {
                i = ((OBUFSIZ_TOTAL - 32)*8 - cs->outbits) / cs->n_bits;
            }

            if (cs->codefull && (long)i > cs->checkpoint - strm->total_in)
            {
                i = (int)(cs->checkpoint - strm->total_in);
            }

            cs->rlop += i;

            strm->total_in += i;
        }

        goto next;

hfound: cs->fcode.e.ent = codetabof(istate, hp);

next:   if (cs->rpos >= cs->rlop)
        {
            goto endlop;
        }

next2:  cs->fcode.e.c = *strm->next_in++;
        strm->avail_in -= 1;
        cs->rpos++;

        // fcode contains the code for the string so far plus the new byte
        // See if this pair is in the table
        {
            long   i;
            long   p;
            long   fc = cs->fcode.code;
            hp = ((((long)(cs->fcode.e.c)) << (HBITS-8)) ^ (long)(cs->fcode.e.ent));

            if ((i = htabof(istate, hp)) == fc) goto hfound;
            if (i == -1)                goto out;

            p = primetab[cs->fcode.e.c];

lookup:     hp = (hp+p)&HMASK;
            if ((i = htabof(istate, hp)) == fc) goto hfound;
            if (i == -1)                goto out;

            hp = (hp+p)&HMASK;
            if ((i = htabof(istate, hp)) == fc) goto hfound;
            if (i == -1)                goto out;

            hp = (hp+p)&HMASK;
            if ((i = htabof(istate, hp)) == fc) goto hfound;
            if (i == -1)                goto out;

            goto lookup;
        }
out:    ;

        // Not in the string table, output a code and start a new string
        output(istate, cs->fcode.e.ent);

        {
            long fc = cs->fcode.code;   // save fcode

            cs->fcode.e.ent = cs->fcode.e.c; // the byte becomes the string

            if (!cs->codefull)
            {
                codetabof(istate, hp) = (unsigned short)cs->free_ent++;
                htabof(istate, hp) = fc;
            }
        }

        goto next;

endlop: if (cs->fcode.e.ent >= FIRST && cs->rpos < cs->rsize)
        {
            goto next2;
        }

        if (cs->rpos > cs->rlop)
        {
            strm->total_in += cs->rpos - cs->rlop;
            cs->rlop = cs->rpos;
        }
    }
    while (cs->rlop < cs->rsize);

    if (strm->total_in > 0)
    {
        output(istate, cs->fcode.e.ent);
    }

    return CMP_OK;
}


//======================================================================
// Decompression


static code_int
input(cmp_internal_state* istate)
{
    decompr_state *ds = &istate->dstate;
    code_int      code;

    Byte *p = &istate->inbuf[ds->posbits >> 3];
    long  v = ((long)(p[0])) | ((long)(p[1]) << 8) | ((long)(p[2]) << 16);

    code = (v >> (ds->posbits & 0x7)) & ds->bitmask;
    ds->posbits += ds->n_bits;

    return code;
}


static size_t
flush_decompress(cmp_stream* strm)
{
    /*  Flush from outbuf to the client. Keep going
        until ds->outwritten becomes ds->outpos.
        Return the number of bytes flushed.
    */
    cmp_internal_state* istate = strm->state;
    decompr_state*      ds  = &istate->dstate;
    size_t              num = 0;

    if (ds->outwritten < ds->outpos)
    {
        num = ds->outpos - ds->outwritten;

        if (num > strm->avail_out)
        {
            num = strm->avail_out;
        }

        if (num > 0)
        {
            memcpy(strm->next_out, istate->outbuf + ds->outwritten, num);
            ds->outwritten += num;
        }
    }

    if (num == 0)
    {
        istate->flushing = 0;
        ds->outpos = 0;
    }

    return num;
}



static int
append_string(cmp_stream* strm)
{
    /*  See if there is more of a string to be flushed
        to the output. This returns 1 if something was
        appended.
    */
    cmp_internal_state* istate = strm->state;
    decompr_state*      ds     = &istate->dstate;
    int  i;

    if (!ds->stackp)
    {
        return 0;
    }

    // The number of bytes in the string
    i = de_stack(istate) - ds->stackp;

    if (i <= 0)
    {
        return 0;
    }

    if (ds->outpos + i >= OBUFSIZ)
    {
        /*  There is no room for a simple copy. We must copy
            some. We couldn't just empty the buffer to fit
            the string as it may be too big for the buffer.
        */
        do
        {
            if (i > OBUFSIZ - ds->outpos)
            {
                i = OBUFSIZ - ds->outpos;
            }

            if (i > 0)
            {
                // There is room for a partial copy
                memcpy(istate->outbuf + ds->outpos, ds->stackp, i);
                ds->outpos += i;
            }

            if (ds->outpos >= OBUFSIZ)
            {
                istate->flushing = 1;
                ds->outwritten   = 0;
                flush_decompress(strm);
            }

            ds->stackp += i;
        }
        while ((i = (de_stack(istate) - ds->stackp)) > 0);
    }
    else
    {
        memcpy(istate->outbuf + ds->outpos, ds->stackp, i);
        ds->outpos += i;
    }

    return i > 0;
}


/*
    Decompress stdin to stdout.  This routine adapts to the codes in the
    file building the "string" table on-the-fly; requiring no table to
    be stored in the compressed file.  The tables used herein are shared
    with those of the compress() routine.  See the definitions above.
*/

cmp_error_code
decompress(
    cmp_stream*     strm,
    cmp_flush_code  flush
    )
{
    cmp_internal_state* istate = strm->state;
    decompr_state*      ds     = &istate->dstate;

    code_int    code;
    code_int    incode;

    if (istate->in_error != CMP_OK)
    {
        return istate->in_error;
    }

    if (istate->flushing)
    {
        /*  We're continuing to flush outbuf. There may also be a
            string in the stack waiting to be added to outbuf.
        */
    }

    // ds->insize is the number of bytes in inbuf. Use strm->avail_in

    // We require at least 3 bytes be available at the outset
    if (strm->total_in == 0)
    {
        // Grab the first chunk of bytes
        ds->insize = strm->avail_in;

        if (ds->insize > IBUFSIZ)
        {
            ds->insize = IBUFSIZ;
        }

        if (ds->insize < 3)
        {
            istate->in_error = CMP_STREAM_ERROR;
            return istate->in_error;
        }

        memcpy(istate->inbuf, strm->next_in, ds->insize);
        ds->rsize = ds->insize;

        if (istate->inbuf[0] != MAGIC_1 || istate->inbuf[1] != MAGIC_2)
        {
            istate->in_error = CMP_DATA_ERROR;
            return istate->in_error;
        }

        istate->maxbits    = istate->inbuf[2] & BIT_MASK;
        istate->block_mode = istate->inbuf[2] & BLOCK_MODE;
        ds->maxmaxcode     = MAXCODE(istate->maxbits);

        if (istate->maxbits > BITS)
        {
            // Too many bits for this library to use
            istate->in_error = CMP_DATA_ERROR;
            return istate->in_error;
        }

        // consume these header bytes
        strm->next_in  += 3;
        strm->avail_in -= 3;
        strm->total_in  = 3;

        ds->maxcode = MAXCODE(ds->n_bits = INIT_BITS)-1;
        ds->bitmask = (1<<ds->n_bits)-1;
        ds->oldcode = -1;
        ds->finchar = 0;
        ds->outpos  = 0;                // offset into the output buffer in bytes
        ds->posbits = 3<<3;             // we've read 3 bytes so far

        ds->free_ent = ((istate->block_mode) ? FIRST : 256);

        // As above, initialize the first 256 entries in the table.
        clear_tab_prefixof(istate);

        for (code = 255 ; code >= 0 ; --code)
        {
            tab_suffixof(istate, code) = (Byte)code;
        }
    }

    do
    {
        int inbits;

resetbuf:   ;
        {
            // Erase the bits we have read from the front of inbuf
            int o = ds->posbits >> 3;
            int e = (o <= ds->insize) ? ds->insize - o : 0;

            for (int i = 0 ; i < e ; ++i)
            {
                istate->inbuf[i] = istate->inbuf[i+o];
            }

            ds->insize = e;
            ds->posbits = 0;
        }

        if (ds->insize < IBUFSIZ_TOTAL - IBUFSIZ)
        {
            // Append some more bytes
            ds->rsize = IBUFSIZ;

            if (ds->rsize > strm->avail_in)
            {
                ds->rsize = strm->avail_in;
            }

            memcpy(istate->inbuf + ds->insize, strm->next_in, ds->rsize);
            ds->insize += ds->rsize;

            strm->next_in  += ds->rsize;
            strm->avail_in -= ds->rsize;
            strm->total_in += ds->rsize;
        }

        /*  (insize % n_bits) is the number of bits left over at the end of
            the buffer after a whole number of codes.

            REVISIT subtracting this from a number of bytes doesn't
            make sense to me. It has something to do with the n_bits<<3
            elsewhere.

        */
        inbits = ((ds->rsize > 0) ?
                        (ds->insize - ds->insize % ds->n_bits)<<3 :
                        (ds->insize<<3) - (ds->n_bits-1));

        while (inbits > ds->posbits)
        {
            if (ds->free_ent > ds->maxcode)
            {
                // Time to increase the code size
                // First do something with the alignment
                int nb = ds->n_bits << 3;

                ds->posbits = (ds->posbits-1) + (nb - (ds->posbits-1 + nb) % nb);

                ++ds->n_bits;
                if (ds->n_bits == istate->maxbits)
                    ds->maxcode = ds->maxmaxcode;
                else
                    ds->maxcode = MAXCODE(ds->n_bits)-1;

                ds->bitmask = (1<<ds->n_bits)-1;
                goto resetbuf;
            }

            code = input(istate);

            if (ds->oldcode == -1)
            {
                if (code >= 256)
                {
                    istate->in_error = CMP_DATA_ERROR;
                    return istate->in_error;
                }

                ds->oldcode = code;
                ds->finchar = (Byte)code;
                istate->outbuf[ds->outpos++] = ds->finchar;
                continue;
            }

            if (code == CLEAR && istate->block_mode)
            {
                clear_tab_prefixof(istate);
                ds->free_ent = FIRST - 1;
                ds->posbits = ((ds->posbits-1) + ((ds->n_bits<<3) -
                            (ds->posbits-1+(ds->n_bits<<3)) % (ds->n_bits<<3)));
                ds->maxcode = MAXCODE(ds->n_bits = INIT_BITS)-1;
                ds->bitmask = (1<<ds->n_bits)-1;
                goto resetbuf;
            }

            incode = code;
            ds->stackp = de_stack(istate);

            if (code >= ds->free_ent)   /* Special case for KwKwK string.   */
            {
                if (code > ds->free_ent)
                {
#if DEBUG
                    Byte *p;

                    ds->posbits -= ds->n_bits;
                    p = &istate->inbuf[ds->posbits>>3];

                    fprintf(stderr, "insize:%d posbits:%d inbuf:%02X %02X %02X %02X %02X (%d)\n",
                            ds->insize, ds->posbits,
                            p[-1],p[0],p[1],p[2],p[3], (ds->posbits&07));
                    fprintf(stderr, "uncompress: corrupt input\n");
#endif

                    // abort
                    istate->in_error = CMP_DATA_ERROR;
                    return istate->in_error;
                }

                *--ds->stackp = ds->finchar;
                code = ds->oldcode;
            }

            while ((cmp_code_int)code >= (cmp_code_int)256)
            { /* Generate output characters in reverse order */
                *--ds->stackp = tab_suffixof(istate, code);
                code = tab_prefixof(istate, code);
            }

            ds->finchar = (Byte)tab_suffixof(istate, code);
            *--ds->stackp = ds->finchar;

            /* And put them out in forward order */
            append_string(strm);

            if (istate->flushing)
            {
                // The client must do something
                return CMP_OK;
            }

            if ((code = ds->free_ent) < ds->maxmaxcode) /* Generate the new entry. */
            {
                tab_prefixof(istate, code) = (unsigned short)ds->oldcode;
                tab_suffixof(istate, code) = ds->finchar;
                ds->free_ent = code+1;
            }

            ds->oldcode = incode;   /* Remember previous code.  */
        }
    }
    while (ds->rsize > 0);

    if (ds->outpos > 0)
    {
        flush_decompress(strm);
    }

    return CMP_OK;
}
