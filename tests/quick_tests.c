
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../compress42.h"

//======================================================================

static void
report_assert(int b, int line, const char* msg)
{
    if (!msg)
    {
        msg = "";
    }

    if (!b)
    {
        fprintf(stderr, "Asertion failure: line %d %s\n", line, msg);
    }
}


#define ASSERT(test)          report_assert(test, __LINE__, "")
#define ASSERT_MSG(test, msg) report_assert(test, __LINE__, msg)



static void
fillBuf(Byte* buffer, size_t num)
{
    for (size_t i = 0; i < num; ++i)
    {
        buffer[i] = random() & 0xff;
    }
}


static void
clearBuf(Byte* buffer, size_t num)
{
    memset(buffer, 0, num);
}



static void
testComprInit()
{
    cmp_error_code err;

    cmp_stream strm;

    err = compress_init(&strm, 0);

    ASSERT(err == CMP_OK);

    compress_free(&strm);
}




static void
testCompr1()
{
    Byte    inbuf[1024];
    Byte    outbuf[8192];

    size_t  insize  = 1024;
    size_t  outsize = 8192;

    cmp_error_code err;
    cmp_stream     strm;

    err = compress_init(&strm, 0);
    ASSERT(err == CMP_OK);

    fillBuf(inbuf, insize);
    clearBuf(outbuf, outsize);

    strm.avail_in = insize;
    strm.next_in  = inbuf;

    strm.avail_out = outsize;
    strm.next_out  = outbuf;

    err = compress(&strm, CMP_FULL_FLUSH);
    ASSERT(err == CMP_OK);

    compress_free(&strm);
}



//======================================================================

int
main(int argc, char** argv)
{
    testComprInit();
    testCompr1();
}
