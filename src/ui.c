/*
 * ui.c – Terminal UI engine for the ATM Simulator
 *
 * Provides raw-mode terminal input, box-drawing helpers, menus,
 * digit/amount input fields, message screens, and ANSI colour helpers.
 * Every visual element is rendered inside a fixed-width box of BOX_W (51)
 * characters built from '*', '-', and spaces.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>

#include "ui.h"

/* ------------------------------------------------------------------ */
/*  Static state                                                       */
/* ------------------------------------------------------------------ */

static struct termios orig_termios;  /* saved terminal attributes      */
static int            cleaned_up = 0; /* guard against double-cleanup  */

/* ------------------------------------------------------------------ */
/*  1. ui_init – switch to raw mode, register cleanup                  */
/* ------------------------------------------------------------------ */

void ui_init(void)
{
    struct termios raw;

    /* Save the original terminal settings */
    tcgetattr(STDIN_FILENO, &orig_termios);

    raw = orig_termios;

    /* Disable canonical mode, echo, and signal generation */
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);

    /* Read returns after 1 byte, no timeout */
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    /* Ensure cleanup runs on normal exit */
    atexit(ui_cleanup);
}

/* ------------------------------------------------------------------ */
/*  2. ui_cleanup – restore original terminal settings                 */
/* ------------------------------------------------------------------ */

