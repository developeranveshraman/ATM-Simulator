/*
 * receipt.c — PNG receipt image generator for the ATM Simulator.
 *
 * Renders transaction details onto a white bitmap using an 8x8 bitmap font
 * at 2× scale, then saves the result as a PNG via png_writer.
 */

#include "receipt.h"
#include "png_writer.h"
#include "font8x8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

/* ── Layout constants ───────────────────────────────────────────────────── */

#define RECEIPT_IMG_W  640                          /* image width  (px)   */
#define RECEIPT_IMG_H  600                          /* image height (px)   */
#define FONT_SCALE     2                            /* each font pixel → 2×2 */
#define CHAR_W         (8 * FONT_SCALE)             /* 16 px per char      */
#define CHAR_H         (8 * FONT_SCALE)             /* 16 px per char      */
#define LINE_H         20                           /* px per text line    */
#define MARGIN_X       (2 * CHAR_W)                 /* 32 px left margin   */
#define CHARS_PER_LINE ((RECEIPT_IMG_W - 2 * MARGIN_X) / CHAR_W)  /* 36 chars */

/* ── Static helpers ─────────────────────────────────────────────────────── */

/*
 * draw_char — render a single 8×8 glyph (scaled 2×) into the pixel buffer.
 *
 * px, py  : top-left corner in pixel coordinates.
 * ch      : ASCII character to draw.
 * r, g, b : foreground colour.
 */
static void draw_char(unsigned char *pixels, int px, int py,
                       char ch, unsigned char r, unsigned char g, unsigned char b)
{
    unsigned char idx = (unsigned char)ch;
    if (idx >= 128) return;                         /* only basic ASCII */

    for (int row = 0; row < 8; row++) {
        unsigned char row_byte = font8x8_basic[idx][row];
        for (int col = 0; col < 8; col++) {
            if (!(row_byte & (1 << col))) continue; /* bit not set → skip */

            /* Fill a FONT_SCALE × FONT_SCALE block */
            int bx = px + col * FONT_SCALE;
            int by = py + row * FONT_SCALE;

            for (int sy = 0; sy < FONT_SCALE; sy++) {
                for (int sx = 0; sx < FONT_SCALE; sx++) {
                    int fx = bx + sx;
                    int fy = by + sy;
                    if (fx < 0 || fx >= RECEIPT_IMG_W || fy < 0 || fy >= RECEIPT_IMG_H)
                        continue;                   /* bounds check */
                    int offset = (fy * RECEIPT_IMG_W + fx) * 3;
                    pixels[offset    ] = r;
                    pixels[offset + 1] = g;
                    pixels[offset + 2] = b;
                }
            }
        }
    }
}

/*
 * draw_text — draw a string at character grid position (x, y).
 *
 * x : column index (0-based, within printable area).
 * y : line index   (0-based).
 */
static void draw_text(unsigned char *pixels, int x, int y,
                       const char *text,
                       unsigned char r, unsigned char g, unsigned char b)
{
    int px = MARGIN_X + x * CHAR_W;
    int py = 20 + y * LINE_H;                       /* 20 px top margin */

    for (int i = 0; text[i] != '\0'; i++) {
        draw_char(pixels, px + i * CHAR_W, py, text[i], r, g, b);
    }
}

/*
 * draw_text_center — draw a string centred on a given line.
 */
static void draw_text_center(unsigned char *pixels, int y,
                              const char *text,
                              unsigned char r, unsigned char g, unsigned char b)
{
    int len = (int)strlen(text);
    int x = (CHARS_PER_LINE - len) / 2;
    if (x < 0) x = 0;
    draw_text(pixels, x, y, text, r, g, b);
}

/*
 * draw_hline — draw a horizontal separator made of '=' across the full
 *              printable width at the given line.
 */
static void draw_hline(unsigned char *pixels, int y,
                        unsigned char r, unsigned char g, unsigned char b)
{
    char buf[CHARS_PER_LINE + 1];
    memset(buf, '=', CHARS_PER_LINE);
    buf[CHARS_PER_LINE] = '\0';
    draw_text(pixels, 0, y, buf, r, g, b);
}

/*
 * draw_dashed — draw a dashed separator made of '-' across the full
 *               printable width at the given line.
 */
