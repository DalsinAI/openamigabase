/* OpenBase core: field types, typed text to stored values and back, and
 * the conversions a change of type makes. No floating point anywhere:
 * numbers are handled as decimal text.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "ob_internal.h"

#include <stdio.h>
#include <string.h>

static const struct {
    const char *key, *name, *sql;
} types[OB_TYPE_COUNT] = {
    { "counter", "Counter", "INTEGER" },
    { "text", "Text", "TEXT" },
    { "memo", "Memo", "TEXT" },
    { "integer", "Integer", "INTEGER" },
    { "number", "Number", "REAL" },
    { "currency", "Currency", "INTEGER" },
    { "date", "Date", "TEXT" },
    { "time", "Time", "TEXT" },
    { "datetime", "Date and time", "TEXT" },
    { "yesno", "Yes/No", "INTEGER" },
    { "choice", "Choice", "TEXT" },
    { "lookup", "Lookup", "INTEGER" },
    { "object", "Datatype object", "BLOB" }
};

const char *ob_type_name(ob_type t) { return (unsigned)t < OB_TYPE_COUNT ? types[t].name : "Expression"; }
const char *ob_type_key(ob_type t) { return (unsigned)t < OB_TYPE_COUNT ? types[t].key : "unknown"; }
const char *ob_type_sql(ob_type t) { return (unsigned)t < OB_TYPE_COUNT ? types[t].sql : ""; }

int ob_type_from_key(const char *key, ob_type *out)
{
    int i;
    if (!key) return OB_ERR_INVALID;
    for (i = 0; i < OB_TYPE_COUNT; i++)
        if (ob_casecmp(key, types[i].key) == 0 || ob_casecmp(key, types[i].name) == 0) {
            *out = (ob_type)i;
            return OB_OK;
        }
    return OB_ERR_INVALID;
}

int ob_type_is_text(ob_type t) { return t == OB_TYPE_TEXT || t == OB_TYPE_MEMO || t == OB_TYPE_CHOICE; }
int ob_type_is_dated(ob_type t) { return t == OB_TYPE_DATE || t == OB_TYPE_TIME || t == OB_TYPE_DATETIME; }
int ob_type_is_whole(ob_type t)
{
    return t == OB_TYPE_COUNTER || t == OB_TYPE_INTEGER || t == OB_TYPE_CURRENCY || t == OB_TYPE_YESNO ||
           t == OB_TYPE_LOOKUP;
}

/* ---- Numbers ------------------------------------------------------------- */

#define LL_MAX 9223372036854775807LL

static int is_digit(int c) { return c >= '0' && c <= '9'; }

static const ob_formats *formats_or_default(const ob_formats *f, ob_formats *tmp)
{
    if (f) return f;
    ob_formats_defaults(tmp);
    return tmp;
}

/* Digits with optional thousands separators in groups of three. Copies
 * the digits to out (if not NULL); returns the characters used, 0 if none. */
static size_t grouped_digits(const char *s, char thousands, char *out, size_t size, size_t *ndigits)
{
    size_t i = 0, n = 0, group = 0;
    int grouped = 0;
    *ndigits = 0;
    while (s[i]) {
        if (is_digit((unsigned char)s[i])) {
            if (out && n + 1 < size) out[n] = s[i];
            n++;
            group++;
            i++;
        } else if (thousands && s[i] == thousands && group > 0 && is_digit((unsigned char)s[i + 1])) {
            if ((grouped && group != 3) || (!grouped && group > 3)) break;
            /* the next group must be exactly three digits */
            if (!(is_digit((unsigned char)s[i + 1]) && is_digit((unsigned char)s[i + 2]) &&
                  is_digit((unsigned char)s[i + 3]) && !is_digit((unsigned char)s[i + 4])))
                break;
            grouped = 1;
            group = 0;
            i++;
        } else
            break;
    }
    if (grouped && group != 3) return 0;
    if (out && size) out[n < size ? n : size - 1] = 0;
    *ndigits = n;
    return i;
}

