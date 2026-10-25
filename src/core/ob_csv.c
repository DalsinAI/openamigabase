/* OpenBase core: CSV import and export (RFC 4180), and the import
 * wizard's guess (Design-OpenBase.md, 8).
 *
 * The reader keeps one record at a time, at most max_record bytes, so a
 * file of any size takes the same memory. It follows RFC 4180 and is
 * lenient where files in the wild need it: LF or CR line ends as well as
 * CR LF, a quote inside an unquoted value kept as it is, spaces after a
 * closing quote skipped, a UTF-8 byte order mark skipped.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "ob_internal.h"

#include <stdio.h>
#include <string.h>

void ob_csv_defaults(ob_csv_options *o)
{
    memset(o, 0, sizeof *o);
    o->separator = ',';
    o->quote = '"';
    o->header = 1;
    o->charset = OB_CHARSET_AUTO;
    ob_formats_defaults(&o->formats);
    o->formats.date_order = OB_DATE_AUTO;
    o->guess_rows = 200;
    o->max_record = 1024L * 1024L;
    o->max_fields = OB_FIELDS_MAX;
    o->bad_value = OB_CSV_SKIP_ROW;
    o->crlf = 1;
}

/* ---- Streams -------------------------------------------------------------- */

long ob_mem_read(void *ctx, void *buf, long size)
{
    ob_mem_source *m = (ob_mem_source *)ctx;
    long n = m->size - m->pos;
    if (n > size) n = size;
    if (n <= 0) return 0;
    memcpy(buf, m->data + m->pos, (size_t)n);
    m->pos += n;
    return n;
}

long ob_file_read(void *ctx, void *buf, long size)
{
    FILE *f = (FILE *)ctx;
    size_t n = fread(buf, 1, (size_t)size, f);
    if (n == 0 && ferror(f)) return -1;
    return (long)n;
}

int ob_file_write(void *ctx, const void *buf, long size)
{
    return fwrite(buf, 1, (size_t)size, (FILE *)ctx) == (size_t)size ? 0 : -1;
}

int ob_buf_write(void *ctx, const void *buf, long size)
{
    return ob_buf_add((ob_buf *)ctx, (const char *)buf, (size_t)size) ? -1 : 0;
}

/* ---- The reader --------------------------------------------------------- */

#define IN_SIZE 4096

struct ob_csv_reader {
    ob_read_fn read;
    void *ctx;
    ob_csv_options o;
    unsigned char in[IN_SIZE];
    long in_len, in_pos;
    int eof, ioerr, started;
    ob_buf raw;           /* the record's bytes as read, each field NUL-terminated */
    ob_buf dec;           /* the record turned into UTF-8 */
    size_t *starts;       /* where each field starts in raw */
    unsigned char *quoted;
    char **ptrs;
    int nfields, cap;
    long line, rec_line;
    ob_charset charset;
    const char *why;
    char why_buf[96];
};

static int get_byte(ob_csv_reader *r)
{
    if (r->in_pos >= r->in_len) {
        long n;
        if (r->eof) return -1;
        n = r->read(r->ctx, r->in, IN_SIZE);
        if (n < 0) {
            r->ioerr = 1;
            r->eof = 1;
            return -2;
        }
        if (n == 0) {
            r->eof = 1;
            return -1;
        }
        r->in_len = n > IN_SIZE ? IN_SIZE : n;
        r->in_pos = 0;
    }
    return r->in[r->in_pos++];
}

/* At least n bytes waiting (or the end of the file): for the byte order
 * mark and the charset, whatever sizes the stream reads in */
static int fill_at_least(ob_csv_reader *r, long n)
{
    if (n > IN_SIZE) n = IN_SIZE;
    if (r->in_pos > 0 && r->in_len - r->in_pos < n) {
        memmove(r->in, r->in + r->in_pos, (size_t)(r->in_len - r->in_pos));
        r->in_len -= r->in_pos;
        r->in_pos = 0;
    }
    while (r->in_len - r->in_pos < n && !r->eof) {
        long got = r->read(r->ctx, r->in + r->in_len, IN_SIZE - r->in_len);
        if (got < 0) {
            r->ioerr = 1;
            r->eof = 1;
            return -2;
        }
        if (got == 0) r->eof = 1;
        r->in_len += got > IN_SIZE - r->in_len ? IN_SIZE - r->in_len : got;
    }
    return 0;
}

static int peek_byte(ob_csv_reader *r)
{
    int c = get_byte(r);
    if (c >= 0) r->in_pos--;
    return c;
}

ob_csv_reader *ob_csv_reader_new(ob_read_fn read, void *ctx, const ob_csv_options *o, ob_error *err)
{
    ob_csv_reader *r;
    if (!read || !o) return ob_fail(err, OB_ERR_INVALID, "Nothing to read."), (ob_csv_reader *)NULL;
    if (!o->separator || o->separator == '\n' || o->separator == '\r' || (o->quote && o->quote == o->separator) ||
        o->quote == '\n' || o->quote == '\r' || ((unsigned char)o->separator & 0x80) || ((unsigned char)o->quote & 0x80))
        return ob_fail(err, OB_ERR_INVALID, "The separator and the quote must be two different plain characters."),
               (ob_csv_reader *)NULL;
    r = (ob_csv_reader *)ob_zalloc(sizeof *r);
    if (!r) return ob_nomem(err), (ob_csv_reader *)NULL;
    r->read = read;
    r->ctx = ctx;
    r->o = *o;
    if (r->o.max_record <= 0) r->o.max_record = 1024L * 1024L;
    if (r->o.max_fields <= 0 || r->o.max_fields > 4096) r->o.max_fields = OB_FIELDS_MAX;
    ob_buf_init(&r->raw, (size_t)r->o.max_record + (size_t)r->o.max_fields + 16);
    ob_buf_init(&r->dec, (size_t)r->o.max_record * 2 + (size_t)r->o.max_fields + 16);
    r->line = 1;
    r->charset = o->charset;
    return r;
}

