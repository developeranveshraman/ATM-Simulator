/*
 * png_writer.c
 * Minimal PNG writer — produces UNCOMPRESSED PNG files from an RGB pixel buffer.
 * No external libraries required (zlib, libpng, etc.).
 *
 * Uses DEFLATE "stored" blocks inside the IDAT chunk so the implementation
 * is straightforward at the cost of larger file sizes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png_writer.h"

/* ------------------------------------------------------------------ */
/*  CRC-32 (ISO 3309 / PNG spec, polynomial 0xEDB88320)              */
/* ------------------------------------------------------------------ */

static unsigned long crc_table[256];
static int           crc_table_ready = 0;

/* Build the CRC-32 lookup table (called once). */
static void crc_table_init(void)
{
    unsigned long c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (unsigned long)n;
        for (k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xEDB88320UL ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc_table[n] = c;
    }
    crc_table_ready = 1;
}

/* ------------------------------------------------------------------ */
/*  Adler-32 checksum (used inside the zlib wrapper)                  */
/* ------------------------------------------------------------------ */

static unsigned long adler32_buf(const unsigned char *buf, size_t len)
{
    unsigned long a = 1, b = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        a = (a + buf[i]) % 65521;
        b = (b + a)      % 65521;
    }
    return (b << 16) | a;
}

/* ------------------------------------------------------------------ */
/*  Helper: write a 32-bit value in big-endian order                  */
/* ------------------------------------------------------------------ */

static void write_be32(unsigned char *dst, unsigned long val)
{
    dst[0] = (unsigned char)((val >> 24) & 0xFF);
    dst[1] = (unsigned char)((val >> 16) & 0xFF);
    dst[2] = (unsigned char)((val >>  8) & 0xFF);
    dst[3] = (unsigned char)((val      ) & 0xFF);
}

/* ------------------------------------------------------------------ */
/*  Helper: write a complete PNG chunk to a FILE*                     */
/*  Chunk layout: length(4B BE) | type(4B) | data | CRC32(4B BE)     */
/*  CRC32 is computed over type + data.                               */
/* ------------------------------------------------------------------ */