int ob_parse_integer(const char *s, const ob_formats *f, long long *out)
{
    ob_formats tmp;
    const char *p;
    char digits[32];
    size_t used, nd, i;
    unsigned long long v = 0;
    int neg = 0;
    char th;
    f = formats_or_default(f, &tmp);
    th = f->thousands != f->decimal ? f->thousands : 0;
    p = ob_skip_space(s);
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    used = grouped_digits(p, th, digits, sizeof digits, &nd);
    if (!used || nd >= sizeof digits) return OB_ERR_INVALID;
    p += used;
    if (*ob_skip_space(p)) return OB_ERR_INVALID;
    for (i = 0; i < nd; i++) {
        unsigned d = (unsigned)(digits[i] - '0');
        if (v > (9223372036854775808ULL - d) / 10) return OB_ERR_INVALID;
        v = v * 10 + d;
    }
    if (!neg && v > (unsigned long long)LL_MAX) return OB_ERR_INVALID;
    if (neg) *out = v == 9223372036854775808ULL ? (-LL_MAX - 1) : -(long long)v;
    else *out = (long long)v;
    return OB_OK;
}

/* A number as canonical text: [-]int[.frac][e[-]exp] */
static int parse_number(const char *s, char decimal, char thousands, char *out, size_t size)
{
    const char *p = ob_skip_space(s);
    char intd[64], fracd[64];
    size_t used, nint = 0, nfrac = 0, n = 0;
    int neg = 0, exp = 0, has_exp = 0, eneg = 0;
    const char *q;
    char tmp[200];
    if (thousands == decimal) thousands = 0;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    used = grouped_digits(p, thousands, intd, sizeof intd, &nint);
    if (nint >= sizeof intd) return OB_ERR_INVALID;
    p += used;
    if (*p == decimal) {
        p++;
        while (is_digit((unsigned char)*p)) {
            if (nfrac + 1 >= sizeof fracd) return OB_ERR_INVALID;
            fracd[nfrac++] = *p++;
        }
    }
    fracd[nfrac] = 0;
    if (!nint && !nfrac) return OB_ERR_INVALID;
    if (*p == 'e' || *p == 'E') {
        q = p + 1;
        if (*q == '-' || *q == '+') eneg = *q++ == '-';
        if (!is_digit((unsigned char)*q)) return OB_ERR_INVALID;
        while (is_digit((unsigned char)*q)) {
            if (exp > 999) return OB_ERR_INVALID;
            exp = exp * 10 + (*q++ - '0');
        }
        has_exp = 1;
        p = q;
    }
    if (*ob_skip_space(p)) return OB_ERR_INVALID;
    /* leading zeros of the whole part go, trailing ones of the fraction too */
    q = intd;
    while (*q == '0' && q[1]) q++;
    while (nfrac && fracd[nfrac - 1] == '0') fracd[--nfrac] = 0;
    n = (size_t)snprintf(tmp, sizeof tmp, "%s%s%s%s", neg ? "-" : "", *q ? q : "0", nfrac ? "." : "", fracd);
    if (has_exp && n < sizeof tmp) snprintf(tmp + n, sizeof tmp - n, "e%s%d", eneg ? "-" : "", exp);
    if (strcmp(tmp, "-0") == 0) strcpy(tmp, "0");
    if (strlen(tmp) + 1 > size) return OB_ERR_LIMIT;
    strcpy(out, tmp);
    return OB_OK;
}

int ob_parse_number(const char *s, const ob_formats *f, char *out, size_t size)
{
    ob_formats tmp;
    f = formats_or_default(f, &tmp);
    return parse_number(s, f->decimal, f->thousands, out, size);
}

/* A canonical decimal ("-12.5e3") times 10^scale, if that is a whole
 * number that fits; else OB_ERR_INVALID. */
static int decimal_scaled(const char *canon, int scale, long long *out)
{
    const char *p = canon;
    char digits[160];
    size_t nd = 0, i;
    int neg = 0, e = 0, eneg = 0;
    unsigned long long v = 0;
    if (*p == '-') neg = 1, p++;
    while (is_digit((unsigned char)*p)) {
        if (nd + 1 >= sizeof digits) return OB_ERR_INVALID;
        digits[nd++] = *p++;
    }
    if (*p == '.') {
        p++;
        while (is_digit((unsigned char)*p)) {
            if (nd + 1 >= sizeof digits) return OB_ERR_INVALID;
            digits[nd++] = *p++;
            scale--;
        }
    }
    if (*p == 'e') {
        p++;
        if (*p == '-') eneg = 1, p++;
        while (is_digit((unsigned char)*p)) e = e * 10 + (*p++ - '0');
        scale += eneg ? -e : e;
    }
    /* drop leading zeros */
    i = 0;
    while (i < nd && digits[i] == '0') i++;
    if (i == nd) {
        *out = 0;
        return OB_OK;
    }
    memmove(digits, digits + i, nd - i);
    nd -= i;
    while (scale < 0) {
        if (!nd || digits[nd - 1] != '0') return OB_ERR_INVALID; /* a fraction would be lost */
        nd--;
        scale++;
    }
    if ((long)nd + scale > 19) return OB_ERR_INVALID;
    for (i = 0; i < nd; i++) {
        unsigned d = (unsigned)(digits[i] - '0');
        if (v > (9223372036854775808ULL - d) / 10) return OB_ERR_INVALID;
        v = v * 10 + d;
    }
    for (; scale > 0; scale--) {
        if (v > 9223372036854775808ULL / 10) return OB_ERR_INVALID;
        v *= 10;
    }
    if (!neg && v > (unsigned long long)LL_MAX) return OB_ERR_INVALID;
    if (v > 9223372036854775808ULL) return OB_ERR_INVALID;
    *out = neg ? (v == 9223372036854775808ULL ? -LL_MAX - 1 : -(long long)v) : (long long)v;
    return OB_OK;
}

