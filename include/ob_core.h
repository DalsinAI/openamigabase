/* OpenBase core: errors, memory, text buffers, formats, notes, and
 * projects (.oadb files). Portable C99; no Amiga calls.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#ifndef OB_CORE_H
#define OB_CORE_H

#include <stddef.h>

#define OB_VERSION_TEXT "OpenBase core 0.1 (B1)"

/* A .oadb project's header (Design-OpenBase.md, 3.1) */
#define OB_APPLICATION_ID 0x4F414442L /* "OADB" */
#define OB_FORMAT_VERSION 1           /* PRAGMA user_version */

/* ---- Errors ------------------------------------------------------------ */

enum {
    OB_OK = 0,
    OB_ERR_NOMEM,       /* out of memory */
    OB_ERR_IO,          /* a file couldn't be read or written */
    OB_ERR_NOT_PROJECT, /* not an OpenBase project */
    OB_ERR_DAMAGED,     /* cut short or damaged */
    OB_ERR_NEWER,       /* made by a newer OpenBase */
    OB_ERR_INVALID,     /* a bad name, value or argument */
    OB_ERR_EXISTS,      /* the name or file is taken */
    OB_ERR_NOT_FOUND,   /* no such table, field, query */
    OB_ERR_RULE,        /* a field's rule or a relationship refused it */
    OB_ERR_SQL,         /* SQLite refused the SQL */
    OB_ERR_UNSUPPORTED, /* OpenBase can't do that (yet) */
    OB_ERR_LIMIT,       /* over one of OpenBase's limits */
    OB_ERR_BUSY,        /* the project is in use */
    OB_ERR_READONLY     /* the project was opened read only */
};

/* Every call that can fail takes an ob_error (which may be NULL) and
 * returns its code. The text is plain words for the user. */
typedef struct ob_error {
    int code;
    char text[256];
} ob_error;

const char *ob_error_name(int code);

/* ---- Memory ------------------------------------------------------------ */

/* Every allocation OpenBase's core makes goes through these, so the Amiga
 * program can give the core an Exec pool, and the tests can fail the n-th
 * allocation. SQLite's own memory is set apart (ob_config). */
typedef struct ob_allocator {
    void *(*alloc)(void *ctx, size_t size);
    void *(*resize)(void *ctx, void *block, size_t size);
    void (*release)(void *ctx, void *block);
    void *ctx;
} ob_allocator;

void ob_set_allocator(const ob_allocator *a); /* NULL: the C library's */
void *ob_alloc(size_t size);
void *ob_zalloc(size_t size);
void *ob_realloc(void *block, size_t size);
void ob_free(void *block);
char *ob_strdup(const char *s);               /* NULL in, NULL out */
char *ob_strndup(const char *s, size_t n);

/* ---- Text buffers ------------------------------------------------------ */

/* A growing NUL-terminated buffer with a ceiling, so no single piece of
 * text (an SQL statement, a CSV record) can take all of an 8 MB machine. */
typedef struct ob_buf {
    char *data;
    size_t len, cap, max; /* max 0: OB_BUF_DEFAULT_MAX */
    int failed;           /* an append ran out of memory or over max */
} ob_buf;

#define OB_BUF_DEFAULT_MAX (1024L * 1024L)

void ob_buf_init(ob_buf *b, size_t max);
void ob_buf_free(ob_buf *b);
void ob_buf_reset(ob_buf *b);
int ob_buf_add(ob_buf *b, const char *s, size_t n);
int ob_buf_str(ob_buf *b, const char *s);
int ob_buf_char(ob_buf *b, char c);
int ob_buf_long(ob_buf *b, long long v);
const char *ob_buf_text(const ob_buf *b); /* "" when empty */

/* ---- Notes (the import report) ----------------------------------------- */

enum { OB_NOTE_INFO, OB_NOTE_WARNING, OB_NOTE_ERROR };

typedef struct ob_note {
    long line;  /* the file's line, from 1; 0: not about a line */
    int level;
    char text[160];
} ob_note;

/* Holds the first `max` notes and counts the rest. */
typedef struct ob_report {
    int count, max;
    long total, errors, warnings;
    ob_note *notes;
} ob_report;

ob_report *ob_report_new(int max); /* max 0: 100 */
void ob_report_free(ob_report *r);
void ob_report_add(ob_report *r, int level, long line, const char *fmt, ...);

/* ---- Formats: how dates and numbers are written ------------------------ */

