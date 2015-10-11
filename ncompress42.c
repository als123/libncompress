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
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>
#include    <ctype.h>
#include    <sys/types.h>

#include    "ncompress42.h"

#define IBUFSIZ 8192
#define OBUFSIZ 8192

//  Extra room for bit diddling
#define IBUFSIZ_ALL (IBUFSIZ+64)
#define OBUFSIZ_ALL (OBUFSIZ+2048)

                            /* Defines for third byte of header                     */
#define MAGIC_1     (Byte)'\037'/* First byte of compressed file               */
#define MAGIC_2     (Byte)'\235'/* Second byte of compressed file              */
#define BIT_MASK    0x1f            /* Mask for 'number of compresssion bits'       */
                                    /* Masks 0x20 and 0x40 are free.                */
                                    /* I think 0x20 should mean that there is       */
                                    /* a fourth header byte (for expansion).        */
#define BLOCK_MODE  0x80            /* Block compresssion if table is full and      */
                                    /* compression rate is dropping flush tables    */

            /* the next two codes should not be changed lightly, as they must not   */
            /* lie within the contiguous general code space.                        */
#define FIRST   257                 /* first free entry                             */
#define CLEAR   256                 /* table clear output code                      */

#define INIT_BITS 9         /* initial number of bits/code */


/*  The original code had a FAST variant. This is the only
    variant left here. A modern processor can do the fast thing.
*/
#define  HBITS       17          /* 50% occupancy */
#define  HSIZE      (1<<HBITS)
#define  HMASK      (HSIZE-1)
#define  HPRIME       9941
#define  BITS           16
#undef   MAXSEG_64K

#define CHECK_GAP 10000

typedef long int        code_int;
typedef long int        count_int;
typedef long int        cmp_code_int;


#define MAXCODE(n)  (1L << (n))

#define output(b,o,c,n) {   Byte  *p = &(b)[(o)>>3];              \
                            long        i = ((long)(c))<<((o)&0x7);    \
                            p[0] |= (Byte)(i);                         \
                            p[1] |= (Byte)(i>>8);                      \
                            p[2] |= (Byte)(i>>16);                     \
                            (o) += (n);                                     \
                        }

#define input(b,o,c,n,m){   Byte      *p = &(b)[(o)>>3];          \
                            (c) = ((((long)(p[0]))|((long)(p[1])<<8)|       \
                                     ((long)(p[2])<<16))>>((o)&0x7))&(m);   \
                            (o) += (n);                                     \
                        }

/*
    To save much memory, we overlay the table used by compress() with those
    used by decompress().  The tab_prefix table is the same size and type
    as the codetab.  The tab_suffix table needs 2**BITS characters.  We
    get this from the beginning of htab.  The output stack uses the rest
    of htab, and contains characters.  There is plenty of room for any
    possible stack (stack used to be 8000 characters).
*/

typedef struct privState
{
    int     block_mode;     // Block compress mode -C compatible with 2.0
    int     maxbits;        // user settable max # bits/code

    count_int       htab[HSIZE];
    unsigned short  codetab[HSIZE];

    Byte    inbuf[IBUFSIZ_ALL];  
    Byte    outbuf[OBUFSIZ_ALL];

    // REVISIT this could be local rather than preserved in the state
    long    bytes_in;               // Total number of byte from input
    long    bytes_out;              // Total number of byte to output

} PrivState;


#define  htabof(ps, i)              ps->htab[i]
#define  codetabof(ps, i)           ps->codetab[i]
#define  tab_prefixof(ps, i)        codetabof(ps, i)
#define  tab_suffixof(ps, i)        ((Byte *)(ps->htab))[i]
#define  de_stack(ps)               ((Byte *)&(ps->htab[HSIZE-1]))


static inline void
clear_htab(PrivState* ps)
{
    memset(ps->htab, -1, HSIZE * sizeof(count_int));
}



static inline void
clear_tab_prefixof(PrivState* ps)
{
    memset(ps->codetab, 0, 256);
}



static void
createPrivState(CompressCtxt* ctxt, int bits)
{
    if (!ctxt->private)
    {
        PrivState* private = (PrivState*)malloc(sizeof(PrivState));

        memset(private, 0, sizeof(PrivState));

        private->block_mode = BLOCK_MODE;
        private->maxbits    = bits;
        private->bytes_in   = 0;
        private->bytes_out  = 0;

        ctxt->private = private;
    }
}



void
initCompress(CompressCtxt* ctxt, int bits)
{
    if (bits == 0)
    {
        bits = BITS;
    }
    else
    if (bits < INIT_BITS)
    {
        bits = INIT_BITS;
    }
    else
    if (bits > BITS)
    {
        bits = BITS;
    }

    ctxt->private = NULL;
    createPrivState(ctxt, bits);
}



