/* =============================================================================
 * ATM SIMULATOR (SIMPLIFIED ALL-IN-ONE VERSION)
 * Built to be easily written, understood, and finished in under 2 hours.
 * Compile: gcc -O2 -Wall atm_simple.c -o atm_simple
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#define BOX_WIDTH 49
#define MAX_ACCOUNTS 50

/* ---------------------------------------------------------------------------
 * 1. DATA STRUCTURES & ACCOUNTS
 * --------------------------------------------------------------------------- */
typedef struct {
    char last4[5];
    char name[40];
    char pin[5];
    double balance;
    int is_blocked;
    int failed_attempts;
} Account;

static Account accounts[MAX_ACCOUNTS];
static int account_count = 0;

/* ---------------------------------------------------------------------------
 * 2. FAST JSON PARSER (Extracts fields without complex libraries)
 * --------------------------------------------------------------------------- */
static void load_accounts_from_json(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        printf("Error: Could not open %s\n", filepath);
        exit(1);
    }

    char line[256];
    Account current;
    memset(&current, 0, sizeof(Account));
    int in_account = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "{") && !strstr(line, "accounts")) {
            memset(&current, 0, sizeof(Account));
            in_account = 1;
        }

        char *p;
        if ((p = strstr(line, "\"last4\":"))) {
            sscanf(p, "\"last4\": \"%4[^\"]\"", current.last4);
        } else if ((p = strstr(line, "\"name\":"))) {
            sscanf(p, "\"name\": \"%39[^\"]\"", current.name);
        } else if ((p = strstr(line, "\"pin\":"))) {
            sscanf(p, "\"pin\": \"%4[^\"]\"", current.pin);
        } else if ((p = strstr(line, "\"balance\":"))) {
            sscanf(p, "\"balance\": %lf", &current.balance);
        } else if ((p = strstr(line, "\"is_blocked\":"))) {
            current.is_blocked = strstr(line, "true") != NULL;
        }

        if (strstr(line, "}") && in_account) {
            if (strlen(current.last4) == 4 && account_count < MAX_ACCOUNTS) {
                accounts[account_count++] = current;
            }
            in_account = 0;
        }
    }
    fclose(fp);
}

static void save_accounts_to_json(const char *filepath) {
    FILE *fp = fopen(filepath, "w");
    if (!fp) return;

    fprintf(fp, "{\n  \"accounts\": [\n");
    for (int i = 0; i < account_count; i++) {
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"last4\": \"%s\",\n", accounts[i].last4);
        fprintf(fp, "      \"name\": \"%s\",\n", accounts[i].name);
        fprintf(fp, "      \"pin\": \"%s\",\n", accounts[i].pin);
        fprintf(fp, "      \"balance\": %.2f,\n", accounts[i].balance);
        fprintf(fp, "      \"is_blocked\": %s,\n", accounts[i].is_blocked ? "true" : "false");
        fprintf(fp, "      \"failed_attempts\": %d\n", accounts[i].failed_attempts);
        fprintf(fp, "    }%s\n", (i == account_count - 1) ? "" : ",");
    }
    fprintf(fp, "  ]\n}\n");
    fclose(fp);
}

/* ---------------------------------------------------------------------------
 * 3. BINARY SEARCH (O(log N) Card Lookup)
 * --------------------------------------------------------------------------- */
static int compare_accounts(const void *a, const void *b) {
    return strcmp(((Account *)a)->last4, ((Account *)b)->last4);
}

static void sort_accounts(void) {
    qsort(accounts, account_count, sizeof(Account), compare_accounts);
}

static int binary_search_card(const char *last4) {
    int low = 0, high = account_count - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        int cmp = strcmp(accounts[mid].last4, last4);
        if (cmp == 0) return mid;
        if (cmp < 0)  low = mid + 1;
        else          high = mid - 1;
    }
    return -1; // Not found
}

/* ---------------------------------------------------------------------------
 * 4. RAW TERMINAL & ARROW KEY INPUT
 * --------------------------------------------------------------------------- */