void ob_csv_reader_free(ob_csv_reader *r)
{
    if (!r) return;
    ob_buf_free(&r->raw);
    ob_buf_free(&r->dec);
    ob_free(r->starts);
    ob_free(r->quoted);
    ob_free(r->ptrs);
    ob_free(r);
}

long ob_csv_line(const ob_csv_reader *r) { return r->rec_line; }
const char *ob_csv_reader_why(const ob_csv_reader *r) { return r->why ? r->why : ""; }
ob_charset ob_csv_charset(const ob_csv_reader *r) { return r->charset; }
int ob_csv_quoted(const ob_csv_reader *r, int i) { return i >= 0 && i < r->nfields && r->quoted[i]; }

static void bad(ob_csv_reader *r, const char *why)
{
    if (!r->why) r->why = why;
}

static int new_field(ob_csv_reader *r, int quoted)
{
    if (r->nfields >= r->o.max_fields) {
        snprintf(r->why_buf, sizeof r->why_buf, "it has more than %d values", r->o.max_fields);
        bad(r, r->why_buf);
        return 0;
    }
    if (r->nfields == r->cap) {
        int cap = r->cap ? r->cap * 2 : 16;
        size_t *s = (size_t *)ob_realloc(r->starts, sizeof(size_t) * (size_t)cap);
        unsigned char *q;
        if (!s) return -1;
        r->starts = s;
        q = (unsigned char *)ob_realloc(r->quoted, (size_t)cap);
        if (!q) return -1;
        r->quoted = q;
        r->cap = cap;
    }
    r->starts[r->nfields] = r->raw.len;
    r->quoted[r->nfields] = (unsigned char)quoted;
    r->nfields++;
    return 1;
}

static int add_byte(ob_csv_reader *r, int c)
{
    if (r->why) return 0; /* a bad record is read to its end, not kept */
    if (c == 0) {
        bad(r, "it holds a zero byte");
        return 0;
    }
    if ((long)r->raw.len >= r->o.max_record) {
        snprintf(r->why_buf, sizeof r->why_buf, "it is longer than %ld bytes", r->o.max_record);
        bad(r, r->why_buf);
        return 0;
    }
    return ob_buf_char(&r->raw, (char)c) == OB_ERR_NOMEM ? -1 : 0;
}

static int end_field(ob_csv_reader *r) { return r->why ? 0 : (ob_buf_char(&r->raw, 0) == OB_ERR_NOMEM ? -1 : 0); }

/* The charset for AUTO, from the first block: UTF-8 if it is (a character
 * cut by the block's end is fine), else ISO-8859-1. */
static void decide_charset(ob_csv_reader *r)
{
    const unsigned char *in = r->in + r->in_pos;
    long n = r->in_len - r->in_pos, cut = n, back = 0;
    if (r->charset != OB_CHARSET_AUTO) return;
    /* a character the block's end cuts is left out of the test */
    while (back < 3 && cut > 0 && (in[cut - 1] & 0xC0) == 0x80) cut--, back++;
    if (cut > 0 && (in[cut - 1] & 0xC0) == 0xC0) cut--; /* its first byte */
    else cut = n;
    r->charset = ob_utf8_valid((const char *)in, (size_t)cut) ? OB_CHARSET_UTF8 : OB_CHARSET_LATIN1;
}

static int start(ob_csv_reader *r, ob_error *err)
{
    int c, skip = r->o.skip_rows;
    r->started = 1;
    if (fill_at_least(r, IN_SIZE) < 0) return ob_fail(err, OB_ERR_IO, "The file can't be read.");
    c = peek_byte(r);
    /* a UTF-8 byte order mark */
    if (c == 0xEF && r->in_len - r->in_pos >= 3 && r->in[r->in_pos + 1] == 0xBB && r->in[r->in_pos + 2] == 0xBF) {
        r->in_pos += 3;
        if (r->charset == OB_CHARSET_AUTO) r->charset = OB_CHARSET_UTF8;
    }
    decide_charset(r);
    while (skip > 0) {
        c = get_byte(r);
        if (c == -2) return ob_fail(err, OB_ERR_IO, "The file can't be read.");
        if (c < 0) break;
        if (c == '\n') r->line++, skip--;
        else if (c == '\r') {
            if (peek_byte(r) == '\n') get_byte(r);
            r->line++, skip--;
        }
    }
    return OB_OK;
}

/* Turn the raw record into UTF-8 fields */
static int decode(ob_csv_reader *r)
{
    int i, latin1 = r->charset == OB_CHARSET_LATIN1;
    if (!latin1) {
        int valid = 1;
        for (i = 0; i < r->nfields && valid; i++) {
            const char *f = r->raw.data + r->starts[i];
            valid = ob_utf8_valid(f, strlen(f));
        }
        if (!valid) {
            if (r->o.charset == OB_CHARSET_UTF8) {
                bad(r, "it isn't UTF-8 text (try ISO-8859-1)");
                return 0;
            }
            latin1 = 1; /* AUTO: this record is ISO-8859-1 */
        }
    }
    ob_free(r->ptrs);
    r->ptrs = (char **)ob_alloc(sizeof(char *) * (size_t)(r->nfields + 1));
    if (!r->ptrs) return -1;
    if (!latin1) {
        for (i = 0; i < r->nfields; i++) r->ptrs[i] = r->raw.data + r->starts[i];
    } else {
        size_t *at = (size_t *)ob_alloc(sizeof(size_t) * (size_t)r->nfields);
        if (!at) return -1;
        ob_buf_reset(&r->dec);
        for (i = 0; i < r->nfields; i++) {
            const char *f = r->raw.data + r->starts[i];
            at[i] = r->dec.len;
            if (ob_latin1_to_utf8(f, strlen(f), &r->dec) || ob_buf_char(&r->dec, 0)) {
                ob_free(at);
                return r->dec.failed == OB_ERR_NOMEM ? -1 : (bad(r, "it is too long"), 0);
            }
        }
        for (i = 0; i < r->nfields; i++) r->ptrs[i] = r->dec.data + at[i];
        ob_free(at);
    }
    r->ptrs[r->nfields] = NULL;
    return 0;
}