/* Currency symbols, as UTF-8 */
static size_t currency_symbol(const char *p)
{
    static const char *const sym[] = { "\xC2\xA3", "\xE2\x82\xAC", "\xC2\xA5", "\xC2\xA4", "$" };
    size_t i;
    for (i = 0; i < sizeof sym / sizeof sym[0]; i++)
        if (strncmp(p, sym[i], strlen(sym[i])) == 0) return strlen(sym[i]);
    return 0;
}

int ob_parse_currency(const char *s, const ob_formats *f, long long *pence)
{
    ob_formats tmp;
    char work[128], canon[128];
    const char *p;
    size_t n = 0, k;
    int neg = 0, paren = 0;
    f = formats_or_default(f, &tmp);
    p = ob_skip_space(s);
    if (*p == '(') paren = 1, p++;
    /* sign and symbol in either order before the number, or a symbol after */
    for (;;) {
        p = ob_skip_space(p);
        if ((*p == '-' || *p == '+') && !neg) {
            neg = *p == '-';
            p++;
            if (*p == '-' || *p == '+') return OB_ERR_INVALID;
            continue;
        }
        if ((k = currency_symbol(p)) != 0) {
            p += k;
            continue;
        }
        break;
    }
    while (*p && n + 1 < sizeof work) {
        if ((k = currency_symbol(p)) != 0) {
            p += k;
            continue;
        }
        if (*p == ')' && paren) {
            paren = 2;
            p++;
            continue;
        }
        if (*p == '-' && !neg && n && (!*ob_skip_space(p + 1) || *ob_skip_space(p + 1) == ')')) {
            /* "12.50-" as some ledgers write it */
            neg = 1;
            p++;
            continue;
        }
        work[n++] = *p++;
    }
    if (*p || paren == 1) return OB_ERR_INVALID;
    work[n] = 0;
    if (paren == 2) neg = !neg;
    if (parse_number(work, f->decimal, f->thousands, canon, sizeof canon)) return OB_ERR_INVALID;
    if (strchr(canon, 'e')) return OB_ERR_INVALID;
    if (canon[0] == '-') return OB_ERR_INVALID; /* the sign was taken above */
    if (decimal_scaled(canon, 2, pence)) return OB_ERR_INVALID;
    if (neg) *pence = -*pence;
    return OB_OK;
}

int ob_format_currency(long long pence, const ob_formats *f, int grouping, char *out, size_t size)
{
    ob_formats tmp;
    char digits[32], res[48];
    unsigned long long u;
    int nd = 0, i, n = 0;
    f = formats_or_default(f, &tmp);
    u = pence < 0 ? (unsigned long long)0 - (unsigned long long)pence : (unsigned long long)pence;
    do {
        digits[nd++] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u || nd < 3);
    if (pence < 0) res[n++] = '-';
    for (i = nd - 1; i >= 2; i--) {
        res[n++] = digits[i];
        if (grouping && f->thousands && f->thousands != f->decimal && i > 2 && (i - 2) % 3 == 0)
            res[n++] = f->thousands;
    }
    res[n++] = f->decimal ? f->decimal : '.';
    res[n++] = digits[1];
    res[n++] = digits[0];
    res[n] = 0;
    if ((size_t)n + 1 > size) return OB_ERR_LIMIT;
    memcpy(out, res, (size_t)n + 1);
    return OB_OK;
}

/* ---- Dates and times ------------------------------------------------------ */

