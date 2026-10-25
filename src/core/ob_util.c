/* OpenBase core: memory, text buffers, notes, errors and UTF-8.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "ob_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Memory -------------------------------------------------------------- */

static void *libc_alloc(void *ctx, size_t n) { OB_UNUSED(ctx); return malloc(n); }
static void *libc_resize(void *ctx, void *b, size_t n) { OB_UNUSED(ctx); return realloc(b, n); }
static void libc_release(void *ctx, void *b) { OB_UNUSED(ctx); free(b); }

static ob_allocator g_alloc = { libc_alloc, libc_resize, libc_release, NULL };

void ob_set_allocator(const ob_allocator *a)
{
    if (a) g_alloc = *a;
    else {
        g_alloc.alloc = libc_alloc;
        g_alloc.resize = libc_resize;
        g_alloc.release = libc_release;
        g_alloc.ctx = NULL;
    }
}

void *ob_alloc(size_t n) { return g_alloc.alloc(g_alloc.ctx, n ? n : 1); }

void *ob_zalloc(size_t n)
{
    void *p = ob_alloc(n);
    if (p) memset(p, 0, n ? n : 1);
    return p;
}

void *ob_realloc(void *b, size_t n)
{
    if (!b) return ob_alloc(n);
    return g_alloc.resize(g_alloc.ctx, b, n ? n : 1);
}

void ob_free(void *b)
{
    if (b) g_alloc.release(g_alloc.ctx, b);
}

char *ob_strndup(const char *s, size_t n)
{
    char *d;
    if (!s) return NULL;
    d = (char *)ob_alloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

char *ob_strdup(const char *s) { return s ? ob_strndup(s, strlen(s)) : NULL; }

/* ---- Buffers ---------------------------------------------------------- */

void ob_buf_init(ob_buf *b, size_t max)
{
    b->data = NULL;
    b->len = b->cap = 0;
    b->max = max ? max : (size_t)OB_BUF_DEFAULT_MAX;
    b->failed = 0;
}

void ob_buf_free(ob_buf *b)
{
    ob_free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
    b->failed = 0;
}

void ob_buf_reset(ob_buf *b)
{
    b->len = 0;
    b->failed = 0;
    if (b->data) b->data[0] = 0;
}

int ob_buf_add(ob_buf *b, const char *s, size_t n)
{
    if (b->failed) return OB_ERR_NOMEM;
    if (n > b->max || b->len + n > b->max) {
        b->failed = OB_ERR_LIMIT;
        return OB_ERR_LIMIT;
    }
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        char *d;
        while (cap < b->len + n + 1) cap *= 2;
        if (cap > b->max + 1) cap = b->max + 1;
        d = (char *)ob_realloc(b->data, cap);
        if (!d) {
            b->failed = OB_ERR_NOMEM;
            return OB_ERR_NOMEM;
        }
        b->data = d;
        b->cap = cap;
    }
    if (n) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
    return OB_OK;
}

int ob_buf_str(ob_buf *b, const char *s) { return ob_buf_add(b, s, strlen(s)); }
int ob_buf_char(ob_buf *b, char c) { return ob_buf_add(b, &c, 1); }