int ob_csv_next(ob_csv_reader *r, int *nfields, char ***fields, ob_error *err)
{
    enum { FIELD_START, UNQUOTED, QUOTED, QUOTE_SEEN, AFTER_QUOTE } state;
    int c, rc, any;
    const int sep = (unsigned char)r->o.separator, quote = (unsigned char)r->o.quote;
    *nfields = 0;
    *fields = NULL;
    if (!r->started && (rc = start(r, err))) return OB_CSV_FAIL;
again:
    ob_buf_reset(&r->raw);
    r->nfields = 0;
    r->why = NULL;
    r->rec_line = r->line;
    state = FIELD_START;
    any = 0;
    for (;;) {
        c = get_byte(r);
        if (c == -2) {
            ob_fail(err, OB_ERR_IO, "The file can't be read (line %ld).", r->line);
            return OB_CSV_FAIL;
        }
        if (c >= 0) any = 1;
        if (state == QUOTED) {
            if (c == -1) {
                bad(r, "a quoted value isn't closed before the end of the file");
                break;
            }
            if (c == quote) {
                state = QUOTE_SEEN;
                continue;
            }
            if (c == '\n' || (c == '\r' && peek_byte(r) != '\n')) r->line++;
            if (add_byte(r, c) < 0) goto nomem;
            continue;
        }
        if (state == QUOTE_SEEN && c == quote) {
            if (add_byte(r, quote) < 0) goto nomem;
            state = QUOTED;
            continue;
        }
        if ((state == QUOTE_SEEN || state == AFTER_QUOTE) && (c == ' ' || c == '\t') && c != sep) {
            state = AFTER_QUOTE; /* spaces after a closing quote */
            continue;
        }
        if (c == -1 || c == '\n' || c == '\r') {
            if (c == '\r' && peek_byte(r) == '\n') get_byte(r);
            if (c >= 0) r->line++;
            if (state == FIELD_START && r->nfields == 0) {
                if (c == -1 && !any) return OB_CSV_END;
                if (c != -1 && r->raw.len == 0) goto again; /* a blank line */
                if (c == -1) return OB_CSV_END;
            }
            if (state == FIELD_START) {
                if ((rc = new_field(r, 0)) < 0) goto nomem;
            }
            if (end_field(r) < 0) goto nomem;
            break;
        }
        if (state == FIELD_START) {
            if (c == sep) {
                if ((rc = new_field(r, 0)) < 0 || end_field(r) < 0) goto nomem;
                continue;
            }
            if (quote && c == quote) {
                if ((rc = new_field(r, 1)) < 0) goto nomem;
                state = QUOTED;
                continue;
            }
            if ((rc = new_field(r, 0)) < 0) goto nomem;
            state = UNQUOTED;
            if (add_byte(r, c) < 0) goto nomem;
            continue;
        }
        if (c == sep) {
            if (end_field(r) < 0) goto nomem;
            state = FIELD_START;
            continue;
        }
        if (state == QUOTE_SEEN || state == AFTER_QUOTE) {
            bad(r, "there is text after a closing quote");
            state = UNQUOTED;
        }
        if (add_byte(r, c) < 0) goto nomem;
    }
    if (r->why) return OB_CSV_BAD;
    if (decode(r) < 0) goto nomem;
    if (r->why) return OB_CSV_BAD;
    *nfields = r->nfields;
    *fields = r->ptrs;
    return OB_CSV_RECORD;
nomem:
    ob_nomem(err);
    return OB_CSV_FAIL;
}

/* ---- The wizard's guess ------------------------------------------------------ */

typedef struct {
    long nonempty, longest;
    int multiline, not_int, not_num, not_cur, has_sym, not_yesno, yesno_word;
    int not_date_dmy, not_date_mdy, not_date_ymd, not_dt_dmy, not_dt_mdy, not_dt_ymd, not_time;
    int too_precise;
} col_stats;

/* Leading zeros ("007"), a leading '+' ("+44..."), or more than 15
 * significant digits: a code, not a number. */
static int code_like(const char *s)
{
    const char *p = ob_skip_space(s);
    int digits = 0, significant = 0;
    if (*p == '+') return 1;
    if (*p == '-') p++;
    if (p[0] == '0' && p[1] >= '0' && p[1] <= '9') return 1;
    for (; *p; p++)
        if (*p >= '0' && *p <= '9') {
            if (*p != '0' || significant) significant++;
            digits++;
        }
    OB_UNUSED(digits);
    return significant > 15;
}

static int has_currency_symbol(const char *s)
{
    return strstr(s, "\xC2\xA3") || strstr(s, "\xE2\x82\xAC") || strchr(s, '$') || strstr(s, "\xC2\xA5");
}