/* Fixed-width digits, for values already checked to fit */
static void put_digits(char *out, unsigned v, int n)
{
    while (n--) {
        out[n] = (char)('0' + v % 10);
        v /= 10;
    }
}

static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int days_in(int y, int m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && leap(y) ? 29 : d[m - 1];
}

static const char *const months[12] = { "january", "february", "march", "april", "may", "june",
                                        "july", "august", "september", "october", "november", "december" };

typedef struct {
    int n;
    int value[3];
    int digits[3]; /* 0: a month name */
} date_tokens;

static int month_word(const char *w, size_t n)
{
    int i;
    if (n < 3) return 0;
    for (i = 0; i < 12; i++) {
        size_t full = strlen(months[i]);
        if ((n == 3 || n == full || (n == 4 && i == 8)) && n <= full && ob_ncasecmp(w, months[i], n) == 0)
            return i + 1; /* "Sept" too */
    }
    return 0;
}

static int tokenize_date(const char *s, date_tokens *t)
{
    const char *p = ob_skip_space(s);
    t->n = 0;
    while (*p) {
        if (t->n == 3) return OB_ERR_INVALID;
        if (is_digit((unsigned char)*p)) {
            int v = 0, d = 0;
            while (is_digit((unsigned char)*p)) {
                if (d >= 4) return OB_ERR_INVALID;
                v = v * 10 + (*p++ - '0');
                d++;
            }
            /* 25th, 1st */
            if ((p[0] == 's' && p[1] == 't') || (p[0] == 'n' && p[1] == 'd') || (p[0] == 'r' && p[1] == 'd') ||
                (p[0] == 't' && p[1] == 'h'))
                p += 2;
            t->value[t->n] = v;
            t->digits[t->n++] = d;
        } else if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            const char *w = p;
            int m;
            while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) p++;
            m = month_word(w, (size_t)(p - w));
            if (!m) return OB_ERR_INVALID;
            if (*p == '.') p++;
            t->value[t->n] = m;
            t->digits[t->n++] = 0;
        } else
            return OB_ERR_INVALID;
        /* one separator, or a comma and a space */
        if (*p == ',') p++;
        if (*p == '-' || *p == '/' || *p == '.' || *p == ' ') p++;
        while (*p == ' ') p++;
    }
    return t->n == 3 ? OB_OK : OB_ERR_INVALID;
}

static int year_of(int v, int digits, int pivot)
{
    if (digits == 4) return v;
    if (digits == 2) return v < pivot ? 2000 + v : 1900 + v;
    return -1;
}

int ob_date_order_evidence(const char *s)
{
    date_tokens t;
    if (tokenize_date(s, &t) || !t.digits[0] || !t.digits[1] || t.digits[0] == 4) return -1;
    if (t.value[0] > 12 && t.value[1] <= 12) return OB_DATE_DMY;
    if (t.value[1] > 12 && t.value[0] <= 12) return OB_DATE_MDY;
    return -1;
}

int ob_parse_date(const char *s, const ob_formats *f, char out[11])
{
    ob_formats tmp;
    date_tokens t;
    int y, m, d, order;
    f = formats_or_default(f, &tmp);
    if (tokenize_date(s, &t)) return OB_ERR_INVALID;
    order = f->date_order;
    if (t.digits[0] == 4 && t.digits[1] && t.digits[2]) { /* ISO, whatever the order */
        y = t.value[0], m = t.value[1], d = t.value[2];
        if (t.digits[1] > 2 || t.digits[2] > 2) return OB_ERR_INVALID;
    } else if (!t.digits[1]) { /* 25 Dec 2026 */
        d = t.value[0], m = t.value[1], y = year_of(t.value[2], t.digits[2], f->year_pivot);
        if (t.digits[0] > 2) return OB_ERR_INVALID;
    } else if (!t.digits[0]) { /* Dec 25, 2026 */
        m = t.value[0], d = t.value[1], y = year_of(t.value[2], t.digits[2], f->year_pivot);
        if (t.digits[1] > 2) return OB_ERR_INVALID;
    } else if (!t.digits[2]) {
        return OB_ERR_INVALID;
    } else {
        if (t.digits[0] > 2 && order != OB_DATE_YMD) return OB_ERR_INVALID;
        if (order == OB_DATE_AUTO) {
            int ev = ob_date_order_evidence(s);
            order = ev < 0 ? OB_DATE_DMY : ev;
        }
        if (order == OB_DATE_YMD) {
            y = year_of(t.value[0], t.digits[0], f->year_pivot), m = t.value[1], d = t.value[2];
            if (t.digits[1] > 2 || t.digits[2] > 2) return OB_ERR_INVALID;
        } else {
            if (t.digits[0] > 2 || t.digits[1] > 2) return OB_ERR_INVALID;
            if (order == OB_DATE_DMY) d = t.value[0], m = t.value[1];
            else m = t.value[0], d = t.value[1];
            y = year_of(t.value[2], t.digits[2], f->year_pivot);
        }
    }
    if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1 || d > days_in(y, m)) return OB_ERR_INVALID;
    put_digits(out, (unsigned)y, 4);
    out[4] = '-';
    put_digits(out + 5, (unsigned)m, 2);
    out[7] = '-';
    put_digits(out + 8, (unsigned)d, 2);
    out[10] = 0;
    return OB_OK;
}

