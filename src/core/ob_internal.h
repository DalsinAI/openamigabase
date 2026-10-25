/* OpenBase core: shared between the core's own files only.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#ifndef OB_INTERNAL_H
#define OB_INTERNAL_H

#include "openbase_core.h"
#include <sqlite3.h>

struct ob_project {
    sqlite3 *db;
    ob_config cfg;
    ob_formats formats;
    int readonly;
    int in_memory;
};

/* Errors */
int ob_fail(ob_error *err, int code, const char *fmt, ...);
int ob_nomem(ob_error *err);
/* An SQLite failure in plain words; `what` says what was being done. */
int ob_sqlite_fail(ob_project *p, int rc, ob_error *err, const char *what);
/* The plain words for a constraint message from SQLite ("CHECK constraint
 * failed: Price:number"), or NULL when it isn't one. */
int ob_constraint_words(ob_project *p, const char *msg, char *out, size_t size);

/* Statements */
int ob_prepare(ob_project *p, const char *sql, sqlite3_stmt **st, ob_error *err);
int ob_exec_quiet(ob_project *p, const char *sql); /* rollback paths */
int ob_begin(ob_project *p, ob_error *err);
int ob_commit(ob_project *p, ob_error *err);
void ob_rollback(ob_project *p);

/* Text */
int ob_casecmp(const char *a, const char *b);            /* ASCII case only, as SQLite */
int ob_ncasecmp(const char *a, const char *b, size_t n);
int ob_utf8_valid(const char *s, size_t n);
size_t ob_utf8_chars(const char *s);
size_t ob_utf8_cut(const char *s, size_t max);            /* bytes <= max, on a character */
int ob_latin1_to_utf8(const char *in, size_t n, ob_buf *out);
int ob_utf8_to_latin1(const char *in, size_t n, ob_buf *out, long *lost);
const char *ob_skip_space(const char *s);
size_t ob_trim_len(const char *s, size_t n);              /* n without trailing spaces */
int ob_is_space(int c);
int ob_sql_keyword(const char *word, size_t n);
int ob_sql_ident_quoted(ob_buf *b, const char *name); /* always in quotes */

/* The design tables */
int ob_schema_init(ob_project *p, ob_error *err);
/* The SQL functions OpenBase gives its connection (ob_convert...) */
int ob_types_register(ob_project *p, ob_error *err);
/* oadb_fields, written for a table */
int ob_fields_write(ob_project *p, const ob_table *t, ob_error *err);
int ob_fields_forget(ob_project *p, const char *table, ob_error *err);
/* ob_table_create inside a transaction the caller holds */
int ob_table_create_tx(ob_project *p, const ob_table *design, ob_error *err);

/* Convert one value for a change of type, as ob_convert() does in SQL.
 * Returns 1 and sets *out (malloc'd text or NULL for an SQL NULL) when the
 * value fits, 0 when it doesn't. */
typedef struct ob_value {
    int kind; /* SQLITE_INTEGER, SQLITE_FLOAT (as text), SQLITE_TEXT, SQLITE_BLOB, SQLITE_NULL */
    long long i;
    const char *text; size_t n;
} ob_value;

/* The value to store for text typed into a field of type t; result kind
 * SQLITE_INTEGER (i), SQLITE_TEXT (buf), or SQLITE_NULL for "". */
int ob_value_from_text(ob_type t, const char *s, const ob_formats *f, ob_value *v,
                       char *buf, size_t size);

#define OB_UNUSED(x) ((void)(x))

#endif