static void observe(col_stats *c, const char *v, const ob_formats *f)
{
    ob_formats d = *f;
    long long i;
    char buf[200];
    int yn;
    size_t chars;
    if (!*v) return;
    c->nonempty++;
    chars = ob_utf8_chars(v);
    if ((long)chars > c->longest) c->longest = (long)chars;
    if (strchr(v, '\n') || strchr(v, '\r')) c->multiline = 1;
    if (code_like(v)) c->not_int = c->not_num = c->not_cur = 1;
    if (!c->not_int && ob_parse_integer(v, f, &i)) c->not_int = 1;
    if (!c->not_num && ob_parse_number(v, f, buf, sizeof buf)) c->not_num = 1;
    if (!c->not_cur && ob_parse_currency(v, f, &i)) c->not_cur = 1;
    if (has_currency_symbol(v)) c->has_sym = 1;
    if (!c->not_yesno) {
        if (ob_parse_yesno(v, f, &yn)) c->not_yesno = 1;
        else if (!(v[0] >= '0' && v[0] <= '9') && v[0] != '-') c->yesno_word = 1;
    }
    d.date_order = OB_DATE_DMY;
    if (!c->not_date_dmy && ob_parse_date(v, &d, buf)) c->not_date_dmy = 1;
    if (!c->not_dt_dmy && (!strchr(v, ':') || ob_parse_datetime(v, &d, buf))) c->not_dt_dmy = 1;
    d.date_order = OB_DATE_MDY;
    if (!c->not_date_mdy && ob_parse_date(v, &d, buf)) c->not_date_mdy = 1;
    if (!c->not_dt_mdy && (!strchr(v, ':') || ob_parse_datetime(v, &d, buf))) c->not_dt_mdy = 1;
    d.date_order = OB_DATE_YMD;
    if (!c->not_date_ymd && ob_parse_date(v, &d, buf)) c->not_date_ymd = 1;
    if (!c->not_dt_ymd && (!strchr(v, ':') || ob_parse_datetime(v, &d, buf))) c->not_dt_ymd = 1;
    if (!c->not_time && ob_parse_time(v, buf)) c->not_time = 1;
}

static ob_type guess_type(const col_stats *c, ob_date_order order)
{
    int date_ok = order == OB_DATE_DMY ? !c->not_date_dmy : order == OB_DATE_MDY ? !c->not_date_mdy : !c->not_date_ymd;
    int dt_ok = order == OB_DATE_DMY ? !c->not_dt_dmy : order == OB_DATE_MDY ? !c->not_dt_mdy : !c->not_dt_ymd;
    if (!c->nonempty) return OB_TYPE_TEXT;
    if (!c->not_yesno && c->yesno_word) return OB_TYPE_YESNO;
    if (!c->not_int) return OB_TYPE_INTEGER;
    if (!c->not_cur && c->has_sym) return OB_TYPE_CURRENCY;
    if (!c->not_num) return OB_TYPE_NUMBER;
    if (date_ok) return OB_TYPE_DATE;
    if (dt_ok) return OB_TYPE_DATETIME;
    if (!c->not_time) return OB_TYPE_TIME;
    if (c->multiline || c->longest > 255) return OB_TYPE_MEMO;
    return OB_TYPE_TEXT;
}

/* A header value made into a field name: trimmed, no control
 * characters, at most OB_NAME_MAX bytes, not one of OpenBase's own. */
static void field_name(const char *in, int col, char out[OB_NAME_MAX + 1])
{
    char tmp[OB_NAME_MAX * 4 + 1];
    size_t n = 0;
    const char *p = ob_skip_space(in);
    for (; *p && n + 1 < sizeof tmp; p++) tmp[n++] = ((unsigned char)*p < 32 || *p == 127) ? ' ' : *p;
    tmp[n] = 0;
    n = ob_trim_len(tmp, n);
    tmp[n] = 0;
    tmp[ob_utf8_cut(tmp, OB_NAME_MAX - 4)] = 0;
    n = ob_trim_len(tmp, strlen(tmp));
    tmp[n] = 0;
    /* tmp is at most OB_NAME_MAX - 4 bytes here */
    if (!n) snprintf(out, OB_NAME_MAX + 1, "Field %d", col + 1);
    else if (ob_ncasecmp(tmp, "oadb_", 5) == 0 || ob_ncasecmp(tmp, "sqlite_", 7) == 0)
        snprintf(out, OB_NAME_MAX + 1, "Field_%.*s", OB_NAME_MAX - 6, tmp);
    else
        snprintf(out, OB_NAME_MAX + 1, "%.*s", OB_NAME_MAX, tmp);
}

static void unique_names(ob_csv_plan *plan)
{
    int i, j, k;
    for (i = 0; i < plan->ncolumns; i++)
        for (k = 2;; k++) {
            int clash = 0;
            for (j = 0; j < i; j++)
                if (ob_casecmp(plan->columns[i].name, plan->columns[j].name) == 0) clash = 1;
            if (!clash || k > 9999) break;
            {
                char base[OB_NAME_MAX + 1];
                size_t n;
                snprintf(base, sizeof base, "%s", plan->columns[i].name);
                /* drop a " 2" this loop added before */
                n = strlen(base);
                if (k > 2) {
                    while (n && base[n - 1] >= '0' && base[n - 1] <= '9') n--;
                    if (n && base[n - 1] == ' ') n--;
                    base[n] = 0;
                }
                base[ob_utf8_cut(base, OB_NAME_MAX - 6)] = 0;
                snprintf(plan->columns[i].name, sizeof plan->columns[i].name, "%.*s %d", OB_NAME_MAX - 6, base, k % 10000);
            }
        }
}

void ob_csv_plan_free(ob_csv_plan *plan)
{
    if (!plan) return;
    ob_free(plan->columns);
    ob_free(plan);
}

static int plan_columns(ob_csv_plan *plan, int n, col_stats **stats)
{
    ob_csv_column *c;
    col_stats *s;
    if (n <= plan->ncolumns) return 0;
    c = (ob_csv_column *)ob_realloc(plan->columns, sizeof(ob_csv_column) * (size_t)n);
    if (!c) return -1;
    plan->columns = c;
    s = (col_stats *)ob_realloc(*stats, sizeof(col_stats) * (size_t)n);
    if (!s) return -1;
    *stats = s;
    memset(&c[plan->ncolumns], 0, sizeof(ob_csv_column) * (size_t)(n - plan->ncolumns));
    memset(&s[plan->ncolumns], 0, sizeof(col_stats) * (size_t)(n - plan->ncolumns));
    plan->ncolumns = n;
    return 0;
}