int ob_parse_time(const char *s, char out[9])
{
    const char *p = ob_skip_space(s);
    int h = 0, m = 0, sec = 0, nd = 0, ampm = 0;
    while (is_digit((unsigned char)*p) && nd < 2) h = h * 10 + (*p++ - '0'), nd++;
    if (!nd || *p != ':') return OB_ERR_INVALID;
    p++;
    if (!is_digit((unsigned char)p[0]) || !is_digit((unsigned char)p[1])) return OB_ERR_INVALID;
    m = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    if (*p == ':') {
        p++;
        if (!is_digit((unsigned char)p[0]) || !is_digit((unsigned char)p[1])) return OB_ERR_INVALID;
        sec = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
        if (*p == '.') { /* only a fraction of zeros: no part of a second is lost */
            p++;
            if (!is_digit((unsigned char)*p)) return OB_ERR_INVALID;
            while (*p == '0') p++;
            if (is_digit((unsigned char)*p)) return OB_ERR_INVALID;
        }
    }
    p = ob_skip_space(p);
    if (ob_ncasecmp(p, "am", 2) == 0 || ob_ncasecmp(p, "a.m.", 4) == 0) ampm = 1;
    else if (ob_ncasecmp(p, "pm", 2) == 0 || ob_ncasecmp(p, "p.m.", 4) == 0) ampm = 2;
    if (ampm) {
        p += p[1] == '.' ? 4 : 2;
        if (h < 1 || h > 12) return OB_ERR_INVALID;
        if (h == 12) h = 0;
        if (ampm == 2) h += 12;
    }
    if (*ob_skip_space(p)) return OB_ERR_INVALID;
    if (h < 0 || h > 23 || m > 59 || sec > 59) return OB_ERR_INVALID;
    put_digits(out, (unsigned)h, 2);
    out[2] = ':';
    put_digits(out + 3, (unsigned)m, 2);
    out[5] = ':';
    put_digits(out + 6, (unsigned)sec, 2);
    out[8] = 0;
    return OB_OK;
}

int ob_parse_datetime(const char *s, const ob_formats *f, char out[20])
{
    const char *colon = strchr(s, ':');
    char date[11], time[9], part[64];
    size_t split;
    if (!colon) {
        if (ob_parse_date(s, f, date)) return OB_ERR_INVALID;
        snprintf(out, 20, "%s 00:00:00", date);
        return OB_OK;
    }
    split = (size_t)(colon - s);
    while (split && s[split - 1] != ' ' && s[split - 1] != 'T') split--;
    if (!split || split >= sizeof part) return OB_ERR_INVALID;
    memcpy(part, s, split - 1);
    part[split - 1] = 0;
    if (ob_parse_date(part, f, date) || ob_parse_time(s + split, time)) return OB_ERR_INVALID;
    snprintf(out, 20, "%s %s", date, time);
    return OB_OK;
}

int ob_parse_yesno(const char *s, const ob_formats *f, int *out)
{
    static const char *const yes[] = { "yes", "true", "y", "on", "1", "-1", "x" };
    static const char *const no[] = { "no", "false", "n", "off", "0" };
    const char *p = ob_skip_space(s);
    size_t n = ob_trim_len(p, strlen(p)), i;
    if (f && f->yes[0] && strlen(f->yes) == n && ob_ncasecmp(p, f->yes, n) == 0) return *out = 1, OB_OK;
    if (f && f->no[0] && strlen(f->no) == n && ob_ncasecmp(p, f->no, n) == 0) return *out = 0, OB_OK;
    for (i = 0; i < sizeof yes / sizeof yes[0]; i++)
        if (strlen(yes[i]) == n && ob_ncasecmp(p, yes[i], n) == 0) return *out = 1, OB_OK;
    for (i = 0; i < sizeof no / sizeof no[0]; i++)
        if (strlen(no[i]) == n && ob_ncasecmp(p, no[i], n) == 0) return *out = 0, OB_OK;
    return OB_ERR_INVALID;
}

