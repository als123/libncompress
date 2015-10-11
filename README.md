
# libncompress

The venerable UNIX `compress` command dates back to at least the
early 1980s. It is well obsolete but sometimes you need to compress or
decompress using its algorithm.

The latest version of this command can be found in the `ncompress`
package, version 4.2.  However it does not provide a library with just
the compression code, such as the `zlib` does for the `gzip` command.

This library contains just the compression and decompression code from
the command.  The code has been repackaged with a simple API.