#define KEY_UP    1001
#define KEY_DOWN  1002
#define KEY_ENTER 1003
#define KEY_ESC   1004

static struct termios orig_termios;

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static int read_key(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    if (c == '\n' || c == '\r') return KEY_ENTER;
    if (c == 27) { // ESC sequence
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_ESC;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESC;
        if (seq[0] == '[') {
            if (seq[1] == 'A') return KEY_UP;
            if (seq[1] == 'B') return KEY_DOWN;
        }
        return KEY_ESC;
    }
    return c;
}

/* ---------------------------------------------------------------------------
 * 5. ENCLOSED ASTERISK BOX UI
 * --------------------------------------------------------------------------- */
static void print_border(void) {
    for (int i = 0; i < BOX_WIDTH; i++) putchar('*');
    putchar('\n');
}

static void print_separator(void) {
    putchar('*');
    for (int i = 0; i < BOX_WIDTH - 2; i++) putchar('-');
    putchar('*');
    putchar('\n');
}

static void print_line(const char *text) {
    int len = (int)strlen(text);
    int space = BOX_WIDTH - 2 - len;
    if (space < 0) space = 0;
    printf("* %s", text);
    for (int i = 0; i < space - 1; i++) putchar(' ');
    printf("*\n");
}

static void print_center(const char *text) {
    int len = (int)strlen(text);
    int pad = (BOX_WIDTH - 2 - len) / 2;
    int rem = BOX_WIDTH - 2 - len - pad;
    if (pad < 0) pad = 0;
    if (rem < 0) rem = 0;
    putchar('*');
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s", text);
    for (int i = 0; i < rem; i++) putchar(' ');
    printf("*\n");
}

static int menu_select(const char *title, const char *subtitle, const char **items, int count) {
    int selected = 0;
    while (1) {
        printf("\033[2J\033[H"); // Clear screen
        print_border();
        print_center("ATM");
        print_center(title);
        if (subtitle) print_center(subtitle);
        print_separator();
        print_line("Use [UP/DOWN] Arrow Keys & [ENTER]");
        print_line("");

        for (int i = 0; i < count; i++) {
            char buf[64];
            if (i == selected) snprintf(buf, sizeof(buf), ">> [%d] %s <<", i + 1, items[i]);
            else              snprintf(buf, sizeof(buf), "   [%d] %s", i + 1, items[i]);
            print_line(buf);
        }

        print_line("");
        print_separator();
        print_line("[ENTER] Select  |  [ESC] Back");
        print_border();

        int k = read_key();
        if (k == KEY_UP)    selected = (selected - 1 + count) % count;
        if (k == KEY_DOWN)  selected = (selected + 1) % count;
        if (k == KEY_ENTER) return selected;
        if (k == KEY_ESC)   return -1;
    }
}

static void input_digits(const char *prompt, char *dest, int count, int masked) {
    int pos = 0;
    dest[0] = '\0';
    while (1) {
        printf("\033[2J\033[H");
        print_border();
        print_center("ATM INPUT");
        print_separator();
        print_line("");
        print_center(prompt);
        print_line("");

        char display[64];
        int off = snprintf(display, sizeof(display), "Input: [ ");
        for (int i = 0; i < count; i++) {
            if (i < pos) off += snprintf(display + off, sizeof(display) - off, "%c ", masked ? '*' : dest[i]);
            else         off += snprintf(display + off, sizeof(display) - off, "_ ");
        }
        snprintf(display + off, sizeof(display) - off, "]");
        print_center(display);

        print_line("");
        print_separator();
        print_line("Type digits | [BACKSPACE] Del | [ESC] Cancel");
        print_border();

        int k = read_key();
        if (k == KEY_ESC) { dest[0] = '\0'; return; }
        if (k == 127 || k == 8) { // Backspace
            if (pos > 0) { pos--; dest[pos] = '\0'; }
        } else if (k >= '0' && k <= '9' && pos < count) {
            dest[pos++] = (char)k;
            dest[pos] = '\0';
            if (pos == count) return; // Auto-confirm
        }
    }
}