static int iso_date(const char *s)
{
    int i;
    for (i = 0; i < 10; i++)
        if (i == 4 || i == 7 ? s[i] != '-' : !is_digit((unsigned char)s[i])) return 0;
    return 1;
}

int ob_format_date(const char *iso, const ob_formats *f, char *out, size_t size)
{
    ob_formats tmp;
    char res[32];
    f = formats_or_default(f, &tmp);
    if (strlen(iso) < 10 || !iso_date(iso) || f->date_order == OB_DATE_YMD || f->date_order == OB_DATE_AUTO) {
        snprintf(res, sizeof res, "%s", iso);
    } else {
        char sep = f->date_sep ? f->date_sep : '/';
        if (f->date_order == OB_DATE_DMY)
            snprintf(res, sizeof res, "%.2s%c%.2s%c%.4s%s", iso + 8, sep, iso + 5, sep, iso, iso + 10);
        else
            snprintf(res, sizeof res, "%.2s%c%.2s%c%.4s%s", iso + 5, sep, iso + 8, sep, iso, iso + 10);
    }
    if (strlen(res) + 1 > size) return OB_ERR_LIMIT;
    strcpy(out, res);
    return OB_OK;
}

/* ---- Typed text into values -------------------------------------------- */

int ob_value_from_text(ob_type t, const char *s, const ob_formats *f, ob_value *v, char *buf, size_t size)
{
    long long i;
    int yn;
    v->kind = SQLITE_NULL;
    v->text = NULL;
    v->n = 0;
    v->i = 0;
    if (!s || !*s) return OB_OK;
    switch (t) {
    case OB_TYPE_TEXT:
    case OB_TYPE_MEMO:
    case OB_TYPE_CHOICE:
    case OB_TYPE_UNKNOWN:
        v->kind = SQLITE_TEXT;
        v->text = s;
        v->n = strlen(s);
        return OB_OK;
    case OB_TYPE_COUNTER:
    case OB_TYPE_INTEGER:
    case OB_TYPE_LOOKUP:
        if (ob_parse_integer(s, f, &i)) return OB_ERR_INVALID;
        v->kind = SQLITE_INTEGER;
        v->i = i;
        return OB_OK;
    case OB_TYPE_CURRENCY:
        if (ob_parse_currency(s, f, &i)) return OB_ERR_INVALID;
        v->kind = SQLITE_INTEGER;
        v->i = i;
        return OB_OK;
    case OB_TYPE_YESNO:
        if (ob_parse_yesno(s, f, &yn)) return OB_ERR_INVALID;
        v->kind = SQLITE_INTEGER;
        v->i = yn;
        return OB_OK;
    case OB_TYPE_NUMBER:
        if (ob_parse_number(s, f, buf, size)) return OB_ERR_INVALID;
        break;
    case OB_TYPE_DATE:
        if (size < 11 || ob_parse_date(s, f, buf)) return OB_ERR_INVALID;
        break;
    case OB_TYPE_TIME:
        if (size < 9 || ob_parse_time(s, buf)) return OB_ERR_INVALID;
        break;
    case OB_TYPE_DATETIME:
        if (size < 20 || ob_parse_datetime(s, f, buf)) return OB_ERR_INVALID;
        break;
    default:
        return OB_ERR_INVALID;
    }
    v->kind = SQLITE_TEXT;
    v->text = buf;
    v->n = strlen(buf);
    return OB_OK;
}

