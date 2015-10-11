
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <ncompress42.h>

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
fillRandom(Byte* buffer, size_t num)
{
    for (size_t i = 0; i < num; ++i)
    {
        buffer[i] = random() & 0xff;
    }
}



static void
fillUniform(Byte* buffer, size_t num, Byte b)
{
    for (size_t i = 0; i < num; ++i)
    {
        buffer[i] = b;
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

typedef struct ctxt
{
    size_t  inSize;
    size_t  outSize;

    size_t  inOff;
    size_t  outOff;

    Byte*   inBuf;
    Byte*   outBuf;
} Ctxt;



static void
initCtxt(Ctxt* ctxt, size_t inSize)
{
    ctxt->inSize  = inSize;
    ctxt->outSize = inSize * 4;

    ctxt->inOff  = 0;
    ctxt->outOff = 0;

    ctxt->inBuf  = (Byte*)malloc(ctxt->inSize);
    ctxt->outBuf = (Byte*)malloc(ctxt->outSize);
}



static void
freeCtxt(Ctxt* ctxt)
{
    if (ctxt->inBuf)
    {
        free(ctxt->inBuf);
        ctxt->inBuf = 0;
    }

    if (ctxt->outBuf)
    {
        free(ctxt->outBuf);
        ctxt->outBuf = 0;
    }
}



static int
reader(Byte* bytes, size_t numBytes, void* ctxt)
{
    Ctxt*  c1    = (Ctxt*)ctxt;
    size_t num   = numBytes;
    size_t avail = c1->inSize - c1->inOff;

    if (num > avail)
    {
        num = avail;
    }

    memcpy(bytes, c1->inBuf + c1->inOff, num);
    c1->inOff += num;
    return num;
}



static int
writer(const Byte* bytes, size_t numBytes, void* ctxt)
{
    Ctxt*  c1   = (Ctxt*)ctxt;
    size_t num  = numBytes;
    size_t room = c1->outSize - c1->outOff;

    if (num > room)
    {
        num = room;
    }

    memcpy(c1->outBuf + c1->outOff, bytes, num);
    c1->outOff += num;
    return num;
}


//======================================================================


static int
testAgainstCompress(
    size_t      tnum,
    const Byte* data,
    size_t      inSize,
    const char* inpath,
    const char* mypath,
    const char* outpath,
    const char* title
    )
{
    // Return true if the test passed
    char    cmd[1000];
    int     ok = 0;
    Ctxt    comprCtxt;

    CompressCtxt cc;
    CompressCtxt dc;
    CompressError err;

    Byte    *result;
    size_t  resultSize;

    cc.reader = reader;
    cc.writer = writer;
    cc.rwCtxt = &comprCtxt;

    initCtxt(&comprCtxt, inSize);
    memcpy(comprCtxt.inBuf, data, inSize);

    initCompress(&cc, 0);

    err = compress(&cc);
    ASSERT(err == CMP_OK);

    putFile(comprCtxt.inBuf, comprCtxt.inSize, inpath);

    sprintf(cmd, "compress -c %s > %s", inpath, outpath);
    ASSERT(system(cmd) == 0);

    getFile(&result, &resultSize, outpath);

    printf("%d: %s Test Against Compress, size = %d\n", tnum, title, inSize);
    printf("    Original size %d\n", inSize);
    printf("    My compressed size %d\n", comprCtxt.outOff);
    printf("    Their compressed size %d\n", resultSize);

    ok = resultSize == comprCtxt.outOff;

    if (ok)
    {
        ok = memcmp(comprCtxt.outBuf, result, resultSize) == 0;
    }

    printf("    %s\n", ok? "Passed" : "Failed");

    if (ok)
    {
        deleteFile(inpath);
        deleteFile(outpath);
    }
    else
    {
        // Save my compressed data
        putFile(comprCtxt.inBuf, comprCtxt.inSize, mypath);
    }

    freeCtxt(&comprCtxt);
    freeCompress(&cc);

    return ok;
}


//======================================================================

/*  Be careful to run the tests from a fixed initial state in the
    random number generator for repeatability.
*/

int
randomTests()
{
    size_t sz   = 1000;
    size_t step = 139;
    size_t num  = 200;
    int    ok   = 1;

    char    inpath[100];
    char    mypath[100];
    char    outpath[100];

    for (size_t i = 1;  i <= num; ++i)
    {
        Ctxt ctxt;

        initCtxt(&ctxt, sz);
        fillRandom(ctxt.inBuf, ctxt.inSize);

        sprintf(inpath,  "random_input_%d", i);
        sprintf(mypath,  "random_my_output_%d", i);
        sprintf(outpath, "random_output_%d", i);

        ok &= testAgainstCompress(i, ctxt.inBuf, ctxt.inSize, inpath, mypath, outpath, "Random");
        sz += step;

        freeCtxt(&ctxt);
    }

    if (!ok)
    {
        printf("Some tests failed\n");
    }

    return ok;
}




int
uniformTests()
{
    size_t sz   = 1000;
    size_t step = 139;
    size_t num  = 200;
    int    ok   = 1;

    char    inpath[100];
    char    mypath[100];
    char    outpath[100];

    for (size_t i = 1;  i <= num; ++i)
    {
        Ctxt ctxt;

        initCtxt(&ctxt, sz);
        fillUniform(ctxt.inBuf, ctxt.inSize, (i * 3) & 0xff);

        sprintf(inpath,  "random_input_%d", i);
        sprintf(mypath,  "random_my_output_%d", i);
        sprintf(outpath, "random_output_%d", i);

        ok &= testAgainstCompress(i, ctxt.inBuf, ctxt.inSize, inpath, mypath, outpath, "Uniform");
        sz += step;

        freeCtxt(&ctxt);
    }

    if (!ok)
    {
        printf("Some tests failed\n");
    }

    return ok;
}






int
main(int argc, char** argv)
{
    int ok = 1;

    ok &= randomTests();
    ok &= uniformTests();

    exit(ok? 0 : 1);
}
