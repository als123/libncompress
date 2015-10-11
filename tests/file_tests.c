
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../compress42.h"

/*  In these tests we write a buffer to a file so that we can compare
    it with the results from the original compress program.
*/

//======================================================================

static void
reportAssert(int b, int line, const char* msg)
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


#define ASSERT(test)          reportAssert(test, __LINE__, "")
#define ASSERT_MSG(test, msg) reportAssert(test, __LINE__, msg)



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
putFile(const Byte* buffer, size_t num, const char* path)
{
    FILE* fp = fopen(path, "w");
    fwrite(buffer, num, sizeof(Byte), fp);
    fclose(fp);
}



static void
getFile(Byte** buffer, size_t* num, const char* path)
{
    // Allocate a buffer for the file
    // We assume that the file doesn't change while reading it.
    FILE* fp = fopen(path, "r");
    long  sz;

    fseek(fp, 0, SEEK_END);

    *num    = ftell(fp);
    *buffer = (Byte*)malloc(sz);

    fseek(fp, 0, SEEK_SET);
    fread(*buffer, *num, sizeof(Byte), fp);
    fclose(fp);
}



static void
deleteFile(const char* path)
{
    unlink(path);
}


//======================================================================


static void
testAgainstCompress(
    size_t      tnum,
    const Byte* inbuf,
    size_t      insize,
    const char* inpath,
    const char* mypath,
    const char* outpath
    )
{
    char    cmd[1000];
    size_t  outsize = insize * 4;
    int     ok;

    Byte    *outbuf = (Byte*)malloc(outsize);
    Byte    *result;
    size_t  resultSize;
    size_t  lastout = 0;

    clearBuf(outbuf, outsize);

    cmp_error_code err;
    cmp_stream     strm;
    cmp_flush_code flush = CMP_FULL_FLUSH;

    err = compress_init(&strm, 0);
    ASSERT(err == CMP_OK);

    strm.avail_in = insize;
    strm.next_in  = inbuf;

    strm.avail_out = outsize;
    strm.next_out  = outbuf;

    for(;;)
    {
        err = compress(&strm, flush);

        if (err == CMP_STREAM_END)
        {
            break;
        }

        ASSERT(err == CMP_FINISH || err == CMP_OK);

        if (lastout == strm.total_out)
        {
            // Nothing more to produce, finish off
            flush = CMP_FINISH;
        }

        lastout = strm.total_out;
    }
    while (err != CMP_STREAM_END);

    putFile(inbuf, insize, inpath);

    sprintf(cmd, "compress -c %s > %s", inpath, outpath);
    ASSERT(system(cmd) == 0);

    getFile(&result, &resultSize, outpath);

    printf("%d: Random Test Against Compress, size = %d\n", tnum, insize);
    printf("    Original size %d\n", insize);
    printf("    My compressed size %d\n", strm.total_out);
    printf("    Their compressed size %d\n", resultSize);

    ok = resultSize == strm.total_out;

    if (ok)
    {
        ok = memcmp(outbuf, result, resultSize) == 0;
    }

    if (ok)
    {
        printf("    Passed\n");
    }
    else
    {
        printf("    Failed\n");
    }

    if (ok)
    {
        deleteFile(inpath);
        deleteFile(outpath);
    }
    else
    {
        // Save my compressed data
        putFile(outbuf, strm.total_out, mypath);
    }

    free(outbuf);
    free(result);
}



static void
testCompressRandomFile(
    size_t      tnum,
    const char* inpath,
    const char* mypath,
    const char* outpath,
    size_t      numBytes
    )
{
    Byte *inbuf = (Byte*)malloc(numBytes);

    fillBuf(inbuf, numBytes);
    testAgainstCompress(tnum, inbuf, numBytes, inpath, mypath, outpath);
    free(inbuf);
}



//======================================================================

/*  Be careful to run the tests from a fixed initial state in the
    random number generator for repeatability.
*/

int
main(int argc, char** argv)
{
    size_t sz   = 1000;
    size_t step = 139;
    size_t num  = 50;

    char    inpath[100];
    char    mypath[100];
    char    outpath[100];

    for (size_t i = 1;  i <= num; ++i)
    {
        sprintf(inpath,  "test_input_%d", i);
        sprintf(mypath,  "test_my_output_%d", i);
        sprintf(outpath, "test_output_%d", i);

        if (i == 50)
        {
            printf("reached the break point\n");
        }

        testCompressRandomFile(i, inpath, mypath, outpath, sz);
        sz += step;
    }
}