static void show_message(const char *title, const char *msg) {
    printf("\033[2J\033[H");
    print_border();
    print_center("ATM");
    print_separator();
    print_line("");
    print_center(title);
    print_line("");
    print_center(msg);
    print_line("");
    print_separator();
    print_line("Press any key to continue...");
    print_border();
    read_key();
}

/* ---------------------------------------------------------------------------
 * 6. LOGGING (logs/ folder)
 * --------------------------------------------------------------------------- */
static void log_action(const char *last4, const char *action, const char *details) {
    mkdir("logs", 0755);
    FILE *fp = fopen("logs/atm.log", "a");
    if (!fp) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    fprintf(fp, "[%s] CARD:****%s | %-12s | %s\n", timestamp, last4 ? last4 : "----", action, details);
    fclose(fp);
}

/* ---------------------------------------------------------------------------
 * 7. COMPACT PNG RECEIPT GENERATOR (Receipt/ folder)
 * Uses standard uncompressed DEFLATE inside a minimal PNG chunk writer.
 * --------------------------------------------------------------------------- */
static unsigned long crc32_table[256];
static int crc_ready = 0;

static void init_crc32(void) {
    for (unsigned int n = 0; n < 256; n++) {
        unsigned long c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        crc32_table[n] = c;
    }
    crc_ready = 1;
}

static unsigned long update_crc32(unsigned long crc, const unsigned char *buf, size_t len) {
    if (!crc_ready) init_crc32();
    for (size_t i = 0; i < len; i++) crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void write_png_chunk(FILE *fp, const char *type, const unsigned char *data, size_t len) {
    unsigned char len_b[4] = { (len >> 24) & 0xFF, (len >> 16) & 0xFF, (len >> 8) & 0xFF, len & 0xFF };
    fwrite(len_b, 1, 4, fp);
    fwrite(type, 1, 4, fp);
    if (len > 0) fwrite(data, 1, len, fp);

    unsigned long crc = update_crc32(0xFFFFFFFFUL, (const unsigned char *)type, 4);
    if (len > 0) crc = update_crc32(crc, data, len);
    crc ^= 0xFFFFFFFFUL;

    unsigned char crc_b[4] = { (crc >> 24) & 0xFF, (crc >> 16) & 0xFF, (crc >> 8) & 0xFF, crc & 0xFF };
    fwrite(crc_b, 1, 4, fp);
}

/* Simple 5x7 matrix bitmap font for basic numbers and uppercase letters */
static const unsigned char FONT_5X7[37][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x7F,0x20,0x18,0x20,0x7F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x00,0x00,0x00,0x00,0x00}  // Space
};

static void draw_glyph(unsigned char *buf, int W, int x, int y, char c) {
    int idx = 36;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c >= 'A' && c <= 'Z') idx = 10 + (c - 'A');
    else if (c >= 'a' && c <= 'z') idx = 10 + (c - 'a');

    for (int col = 0; col < 5; col++) {
        unsigned char bits = FONT_5X7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                // Scale 2x for readability
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int px = x + col * 2 + dx;
                        int py = y + row * 2 + dy;
                        int offset = (py * W + px) * 3;
                        buf[offset] = 0; buf[offset+1] = 0; buf[offset+2] = 0; // Black
                    }
                }
            }
        }
    }
}

static void draw_string(unsigned char *buf, int W, int x, int y, const char *s) {
    while (*s) {
        draw_glyph(buf, W, x, y, *s);
        x += 14; // Char spacing
        s++;
    }
}