int ob_default_sql(ob_type t, const char *typed, const ob_formats *f, ob_buf *out, ob_error *err)
{
    char buf[64];
    ob_value v;
    const char *p = typed ? ob_skip_space(typed) : "";
    ob_buf_reset(out);
    if (!*p) return OB_OK;
    if (t == OB_TYPE_COUNTER || t == OB_TYPE_OBJECT)
        return ob_fail(err, OB_ERR_INVALID, "A %s field can't have a default.", ob_type_name(t));
    if (t == OB_TYPE_DATE && ob_casecmp(p, "today") == 0) return ob_buf_str(out, "(date('now','localtime'))");
    if (t == OB_TYPE_TIME && ob_casecmp(p, "now") == 0) return ob_buf_str(out, "(time('now','localtime'))");
    if (t == OB_TYPE_DATETIME && ob_casecmp(p, "now") == 0)
        return ob_buf_str(out, "(datetime('now','localtime'))");
    if (ob_value_from_text(t, typed, f, &v, buf, sizeof buf))
        return ob_fail(err, OB_ERR_INVALID, "\"%s\" isn't a %s.", typed, ob_type_name(t));
    if (v.kind == SQLITE_INTEGER) return ob_buf_long(out, v.i);
    if (t == OB_TYPE_NUMBER) return ob_buf_str(out, v.text);
    return ob_sql_text(out, v.text);
}

/* ---- Changing a value's type (ob_convert in SQL) ------------------------- */

typedef struct {
    int ok;
    int kind; /* SQLITE_INTEGER, SQLITE_TEXT, SQLITE_NULL, or 0: the value as it is */
    long long i;
    char text[200]; /* as long as parse_number's longest */
} conv_out;

static void conv_text(conv_out *o, const char *s)
{
    o->ok = 1;
    o->kind = SQLITE_TEXT;
    snprintf(o->text, sizeof o->text, "%s", s);
}

static void conv_int(conv_out *o, long long i)
{
    o->ok = 1;
    o->kind = SQLITE_INTEGER;
    o->i = i;
}

/* pence as plain decimal text: 1234.5 */
static void pence_text(long long pence, char *out, size_t size)
{
    ob_formats f;
    ob_formats_defaults(&f);
    ob_format_currency(pence, &f, 0, out, size);
}

static void convert(sqlite3_value *x, ob_type from, ob_type to, const ob_formats *f, conv_out *o)
{
    int kind = sqlite3_value_type(x);
    const char *txt;
    char canon[200];
    long long i;
    int yn;
    memset(o, 0, sizeof *o);
    if (kind == SQLITE_NULL || from == to) {
        o->ok = 1;
        return;
    }
    if (kind == SQLITE_BLOB) {
        o->ok = to == OB_TYPE_OBJECT;
        return;
    }
    txt = (const char *)sqlite3_value_text(x);
    if (!txt) return;
    switch (to) {
    case OB_TYPE_TEXT:
    case OB_TYPE_MEMO:
    case OB_TYPE_CHOICE:
        if (from == OB_TYPE_CURRENCY && kind == SQLITE_INTEGER) {
            pence_text(sqlite3_value_int64(x), canon, sizeof canon);
            conv_text(o, canon);
        } else if (from == OB_TYPE_YESNO && kind == SQLITE_INTEGER) {
            conv_text(o, sqlite3_value_int64(x) ? f->yes : f->no);
        } else
            o->ok = 1; /* as it is: TEXT affinity takes it */
        return;
    case OB_TYPE_COUNTER:
    case OB_TYPE_INTEGER:
    case OB_TYPE_LOOKUP:
        if (kind == SQLITE_INTEGER) {
            i = sqlite3_value_int64(x);
            if (from == OB_TYPE_CURRENCY) {
                if (i % 100 == 0) conv_int(o, i / 100);
            } else
                conv_int(o, i);
        } else if (kind == SQLITE_FLOAT) {
            if (!parse_number(txt, '.', 0, canon, sizeof canon) && !decimal_scaled(canon, 0, &i)) conv_int(o, i);
        } else if (!ob_parse_integer(txt, f, &i)) {
            conv_int(o, i);
        } else if (!ob_parse_number(txt, f, canon, sizeof canon) && !decimal_scaled(canon, 0, &i)) {
            conv_int(o, i);
        }
        return;
    case OB_TYPE_NUMBER:
        if (kind == SQLITE_INTEGER && from == OB_TYPE_CURRENCY) {
            pence_text(sqlite3_value_int64(x), canon, sizeof canon);
            conv_text(o, canon);
        } else if (kind == SQLITE_INTEGER || kind == SQLITE_FLOAT) {
            o->ok = 1;
        } else if (!ob_parse_number(txt, f, canon, sizeof canon)) {
            conv_text(o, canon);
        } else if (!ob_parse_currency(txt, f, &i)) {
            pence_text(i, canon, sizeof canon);
            conv_text(o, canon);
        }
        return;
    case OB_TYPE_CURRENCY:
        if (kind == SQLITE_INTEGER) {
            i = sqlite3_value_int64(x);
            if (i <= 92233720368547758LL && i >= -92233720368547758LL) conv_int(o, i * 100);
        } else if (kind == SQLITE_FLOAT) {
            if (!parse_number(txt, '.', 0, canon, sizeof canon) && !decimal_scaled(canon, 2, &i)) conv_int(o, i);
        } else if (!ob_parse_currency(txt, f, &i)) {
            conv_int(o, i);
        }
        return;
    case OB_TYPE_DATE:
        if (kind == SQLITE_TEXT) {
            char dt[20];
            if (!ob_parse_date(txt, f, canon)) conv_text(o, canon);
            else if (!ob_parse_datetime(txt, f, dt) && strcmp(dt + 10, " 00:00:00") == 0) {
                dt[10] = 0;
                conv_text(o, dt);
            }
        }
        return;
    case OB_TYPE_TIME:
        if (kind == SQLITE_TEXT && !ob_parse_time(txt, canon)) conv_text(o, canon);
        return;
    case OB_TYPE_DATETIME:
        if (kind == SQLITE_TEXT && !ob_parse_datetime(txt, f, canon)) conv_text(o, canon);
        return;
    case OB_TYPE_YESNO:
        if (kind == SQLITE_INTEGER || kind == SQLITE_FLOAT) {
            if (!parse_number(txt, '.', 0, canon, sizeof canon) && !decimal_scaled(canon, 0, &i) && (i == 0 || i == 1))
                conv_int(o, i);
        } else if (!ob_parse_yesno(txt, f, &yn))
            conv_int(o, yn);
        return;
    case OB_TYPE_OBJECT:
    default:
        return;
    }
}

