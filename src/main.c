/* ============================================================
 *  ATM Simulator — Main Application
 *  Entry point, session state machine, transaction handlers.
 * ============================================================ */

#include "ui.h"
#include "account.h"
#include "logger.h"
#include "receipt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

/* ---- global state ---- */
static Account accounts[MAX_ACCOUNTS];
static int     account_count = 0;
static const char *DATA_FILE = "data/accounts.json";

/* ---- signal handler ---- */
static void on_signal(int sig) {
    (void)sig;
    ui_cleanup();
    logger_close();
    exit(0);
}

/* ---- helpers ---- */
static void generate_txn_id(char *buf, size_t len) {
    srand((unsigned)time(NULL));
    snprintf(buf, len, "TXN-%06d", rand() % 1000000);
}

static void make_receipt(Account *acc, const char *txn_type, double amount) {
    const char *items[] = { "Yes, Print Receipt (PNG)", "No, Skip Receipt" };
    int choice = ui_menu("PRINT RECEIPT?", NULL, items, 2, 0);
    if (choice != 0) return;

    ReceiptInfo ri;
    memset(&ri, 0, sizeof(ri));

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(ri.date, sizeof(ri.date), "%Y-%m-%d", t);
    strftime(ri.time_str, sizeof(ri.time_str), "%H:%M:%S", t);

    generate_txn_id(ri.txn_id, sizeof(ri.txn_id));
    snprintf(ri.card_masked, sizeof(ri.card_masked), "************%s", acc->last4);
    strncpy(ri.name, acc->name, sizeof(ri.name) - 1);
    strncpy(ri.txn_type, txn_type, sizeof(ri.txn_type) - 1);
    ri.amount  = amount;
    ri.balance = acc->balance;
    strncpy(ri.status, "COMPLETED", sizeof(ri.status) - 1);

    if (receipt_generate(&ri) == 0) {
        ui_message("RECEIPT SAVED", "PNG receipt generated in Receipt/ folder.", 1);
    } else {
        ui_message("ERROR", "Could not generate receipt.", 1);
    }
}

/* ---- transaction handlers ---- */
static void handle_balance(Account *acc) {
    char bal[64];
    snprintf(bal, sizeof(bal), "Available Balance: $%.2f", acc->balance);
    ui_message("ACCOUNT BALANCE", bal, 1);
    logger_log("INFO", acc->last4, "BALANCE_INQ",
               "Balance inquiry | Balance: $%.2f", acc->balance);
}

static void handle_withdrawal(Account *acc) {
    const char *items[] = {
        "$20.00", "$50.00", "$100.00", "$200.00",
        "Custom Amount", "Back to Menu"
    };
    int choice = ui_menu("CASH WITHDRAWAL", NULL, items, 6, 0);

    double amount = 0;
    switch (choice) {
        case 0: amount =  20; break;
        case 1: amount =  50; break;
        case 2: amount = 100; break;
        case 3: amount = 200; break;
        case 4:
            if (!ui_input_amount("Enter withdrawal amount:", &amount)) return;
            if (amount <= 0) { ui_message("ERROR", "Invalid amount.", 1); return; }
            break;
        default: return;
    }

    if (amount > acc->balance) {
        ui_message("INSUFFICIENT FUNDS",
                   "You do not have enough balance.", 1);
        logger_log("WARN", acc->last4, "WITHDRAW_FAIL",
                   "Insufficient funds | Requested: $%.2f | Balance: $%.2f",
                   amount, acc->balance);
        return;
    }

    acc->balance -= amount;
    accounts_save(DATA_FILE, accounts, account_count);

    char msg[128];
    snprintf(msg, sizeof(msg),
             "Dispensed: $%.2f | New Balance: $%.2f", amount, acc->balance);
    ui_message("CASH DISPENSED", msg, 0);

    logger_log("INFO", acc->last4, "WITHDRAWAL",
               "Amount: $%.2f | Balance: $%.2f", amount, acc->balance);

    make_receipt(acc, "CASH WITHDRAWAL", amount);
}