int ob_csv_guess(ob_read_fn read, void *ctx, const ob_csv_options *o, ob_csv_plan **out, ob_error *err)
{
    ob_csv_reader *r;
    ob_csv_plan *plan;
    col_stats *stats = NULL;
    int rc = OB_OK, n, i, got, named = 0, dmy = 0, mdy = 0;
    char **f;
    long rows = 0, limit;
    *out = NULL;
    if (!o) return ob_fail(err, OB_ERR_INVALID, "No settings.");
    if (!(r = ob_csv_reader_new(read, ctx, o, err))) return err ? err->code : OB_ERR_INVALID;
    if (!(plan = (ob_csv_plan *)ob_zalloc(sizeof *plan))) {
        ob_csv_reader_free(r);
        return ob_nomem(err);
    }
    plan->key_column = -1;
    limit = o->guess_rows > 0 ? o->guess_rows : 200;
    if (o->header) {
        got = ob_csv_next(r, &n, &f, err);
        if (got == OB_CSV_FAIL) {
            rc = err ? err->code : OB_ERR_IO;
            goto done;
        }
        if (got == OB_CSV_BAD) {
            rc = ob_fail(err, OB_ERR_INVALID, "The header row can't be read: %s.", ob_csv_reader_why(r));
            goto done;
        }
        if (got == OB_CSV_RECORD) {
            if (plan_columns(plan, n, &stats)) goto nomem;
            for (i = 0; i < n; i++) field_name(f[i], i, plan->columns[i].name);
            named = n;
        }
    }
    while (rows < limit && (got = ob_csv_next(r, &n, &f, err)) != OB_CSV_END) {
        if (got == OB_CSV_FAIL) {
            rc = err ? err->code : OB_ERR_IO;
            goto done;
        }
        rows++;
        if (got == OB_CSV_BAD) continue;
        /* with a header, it says how many columns there are; extra values
         * are a bad row's, reported on import */
        if (named && n > named) n = named;
        if (plan_columns(plan, n, &stats)) goto nomem;
        for (i = 0; i < n; i++) {
            int ev;
            observe(&stats[i], f[i], &o->formats);
            ev = ob_date_order_evidence(f[i]);
            if (ev == OB_DATE_DMY) dmy++;
            else if (ev == OB_DATE_MDY) mdy++;
        }
    }
    for (i = named; i < plan->ncolumns; i++) field_name("", i, plan->columns[i].name);
    unique_names(plan);
    plan->rows_seen = rows;
    plan->charset = ob_csv_charset(r) == OB_CHARSET_AUTO ? OB_CHARSET_UTF8 : ob_csv_charset(r);
    plan->date_order = o->formats.date_order;
    if (plan->date_order == OB_DATE_AUTO) plan->date_order = mdy > dmy ? OB_DATE_MDY : OB_DATE_DMY;
    for (i = 0; i < plan->ncolumns; i++) {
        ob_csv_column *c = &plan->columns[i];
        c->type = guess_type(&stats[i], plan->date_order);
        /* ISO dates read the same in any order */
        if (c->type == OB_TYPE_TEXT && !stats[i].not_date_ymd && stats[i].nonempty) c->type = OB_TYPE_DATE;
        if (c->type == OB_TYPE_TEXT && !stats[i].not_dt_ymd && stats[i].nonempty) c->type = OB_TYPE_DATETIME;
        c->nonempty = stats[i].nonempty;
        c->longest = stats[i].longest;
        if (plan->key_column < 0 && c->type == OB_TYPE_INTEGER && ob_casecmp(c->name, "id") == 0) {
            plan->key_column = i;
            c->type = OB_TYPE_COUNTER;
        }
    }
    if (!plan->ncolumns) rc = ob_fail(err, OB_ERR_INVALID, "The file holds no values.");
    goto done;
nomem:
    rc = ob_nomem(err);
done:
    ob_free(stats);
    ob_csv_reader_free(r);
    if (rc) ob_csv_plan_free(plan);
    else *out = plan;
    return rc;
}

/* ---- Import -------------------------------------------------------------- */

static int bind_value(sqlite3_stmt *st, int at, const ob_value *v)
{
    switch (v->kind) {
    case SQLITE_INTEGER: return sqlite3_bind_int64(st, at, v->i);
    case SQLITE_TEXT: return sqlite3_bind_text(st, at, v->text, (int)v->n, SQLITE_TRANSIENT);
    default: return sqlite3_bind_null(st, at);
    }
}

/* Make the table from the plan */
static int make_table(ob_project *p, const ob_csv_plan *plan, const char *name, ob_report *rep, ob_error *err)
{
    ob_table *t;
    int i, rc;
    char counter[OB_NAME_MAX + 1] = "id";
    if (!(t = ob_table_new(name, 0, err))) return OB_ERR_NOMEM;
    if (plan->key_column < 0 || plan->key_column >= plan->ncolumns || plan->columns[plan->key_column].skip) {
        int k = 2;
        /* a Counter named "id", unless a column is */
        for (;;) {
            int clash = 0;
            for (i = 0; i < plan->ncolumns; i++)
                if (!plan->columns[i].skip && ob_casecmp(plan->columns[i].name, counter) == 0) clash = 1;
            if (!clash) break;
            snprintf(counter, sizeof counter, "id %d", k++);
        }
        if (!ob_table_add_field(t, counter, OB_TYPE_COUNTER, err)) {
            ob_table_free(t);
            return OB_ERR_NOMEM;
        }
    }
    for (i = 0; i < plan->ncolumns; i++) {
        const ob_csv_column *c = &plan->columns[i];
        ob_type type = c->type;
        if (c->skip) continue;
        if (i == plan->key_column) type = OB_TYPE_COUNTER;
        else if (type == OB_TYPE_COUNTER || type == OB_TYPE_LOOKUP) type = OB_TYPE_INTEGER;
        else if (type == OB_TYPE_OBJECT || (unsigned)type >= OB_TYPE_COUNT) {
            ob_report_add(rep, OB_NOTE_WARNING, 0, "%s is left out: a CSV file can't hold Datatype objects.", c->name);
            continue;
        }
        if (!ob_table_add_field(t, c->name, type, err)) {
            ob_table_free(t);
            return err ? err->code : OB_ERR_NOMEM;
        }
    }
    rc = ob_table_create_tx(p, t, err);
    ob_table_free(t);
    return rc;
}