int ob_buf_long(ob_buf *b, long long v)
{
    char tmp[24];
    int i = 23;
    unsigned long long u = v < 0 ? (unsigned long long)0 - (unsigned long long)v : (unsigned long long)v;
    tmp[i] = 0;
    do {
        tmp[--i] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    if (v < 0) tmp[--i] = '-';
    return ob_buf_str(b, tmp + i);
}

const char *ob_buf_text(const ob_buf *b) { return b->data ? b->data : ""; }

/* ---- Notes ------------------------------------------------------------ */

ob_report *ob_report_new(int max)
{
    ob_report *r = (ob_report *)ob_zalloc(sizeof *r);
    if (!r) return NULL;
    r->max = max > 0 ? max : 100;
    return r;
}

void ob_report_free(ob_report *r)
{
    if (!r) return;
    ob_free(r->notes);
    ob_free(r);
}

void ob_report_add(ob_report *r, int level, long line, const char *fmt, ...)
{
    va_list ap;
    ob_note *n;
    if (!r) return;
    r->total++;
    if (level == OB_NOTE_ERROR) r->errors++;
    else if (level == OB_NOTE_WARNING) r->warnings++;
    if (r->count >= r->max) return;
    if (!r->notes) {
        r->notes = (ob_note *)ob_zalloc(sizeof(ob_note) * (size_t)r->max);
        if (!r->notes) return; /* counted, not kept */
    }
    n = &r->notes[r->count++];
    n->line = line;
    n->level = level;
    va_start(ap, fmt);
    vsnprintf(n->text, sizeof n->text, fmt, ap);
    va_end(ap);
    n->text[ob_utf8_cut(n->text, sizeof n->text - 1)] = 0;
}

/* ---- Errors ----------------------------------------------------------- */

const char *ob_error_name(int code)
{
    static const char *const names[] = {
        "OK", "out of memory", "file error", "not an OpenBase project", "damaged",
        "made by a newer OpenBase", "not valid", "already exists", "not found",
        "refused by a rule", "SQL error", "not supported", "over a limit", "in use",
        "read only"
    };
    if (code < 0 || code >= (int)(sizeof names / sizeof names[0])) return "unknown error";
    return names[code];
}

int ob_fail(ob_error *err, int code, const char *fmt, ...)
{
    if (err) {
        va_list ap;
        err->code = code;
        va_start(ap, fmt);
        vsnprintf(err->text, sizeof err->text, fmt, ap);
        va_end(ap);
        err->text[ob_utf8_cut(err->text, sizeof err->text - 1)] = 0;
    }
    return code;
}

int ob_nomem(ob_error *err) { return ob_fail(err, OB_ERR_NOMEM, "There isn't enough memory."); }

/* ---- Text --------------------------------------------------------------- */

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int ob_casecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = lower((unsigned char)*a), y = lower((unsigned char)*b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
}

int ob_ncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int x = lower((unsigned char)*a), y = lower((unsigned char)*b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
    return 0;
}

int ob_is_space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

const char *ob_skip_space(const char *s)
{
    while (ob_is_space((unsigned char)*s)) s++;
    return s;
}

size_t ob_trim_len(const char *s, size_t n)
{
    while (n && ob_is_space((unsigned char)s[n - 1])) n--;
    return n;
}

/* Strict UTF-8: no overlongs, no surrogates, nothing past U+10FFFF. */
int ob_utf8_valid(const char *str, size_t n)
{
    const unsigned char *s = (const unsigned char *)str;
    size_t i = 0;
    while (i < n) {
        unsigned c = s[i];
        size_t k;
        unsigned long cp;
        if (c < 0x80) { i++; continue; }
        if (c >= 0xC2 && c <= 0xDF) { k = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { k = 2; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { k = 3; cp = c & 0x07; }
        else return 0;
        if (n - i <= k) return 0; /* cut short */
        {
            size_t j;
            for (j = 1; j <= k; j++) {
                if ((s[i + j] & 0xC0) != 0x80) return 0;
                cp = (cp << 6) | (s[i + j] & 0x3F);
            }
        }
        if ((k == 2 && cp < 0x800) || (k == 3 && (cp < 0x10000 || cp > 0x10FFFF))) return 0;
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
        i += k + 1;
    }
    return 1;
}

size_t ob_utf8_chars(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

size_t ob_utf8_cut(const char *s, size_t max)
{
    size_t n = strlen(s);
    if (n <= max) return n;
    n = max;
    while (n && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

int ob_latin1_to_utf8(const char *in, size_t n, ob_buf *out)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in[i];
        int rc;
        if (c < 0x80) rc = ob_buf_char(out, (char)c);
        else {
            char two[2];
            two[0] = (char)(0xC0 | (c >> 6));
            two[1] = (char)(0x80 | (c & 0x3F));
            rc = ob_buf_add(out, two, 2);
        }
        if (rc) return rc;
    }
    return OB_OK;
}

/* Characters past U+00FF become '?' and are counted in *lost. The input
 * is valid UTF-8 (it came from the database). */
int ob_utf8_to_latin1(const char *in, size_t n, ob_buf *out, long *lost)
{
    const unsigned char *s = (const unsigned char *)in;
    size_t i = 0;
    while (i < n) {
        unsigned c = s[i];
        unsigned long cp;
        size_t k;
        int rc;
        if (c < 0x80) { cp = c; k = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; k = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; k = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; k = 3; }
        else { cp = 0x100; k = 0; }
        if (i + k >= n && k) { cp = 0x100; k = n - i - 1; }
        else {
            size_t j;
            for (j = 1; j <= k; j++) cp = (cp << 6) | (s[i + j] & 0x3F);
        }
        if (cp > 0xFF) {
            if (lost) (*lost)++;
            cp = '?';
        }
        rc = ob_buf_char(out, (char)(unsigned char)cp);
        if (rc) return rc;
        i += k + 1;
    }
    return OB_OK;
}