void
initDecompress(CompressCtxt* ctxt)
{
    ctxt->private = NULL;
    createPrivState(ctxt, 0);
}



void
freeCompress(CompressCtxt* ctxt)
{
    if (ctxt->private)
    {
        free(ctxt->private);
        ctxt->private = NULL;
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


/*****************************************************************
    Algorithm from "A Technique for High Performance Data Compression",
    Terry A. Welch, IEEE Computer Vol 17, No 6 (June 1984), pp 8-19.

    Algorithm:
    Modified Lempel-Ziv method (LZW).  Basically finds common substrings
    and replaces them with a variable size code.  This is deterministic,
    and can be done on the fly.  Thus, the decompression procedure needs
    no input table, but tracks the way the table was built.
*/


/*
    compress from an input stream to an output

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

CompressError
compress(CompressCtxt* ctxt)
{
    long        hp;
    int         rpos;
    long        fc;
    int         outbits;
    int         rlop;
    int         rsize;
    int         stcode;
    code_int    free_ent;
    int         boff;
    int         n_bits;
    int         ratio;
    long        checkpoint;
    code_int    extcode;
    PrivState*  ps = (PrivState*)ctxt->private;

    union
    {
        long            code;
        struct
        {
            Byte       c;
            unsigned short  ent;
        } e;
    } fcode;


    ratio = 0;
    checkpoint = CHECK_GAP;
    extcode = MAXCODE(n_bits = INIT_BITS)+1;
    stcode = 1;
    free_ent = FIRST;

    ps->outbuf[0] = MAGIC_1;
    ps->outbuf[1] = MAGIC_2;
    ps->outbuf[2] = (char)(ps->maxbits | ps->block_mode);
    boff = outbits = (3<<3);
    fcode.code = 0;

    clear_htab(ps);

    while ((rsize = (ctxt->reader)(ps->inbuf, IBUFSIZ, ctxt->rwCtxt)) > 0)
    {
        if (ps->bytes_in == 0)
        {
            fcode.e.ent = ps->inbuf[0];
            rpos = 1;
        }
        else
            rpos = 0;

        rlop = 0;

        do
        {
            if (free_ent >= extcode && fcode.e.ent < FIRST)
            {
                if (n_bits < ps->maxbits)
                {
                    boff = outbits = (outbits-1)+((n_bits<<3)-
                                ((outbits-boff-1+(n_bits<<3))%(n_bits<<3)));
                    if (++n_bits < ps->maxbits)
                        extcode = MAXCODE(n_bits)+1;
                    else
                        extcode = MAXCODE(n_bits);
                }
                else
                {
                    extcode = MAXCODE(16)+OBUFSIZ;
                    stcode = 0;
                }
            }

            if (!stcode && ps->bytes_in >= checkpoint && fcode.e.ent < FIRST)
            {
                long int rat;

                checkpoint = ps->bytes_in + CHECK_GAP;

                if (ps->bytes_in > 0x007fffff)
                {                           /* shift will overflow */
                    rat = (ps->bytes_out + (outbits>>3)) >> 8;

                    if (rat == 0)               /* Don't divide by zero */
                        rat = 0x7fffffff;
                    else
                        rat = ps->bytes_in / rat;
                }
                else
                {
                    rat = (ps->bytes_in << 8) / (ps->bytes_out+(outbits>>3));   /* 8 fractional bits */
                }

                if (rat >= ratio)
                {
                    ratio = (int)rat;
                }
                else
                {
                    ratio = 0;

                    clear_htab(ps);
                    output(ps->outbuf, outbits, CLEAR, n_bits);

                    boff = outbits = (outbits-1)+((n_bits<<3)-
                                ((outbits-boff-1+(n_bits<<3))%(n_bits<<3)));

                    extcode = MAXCODE(n_bits = INIT_BITS)+1;
                    free_ent = FIRST;
                    stcode = 1;
                }
            }

            if (outbits >= (OBUFSIZ<<3))
            {
                if ((ctxt->writer)(ps->outbuf, OBUFSIZ, ctxt->rwCtxt) != OBUFSIZ)
                {
                    return CMP_WRITE_ERROR;
                }

                outbits -= (OBUFSIZ<<3);
                boff = -(((OBUFSIZ<<3)-boff)%(n_bits<<3));
                ps->bytes_out += OBUFSIZ;

                memcpy(ps->outbuf, ps->outbuf+OBUFSIZ, (outbits>>3)+1);
                memset(ps->outbuf+(outbits>>3)+1, '\0', OBUFSIZ);
            }

            {
                int i = rsize-rlop;

                if ((code_int)i > extcode-free_ent)
                {
                    i = (int)(extcode-free_ent);
                }

                if (i > ((OBUFSIZ_ALL - 32)*8 - outbits) / n_bits)
                {
                    i = ((OBUFSIZ_ALL - 32)*8 - outbits) / n_bits;
                }

                if (!stcode && (long)i > checkpoint - ps->bytes_in)
                {
                    i = (int)(checkpoint - ps->bytes_in);
                }

                rlop += i;
                ps->bytes_in += i;
            }

            goto next;