typedef enum {
    OB_DATE_YMD,  /* 2026-10-25, the stored form */
    OB_DATE_DMY,  /* 25/10/2026 */
    OB_DATE_MDY,  /* 10/25/2026 */
    OB_DATE_AUTO  /* reading only: ISO, else day first unless a value says otherwise */
} ob_date_order;

typedef struct ob_formats {
    ob_date_order date_order;
    char date_sep;   /* written DMY and MDY dates: '/' */
    int year_pivot;  /* two-digit years below it are 20xx, others 19xx: 50 */
    char decimal;    /* '.' or ',' */
    char thousands;  /* ',', '.', ' ', '\'' or 0 for none */
    char yes[8], no[8]; /* how Yes/No is written: "Yes", "No" */
} ob_formats;

void ob_formats_defaults(ob_formats *f);

/* ---- The SQLite config point (decided by phase B0) --------------------- */

/* Everything B0 measures on the 68040 is set here and only here. Today's
 * defaults are SQLite's own, so the host tests run on any SQLite. */
typedef struct ob_config {
    const char *vfs;          /* NULL: SQLite's default. B0: "unix-dotfile" or "unix-none" */
    const char *journal_mode; /* NULL: SQLite's default (DELETE). B0 measures TRUNCATE */
    long cache_kib;           /* page cache; 0: SQLite's default (about 2 MB) */
    long heap_limit_kib;      /* sqlite3_soft_heap_limit64; 0: none */
    int page_size;            /* new projects; 0: SQLite's default */
    int busy_ms;              /* wait for a lock; 0: don't */
    const char *temp_dir;     /* sqlite3_temp_directory (process-wide): SQLite's Unix
                                 code can't find one on AmigaDOS. NULL: leave it */
    long max_object_kib;      /* a Datatype object's limit (Prefs): 4096 */
} ob_config;

void ob_config_defaults(ob_config *c);
/* The page cache for a machine with this much free memory (11.3):
 * 256 KB at 8 MB, rising to 2 MB at 32 MB and above. */
long ob_config_cache_for(unsigned long free_bytes);

/* ---- Projects ---------------------------------------------------------- */

typedef struct ob_project ob_project;
struct sqlite3;

enum { OB_OPEN_READONLY = 1, OB_OPEN_NO_CHECK = 2 };

/* A new project. Refuses a file that exists. ":memory:" makes one in memory. */
int ob_project_create(const char *path, const char *title, const ob_config *cfg,
                      ob_project **out, ob_error *err);
/* Opens a project: checks its header, its format version and runs
 * PRAGMA quick_check (unless OB_OPEN_NO_CHECK). */
int ob_project_open(const char *path, unsigned flags, const ob_config *cfg,
                    ob_project **out, ob_error *err);
void ob_project_close(ob_project *p);

/* full: PRAGMA integrity_check (Tools), else quick_check (on open) */
int ob_project_check(ob_project *p, int full, ob_error *err);
int ob_project_save_copy(ob_project *p, const char *path, ob_error *err); /* VACUUM INTO */
int ob_project_compact(ob_project *p, ob_error *err);                     /* VACUUM */

/* oadb_meta: "title", "start_form", options. get gives "" for a key not set. */
int ob_meta_get(ob_project *p, const char *key, char *buf, size_t size, ob_error *err);
int ob_meta_set(ob_project *p, const char *key, const char *value, ob_error *err);

/* Runs SQL with no results; for the program's own statements and tests. */
int ob_exec(ob_project *p, const char *sql, ob_error *err);
/* The first column of the first row as a number (counts, checks). */
int ob_sql_long(ob_project *p, const char *sql, long long *out, ob_error *err);

/* The project's formats (oadb_meta: date_order, decimal, thousands): how
 * dates and numbers are typed in it, used by changes of type and criteria. */
const ob_formats *ob_project_formats(ob_project *p);
int ob_project_set_formats(ob_project *p, const ob_formats *f, ob_error *err);

struct sqlite3 *ob_project_db(ob_project *p);
const ob_config *ob_project_config(ob_project *p);
int ob_project_readonly(ob_project *p);

/* Is this the start of an OpenBase project (Shared 2.1's owf_file_kind)?
 * file_size < 0: unknown. 0 when it is, else an OB_ERR_ code. */
int ob_header_check(const unsigned char *head, size_t n, long long file_size, ob_error *err);

#endif