/* ob_convert(x, 'from', 'to', clear, date_order, decimal) and
 * ob_convert_ok(x, 'from', 'to', date_order, decimal) */
static int convert_args(sqlite3_context *ctx, sqlite3_value **argv, int order_arg, ob_type *from, ob_type *to,
                        ob_formats *f)
{
    const char *dec;
    if (ob_type_from_key((const char *)sqlite3_value_text(argv[1]), from) ||
        ob_type_from_key((const char *)sqlite3_value_text(argv[2]), to)) {
        sqlite3_result_error(ctx, "ob_convert: unknown type", -1);
        return 0;
    }
    ob_formats_defaults(f);
    f->date_order = (ob_date_order)sqlite3_value_int(argv[order_arg]);
    if ((unsigned)f->date_order > OB_DATE_AUTO) f->date_order = OB_DATE_YMD;
    dec = (const char *)sqlite3_value_text(argv[order_arg + 1]);
    if (dec && *dec == ',') {
        f->decimal = ',';
        f->thousands = '.';
    }
    return 1;
}

static void fn_convert(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    ob_type from, to;
    ob_formats f;
    conv_out o;
    OB_UNUSED(argc);
    if (!convert_args(ctx, argv, 4, &from, &to, &f)) return;
    convert(argv[0], from, to, &f, &o);
    if (!o.ok) {
        if (sqlite3_value_int(argv[3])) sqlite3_result_null(ctx);
        else sqlite3_result_value(ctx, argv[0]);
    } else if (o.kind == SQLITE_INTEGER)
        sqlite3_result_int64(ctx, o.i);
    else if (o.kind == SQLITE_TEXT)
        sqlite3_result_text(ctx, o.text, -1, SQLITE_TRANSIENT);
    else
        sqlite3_result_value(ctx, argv[0]);
}

static void fn_convert_ok(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    ob_type from, to;
    ob_formats f;
    conv_out o;
    OB_UNUSED(argc);
    if (!convert_args(ctx, argv, 3, &from, &to, &f)) return;
    convert(argv[0], from, to, &f, &o);
    sqlite3_result_int(ctx, o.ok);
}

int ob_types_register(ob_project *p, ob_error *err)
{
    int flags = SQLITE_UTF8 | SQLITE_DETERMINISTIC;
    int rc;
#ifdef SQLITE_DIRECTONLY
    flags |= SQLITE_DIRECTONLY; /* never from a view or trigger in the file */
#endif
    rc = sqlite3_create_function(p->db, "ob_convert", 6, flags, NULL, fn_convert, NULL, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_create_function(p->db, "ob_convert_ok", 5, flags, NULL, fn_convert_ok, NULL, NULL);
    if (rc != SQLITE_OK) return ob_sqlite_fail(p, rc, err, "setting up the project");
    return OB_OK;
}
