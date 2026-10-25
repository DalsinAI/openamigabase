/* OpenBase core: CSV import and export (RFC 4180), with the import
 * wizard's settings as an API (Design-OpenBase.md, 8). Records are read
 * one at a time, so a file of any size takes the same memory.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#ifndef OB_CSV_H
#define OB_CSV_H

#include "ob_schema.h"

typedef enum { OB_CHARSET_UTF8, OB_CHARSET_LATIN1, OB_CHARSET_AUTO } ob_charset;

enum {
    OB_CSV_SKIP_ROW,   /* a value that doesn't fit its field: the row is left out */
    OB_CSV_EMPTY_VALUE /* the value is left empty and the row goes in */
};

typedef struct ob_csv_options {
    char separator;      /* ',' (or ';', '\t', '|'...) */
    char quote;          /* '"'; 0: no quoting */
    int header;          /* the first row holds the field names */
    ob_charset charset;  /* AUTO: UTF-8 if the start of the file is, else ISO-8859-1 */
    ob_formats formats;  /* dates and numbers in the file */
    int skip_rows;       /* lines to skip before the header or the data */
    int guess_rows;      /* rows read to guess types: 200 */
    long max_record;     /* bytes in one record: 1 MB */
    int max_fields;      /* fields in one record: OB_FIELDS_MAX */
    int bad_value;       /* OB_CSV_SKIP_ROW or OB_CSV_EMPTY_VALUE */
    /* export */
    int crlf;            /* line ends: 1 CR LF (RFC 4180), 0 LF */
    int quote_all;       /* quote every value, not only those that need it */
} ob_csv_options;

void ob_csv_defaults(ob_csv_options *o);

/* Where bytes come from and go to */
typedef long (*ob_read_fn)(void *ctx, void *buf, long size);       /* bytes, 0 at the end, -1 error */
typedef int (*ob_write_fn)(void *ctx, const void *buf, long size); /* 0 done */

typedef struct ob_mem_source { const char *data; long size, pos; } ob_mem_source;
long ob_mem_read(void *ctx, void *buf, long size);
long ob_file_read(void *ctx, void *buf, long size);           /* ctx: a FILE * */
int ob_file_write(void *ctx, const void *buf, long size);     /* ctx: a FILE * */
int ob_buf_write(void *ctx, const void *buf, long size);      /* ctx: an ob_buf * */

/* ---- The reader: one record at a time --------------------------------- */

typedef struct ob_csv_reader ob_csv_reader;

enum { OB_CSV_RECORD = 1, OB_CSV_END = 0, OB_CSV_BAD = -1, OB_CSV_FAIL = -2 };

ob_csv_reader *ob_csv_reader_new(ob_read_fn read, void *ctx, const ob_csv_options *o,
                                 ob_error *err);
/* OB_CSV_RECORD: a record, its fields UTF-8 and NUL-terminated;
 * OB_CSV_BAD: a record that can't be read (why: ob_csv_reader_why), and
 * reading can go on; OB_CSV_END; OB_CSV_FAIL: reading stopped (err). */
int ob_csv_next(ob_csv_reader *r, int *nfields, char ***fields, ob_error *err);
/* Was this field quoted? ("" is an empty text; nothing is no value) */
int ob_csv_quoted(const ob_csv_reader *r, int field);
long ob_csv_line(const ob_csv_reader *r); /* the line the record started on */
const char *ob_csv_reader_why(const ob_csv_reader *r);
ob_charset ob_csv_charset(const ob_csv_reader *r); /* after AUTO */
void ob_csv_reader_free(ob_csv_reader *r);

/* ---- The wizard: what the file holds ------------------------------------ */

typedef struct ob_csv_column {
    char name[OB_NAME_MAX + 1]; /* from the header, made a good field name */
    ob_type type;               /* guessed; change it before importing */
    int skip;                   /* 1: leave this column out */
    long nonempty;              /* values seen in the guess rows */
    long longest;               /* in characters */
    char target[OB_NAME_MAX + 1]; /* importing into a table that exists: its field ("" by name or place) */
} ob_csv_column;

typedef struct ob_csv_plan {
    int ncolumns;
    ob_csv_column *columns;
    int key_column;          /* a column that becomes the Counter key; -1: a new "id" */
    ob_charset charset;      /* found (AUTO) or as asked */
    ob_date_order date_order;/* found (AUTO) or as asked */
    long rows_seen;
} ob_csv_plan;

/* Reads the header and the first guess_rows rows, and guesses each
 * column's type. */
int ob_csv_guess(ob_read_fn read, void *ctx, const ob_csv_options *o, ob_csv_plan **out,
                 ob_error *err);
void ob_csv_plan_free(ob_csv_plan *plan);

typedef struct ob_csv_result {
    long rows_read, rows_added, rows_bad, values_emptied;
} ob_csv_result;

/* Imports into `table`: made from the plan when it doesn't exist, else
 * added to (columns matched by name, or by place without a header).
 * Bad rows are listed in the report and left out; everything else goes in
 * one transaction. A failure to read or write stops it and changes nothing. */
int ob_csv_import(ob_project *p, ob_read_fn read, void *ctx, const ob_csv_options *o,
                  const ob_csv_plan *plan, const char *table, ob_report *rep,
                  ob_csv_result *res, ob_error *err);

/* ---- Export ---------------------------------------------------------- */

/* A table, written by its fields' types (Currency as 1234.50, Yes/No as
 * the formats' words, dates in the formats' order). Datatype objects are
 * left out and noted. NULL is written as nothing and an empty text as "". */
int ob_csv_export_table(ob_project *p, const char *table, ob_write_fn write, void *ctx,
                        const ob_csv_options *o, ob_report *rep, long *rows, ob_error *err);
/* Any SELECT; types[i] (may be NULL, or OB_TYPE_UNKNOWN) says how to write column i. */
int ob_csv_export_sql(ob_project *p, const char *sql, const ob_type *types, int ntypes,
                      ob_write_fn write, void *ctx, const ob_csv_options *o, ob_report *rep,
                      long *rows, ob_error *err);

#endif
