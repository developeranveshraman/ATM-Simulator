/*
 * logger.c - Audit logging for the ATM Simulator
 *
 * Writes timestamped, structured log entries to daily log files
 * in the "logs/" directory. Automatically rotates to a new file
 * when the date changes.
 */

#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------------------
 * Static state
 * --------------------------------------------------------------------------- */

static FILE *log_file = NULL;        /* Handle to the current day's log file  */
static char current_date[16] = "";   /* Tracks the date for file rotation     */

/* ---------------------------------------------------------------------------
 * Helper: get the current date string "YYYY-MM-DD" into a caller buffer
 * --------------------------------------------------------------------------- */
static void get_date_string(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, len, "%Y-%m-%d", tm_info);
}

/* ---------------------------------------------------------------------------
 * Helper: get the current timestamp string "[YYYY-MM-DD HH:MM:SS]"
 * --------------------------------------------------------------------------- */
static void get_timestamp(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, len, "[%Y-%m-%d %H:%M:%S]", tm_info);
}

/* ---------------------------------------------------------------------------
 * logger_init  –  Open (or reopen) the log file for the current date
 * --------------------------------------------------------------------------- */
void logger_init(void)
{
    char filename[64];
    char timestamp[32];

    /* Ensure the logs directory exists */
    mkdir("logs", 0755);

    /* Determine today's date and build the filename */
    get_date_string(current_date, sizeof(current_date));
    snprintf(filename, sizeof(filename), "logs/atm_%s.log", current_date);

    /* Open in append mode so we never clobber previous sessions */
    log_file = fopen(filename, "a");
    if (!log_file) {
        fprintf(stderr, "ERROR: Could not open log file '%s'\n", filename);
        return;
    }

    /* Write a session header */
    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(log_file,
            "\n========================================================\n"
            "%s === ATM SESSION STARTED ===\n"
            "========================================================\n",
            timestamp);
    fflush(log_file);
}

/* ---------------------------------------------------------------------------
 * logger_log  –  Write a single structured log line
 *
 *   level     : severity tag, e.g. "INFO", "WARN", "ERROR"
 *   card_last4: last 4 digits of the card number (NULL → "----")
 *   action    : action label, e.g. "WITHDRAWAL", "AUTH_FAIL"
 *   fmt, ...  : printf-style detail message
 *
 * Output format:
 *   [YYYY-MM-DD HH:MM:SS] [LEVEL]  CARD:****XXXX | ACTION:action | detail
 * --------------------------------------------------------------------------- */
void logger_log(const char *level, const char *card_last4,
                const char *action, const char *fmt, ...)
{
    char timestamp[32];
    char date_now[16];
    char detail[512];
    va_list args;

    /* Lazy initialisation */
    if (!log_file) {
        logger_init();
        if (!log_file) return;   /* give up if we still can't open */
    }

    /* Date-change detection: rotate to a new file at midnight */
    get_date_string(date_now, sizeof(date_now));
    if (strcmp(date_now, current_date) != 0) {
        logger_close();
        logger_init();
        if (!log_file) return;
    }

    /* Build the detail string from the variadic arguments */
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);

    /* Build and write the log line */
    get_timestamp(timestamp, sizeof(timestamp));

    const char *card_display = card_last4 ? card_last4 : "----";

    fprintf(log_file, "%s [%-5s]  CARD:****%s | ACTION:%-12s | %s\n",
            timestamp, level, card_display, action, detail);
    fflush(log_file);
}

/* ---------------------------------------------------------------------------
 * logger_close  –  Flush the session footer and release the file handle
 * --------------------------------------------------------------------------- */
void logger_close(void)
{
    if (log_file) {
        char timestamp[32];
        get_timestamp(timestamp, sizeof(timestamp));

        fprintf(log_file,
                "========================================================\n"
                "%s === ATM SESSION ENDED ===\n"
                "========================================================\n\n",
                timestamp);
        fflush(log_file);

        fclose(log_file);
        log_file = NULL;
    }
}
