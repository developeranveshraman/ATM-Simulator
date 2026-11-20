/*
 * account.c – Account management with JSON persistence and binary search.
 *
 * Provides functions to load/save accounts from/to a JSON file,
 * sort accounts by their last-4 card digits, and perform an
 * efficient binary search lookup.
 */

#include "account.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  accounts_load – Read accounts from a JSON file into an array.     */
/*                                                                    */
/*  Expected JSON structure:                                          */
/*  {                                                                 */
/*    "accounts": [                                                   */
/*      { "last4": "1234", "card_number": "...", "pin": "5678",       */
/*        "name": "...", "balance": 1000.0, "is_blocked": false,      */
/*        "failed_attempts": 0 },                                     */
/*      ...                                                           */
/*    ]                                                               */
/*  }                                                                 */
/*                                                                    */
/*  Returns 0 on success, -1 on error.                                */
/* ------------------------------------------------------------------ */
int accounts_load(const char *filename, Account *accounts, int *count)
{
    /* Parse the entire JSON file into a tree */
    JsonNode *root = json_parse_file(filename);
    if (!root) {
        fprintf(stderr, "accounts_load: failed to parse %s\n", filename);
        return -1;
    }

    /* Retrieve the "accounts" array from the root object */
    JsonNode *arr = json_get(root, "accounts");
    if (!arr) {
        fprintf(stderr, "accounts_load: missing \"accounts\" array\n");
        json_free(root);
        return -1;
    }

    /* Iterate through every element in the array */
    int i = 0;
    JsonNode *elem;
    while ((elem = json_index(arr, i)) != NULL) {
        Account *a = &accounts[i];

        /* Extract string fields */
        JsonNode *n;

        n = json_get(elem, "last4");
        if (n) {
            strncpy(a->last4, json_string(n), sizeof(a->last4) - 1);
            a->last4[sizeof(a->last4) - 1] = '\0';
        }

        n = json_get(elem, "card_number");
        if (n) {
            strncpy(a->card_number, json_string(n), sizeof(a->card_number) - 1);
            a->card_number[sizeof(a->card_number) - 1] = '\0';
        }

        n = json_get(elem, "pin");
        if (n) {
            strncpy(a->pin, json_string(n), sizeof(a->pin) - 1);
            a->pin[sizeof(a->pin) - 1] = '\0';
        }

        n = json_get(elem, "name");
        if (n) {
            strncpy(a->name, json_string(n), sizeof(a->name) - 1);
            a->name[sizeof(a->name) - 1] = '\0';
        }

        /* Extract numeric / boolean fields */
        n = json_get(elem, "balance");
        if (n) a->balance = json_number(n);

        n = json_get(elem, "is_blocked");
        if (n) a->is_blocked = json_bool(n);

        n = json_get(elem, "failed_attempts");
        if (n) a->failed_attempts = (int)json_number(n);

        i++;
    }

    *count = i;

    json_free(root);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Helper – Allocate and initialise a new JsonNode.                  */
/* ------------------------------------------------------------------ */
static JsonNode *make_node(JsonType type)
{
    JsonNode *n = (JsonNode *)calloc(1, sizeof(JsonNode));
    if (n) n->type = type;
    return n;
}

/* Convenience helpers for building the JSON tree */
static JsonNode *make_string_node(const char *key, const char *value)
{
    JsonNode *n = make_node(JSON_STRING);
    if (!n) return NULL;
    n->key      = strdup(key);
    n->string   = strdup(value);
    return n;
}

static JsonNode *make_number_node(const char *key, double value)
{
    JsonNode *n = make_node(JSON_NUMBER);
    if (!n) return NULL;
    n->key    = strdup(key);
    n->number = value;
    return n;
}

static JsonNode *make_bool_node(const char *key, int value)
{
    JsonNode *n = make_node(JSON_BOOL);
    if (!n) return NULL;
    n->key     = strdup(key);
    n->boolean = value;
    return n;
}

/* ------------------------------------------------------------------ */
/*  accounts_save – Write accounts array to a JSON file.              */
/*                                                                    */
/*  Builds a JSON tree in memory, writes it out, then frees it.       */
/*  Returns 0 on success, -1 on error.                                */
/* ------------------------------------------------------------------ */
int accounts_save(const char *filename, const Account *accounts, int count)
{
    /* Root object: { "accounts": [ ... ] } */
    JsonNode *root = make_node(JSON_OBJECT);
    if (!root) return -1;

    /* The array that will hold every account object */
    JsonNode *arr = make_node(JSON_ARRAY);
    if (!arr) { json_free(root); return -1; }
    arr->key = strdup("accounts");

    /* Link the array as the first child of root */
    root->child = arr;

    JsonNode *prev_elem = NULL;

    for (int i = 0; i < count; i++) {
        const Account *a = &accounts[i];

        /* Create an object node for this account */
        JsonNode *obj = make_node(JSON_OBJECT);
        if (!obj) { json_free(root); return -1; }

        /* Build the chain of field nodes (linked via ->next) */
        JsonNode *f_last4   = make_string_node("last4",       a->last4);
        JsonNode *f_card    = make_string_node("card_number",  a->card_number);
        JsonNode *f_pin     = make_string_node("pin",          a->pin);
        JsonNode *f_name    = make_string_node("name",         a->name);
        JsonNode *f_balance = make_number_node("balance",      a->balance);
        JsonNode *f_blocked = make_bool_node  ("is_blocked",   a->is_blocked);
        JsonNode *f_failed  = make_number_node("failed_attempts", (double)a->failed_attempts);

        /* Chain the fields together as siblings */
        f_last4->next   = f_card;
        f_card->next    = f_pin;
        f_pin->next     = f_name;
        f_name->next    = f_balance;
        f_balance->next = f_blocked;
        f_blocked->next = f_failed;
        /* f_failed->next remains NULL (end of chain) */

        obj->child = f_last4;

        /* Append this object to the array's child list */
        if (prev_elem)
            prev_elem->next = obj;
        else
            arr->child = obj;

        prev_elem = obj;
    }

    /* Write the tree out to the file */
    if (json_write_file(filename, root) != 0) {
        json_free(root);
        return -1;
    }

    json_free(root);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Comparator for qsort – orders accounts by last4 (ascending).     */
/* ------------------------------------------------------------------ */
static int cmp_last4(const void *a, const void *b)
{
    const Account *aa = (const Account *)a;
    const Account *bb = (const Account *)b;
    return strcmp(aa->last4, bb->last4);
}

/* ------------------------------------------------------------------ */
/*  accounts_sort – Sort the accounts array by last4 digits.          */
/* ------------------------------------------------------------------ */
void accounts_sort(Account *accounts, int count)
{
    qsort(accounts, (size_t)count, sizeof(Account), cmp_last4);
}

/* ------------------------------------------------------------------ */
/*  accounts_search – Binary search for an account by last4 digits.   */
/*                                                                    */
/*  ALGORITHM (iterative binary search):                              */
/*    1. Maintain two pointers, 'low' and 'high', that bracket the    */
/*       search space within the sorted array.                        */
/*    2. Compute the midpoint: mid = low + (high - low) / 2           */
/*       (avoids potential integer overflow of (low + high) / 2).     */
/*    3. Compare the target last4 string with accounts[mid].last4:    */
/*         – If equal  → found; return mid.                           */
/*         – If target < mid → narrow search to left half  (high=mid-1) */
/*         – If target > mid → narrow search to right half (low=mid+1)  */
/*    4. If low > high the element is not present; return -1.         */
/*                                                                    */
/*  Time complexity:  O(log n)                                        */
/*  Space complexity: O(1)                                            */
/*                                                                    */
/*  Returns the index of the matching account, or -1 if not found.   */
/* ------------------------------------------------------------------ */
int accounts_search(const Account *accounts, int count, const char *last4)
{
    int low  = 0;
    int high = count - 1;

    while (low <= high) {
        int mid = low + (high - low) / 2;   /* safe midpoint */

        int cmp = strcmp(accounts[mid].last4, last4);

        if (cmp == 0) {
            /* Found the target at index mid */
            return mid;
        } else if (cmp < 0) {
            /* accounts[mid].last4 is lexicographically before target;
               discard the left half including mid */
            low = mid + 1;
        } else {
            /* accounts[mid].last4 is lexicographically after target;
               discard the right half including mid */
            high = mid - 1;
        }
    }

    /* Target not found in the array */
    return -1;
}