int ob_csv_import(ob_project *p, ob_read_fn read, void *ctx, const ob_csv_options *o, const ob_csv_plan *plan,
                  const char *table, ob_report *rep, ob_csv_result *res, ob_error *err)
{
    ob_csv_result local;
    ob_csv_options opts;
    ob_csv_reader *r = NULL;
    ob_table *t = NULL;
    sqlite3_stmt *st = NULL;
    ob_buf sql;
    int *target = NULL; /* plan column -> table field, or -1 */
    int rc, i, n, got, nbind = 0, header_done;
    char **f;
    char buf[256];
    if (!res) res = &local;
    memset(res, 0, sizeof *res);
    if (!p || !o || !plan || !table) return ob_fail(err, OB_ERR_INVALID, "Nothing to import.");
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if (plan->ncolumns < 1) return ob_fail(err, OB_ERR_INVALID, "The import has no columns.");
    opts = *o;
    if (opts.formats.date_order == OB_DATE_AUTO) opts.formats.date_order = plan->date_order;
    if (opts.formats.date_order == OB_DATE_AUTO) opts.formats.date_order = OB_DATE_DMY;
    ob_buf_init(&sql, 0);
    if ((rc = ob_begin(p, err))) return rc;
    if (!ob_table_exists(p, table)) {
        if ((rc = ob_name_check(table, "table", err)) || (rc = make_table(p, plan, table, rep, err))) goto fail;
    }
    if ((rc = ob_table_read(p, table, &t, err))) goto fail;
    if (!(target = (int *)ob_alloc(sizeof(int) * (size_t)plan->ncolumns))) goto nomem;
    /* match columns to fields: by the plan's target, by name, or by place */
    for (i = 0; i < plan->ncolumns; i++) {
        const ob_csv_column *c = &plan->columns[i];
        const ob_field *fd = NULL;
        target[i] = -1;
        if (c->skip) continue;
        if (c->target[0]) fd = ob_table_field(t, c->target);
        else if (o->header || plan->columns[i].name[0]) fd = ob_table_field(t, c->name);
        if (!fd && !o->header && i < t->nfields) fd = &t->fields[i];
        if (!fd) {
            ob_report_add(rep, OB_NOTE_WARNING, 0, "%s is left out: %s has no field for it.", c->name, t->name);
            continue;
        }
        if (fd->type == OB_TYPE_OBJECT) {
            ob_report_add(rep, OB_NOTE_WARNING, 0, "%s is left out: a CSV file can't hold Datatype objects.", c->name);
            continue;
        }
        {
            int j, dup = 0;
            for (j = 0; j < i; j++)
                if (target[j] == (int)(fd - t->fields)) dup = 1;
            if (dup) {
                ob_report_add(rep, OB_NOTE_WARNING, 0, "%s is left out: another column already fills %s.", c->name, fd->name);
                continue;
            }
        }
        target[i] = (int)(fd - t->fields);
    }
    ob_buf_str(&sql, "INSERT INTO ");
    ob_sql_ident_quoted(&sql, t->name);
    ob_buf_str(&sql, " (");
    for (i = 0; i < plan->ncolumns; i++)
        if (target[i] >= 0) {
            if (nbind++) ob_buf_str(&sql, ", ");
            ob_sql_ident_quoted(&sql, t->fields[target[i]].name);
        }
    ob_buf_str(&sql, ") VALUES (");
    for (i = 0; i < nbind; i++) ob_buf_str(&sql, i ? ", ?" : "?");
    ob_buf_char(&sql, ')');
    if (!nbind) {
        rc = ob_fail(err, OB_ERR_INVALID, "No column of the file has a field in %s.", t->name);
        goto fail;
    }
    if (sql.failed) goto nomem;
    if ((rc = ob_prepare(p, ob_buf_text(&sql), &st, err))) goto fail;
    if (!(r = ob_csv_reader_new(read, ctx, &opts, err))) {
        rc = err ? err->code : OB_ERR_INVALID;
        goto fail;
    }
    header_done = !o->header;
    while ((got = ob_csv_next(r, &n, &f, err)) != OB_CSV_END) {
        int at = 0, row_bad = 0;
        long line = ob_csv_line(r);
        if (got == OB_CSV_FAIL) {
            rc = err ? err->code : OB_ERR_IO;
            goto fail;
        }
        if (!header_done) {
            header_done = 1;
            if (got == OB_CSV_RECORD) continue;
        }
        res->rows_read++;
        if (got == OB_CSV_BAD) {
            ob_report_add(rep, OB_NOTE_ERROR, line, "Left out: %s.", ob_csv_reader_why(r));
            res->rows_bad++;
            continue;
        }
        if (n < plan->ncolumns) {
            ob_report_add(rep, OB_NOTE_ERROR, line, "Left out: it has %d value%s; the file's rows have %d.", n, n == 1 ? "" : "s",
                          plan->ncolumns);
            res->rows_bad++;
            continue;
        }
        if (n > plan->ncolumns) {
            int extra = 0;
            for (i = plan->ncolumns; i < n; i++)
                if (*f[i]) extra = 1;
            if (extra) {
                ob_report_add(rep, OB_NOTE_ERROR, line, "Left out: it has %d values; the file's rows have %d.", n, plan->ncolumns);
                res->rows_bad++;
                continue;
            }
        }
        sqlite3_reset(st);
        sqlite3_clear_bindings(st);
        for (i = 0; i < plan->ncolumns && !row_bad; i++) {
            const ob_field *fd;
            ob_value v;
            if (target[i] < 0) continue;
            fd = &t->fields[target[i]];
            at++;
            if (!*f[i]) {
                /* "" is an empty text; nothing is no value */
                if (ob_csv_quoted(r, i) && ob_type_is_text(fd->type)) sqlite3_bind_text(st, at, "", 0, SQLITE_STATIC);
                continue;
            }
            if (ob_value_from_text(fd->type, f[i], &opts.formats, &v, buf, sizeof buf)) {
                char shown[48];
                snprintf(shown, sizeof shown, "%s", f[i]);
                shown[ob_utf8_cut(shown, 40)] = 0;
                if (opts.bad_value == OB_CSV_EMPTY_VALUE) {
                    ob_report_add(rep, OB_NOTE_WARNING, line, "%s left empty: \"%s\" isn't a %s.", fd->name, shown,
                                  ob_type_name(fd->type));
                    res->values_emptied++;
                    continue;
                }
                ob_report_add(rep, OB_NOTE_ERROR, line, "Left out: %s: \"%s\" isn't a %s.", fd->name, shown, ob_type_name(fd->type));
                row_bad = 1;
                break;
            }
            if (bind_value(st, at, &v) != SQLITE_OK) goto nomem;
        }
        if (row_bad) {
            res->rows_bad++;
            continue;
        }
        got = sqlite3_step(st);
        if (got == SQLITE_DONE) {
            res->rows_added++;
        } else if ((got & 0xFF) == SQLITE_CONSTRAINT || (got & 0xFF) == SQLITE_MISMATCH) {
            ob_error why;
            ob_sqlite_fail(p, got, &why, "adding a record");
            ob_report_add(rep, OB_NOTE_ERROR, line, "Left out: %s", why.text);
            res->rows_bad++;
        } else {
            rc = ob_sqlite_fail(p, got, err, "adding the records");
            goto fail;
        }
    }
    sqlite3_finalize(st);
    st = NULL;
    rc = ob_commit(p, err);
    goto done;
nomem:
    rc = ob_nomem(err);
fail:
    sqlite3_finalize(st);
    st = NULL;
    ob_rollback(p);
    res->rows_added = 0;
done:
    ob_csv_reader_free(r);
    ob_table_free(t);
    ob_free(target);
    ob_buf_free(&sql);
    return rc;
}

