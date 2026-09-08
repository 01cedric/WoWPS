/* uncompr.c -- decompress a memory buffer
 *
 * The zlib 1.2.5 copy vendored under extern/StormLib/src/zlib ships without
 * uncompr.c (StormLib never calls uncompress()), but the WoWee client does
 * (src/game/entity_controller.cpp, movement_handler.cpp). This is the
 * uncompress() of zlib 1.2.5 for the wowee_ps4_zlib target, so the vendored
 * tree stays untouched. Semantics match zlib 1.2.5: *destLen is the capacity
 * on entry and the produced size on return; truncated input yields
 * Z_DATA_ERROR, a too-small output buffer yields Z_BUF_ERROR.
 *
 * Copyright (C) 1995-2003, 2010 Jean-loup Gailly.
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#define ZLIB_INTERNAL
#include "zlib.h"

int ZEXPORT uncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen)
{
    z_stream stream;
    int err;

    stream.next_in = (Bytef*)source;
    stream.avail_in = (uInt)sourceLen;
    /* Check for source > 64K on 16-bit machine: */
    if ((uLong)stream.avail_in != sourceLen) return Z_BUF_ERROR;

    stream.next_out = dest;
    stream.avail_out = (uInt)*destLen;
    if ((uLong)stream.avail_out != *destLen) return Z_BUF_ERROR;

    stream.zalloc = (alloc_func)0;
    stream.zfree = (free_func)0;
    stream.opaque = (voidpf)0;

    err = inflateInit(&stream);
    if (err != Z_OK) return err;

    err = inflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        inflateEnd(&stream);
        if (err == Z_NEED_DICT || (err == Z_BUF_ERROR && stream.avail_in == 0))
            return Z_DATA_ERROR;
        return err;
    }
    *destLen = stream.total_out;

    err = inflateEnd(&stream);
    return err;
}