static int write_chunk(FILE *fp, const char *type,
                       const unsigned char *data, unsigned long data_len)
{
    unsigned char header[8];
    unsigned char crc_bytes[4];
    unsigned long crc;

    /* Length (4 bytes, big-endian) */
    write_be32(header, data_len);

    /* Type (4 ASCII bytes) */
    memcpy(header + 4, type, 4);

    if (fwrite(header, 1, 8, fp) != 8)
        return -1;

    /* Data */
    if (data_len > 0 && fwrite(data, 1, data_len, fp) != data_len)
        return -1;

    /* CRC-32 over type + data */
    crc = 0xFFFFFFFFUL;
    if (!crc_table_ready)
        crc_table_init();
    {
        size_t i;
        /* Hash the 4 type bytes */
        for (i = 0; i < 4; i++)
            crc = crc_table[(crc ^ (unsigned char)type[i]) & 0xFF] ^ (crc >> 8);
        /* Hash the data bytes */
        for (i = 0; i < data_len; i++)
            crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    crc ^= 0xFFFFFFFFUL;

    write_be32(crc_bytes, crc);
    if (fwrite(crc_bytes, 1, 4, fp) != 4)
        return -1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  png_save — write an RGB image as an uncompressed PNG              */
/* ------------------------------------------------------------------ */

int png_save(const char *filename, const unsigned char *pixels,
             int width, int height)
{
    /* PNG 8-byte signature */
    static const unsigned char png_sig[8] = {
        137, 80, 78, 71, 13, 10, 26, 10
    };

    FILE          *fp        = NULL;
    unsigned char *raw_rows  = NULL;   /* filter-byte-prefixed rows      */
    unsigned char *idat_buf  = NULL;   /* full IDAT data payload         */
    size_t         row_bytes;          /* bytes per filtered row          */
    size_t         raw_size;           /* total raw image bytes           */
    size_t         idat_size;          /* total IDAT data size            */
    int            ret       = -1;

    if (!filename || !pixels || width <= 0 || height <= 0)
        return -1;

    /* Ensure CRC table is ready */
    if (!crc_table_ready)
        crc_table_init();

    /* ---- Open output file ---- */
    fp = fopen(filename, "wb");
    if (!fp)
        return -1;

    /* ---- Write PNG signature ---- */
    if (fwrite(png_sig, 1, 8, fp) != 8)
        goto cleanup;

    /* ---- Write IHDR chunk ---- */
    {
        unsigned char ihdr[13];
        write_be32(ihdr + 0, (unsigned long)width);
        write_be32(ihdr + 4, (unsigned long)height);
        ihdr[8]  = 8;   /* bit depth          */
        ihdr[9]  = 2;   /* color type: RGB    */
        ihdr[10] = 0;   /* compression method */
        ihdr[11] = 0;   /* filter method      */
        ihdr[12] = 0;   /* interlace method   */
        if (write_chunk(fp, "IHDR", ihdr, 13) != 0)
            goto cleanup;
    }

    /* ---- Build raw image data (filter byte + row pixels) ---- */
    row_bytes = 1 + (size_t)width * 3;          /* 1 filter byte + RGB */
    raw_size  = row_bytes * (size_t)height;

    raw_rows = (unsigned char *)malloc(raw_size);
    if (!raw_rows)
        goto cleanup;

    {
        int y;
        for (y = 0; y < height; y++) {
            size_t offset = (size_t)y * row_bytes;
            raw_rows[offset] = 0;               /* filter = None */
            memcpy(raw_rows + offset + 1,
                   pixels + (size_t)y * (size_t)width * 3,
                   (size_t)width * 3);
        }
    }

    /* ---- Build IDAT payload (zlib wrapper + DEFLATE stored blocks) ---- */
    /*
     * Layout:
     *   [0x78, 0x01]                       zlib header (CMF, FLG)
     *   { DEFLATE stored blocks }          raw_size bytes in <=65535-byte blocks
     *   [adler32 big-endian 4 bytes]       zlib checksum
     *
     * Each DEFLATE stored block:
     *   1 byte  : BFINAL (1 for last) | 0x00
     *   2 bytes : LEN   (little-endian)
     *   2 bytes : NLEN  (~LEN, little-endian)
     *   LEN bytes of literal data
     */
    {
        size_t max_block   = 65535;
        size_t num_blocks  = (raw_size + max_block - 1) / max_block;
        size_t block_hdr   = 5;                 /* 1 + 2 + 2 per block */
        size_t zlib_hdr    = 2;
        size_t adler_sz    = 4;
        size_t remaining, src_off, dst_off;
        size_t b;
        unsigned long adler;

        idat_size = zlib_hdr + num_blocks * block_hdr + raw_size + adler_sz;
        idat_buf  = (unsigned char *)malloc(idat_size);
        if (!idat_buf)
            goto cleanup;

        /* Zlib header: CMF=0x78 (deflate, window 32K), FLG=0x01 */
        /* Check: (0x78 * 256 + 0x01) = 30721, 30721 % 31 == 0  */
        idat_buf[0] = 0x78;
        idat_buf[1] = 0x01;

        dst_off   = 2;
        src_off   = 0;
        remaining = raw_size;

        for (b = 0; b < num_blocks; b++) {
            size_t         block_len = (remaining > max_block) ? max_block : remaining;
            int            is_last   = (b == num_blocks - 1);
            unsigned short len_le    = (unsigned short)block_len;
            unsigned short nlen_le   = (unsigned short)(~len_le);

            /* BFINAL | BTYPE=00 (stored) */
            idat_buf[dst_off++] = is_last ? 0x01 : 0x00;

            /* LEN little-endian */
            idat_buf[dst_off++] = (unsigned char)( len_le       & 0xFF);
            idat_buf[dst_off++] = (unsigned char)((len_le >> 8) & 0xFF);

            /* NLEN little-endian */
            idat_buf[dst_off++] = (unsigned char)( nlen_le       & 0xFF);
            idat_buf[dst_off++] = (unsigned char)((nlen_le >> 8) & 0xFF);

            /* Literal data */
            memcpy(idat_buf + dst_off, raw_rows + src_off, block_len);
            dst_off   += block_len;
            src_off   += block_len;
            remaining -= block_len;
        }

        /* Adler-32 checksum over the raw (uncompressed) data, big-endian */
        adler = adler32_buf(raw_rows, raw_size);
        write_be32(idat_buf + dst_off, adler);
        dst_off += 4;

        /* Write the IDAT chunk */
        if (write_chunk(fp, "IDAT", idat_buf, (unsigned long)dst_off) != 0)
            goto cleanup;
    }

    /* ---- Write IEND chunk (empty) ---- */
    if (write_chunk(fp, "IEND", NULL, 0) != 0)
        goto cleanup;

    ret = 0;   /* success */

cleanup:
    if (fp)
        fclose(fp);
    free(raw_rows);
    free(idat_buf);
    return ret;
}