static void handle_fast_cash(Account *acc) {
    double amount = 100.00;
    if (amount > acc->balance) {
        ui_message("INSUFFICIENT FUNDS",
                   "Balance too low for $100.00 fast cash.", 1);
        return;
    }

    acc->balance -= amount;
    accounts_save(DATA_FILE, accounts, account_count);

    char msg[128];
    snprintf(msg, sizeof(msg),
             "Fast Cash: $%.2f dispensed | Balance: $%.2f",
             amount, acc->balance);
    ui_message("FAST CASH", msg, 0);

    logger_log("INFO", acc->last4, "FAST_CASH",
               "Amount: $%.2f | Balance: $%.2f", amount, acc->balance);

    make_receipt(acc, "FAST CASH", amount);
}

static void handle_deposit(Account *acc) {
    double amount = 0;
    if (!ui_input_amount("Enter deposit amount:", &amount)) return;
    if (amount <= 0) { ui_message("ERROR", "Invalid amount.", 1); return; }

    acc->balance += amount;
    accounts_save(DATA_FILE, accounts, account_count);

    char msg[128];
    snprintf(msg, sizeof(msg),
             "Deposited: $%.2f | New Balance: $%.2f", amount, acc->balance);
    ui_message("DEPOSIT SUCCESSFUL", msg, 0);

    logger_log("INFO", acc->last4, "DEPOSIT",
               "Amount: $%.2f | Balance: $%.2f", amount, acc->balance);

    make_receipt(acc, "CASH DEPOSIT", amount);
}

static void handle_change_pin(Account *acc) {
    /* verify current PIN first */
    char old_pin[5];
    if (!ui_input_digits("Enter current PIN:", old_pin, 4, 1)) return;
    if (strcmp(old_pin, acc->pin) != 0) {
        ui_message("ERROR", "Current PIN is incorrect.", 1);
        logger_log("WARN", acc->last4, "PIN_CHANGE_FAIL", "Incorrect current PIN");
        return;
    }

    char new_pin[5];
    if (!ui_input_digits("Enter new 4-digit PIN:", new_pin, 4, 1)) return;

    char confirm[5];
    if (!ui_input_digits("Confirm new PIN:", confirm, 4, 1)) return;

    if (strcmp(new_pin, confirm) != 0) {
        ui_message("ERROR", "PINs do not match. Try again.", 1);
        return;
    }

    strncpy(acc->pin, new_pin, 5);
    accounts_save(DATA_FILE, accounts, account_count);

    ui_message("PIN CHANGED", "Your PIN has been updated.", 1);
    logger_log("INFO", acc->last4, "PIN_CHANGE", "PIN changed successfully");
}

/* ---- mini statement ---- */
static void handle_mini_statement(Account *acc) {
    char bal[64];
    snprintf(bal, sizeof(bal), "Current Balance: $%.2f", acc->balance);
    ui_message("MINI STATEMENT", bal, 1);
    logger_log("INFO", acc->last4, "MINI_STMT",
               "Mini statement viewed | Balance: $%.2f", acc->balance);
}