/* ---- Export ------------------------------------------------------------- */

typedef struct {
    ob_write_fn write;
    void *ctx;
    ob_buf out;   /* what's waiting to be written */
    ob_buf tmp;
    int failed;
} csv_writer;

static int flush(csv_writer *w)
{
    if (w->out.len && !w->failed && w->write(w->ctx, w->out.data, (long)w->out.len)) w->failed = 1;
    ob_buf_reset(&w->out);
    return w->failed;
}

static void put(csv_writer *w, const char *s, size_t n)
{
    if (ob_buf_add(&w->out, s, n)) w->failed = 1;
    if (w->out.len >= 8192) flush(w);
}

static void put_value(csv_writer *w, const ob_csv_options *o, const char *s, size_t n, int force_quote, long *lost,
                      ob_report *rep, long row)
{
    const char *text = s;
    size_t len = n, i;
    int need = force_quote || o->quote_all;
    if (o->charset == OB_CHARSET_LATIN1) {
        ob_buf_reset(&w->tmp);
        if (ob_utf8_to_latin1(s, n, &w->tmp, lost)) {
            w->failed = 1;
            return;
        }
        text = ob_buf_text(&w->tmp);
        len = w->tmp.len;
    }
    for (i = 0; i < len && !need; i++)
        if (text[i] == o->separator || (o->quote && text[i] == o->quote) || text[i] == '\n' || text[i] == '\r') need = 1;
    if (len && (text[0] == ' ' || text[len - 1] == ' ')) need = 1;
    if (!need) {
        put(w, text, len);
        return;
    }
    if (!o->quote) {
        /* nothing can quote it: the separators and line ends become spaces */
        for (i = 0; i < len; i++) {
            char c = text[i];
            if (c == o->separator || c == '\n' || c == '\r') c = ' ';
            put(w, &c, 1);
        }
        ob_report_add(rep, OB_NOTE_WARNING, row, "A value held the separator or a line end; without quotes they became spaces.");
        return;
    }
    put(w, &o->quote, 1);
    for (i = 0; i < len; i++) {
        if (text[i] == o->quote) put(w, &o->quote, 1);
        put(w, &text[i], 1);
    }
    put(w, &o->quote, 1);
}