hfound:     fcode.e.ent = codetabof(ps, hp);

next:       if (rpos >= rlop)
            {
                goto endlop;
            }

next2:      fcode.e.c = ps->inbuf[rpos++];
            {
                long   i;
                long   p;
                fc = fcode.code;
                hp = ((((long)(fcode.e.c)) << (HBITS-8)) ^ (long)(fcode.e.ent));

                if ((i = htabof(ps, hp)) == fc) goto hfound;
                if (i == -1)                goto out;

                p = primetab[fcode.e.c];
lookup:             hp = (hp+p)&HMASK;
                if ((i = htabof(ps, hp)) == fc) goto hfound;
                if (i == -1)                goto out;
                hp = (hp+p)&HMASK;
                if ((i = htabof(ps, hp)) == fc) goto hfound;
                if (i == -1)                goto out;
                hp = (hp+p)&HMASK;
                if ((i = htabof(ps, hp)) == fc) goto hfound;
                if (i == -1)                goto out;
                goto lookup;
            }
out:        ;
            output(ps->outbuf, outbits, fcode.e.ent, n_bits);

            {
                long   fc;
                fc = fcode.code;
                fcode.e.ent = fcode.e.c;


                if (stcode)
                {
                    codetabof(ps, hp) = (unsigned short)free_ent++;
                    htabof(ps, hp) = fc;
                }
            }

            goto next;

endlop:     if (fcode.e.ent >= FIRST && rpos < rsize)
            {
                goto next2;
            }

            if (rpos > rlop)
            {
                ps->bytes_in += rpos-rlop;
                rlop = rpos;
            }
        }
        while (rlop < rsize);
    }

    if (rsize < 0)
    {
        return CMP_OTHER_ERROR;
    }

    if (ps->bytes_in > 0)
    {
        output(ps->outbuf, outbits, fcode.e.ent, n_bits);
    }

    if ((ctxt->writer)(ps->outbuf, (outbits+7)>>3, ctxt->rwCtxt) != (outbits+7)>>3)
    {
        return CMP_WRITE_ERROR;
    }

    ps->bytes_out += (outbits+7)>>3;

    return CMP_OK;
}



/*
    Decompress stdin to stdout.  This routine adapts to the codes in the
    file building the "string" table on-the-fly; requiring no table to
    be stored in the compressed file.  The tables used herein are shared
    with those of the compress() routine.  See the definitions above.
*/