static void generate_receipt_png(const Account *acc, const char *action, double amt) {
    mkdir("Receipt", 0755);
    int W = 360, H = 320;
    size_t raw_row = 1 + W * 3;
    unsigned char *raw = malloc(raw_row * H);
    memset(raw, 0, raw_row * H);

    // Fill white pixels
    unsigned char *img = malloc(W * H * 3);
    memset(img, 255, W * H * 3);

    // Draw text strings
    char line[64];
    draw_string(img, W, 70, 30, "ATM RECEIPT");
    draw_string(img, W, 30, 70, "BANK: GLOBAL DIGITAL");

    snprintf(line, sizeof(line), "CARD: ****%s", acc->last4);
    draw_string(img, W, 30, 110, line);

    snprintf(line, sizeof(line), "ACTION: %s", action);
    draw_string(img, W, 30, 150, line);

    snprintf(line, sizeof(line), "AMT: $%.2f", amt);
    draw_string(img, W, 30, 190, line);

    snprintf(line, sizeof(line), "BAL: $%.2f", acc->balance);
    draw_string(img, W, 30, 230, line);

    draw_string(img, W, 50, 280, "THANK YOU");

    // Copy to raw rows with 0 filter byte
    for (int y = 0; y < H; y++) {
        raw[y * raw_row] = 0; // Filter: None
        memcpy(&raw[y * raw_row + 1], &img[y * W * 3], W * 3);
    }
    free(img);

    // Save PNG
    char path[64];
    snprintf(path, sizeof(path), "Receipt/receipt_%ld.png", (long)time(NULL));
    FILE *fp = fopen(path, "wb");
    if (!fp) { free(raw); return; }

    // PNG Header
    const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, fp);

    // IHDR
    unsigned char ihdr[13] = {
        (W >> 24) & 0xFF, (W >> 16) & 0xFF, (W >> 8) & 0xFF, W & 0xFF,
        (H >> 24) & 0xFF, (H >> 16) & 0xFF, (H >> 8) & 0xFF, H & 0xFF,
        8, 2, 0, 0, 0 // 8-bit RGB, no interlace
    };
    write_png_chunk(fp, "IHDR", ihdr, 13);

    // IDAT (DEFLATE uncompressed stored blocks)
    size_t raw_len = raw_row * H;
    size_t idat_cap = raw_len + 64 + (raw_len / 65535 + 1) * 5;
    unsigned char *idat = malloc(idat_cap);
    size_t idat_len = 0;

    idat[idat_len++] = 0x78; // zlib header
    idat[idat_len++] = 0x01;

    size_t src = 0;
    while (src < raw_len) {
        size_t blk = raw_len - src;
        if (blk > 65535) blk = 65535;
        int last = (src + blk == raw_len);
        idat[idat_len++] = last ? 0x01 : 0x00;
        idat[idat_len++] = blk & 0xFF;
        idat[idat_len++] = (blk >> 8) & 0xFF;
        idat[idat_len++] = (~blk) & 0xFF;
        idat[idat_len++] = ((~blk) >> 8) & 0xFF;
        memcpy(idat + idat_len, raw + src, blk);
        idat_len += blk;
        src += blk;
    }

    // Adler-32
    unsigned long s1 = 1, s2 = 0;
    for (size_t i = 0; i < raw_len; i++) {
        s1 = (s1 + raw[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    unsigned long adler = (s2 << 16) | s1;
    idat[idat_len++] = (adler >> 24) & 0xFF;
    idat[idat_len++] = (adler >> 16) & 0xFF;
    idat[idat_len++] = (adler >> 8) & 0xFF;
    idat[idat_len++] = adler & 0xFF;

    write_png_chunk(fp, "IDAT", idat, idat_len);
    write_png_chunk(fp, "IEND", NULL, 0);

    fclose(fp);
    free(raw);
    free(idat);

    show_message("RECEIPT SAVED", "Receipt image created in Receipt/ folder");
}

/* ---------------------------------------------------------------------------
 * 8. MAIN WORKFLOW
 * --------------------------------------------------------------------------- */
int main(void) {
    const char *DATA_FILE = "data/accounts.json";
    load_accounts_from_json(DATA_FILE);
    sort_accounts(); // Required for binary search

    enable_raw_mode();
    log_action(NULL, "BOOT", "ATM Simulator started");

    while (1) {
        char last4[5];
        input_digits("ENTER LAST 4 DIGITS OF CARD", last4, 4, 0);
        if (strlen(last4) != 4) break; // ESC pressed

        // Binary search lookup
        int idx = binary_search_card(last4);
        if (idx == -1) {
            show_message("NOT FOUND", "Card number not found in database.");
            log_action(last4, "SEARCH_FAIL", "Card not found");
            continue;
        }

        Account *acc = &accounts[idx];
        if (acc->is_blocked) {
            show_message("CARD BLOCKED", "This card is locked. Contact bank.");
            log_action(last4, "BLOCKED", "Attempt to access locked card");
            continue;
        }

        // PIN Verification
        int authenticated = 0;
        for (int attempt = 1; attempt <= 3; attempt++) {
            char pin[5];
            input_digits("ENTER 4-DIGIT PIN", pin, 4, 1);
            if (strlen(pin) != 4) break;

            if (strcmp(pin, acc->pin) == 0) {
                authenticated = 1;
                acc->failed_attempts = 0;
                save_accounts_to_json(DATA_FILE);
                break;
            } else {
                acc->failed_attempts++;
                if (acc->failed_attempts >= 3) {
                    acc->is_blocked = 1;
                    save_accounts_to_json(DATA_FILE);
                    show_message("CARD LOCKED", "3 wrong PIN attempts. Card blocked!");
                    log_action(last4, "CARD_LOCKED", "Card blocked after 3 failed PINs");
                    break;
                }
                char warn[64];
                snprintf(warn, sizeof(warn), "Incorrect PIN! Attempts remaining: %d", 3 - acc->failed_attempts);
                show_message("WRONG PIN", warn);
                log_action(last4, "PIN_FAIL", "Failed PIN entry");
            }
        }

        if (!authenticated) continue;
        log_action(last4, "LOGIN", "Customer logged in successfully");

        // Transaction Menu
        int session = 1;
        while (session) {
            char greeting[64];
            snprintf(greeting, sizeof(greeting), "Welcome, %s | Bal: $%.2f", acc->name, acc->balance);

            const char *options[] = {
                "Check Balance",
                "Withdraw Cash ($100)",
                "Deposit Funds ($200)",
                "Eject Card / Exit"
            };

            int choice = menu_select("MAIN MENU", greeting, options, 4);

            if (choice == 0) { // Balance
                char bmsg[64];
                snprintf(bmsg, sizeof(bmsg), "Your Available Balance: $%.2f", acc->balance);
                show_message("BALANCE INQUIRY", bmsg);
                log_action(last4, "BALANCE", "Balance viewed");
            } else if (choice == 1) { // Withdraw
                if (acc->balance >= 100.0) {
                    acc->balance -= 100.0;
                    save_accounts_to_json(DATA_FILE);
                    show_message("SUCCESS", "$100.00 cash dispensed!");
                    log_action(last4, "WITHDRAW", "$100.00 withdrawn");

                    const char *rcpt_opts[] = { "Yes, Print PNG Receipt", "No, Skip" };
                    if (menu_select("RECEIPT", "Would you like a receipt?", rcpt_opts, 2) == 0) {
                        generate_receipt_png(acc, "WITHDRAWAL", 100.0);
                    }
                } else {
                    show_message("DECLINED", "Insufficient funds for $100.00.");
                    log_action(last4, "WITHDRAW_FAIL", "Insufficient balance");
                }
            } else if (choice == 2) { // Deposit
                acc->balance += 200.0;
                save_accounts_to_json(DATA_FILE);
                show_message("SUCCESS", "$200.00 deposited into account!");
                log_action(last4, "DEPOSIT", "$200.00 deposited");

                const char *rcpt_opts[] = { "Yes, Print PNG Receipt", "No, Skip" };
                if (menu_select("RECEIPT", "Would you like a receipt?", rcpt_opts, 2) == 0) {
                    generate_receipt_png(acc, "DEPOSIT", 200.0);
                }
            } else { // Exit
                session = 0;
                show_message("GOODBYE", "Card ejected. Please take your card.");
                log_action(last4, "LOGOUT", "Session finished");
            }
        }
    }

    printf("\nATM Simulator shut down.\n");
    return 0;
}