static int export_rows(ob_project *p, sqlite3_stmt *st, const ob_type *types, int ntypes, ob_write_fn write, void *ctx,
                       const ob_csv_options *o, ob_report *rep, long *rows, ob_error *err)
{
    csv_writer w;
    int cols = sqlite3_column_count(st), i, rc;
    long lost = 0, count = 0, blobs = 0;
    const char *eol = o->crlf ? "\r\n" : "\n";
    char sep = o->separator ? o->separator : ',';
    memset(&w, 0, sizeof w);
    w.write = write;
    w.ctx = ctx;
    ob_buf_init(&w.out, 64 * 1024 + (size_t)o->max_record * 2);
    ob_buf_init(&w.tmp, 0);
    if (o->header) {
        for (i = 0; i < cols; i++) {
            const char *name = sqlite3_column_name(st, i);
            if (i) put(&w, &sep, 1);
            put_value(&w, o, name ? name : "", name ? strlen(name) : 0, 0, &lost, rep, 0);
        }
        put(&w, eol, strlen(eol));
    }
    while ((rc = sqlite3_step(st)) == SQLITE_ROW && !w.failed) {
        count++;
        for (i = 0; i < cols; i++) {
            ob_type t = types && i < ntypes ? types[i] : OB_TYPE_UNKNOWN;
            int kind = sqlite3_column_type(st, i);
            char buf[64];
            if (i) put(&w, &sep, 1);
            if (kind == SQLITE_NULL) continue;
            if (kind == SQLITE_BLOB) {
                blobs++;
                continue;
            }
            if (kind == SQLITE_INTEGER && t == OB_TYPE_CURRENCY) {
                ob_format_currency(sqlite3_column_int64(st, i), &o->formats, 0, buf, sizeof buf);
                put_value(&w, o, buf, strlen(buf), 0, &lost, rep, count);
            } else if (kind == SQLITE_INTEGER && t == OB_TYPE_YESNO) {
                const char *word = sqlite3_column_int64(st, i) ? o->formats.yes : o->formats.no;
                put_value(&w, o, word, strlen(word), 0, &lost, rep, count);
            } else if (kind == SQLITE_TEXT && ob_type_is_dated(t) && t != OB_TYPE_TIME) {
                const char *v = (const char *)sqlite3_column_text(st, i);
                char shown[48];
                ob_format_date(v ? v : "", &o->formats, shown, sizeof shown);
                put_value(&w, o, shown, strlen(shown), 0, &lost, rep, count);
            } else {
                const char *v = (const char *)sqlite3_column_text(st, i);
                size_t n = (size_t)sqlite3_column_bytes(st, i);
                if (!v) {
                    w.failed = 1;
                    break;
                }
                if (kind == SQLITE_FLOAT && o->formats.decimal == ',') {
                    char num[64];
                    size_t k;
                    snprintf(num, sizeof num, "%s", v);
                    for (k = 0; num[k]; k++)
                        if (num[k] == '.') num[k] = ',';
                    put_value(&w, o, num, strlen(num), 0, &lost, rep, count);
                } else
                    put_value(&w, o, v, n, kind == SQLITE_TEXT && n == 0, &lost, rep, count);
            }
        }
        put(&w, eol, strlen(eol));
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        rc = ob_sqlite_fail(p, rc, err, "reading the records");
    } else {
        flush(&w);
        rc = w.failed ? ob_fail(err, OB_ERR_IO, "The CSV file can't be written.") : OB_OK;
    }
    if (lost) ob_report_add(rep, OB_NOTE_WARNING, 0, "%ld character%s outside ISO-8859-1 became \"?\".", lost, lost == 1 ? "" : "s");
    if (blobs) ob_report_add(rep, OB_NOTE_WARNING, 0, "%ld binary value%s left out.", blobs, blobs == 1 ? " was" : "s were");
    ob_buf_free(&w.out);
    ob_buf_free(&w.tmp);
    if (rows) *rows = count;
    return rc;
}

int ob_csv_export_sql(ob_project *p, const char *sql, const ob_type *types, int ntypes, ob_write_fn write, void *ctx,
                      const ob_csv_options *o, ob_report *rep, long *rows, ob_error *err)
{
    sqlite3_stmt *st;
    int rc;
    if (rows) *rows = 0;
    if (!p || !sql || !write || !o) return ob_fail(err, OB_ERR_INVALID, "Nothing to export.");
    if (!o->separator || o->separator == o->quote) return ob_fail(err, OB_ERR_INVALID, "The separator and the quote must differ.");
    if ((rc = ob_prepare(p, sql, &st, err))) return rc;
    if (!sqlite3_stmt_readonly(st) || sqlite3_column_count(st) == 0) {
        sqlite3_finalize(st);
        return ob_fail(err, OB_ERR_INVALID, "Only a query (SELECT) can be exported.");
    }
    rc = export_rows(p, st, types, ntypes, write, ctx, o, rep, rows, err);
    sqlite3_finalize(st);
    return rc;
}

int ob_csv_export_table(ob_project *p, const char *table, ob_write_fn write, void *ctx, const ob_csv_options *o, ob_report *rep,
                        long *rows, ob_error *err)
{
    ob_table *t;
    ob_type *types = NULL;
    ob_buf sql;
    int rc, i, n = 0;
    if (rows) *rows = 0;
    if ((rc = ob_table_read(p, table, &t, err))) return rc;
    ob_buf_init(&sql, 0);
    if (!(types = (ob_type *)ob_alloc(sizeof(ob_type) * (size_t)(t->nfields + 1)))) {
        rc = ob_nomem(err);
        goto done;
    }
    ob_buf_str(&sql, "SELECT ");
    for (i = 0; i < t->nfields; i++) {
        if (t->fields[i].type == OB_TYPE_OBJECT) {
            ob_report_add(rep, OB_NOTE_WARNING, 0, "%s is left out: a CSV file can't hold Datatype objects.", t->fields[i].name);
            continue;
        }
        if (n) ob_buf_str(&sql, ", ");
        ob_sql_ident_quoted(&sql, t->fields[i].name);
        types[n++] = t->fields[i].type;
    }
    if (!n) {
        rc = ob_fail(err, OB_ERR_INVALID, "%s has no fields a CSV file can hold.", t->name);
        goto done;
    }
    ob_buf_str(&sql, " FROM ");
    ob_sql_ident_quoted(&sql, t->name);
    ob_buf_str(&sql, t->without_rowid ? "" : " ORDER BY rowid");
    if (sql.failed) {
        rc = ob_nomem(err);
        goto done;
    }
    rc = ob_csv_export_sql(p, ob_buf_text(&sql), types, n, write, ctx, o, rep, rows, err);
done:
    ob_free(types);
    ob_buf_free(&sql);
    ob_table_free(t);
    return rc;
}