/* ---- main ---- */
int main(void) {
    /* install signal handlers */
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* initialise subsystems */
    ui_init();
    logger_init();

    /* load accounts from JSON */
    if (accounts_load(DATA_FILE, accounts, &account_count) != 0) {
        ui_cleanup();
        fprintf(stderr, "ERROR: cannot load %s\n", DATA_FILE);
        return 1;
    }

    /* sort for binary search */
    accounts_sort(accounts, account_count);
    logger_log("INFO", NULL, "SYSTEM", "Loaded %d accounts, sorted for binary search", account_count);

    /* ============ main session loop ============ */
    while (1) {
        /* ---- welcome screen ---- */
        ui_clear();
        ui_color_green();
        ui_box_top();
        ui_box_center("A T M");
        ui_box_center("GLOBAL DIGITAL BANK");
        ui_box_top();
        ui_box_empty();
        ui_box_center("WELCOME TO THE ATM TERMINAL");
        ui_box_empty();
        ui_box_line("  Search Algorithm : Binary Search [O(log N)]");
        ui_box_line("  Data Source      : data/accounts.json");
        ui_box_empty();
        ui_box_center(">> INSERT CARD: ENTER LAST 4 DIGITS <<");
        ui_box_empty();
        ui_box_separator();
        ui_box_line("  [ESC] Shut Down ATM");
        ui_box_bottom();
        ui_color_reset();

        /* read last 4 digits of card */
        char last4[5];
        if (!ui_input_digits("Enter last 4 digits of your card:", last4, 4, 0)) {
            ui_cleanup();
            logger_close();
            printf("\nATM shutdown successfully.\n");
            return 0;
        }

        /* binary search */
        int idx = accounts_search(accounts, account_count, last4);

        if (idx < 0) {
            ui_color_red();
            ui_message("CARD NOT FOUND",
                       "No account matches these digits.", 1);
            ui_color_reset();
            logger_log("WARN", last4, "CARD_NOT_FOUND",
                       "Binary search returned no match");
            continue;
        }

        Account *acc = &accounts[idx];

        if (acc->is_blocked) {
            ui_color_red();
            ui_message("CARD BLOCKED",
                       "Contact your bank branch for help.", 1);
            ui_color_reset();
            logger_log("WARN", last4, "BLOCKED_ACCESS",
                       "Attempted access on blocked card");
            continue;
        }

        /* ---- PIN authentication (max 3 attempts) ---- */
        int authenticated = 0;
        for (int attempt = 1; attempt <= 3; attempt++) {
            char pin[5];
            if (!ui_input_digits("Enter your 4-digit PIN:", pin, 4, 1)) {
                /* User cancelled */
                break;
            }

            if (strcmp(pin, acc->pin) == 0) {
                authenticated = 1;
                acc->failed_attempts = 0;
                accounts_save(DATA_FILE, accounts, account_count);
                break;
            }

            acc->failed_attempts++;

            if (acc->failed_attempts >= 3) {
                acc->is_blocked = 1;
                accounts_save(DATA_FILE, accounts, account_count);
                ui_color_red();
                ui_message("CARD BLOCKED",
                           "3 failed attempts. Card is now locked.", 1);
                ui_color_reset();
                logger_log("ERROR", last4, "CARD_BLOCKED",
                           "Blocked after 3 failed PIN attempts");
                break;
            }

            char msg[80];
            snprintf(msg, sizeof(msg),
                     "Wrong PIN. %d attempt(s) remaining.",
                     3 - acc->failed_attempts);
            ui_color_yellow();
            ui_message("INCORRECT PIN", msg, 1);
            ui_color_reset();
            logger_log("WARN", last4, "AUTH_FAIL",
                       "Failed PIN attempt %d/3", acc->failed_attempts);
        }

        if (!authenticated) continue;

        logger_log("INFO", last4, "AUTH_SUCCESS",
                   "Authenticated: %s", acc->name);

        /* ---- main menu loop ---- */
        int session = 1;
        while (session) {
            char subtitle[80];
            snprintf(subtitle, sizeof(subtitle),
                     "Welcome, %s  |  ****%s", acc->name, acc->last4);

            const char *menu[] = {
                "Check Account Balance",
                "Cash Withdrawal",
                "Fast Cash ($100.00)",
                "Deposit Funds",
                "Mini Statement",
                "Change PIN",
                "Eject Card & Exit"
            };

            int sel = ui_menu("MAIN MENU", subtitle, menu, 7, 0);

            switch (sel) {
                case 0: handle_balance(acc);        break;
                case 1: handle_withdrawal(acc);     break;
                case 2: handle_fast_cash(acc);      break;
                case 3: handle_deposit(acc);        break;
                case 4: handle_mini_statement(acc);  break;
                case 5: handle_change_pin(acc);     break;
                case 6: /* fall through */
                case -1:
                    session = 0;
                    ui_message("CARD EJECTED",
                               "Thank you for using our ATM!", 1);
                    logger_log("INFO", last4, "LOGOUT",
                               "Session ended, card ejected");
                    break;
            }
        }
    }

    /* unreachable but good practice */
    logger_close();
    ui_cleanup();
    return 0;
}
