
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ncompress42.h>

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


//======================================================================


typedef struct ctxt1
{
    size_t  insize;
    size_t  outsize;

    size_t  inoff;
    size_t  outoff;

    Byte    inbuf[1024];
    Byte    outbuf[8192];
} Ctxt1;



static void
initCtxt1(Ctxt1* ctxt)
{
    ctxt->insize  = 1024;
    ctxt->outsize = 8192;

    ctxt->inoff  = 0;
    ctxt->outoff = 0;
}



static int
reader1(Byte* bytes, size_t numBytes, void* ctxt)
{
    Ctxt1* c1  = (Ctxt1*)ctxt;
    size_t num = numBytes;
    size_t avail = c1->insize - c1->inoff;

    if (num > avail)
    {
        num = avail;
    }

    memcpy(bytes, c1->inbuf + c1->inoff, num);
    c1->inoff += num;
    return num;
}



static int
writer1(const Byte* bytes, size_t numBytes, void* ctxt)
{
    Ctxt1* c1  = (Ctxt1*)ctxt;
    size_t num = numBytes;
    size_t room = c1->outsize - c1->outoff;

    if (num > room)
    {
        num = room;
    }

    memcpy(c1->outbuf + c1->outoff, bytes, num);
    c1->outoff += num;
    return num;
}



static void
testCompr1()
{
    int   ok;
    Ctxt1 comprCtxt;
    Ctxt1 decompCtxt;
    CompressCtxt cc;
    CompressCtxt dc;
    CompressError err;

    cc.reader = reader1;
    cc.writer = writer1;
    cc.rwCtxt = &comprCtxt;

    dc.reader = reader1;
    dc.writer = writer1;
    dc.rwCtxt = &decompCtxt;

    initCtxt1(&comprCtxt);
    initCtxt1(&decompCtxt);

    initCompress(&cc, 0);
    initCompress(&dc, 0);

    fillBuf(comprCtxt.inbuf, comprCtxt.insize);
    clearBuf(comprCtxt.outbuf, comprCtxt.outsize);

    err = compress(&cc);
    ASSERT(err == CMP_OK);

    // Try decompressing
    decompCtxt.insize = comprCtxt.outoff;
    memcpy(decompCtxt.inbuf, comprCtxt.outbuf, comprCtxt.outoff);

    err = decompress(&dc);
    ASSERT(err == CMP_OK);

    ok = decompCtxt.outoff == comprCtxt.inoff;

    if (ok)
    {
        ok = (memcmp(comprCtxt.inbuf, decompCtxt.outbuf, decompCtxt.outoff) == 0);
    }

    printf("%s\n", ok? "Passed" : "Failed");

    freeCompress(&cc);
    freeCompress(&dc);
}



//======================================================================

int
main(int argc, char** argv)
{
    testCompr1();
}