void ui_cleanup(void)
{
    if (cleaned_up) return;
    cleaned_up = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\033[0m");   /* reset colours */
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  3. ui_clear – clear screen and home cursor                         */
/* ------------------------------------------------------------------ */

void ui_clear(void)
{
    printf("\033[2J\033[H");
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  4. ui_read_key – read a keypress, decode escape sequences          */
/* ------------------------------------------------------------------ */

int ui_read_key(void)
{
    unsigned char c;

    if (read(STDIN_FILENO, &c, 1) != 1)
        return -1;

    /* Enter / Return */
    if (c == '\n' || c == '\r')
        return KEY_ENTER;

    /* Escape – could be ESC alone or an arrow-key sequence */
    if (c == 27) {
        unsigned char seq[2];

        /* Try to read the next byte */
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return KEY_ESC;

        if (seq[0] == '[') {
            if (read(STDIN_FILENO, &seq[1], 1) != 1)
                return KEY_ESC;

            switch (seq[1]) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                default:  return KEY_ESC;
            }
        }

        return KEY_ESC;
    }

    /* Backspace (127 on most terminals, 8 on some) */
    if (c == 127 || c == 8)
        return KEY_BACKSPACE;

    /* Everything else – return the character value */
    return (int)c;
}

/* ------------------------------------------------------------------ */
/*  5. ui_box_top – top border row                                     */
/* ------------------------------------------------------------------ */

void ui_box_top(void)
{
    for (int i = 0; i < BOX_W; i++)
        putchar('*');
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/*  6. ui_box_bottom – bottom border row (identical to top)            */
/* ------------------------------------------------------------------ */

void ui_box_bottom(void)
{
    for (int i = 0; i < BOX_W; i++)
        putchar('*');
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/*  7. ui_box_line – left-aligned text row inside the box              */
/* ------------------------------------------------------------------ */

void ui_box_line(const char *text)
{
    int len = (int)strlen(text);

    putchar('*');

    if (len >= BOX_INNER) {
        /* Truncate to fit */
        for (int i = 0; i < BOX_INNER; i++)
            putchar(text[i]);
    } else {
        /* Print text then pad with spaces */
        printf("%s", text);
        for (int i = len; i < BOX_INNER; i++)
            putchar(' ');
    }

    putchar('*');
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/*  8. ui_box_center – centre-aligned text row inside the box          */
/* ------------------------------------------------------------------ */

void ui_box_center(const char *text)
{
    int len = (int)strlen(text);

    putchar('*');

    if (len >= BOX_INNER) {
        /* Truncate to fit */
        for (int i = 0; i < BOX_INNER; i++)
            putchar(text[i]);
    } else {
        int left_pad  = (BOX_INNER - len) / 2;
        int right_pad = BOX_INNER - len - left_pad;

        for (int i = 0; i < left_pad; i++)
            putchar(' ');
        printf("%s", text);
        for (int i = 0; i < right_pad; i++)
            putchar(' ');
    }

    putchar('*');
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/*  9. ui_box_separator – dashed separator row                         */
/* ------------------------------------------------------------------ */

void ui_box_separator(void)
{
    putchar('*');
    for (int i = 0; i < BOX_INNER; i++)
        putchar('-');
    putchar('*');
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/* 10. ui_box_empty – blank row inside the box                         */
/* ------------------------------------------------------------------ */

void ui_box_empty(void)
{
    putchar('*');
    for (int i = 0; i < BOX_INNER; i++)
        putchar(' ');
    putchar('*');
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/* 11. ui_menu – interactive menu with arrow-key navigation            */
/* ------------------------------------------------------------------ */

int ui_menu(const char *title, const char *subtitle,
            const char **items, int count, int initial)
{
    int selected = initial;

    for (;;) {
        /* ---- redraw ---- */
        ui_clear();
        ui_box_top();
        ui_box_center(title);

        if (subtitle && subtitle[0] != '\0')
            ui_box_center(subtitle);

        ui_box_separator();
        ui_box_line("  Use [UP/DOWN] Arrow Keys & [ENTER]");
        ui_box_empty();

        /* Menu items */
        for (int i = 0; i < count; i++) {
            char line[256];

            if (i == selected)
                snprintf(line, sizeof(line), "  >> [%d] %s  <<", i + 1, items[i]);
            else
                snprintf(line, sizeof(line), "     [%d] %s", i + 1, items[i]);

            ui_box_line(line);
        }

        ui_box_empty();
        ui_box_separator();
        ui_box_line("  [ENTER] Select  |  [ESC] Back");
        ui_box_bottom();
        fflush(stdout);

        /* ---- handle input ---- */
        int key = ui_read_key();

        switch (key) {
            case KEY_UP:
                selected = (selected - 1 + count) % count;
                break;
            case KEY_DOWN:
                selected = (selected + 1) % count;
                break;
            case KEY_ENTER:
                return selected;
            case KEY_ESC:
                return -1;
            default:
                break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 12. ui_input_digits – fixed-length numeric input (optionally masked)*/
/* ------------------------------------------------------------------ */

int ui_input_digits(const char *prompt, char *buf, int count, int masked)
{
    int pos = 0;              /* how many digits entered so far */
    memset(buf, 0, (size_t)(count + 1));

    for (;;) {
        /* ---- redraw ---- */
        ui_clear();
        ui_box_top();
        ui_box_center(prompt);
        ui_box_separator();
        ui_box_empty();

        /* Build the input field string, e.g. "  Input: [ 1 2 _ _ ]" */
        char field[256];
        int  off = 0;

        off += snprintf(field + off, sizeof(field) - (size_t)off, "  Input: [ ");
        for (int i = 0; i < count; i++) {
            if (i > 0)
                off += snprintf(field + off, sizeof(field) - (size_t)off, " ");

            if (i < pos)
                off += snprintf(field + off, sizeof(field) - (size_t)off,
                                "%c", masked ? '*' : buf[i]);
            else
                off += snprintf(field + off, sizeof(field) - (size_t)off, "_");
        }
        snprintf(field + off, sizeof(field) - (size_t)off, " ]");

        ui_box_line(field);
        ui_box_empty();
        ui_box_separator();
        ui_box_line("  Type digits | [BACKSPACE] Del | [ESC] Cancel");
        ui_box_bottom();
        fflush(stdout);

        /* ---- handle input ---- */
        int key = ui_read_key();

        if (key == KEY_ESC) {
            buf[0] = '\0';
            return 0;
        } else if (key == KEY_BACKSPACE) {
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
            }
        } else if (key >= '0' && key <= '9' && pos < count) {
            buf[pos] = (char)key;
            pos++;

            /* Auto-confirm once all digits are entered */
            if (pos == count) {
                buf[pos] = '\0';
                return 1;
            }
        }
        /* Ignore all other keys */
    }
}

/* ------------------------------------------------------------------ */
/* 13. ui_input_amount – free-form dollar amount input                 */
/* ------------------------------------------------------------------ */

int ui_input_amount(const char *prompt, double *amount)
{
    char  buf[64];
    int   pos     = 0;
    int   has_dot = 0;

    memset(buf, 0, sizeof(buf));

    for (;;) {
        /* ---- redraw ---- */
        ui_clear();
        ui_box_top();
        ui_box_center(prompt);
        ui_box_separator();
        ui_box_empty();

        /* Build the amount display, e.g. "  Amount: $ [ 123.45     ]" */
        char field[256];
        int  max_digits = 10;  /* max visible characters in bracket */

        snprintf(field, sizeof(field), "  Amount: $ [ ");
        int flen = (int)strlen(field);

        /* Append typed characters */
        for (int i = 0; i < pos && i < max_digits; i++)
            field[flen++] = buf[i];

        /* Pad remaining space */
        for (int i = pos; i < max_digits; i++)
            field[flen++] = ' ';

        field[flen++] = ']';
        field[flen]   = '\0';

        ui_box_line(field);
        ui_box_empty();
        ui_box_separator();
        ui_box_line("  [ENTER] Confirm  |  [ESC] Cancel");
        ui_box_bottom();
        fflush(stdout);

        /* ---- handle input ---- */
        int key = ui_read_key();

        if (key == KEY_ENTER) {
            if (pos > 0) {
                buf[pos] = '\0';
                *amount = atof(buf);
                return 1;
            }
            /* Ignore ENTER when nothing typed */
        } else if (key == KEY_ESC) {
            return 0;
        } else if (key == KEY_BACKSPACE) {
            if (pos > 0) {
                pos--;
                if (buf[pos] == '.')
                    has_dot = 0;
                buf[pos] = '\0';
            }
        } else if (key == '.' && !has_dot && pos > 0 && pos < (int)sizeof(buf) - 2) {
            /* Allow one dot, but not as the first character */
            buf[pos] = '.';
            pos++;
            has_dot = 1;
        } else if (key >= '0' && key <= '9' && pos < (int)sizeof(buf) - 2) {
            buf[pos] = (char)key;
            pos++;
        }
        /* Ignore everything else */
    }
}

/* ------------------------------------------------------------------ */
/* 14. ui_message – display a one- or two-line message screen          */
/* ------------------------------------------------------------------ */

void ui_message(const char *line1, const char *line2, int wait_for_key)
{
    ui_clear();
    ui_box_top();
    ui_box_empty();

    /* Line 1 in bold */
    ui_color_bold();
    ui_box_center(line1);
    ui_color_reset();

    ui_box_empty();

    if (line2 && line2[0] != '\0')
        ui_box_center(line2);

    ui_box_empty();
    ui_box_separator();

    if (wait_for_key) {
        ui_box_line("  Press any key to continue...");
        ui_box_bottom();
        fflush(stdout);
        ui_read_key();
    } else {
        ui_box_empty();
        ui_box_bottom();
        fflush(stdout);
        usleep(2000000);   /* 2 seconds */
    }
}

/* ------------------------------------------------------------------ */
/* 15. ANSI colour helpers                                             */
/* ------------------------------------------------------------------ */

void ui_color_green(void)  { printf("\033[1;32m"); }
void ui_color_yellow(void) { printf("\033[1;33m"); }
void ui_color_red(void)    { printf("\033[1;31m"); }
void ui_color_cyan(void)   { printf("\033[1;36m"); }
void ui_color_bold(void)   { printf("\033[1m");    }
void ui_color_reset(void)  { printf("\033[0m");    }
