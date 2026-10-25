/* OpenBase host tests: a small harness.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#ifndef OBTEST_H
#define OBTEST_H

#include "openbase_core.h"
#include <stdio.h>
#include <string.h>

extern int obt_checks, obt_failures;
extern const char *obt_dir; /* a scratch folder for files */

#define CHECK(cond)                                                                 \
    do {                                                                            \
        obt_checks++;                                                               \
        if (!(cond)) {                                                              \
            obt_failures++;                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                           \
    } while (0)

#define CHECK_STR(a, b)                                                                              \
    do {                                                                                             \
        const char *obt_a = (a), *obt_b = (b);                                                       \
        obt_checks++;                                                                                \
        if (!obt_a || !obt_b || strcmp(obt_a, obt_b) != 0) {                                         \
            obt_failures++;                                                                          \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, obt_a ? obt_a : "(null)", \
                    obt_b ? obt_b : "(null)");                                                       \
        }                                                                                            \
    } while (0)

/* A call that must succeed; prints the error's words if it doesn't */
#define OK(call)                                                                                          \
    do {                                                                                                  \
        ob_error obt_e;                                                                                   \
        int obt_rc;                                                                                       \
        memset(&obt_e, 0, sizeof obt_e);                                                                  \
        obt_rc = (call);                                                                                  \
        obt_checks++;                                                                                     \
        if (obt_rc != OB_OK) {                                                                            \
            obt_failures++;                                                                               \
            fprintf(stderr, "%s:%d: %s -> %d (%s)\n", __FILE__, __LINE__, #call, obt_rc, obt_e.text);     \
        }                                                                                                 \
    } while (0)

/* For OK(): the error variable the call writes */
#define E (&obt_e)

/* Each suite */
void suite_types(void);
void suite_project(void);
void suite_schema(void);
void suite_csv(void);
void suite_query(void);
void suite_golden(const char *dir);
void suite_fuzz(void);
void suite_alloc(void);

/* Helpers (test_main.c) */
char *obt_path(const char *name); /* in obt_dir; static buffer, rotating */
ob_project *obt_memory_project(void);
long long obt_long(ob_project *p, const char *sql);
char *obt_text(ob_project *p, const char *sql); /* the first value, malloc'd ("" for none) */
char *obt_read_file(const char *path, long *size);
int obt_write_file(const char *path, const void *data, long size);
/* Every row of a SELECT as text: values tab-separated, rows by newline */
char *obt_rows(ob_project *p, const char *sql);

#endif