CompressError
decompress(CompressCtxt* ctxt)
{
    Byte        *stackp;
    code_int    code;
    int         finchar;
    code_int    oldcode;
    code_int    incode;
    int         inbits;
    int         posbits;
    int         outpos;
    int         insize;
    int         bitmask;
    code_int    free_ent;
    code_int    maxcode;
    code_int    maxmaxcode;
    int         n_bits;
    int         rsize;
    PrivState*  ps = (PrivState*)ctxt->private;

    ps->bytes_in = 0;
    ps->bytes_out = 0;
    insize = 0;

    while (insize < 3 && (rsize = (ctxt->reader)(ps->inbuf + insize, IBUFSIZ, ctxt->rwCtxt)) > 0)
    {
        insize += rsize;
    }

    if (insize < 3 || ps->inbuf[0] != MAGIC_1 || ps->inbuf[1] != MAGIC_2)
    {
        return CMP_DATA_ERROR;
    }

    ps->maxbits    = ps->inbuf[2] & BIT_MASK;
    ps->block_mode = ps->inbuf[2] & BLOCK_MODE;

    maxmaxcode = MAXCODE(ps->maxbits);

    if (ps->maxbits > BITS)
    {
        return CMP_BITS_ERROR;
    }

    ps->bytes_in = insize;
    maxcode = MAXCODE(n_bits = INIT_BITS)-1;
    bitmask = (1<<n_bits)-1;
    oldcode = -1;
    finchar = 0;
    outpos  = 0;
    posbits = 3<<3;

    free_ent = ((ps->block_mode) ? FIRST : 256);

    clear_tab_prefixof(ps);   // As above, initialize the first 256 entries in the table.

    for (code = 255 ; code >= 0 ; --code) 
    {
        tab_suffixof(ps, code) = (Byte)code;
    }

    do
    {
resetbuf:   ;
        {
            int    i;
            int    e;
            int    o;

            o = posbits >> 3;
            e = (o <= insize) ? insize - o : 0;

            for (i = 0 ; i < e ; ++i)
            {
                ps->inbuf[i] = ps->inbuf[i+o];
            }

            insize = e;
            posbits = 0;
        }

        if (insize < IBUFSIZ_ALL - IBUFSIZ)
        {
            if ((rsize = (ctxt->reader)(ps->inbuf + insize, IBUFSIZ, ctxt->rwCtxt)) < 0)
            {
                return CMP_READ_ERROR;
            }

            insize += rsize;
        }

        inbits = ((rsize > 0) ? (insize - insize%n_bits)<<3 :
                                (insize<<3)-(n_bits-1));

        while (inbits > posbits)
        {
            if (free_ent > maxcode)
            {
                posbits = ((posbits-1) + ((n_bits<<3) -
                                 (posbits-1+(n_bits<<3))%(n_bits<<3)));

                ++n_bits;
                if (n_bits == ps->maxbits)
                    maxcode = maxmaxcode;
                else
                    maxcode = MAXCODE(n_bits)-1;

                bitmask = (1<<n_bits)-1;
                goto resetbuf;
            }

            input(ps->inbuf, posbits, code, n_bits, bitmask);

            if (oldcode == -1)
            {
                if (code >= 256) {
#if 0
                    fprintf(stderr, "oldcode:-1 code:%i\n", (int)(code));
                    fprintf(stderr, "uncompress: corrupt input\n");
#endif
                    return CMP_DATA_ERROR;
                }
                ps->outbuf[outpos++] = (Byte)(finchar = (int)(oldcode = code));
                continue;
            }

            if (code == CLEAR && ps->block_mode)
            {
                clear_tab_prefixof(ps);
                free_ent = FIRST - 1;
                posbits = ((posbits-1) + ((n_bits<<3) -
                            (posbits-1+(n_bits<<3))%(n_bits<<3)));
                maxcode = MAXCODE(n_bits = INIT_BITS)-1;
                bitmask = (1<<n_bits)-1;
                goto resetbuf;
            }

            incode = code;
            stackp = de_stack(ps);

            if (code >= free_ent)   /* Special case for KwKwK string.   */
            {
                if (code > free_ent)
                {
#if 0
                    Byte *p;

                    posbits -= n_bits;
                    p = &ps->inbuf[posbits>>3];

                    fprintf(stderr, "insize:%d posbits:%d inbuf:%02X %02X %02X %02X %02X (%d)\n", insize, posbits,
                            p[-1],p[0],p[1],p[2],p[3], (posbits&07));
                    fprintf(stderr, "uncompress: corrupt input\n");
#endif
                    return CMP_DATA_ERROR;
                }

                *--stackp = (Byte)finchar;
                code = oldcode;
            }

            while ((cmp_code_int)code >= (cmp_code_int)256)
            {
                // Generate output characters in reverse order
                *--stackp = tab_suffixof(ps, code);
                code = tab_prefixof(ps, code);
            }

            *--stackp = (Byte)(finchar = tab_suffixof(ps, code));

            // And put them out in forward order
            {
                int    i;

                if (outpos + (i = (de_stack(ps) - stackp)) >= OBUFSIZ)
                {
                    do
                    {
                        if (i > OBUFSIZ - outpos)
                        {
                            i = OBUFSIZ-outpos;
                        }

                        if (i > 0)
                        {
                            memcpy(ps->outbuf + outpos, stackp, i);
                            outpos += i;
                        }

                        if (outpos >= OBUFSIZ)
                        {
                            if ((ctxt->writer)(ps->outbuf, outpos, ctxt->rwCtxt) != outpos)
                            {
                                return CMP_WRITE_ERROR;
                            }

                            outpos = 0;
                        }
                        stackp+= i;
                    }
                    while ((i = (de_stack(ps) - stackp)) > 0);
                }
                else
                {
                    memcpy(ps->outbuf + outpos, stackp, i);
                    outpos += i;
                }
            }

            if ((code = free_ent) < maxmaxcode) /* Generate the new entry. */
            {
                tab_prefixof(ps, code) = (unsigned short)oldcode;
                tab_suffixof(ps, code) = (Byte)finchar;
                free_ent = code+1;
            }

            oldcode = incode;   /* Remember previous code.  */
        }

        ps->bytes_in += rsize;
    }
    while (rsize > 0);

    if (outpos > 0 && (ctxt->writer)(ps->outbuf, outpos, ctxt->rwCtxt) != outpos)
    {
        return CMP_WRITE_ERROR;
    }

    return CMP_OK;
}