static void draw_dashed(unsigned char *pixels, int y,
                         unsigned char r, unsigned char g, unsigned char b)
{
    char buf[CHARS_PER_LINE + 1];
    memset(buf, '-', CHARS_PER_LINE);
    buf[CHARS_PER_LINE] = '\0';
    draw_text(pixels, 0, y, buf, r, g, b);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * receipt_generate — render a full transaction receipt and save it as PNG.
 *
 * Returns 0 on success, -1 on error.
 */
int receipt_generate(const ReceiptInfo *info)
{
    /* Ensure the output directory exists */
    mkdir("Receipt", 0755);

    /* Allocate the RGB pixel buffer */
    size_t buf_size = (size_t)RECEIPT_IMG_W * RECEIPT_IMG_H * 3;
    unsigned char *pixels = (unsigned char *)malloc(buf_size);
    if (!pixels) {
        fprintf(stderr, "[receipt] Failed to allocate pixel buffer.\n");
        return -1;
    }

    /* Fill with white background */
    memset(pixels, 255, buf_size);

    /* ── Draw receipt content (all text in black) ───────────────────────── */

    /* Line 0 : blank */

    /* Line 1 : heavy separator */
    draw_hline(pixels, 1, 0, 0, 0);

    /* Lines 2–3 : bank name & terminal */
    draw_text_center(pixels, 2, "GLOBAL DIGITAL BANK", 0, 0, 0);
    draw_text_center(pixels, 3, "ATM TERMINAL #004",   0, 0, 0);

    /* Line 4 : heavy separator */
    draw_hline(pixels, 4, 0, 0, 0);

    /* Line 5 : blank */

    /* Lines 6–8 : transaction metadata */
    {
        char line[64];
        snprintf(line, sizeof(line), "DATE: %s", info->date);
        draw_text(pixels, 0, 6, line, 0, 0, 0);

        snprintf(line, sizeof(line), "TIME: %s", info->time_str);
        draw_text(pixels, 0, 7, line, 0, 0, 0);

        snprintf(line, sizeof(line), "TXN:  %s", info->txn_id);
        draw_text(pixels, 0, 8, line, 0, 0, 0);
    }

    /* Line 9  : blank */

    /* Line 10 : dashed separator */
    draw_dashed(pixels, 10, 0, 0, 0);

    /* Lines 11–12 : card & account holder */
    {
        char line[96];
        snprintf(line, sizeof(line), "CARD:  %s", info->card_masked);
        draw_text(pixels, 0, 11, line, 0, 0, 0);

        snprintf(line, sizeof(line), "NAME:  %s", info->name);
        draw_text(pixels, 0, 12, line, 0, 0, 0);
    }

    /* Line 13 : dashed separator */
    draw_dashed(pixels, 13, 0, 0, 0);

    /* Line 14 : blank */

    /* Lines 15–18 : transaction details */
    {
        char line[96];
        snprintf(line, sizeof(line), "TYPE:    %s", info->txn_type);
        draw_text(pixels, 0, 15, line, 0, 0, 0);

        snprintf(line, sizeof(line), "AMOUNT:  $%.2f", info->amount);
        draw_text(pixels, 0, 16, line, 0, 0, 0);

        snprintf(line, sizeof(line), "BALANCE: $%.2f", info->balance);
        draw_text(pixels, 0, 17, line, 0, 0, 0);

        snprintf(line, sizeof(line), "STATUS:  %s", info->status);
        draw_text(pixels, 0, 18, line, 0, 0, 0);
    }

    /* Line 19 : blank */

    /* Line 20 : dashed separator */
    draw_dashed(pixels, 20, 0, 0, 0);

    /* Line 21 : blank */

    /* Lines 22–23 : thank-you message */
    draw_text_center(pixels, 22, "THANK YOU FOR BANKING", 0, 0, 0);
    draw_text_center(pixels, 23, "WITH US!",              0, 0, 0);

    /* Line 24 : blank */

    /* Line 25 : heavy separator */
    draw_hline(pixels, 25, 0, 0, 0);

    /* ── Save PNG ───────────────────────────────────────────────────────── */

    /* Build filename from current wall-clock time */
    char filename[128];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    snprintf(filename, sizeof(filename),
             "Receipt/receipt_%04d%02d%02d_%02d%02d%02d.png",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

    int rc = png_save(filename, pixels, RECEIPT_IMG_W, RECEIPT_IMG_H);
    free(pixels);

    if (rc != 0) {
        fprintf(stderr, "[receipt] Failed to write PNG: %s\n", filename);
        return -1;
    }

    printf("Receipt saved: %s\n", filename);
    return 0;
}
