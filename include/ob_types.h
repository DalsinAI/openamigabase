/* OpenBase core: field types (Design-OpenBase.md, 4.1), and turning typed
 * text into stored values and back. No floating point: Number values stay
 * text until SQLite's REAL affinity takes them, and Currency is whole
 * pence or cents.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#ifndef OB_TYPES_H
#define OB_TYPES_H

#include "ob_core.h"

typedef enum {
    OB_TYPE_COUNTER,  /* INTEGER PRIMARY KEY */
    OB_TYPE_TEXT,     /* TEXT, an optional maximum length */
    OB_TYPE_MEMO,     /* TEXT, any length */
    OB_TYPE_INTEGER,  /* INTEGER */
    OB_TYPE_NUMBER,   /* REAL */
    OB_TYPE_CURRENCY, /* INTEGER: pence or cents */
    OB_TYPE_DATE,     /* TEXT YYYY-MM-DD */
    OB_TYPE_TIME,     /* TEXT HH:MM:SS */
    OB_TYPE_DATETIME, /* TEXT YYYY-MM-DD HH:MM:SS */
    OB_TYPE_YESNO,    /* INTEGER 0 or 1 */
    OB_TYPE_CHOICE,   /* TEXT, one of a list */
    OB_TYPE_LOOKUP,   /* INTEGER, another table's key */
    OB_TYPE_OBJECT,   /* BLOB, with name and type companion columns */
    OB_TYPE_COUNT,
    OB_TYPE_UNKNOWN = OB_TYPE_COUNT /* an expression's result */
} ob_type;

const char *ob_type_name(ob_type t);       /* "Date and time" */
const char *ob_type_key(ob_type t);        /* "datetime": as kept in oadb_fields */
int ob_type_from_key(const char *key, ob_type *out); /* key or name, any case */
const char *ob_type_sql(ob_type t);        /* "INTEGER", "TEXT", "REAL", "BLOB" */
int ob_type_is_text(ob_type t);            /* Text, Memo, Choice */
int ob_type_is_dated(ob_type t);           /* Date, Time, Date and time */
int ob_type_is_whole(ob_type t);           /* Counter, Integer, Currency, Yes/No, Lookup */

/* Parsers: 0 on success, OB_ERR_INVALID if the text isn't one. Spaces
 * around the value are ignored. */
int ob_parse_integer(const char *s, const ob_formats *f, long long *out);
/* A number, written back canonically ("-1234.5", "1.5e10") for SQLite. */
int ob_parse_number(const char *s, const ob_formats *f, char *out, size_t size);
/* Money: "1,234.50", "-£3", "(2.10)", "€ 4,5" with a decimal comma.
 * More than two decimals is refused unless they are zeros. */
int ob_parse_currency(const char *s, const ob_formats *f, long long *pence);
/* A date in the formats' order (ISO always works); month names in
 * English ("25 Dec 2026", "Dec 25, 2026"). Out: YYYY-MM-DD. */
int ob_parse_date(const char *s, const ob_formats *f, char out[11]);
int ob_parse_time(const char *s, char out[9]); /* 9:05, 09:05:30, 9:05 pm */
int ob_parse_datetime(const char *s, const ob_formats *f, char out[20]);
/* yes no true false y n on off 1 0, and the formats' own words (f may be NULL) */
int ob_parse_yesno(const char *s, const ob_formats *f, int *out);

/* Formatting for display and export */
int ob_format_currency(long long pence, const ob_formats *f, int grouping, char *out, size_t size);
int ob_format_date(const char *iso, const ob_formats *f, char *out, size_t size);

/* A date's order guessed from values: OB_DATE_DMY or OB_DATE_MDY when a
 * value proves it, else -1. */
int ob_date_order_evidence(const char *s);

/* Text typed into a field's default as SQL (a literal, or an expression in
 * brackets): "today" and "now" become the date and time when a record is
 * made. */
int ob_default_sql(ob_type t, const char *typed, const ob_formats *f, ob_buf *out, ob_error *err);

#endif
