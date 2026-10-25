/* OpenBase core: tables, fields, keys, indexes and relationships
 * (Design-OpenBase.md, 4.1 and 4.2).
 *
 * A table's SQL says its types, keys, NOT NULL, defaults, CHECK rules and
 * FOREIGN KEYs; oadb_fields says what SQL can't (the OpenBase type, label,
 * format, choices, the rule's words). A change that ALTER TABLE can't make
 * follows SQLite's own twelve steps (sqlite.org/lang_altertable.html,
 * "Making Other Kinds Of Table Schema Changes"): with foreign keys off,
 * in one transaction, make the new table, copy the rows, drop the old,
 * rename the new, put back indexes, triggers and views, check the foreign
 * keys, commit.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "ob_internal.h"

#include <stdio.h>
#include <string.h>

#define COMPANION_NAME "_name"
#define COMPANION_TYPE "_type"

/* ---- Strings and names ---------------------------------------------------- */

int ob_strset(char **dst, const char *src, ob_error *err)
{
    char *d = NULL;
    if (src && !(d = ob_strdup(src))) return ob_nomem(err);
    ob_free(*dst);
    *dst = d;
    return OB_OK;
}

int ob_name_check(const char *name, const char *what, ob_error *err)
{
    const unsigned char *p;
    if (!name || !*name) return ob_fail(err, OB_ERR_INVALID, "The %s needs a name.", what);
    if (strlen(name) > OB_NAME_MAX)
        return ob_fail(err, OB_ERR_INVALID, "The %s's name is too long (%d bytes at most).", what, OB_NAME_MAX);
    if (!ob_utf8_valid(name, strlen(name)))
        return ob_fail(err, OB_ERR_INVALID, "The %s's name isn't UTF-8 text.", what);
    for (p = (const unsigned char *)name; *p; p++)
        if (*p < 32 || *p == 127) return ob_fail(err, OB_ERR_INVALID, "The %s's name holds a control character.", what);
    if (ob_is_space((unsigned char)name[0]) || ob_is_space((unsigned char)name[strlen(name) - 1]))
        return ob_fail(err, OB_ERR_INVALID, "The %s's name can't start or end with a space.", what);
    if (ob_ncasecmp(name, "oadb_", 5) == 0 || ob_ncasecmp(name, "sqlite_", 7) == 0)
        return ob_fail(err, OB_ERR_INVALID, "Names starting \"oadb_\" or \"sqlite_\" are OpenBase's and SQLite's own.");
    return OB_OK;
}

/* ---- Designs in memory ------------------------------------------------------- */

static void field_clear(ob_field *f)
{
    ob_free(f->name);
    ob_free(f->was);
    ob_free(f->label);
    ob_free(f->format);
    ob_free(f->choices);
    ob_free(f->default_sql);
    ob_free(f->rule);
    ob_free(f->rule_text);
    ob_free(f->lookup_table);
    ob_free(f->lookup_show);
    memset(f, 0, sizeof *f);
}

void ob_relation_clear(ob_relation *r)
{
    int i;
    ob_free(r->child);
    ob_free(r->parent);
    for (i = 0; i < OB_KEY_MAX; i++) {
        ob_free(r->child_fields[i]);
        ob_free(r->parent_fields[i]);
    }
    memset(r, 0, sizeof *r);
}

static void index_clear(ob_index *ix)
{
    int i;
    ob_free(ix->name);
    for (i = 0; i < OB_INDEX_FIELDS; i++) ob_free(ix->fields[i]);
    memset(ix, 0, sizeof *ix);
}

void ob_table_free(ob_table *t)
{
    int i;
    if (!t) return;
    for (i = 0; i < t->nfields; i++) field_clear(&t->fields[i]);
    for (i = 0; i < t->nindexes; i++) index_clear(&t->indexes[i]);
    for (i = 0; i < t->nrelations; i++) ob_relation_clear(&t->relations[i]);
    for (i = 0; i < OB_KEY_MAX; i++) ob_free(t->key[i]);
    ob_free(t->fields);
    ob_free(t->indexes);
    ob_free(t->relations);
    ob_free(t->name);
    ob_free(t->was);
    ob_free(t);
}

ob_table *ob_table_new(const char *name, int with_counter, ob_error *err)
{
    ob_table *t = (ob_table *)ob_zalloc(sizeof *t);
    if (!t) return ob_nomem(err), (ob_table *)NULL;
    if (name && ob_strset(&t->name, name, err)) {
        ob_table_free(t);
        return NULL;
    }
    if (with_counter && !ob_table_add_field(t, "id", OB_TYPE_COUNTER, err)) {
        ob_table_free(t);
        return NULL;
    }
    return t;
}

ob_field *ob_table_field(const ob_table *t, const char *name)
{
    int i;
    if (!t || !name) return NULL;
    for (i = 0; i < t->nfields; i++)
        if (t->fields[i].name && ob_casecmp(t->fields[i].name, name) == 0) return &t->fields[i];
    return NULL;
}

ob_field *ob_table_add_field(ob_table *t, const char *name, ob_type type, ob_error *err)
{
    ob_field *f;
    if (t->nfields >= OB_FIELDS_MAX)
        return ob_fail(err, OB_ERR_LIMIT, "A table can have %d fields at most.", OB_FIELDS_MAX), (ob_field *)NULL;
    if ((unsigned)type >= OB_TYPE_COUNT) return ob_fail(err, OB_ERR_INVALID, "That isn't a field type."), (ob_field *)NULL;
    f = (ob_field *)ob_realloc(t->fields, sizeof(ob_field) * (size_t)(t->nfields + 1));
    if (!f) return ob_nomem(err), (ob_field *)NULL;
    t->fields = f;
    f = &t->fields[t->nfields];
    memset(f, 0, sizeof *f);
    if (ob_strset(&f->name, name, err)) return NULL;
    f->type = type;
    t->nfields++;
    return f;
}

int ob_table_remove_field(ob_table *t, const char *name, ob_error *err)
{
    ob_field *f = ob_table_field(t, name);
    int i;
    if (!f) return ob_fail(err, OB_ERR_NOT_FOUND, "There is no field called %s.", name ? name : "");
    i = (int)(f - t->fields);
    field_clear(f);
    memmove(&t->fields[i], &t->fields[i + 1], sizeof(ob_field) * (size_t)(t->nfields - i - 1));
    t->nfields--;
    return OB_OK;
}

ob_index *ob_table_add_index(ob_table *t, const char *field, int unique, ob_error *err)
{
    ob_index *ix = (ob_index *)ob_realloc(t->indexes, sizeof(ob_index) * (size_t)(t->nindexes + 1));
    if (!ix) return ob_nomem(err), (ob_index *)NULL;
    t->indexes = ix;
    ix = &t->indexes[t->nindexes++];
    memset(ix, 0, sizeof *ix);
    ix->unique = unique;
    if (field && ob_table_add_index_field(ix, field, err)) {
        t->nindexes--;
        index_clear(ix);
        return NULL;
    }
    return ix;
}

int ob_table_add_index_field(ob_index *ix, const char *field, ob_error *err)
{
    if (ix->nfields >= OB_INDEX_FIELDS)
        return ob_fail(err, OB_ERR_LIMIT, "An index can hold %d fields at most.", OB_INDEX_FIELDS);
    if (ob_strset(&ix->fields[ix->nfields], field, err)) return OB_ERR_NOMEM;
    ix->nfields++;
    return OB_OK;
}

ob_relation *ob_table_add_relation(ob_table *t, const char *field, const char *parent, ob_relrule on_delete,
                                   ob_error *err)
{
    ob_relation *r = (ob_relation *)ob_realloc(t->relations, sizeof(ob_relation) * (size_t)(t->nrelations + 1));
    if (!r) return ob_nomem(err), (ob_relation *)NULL;
    t->relations = r;
    r = &t->relations[t->nrelations];
    memset(r, 0, sizeof *r);
    if (ob_strset(&r->child, t->name, err) || ob_strset(&r->child_fields[0], field, err) ||
        ob_strset(&r->parent, parent, err)) {
        ob_relation_clear(r);
        return NULL;
    }
    r->nfields = 1;
    r->on_delete = on_delete;
    r->on_update = OB_REL_REFUSE;
    t->nrelations++;
    return r;
}

int ob_table_remove_relation(ob_table *t, const char *field)
{
    int i;
    for (i = 0; i < t->nrelations; i++)
        if (t->relations[i].child_fields[0] && ob_casecmp(t->relations[i].child_fields[0], field) == 0) {
            ob_relation_clear(&t->relations[i]);
            memmove(&t->relations[i], &t->relations[i + 1], sizeof(ob_relation) * (size_t)(t->nrelations - i - 1));
            t->nrelations--;
            return OB_OK;
        }
    return OB_ERR_NOT_FOUND;
}

static int copy_str(char **dst, const char *src, int *bad)
{
    if (src && !(*dst = ob_strdup(src))) *bad = 1;
    return 0;
}

static void relation_copy(ob_relation *d, const ob_relation *s, int *bad)
{
    int k;
    memset(d, 0, sizeof *d);
    d->nfields = s->nfields;
    d->on_delete = s->on_delete;
    d->on_update = s->on_update;
    copy_str(&d->child, s->child, bad);
    copy_str(&d->parent, s->parent, bad);
    for (k = 0; k < OB_KEY_MAX; k++) {
        copy_str(&d->child_fields[k], s->child_fields[k], bad);
        copy_str(&d->parent_fields[k], s->parent_fields[k], bad);
    }
}

ob_table *ob_table_copy(const ob_table *s, ob_error *err)
{
    ob_table *t = (ob_table *)ob_zalloc(sizeof *t);
    int i, k, bad = 0;
    if (!t) return ob_nomem(err), (ob_table *)NULL;
    copy_str(&t->name, s->name, &bad);
    copy_str(&t->was, s->was, &bad);
    t->adopted = s->adopted;
    t->without_rowid = s->without_rowid;
    t->nkey = s->nkey;
    for (k = 0; k < OB_KEY_MAX; k++) copy_str(&t->key[k], s->key[k], &bad);
    if (s->nfields) {
        if (!(t->fields = (ob_field *)ob_zalloc(sizeof(ob_field) * (size_t)s->nfields))) bad = 1;
        else {
            t->nfields = s->nfields;
            for (i = 0; i < s->nfields; i++) {
                const ob_field *a = &s->fields[i];
                ob_field *b = &t->fields[i];
                b->type = a->type;
                b->maxlen = a->maxlen;
                b->required = a->required;
                copy_str(&b->name, a->name, &bad);
                copy_str(&b->was, a->was, &bad);
                copy_str(&b->label, a->label, &bad);
                copy_str(&b->format, a->format, &bad);
                copy_str(&b->choices, a->choices, &bad);
                copy_str(&b->default_sql, a->default_sql, &bad);
                copy_str(&b->rule, a->rule, &bad);
                copy_str(&b->rule_text, a->rule_text, &bad);
                copy_str(&b->lookup_table, a->lookup_table, &bad);
                copy_str(&b->lookup_show, a->lookup_show, &bad);
            }
        }
    }
    if (s->nindexes) {
        if (!(t->indexes = (ob_index *)ob_zalloc(sizeof(ob_index) * (size_t)s->nindexes))) bad = 1;
        else {
            t->nindexes = s->nindexes;
            for (i = 0; i < s->nindexes; i++) {
                t->indexes[i].unique = s->indexes[i].unique;
                t->indexes[i].nfields = s->indexes[i].nfields;
                copy_str(&t->indexes[i].name, s->indexes[i].name, &bad);
                for (k = 0; k < OB_INDEX_FIELDS; k++) copy_str(&t->indexes[i].fields[k], s->indexes[i].fields[k], &bad);
            }
        }
    }
    if (s->nrelations) {
        if (!(t->relations = (ob_relation *)ob_zalloc(sizeof(ob_relation) * (size_t)s->nrelations))) bad = 1;
        else {
            t->nrelations = s->nrelations;
            for (i = 0; i < s->nrelations; i++) relation_copy(&t->relations[i], &s->relations[i], &bad);
        }
    }
    if (bad) {
        ob_table_free(t);
        return ob_nomem(err), (ob_table *)NULL;
    }
    return t;
}

/* ---- SQL for a design --------------------------------------------------------- */

static const char *rel_sql(ob_relrule r)
{
    return r == OB_REL_CASCADE ? "CASCADE" : r == OB_REL_CLEAR ? "SET NULL" : "RESTRICT";
}

static int add_ident(ob_buf *b, const char *a, const char *suffix)
{
    char tmp[OB_NAME_MAX + 16];
    if (!suffix) return ob_sql_ident_quoted(b, a);
    snprintf(tmp, sizeof tmp, "%s%s", a, suffix);
    return ob_sql_ident_quoted(b, tmp);
}

/* CONSTRAINT "Field:kind" CHECK (...) */
static int check_start(ob_buf *b, const char *field, const char *kind)
{
    char name[OB_NAME_MAX + 16];
    snprintf(name, sizeof name, "%s:%s", field, kind);
    ob_buf_str(b, " CONSTRAINT ");
    ob_sql_ident_quoted(b, name);
    return ob_buf_str(b, " CHECK (");
}

static int choices_sql(ob_buf *b, const char *choices)
{
    const char *p = choices;
    int first = 1;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n && p[n - 1] == '\r') n--;
        if (n) {
            char *one = ob_strndup(p, n);
            if (!one) return OB_ERR_NOMEM;
            if (!first) ob_buf_str(b, ", ");
            ob_sql_text(b, one);
            ob_free(one);
            first = 0;
        }
        if (!e) break;
        p = e + 1;
    }
    return first ? OB_ERR_INVALID : OB_OK;
}

/* A default SQLite takes as it is: a number, a string or blob literal,
 * NULL, TRUE, FALSE, CURRENT_*; anything else needs brackets. SQLite
 * reports an expression's default without them. */
static int default_is_literal(const char *s)
{
    static const char *const words[] = { "NULL", "TRUE", "FALSE", "CURRENT_TIME", "CURRENT_DATE", "CURRENT_TIMESTAMP" };
    const char *p = s;
    size_t i;
    if (*p == '(') return 1;
    if (*p == '\'' || ((*p == 'x' || *p == 'X') && p[1] == '\'')) {
        if (*p != '\'') p++;
        for (p++; *p; p++)
            if (*p == '\'') {
                if (p[1] == '\'') p++;
                else return p[1] == 0;
            }
        return 0;
    }
    for (i = 0; i < sizeof words / sizeof words[0]; i++)
        if (ob_casecmp(s, words[i]) == 0) return 1;
    if (*p == '-' || *p == '+') p++;
    if (!*p) return 0;
    for (; *p; p++)
        if (!((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+')) return 0;
    return 1;
}

static int field_sql(ob_buf *b, const ob_field *f)
{
    const char *n = f->name;
    ob_sql_ident_quoted(b, n);
    ob_buf_char(b, ' ');
    ob_buf_str(b, ob_type_sql(f->type));
    if (f->type == OB_TYPE_COUNTER) return ob_buf_str(b, " PRIMARY KEY");
    if (f->required) ob_buf_str(b, " NOT NULL");
    if (f->default_sql && *f->default_sql) {
        int bare = default_is_literal(f->default_sql);
        ob_buf_str(b, bare ? " DEFAULT " : " DEFAULT (");
        ob_buf_str(b, f->default_sql);
        if (!bare) ob_buf_char(b, ')');
    }
    switch (f->type) {
    case OB_TYPE_INTEGER:
    case OB_TYPE_LOOKUP:
    case OB_TYPE_CURRENCY:
        check_start(b, n, f->type == OB_TYPE_CURRENCY ? "currency" : "integer");
        ob_buf_str(b, "typeof(");
        ob_sql_ident_quoted(b, n);
        ob_buf_str(b, ") IN ('integer', 'null'))");
        break;
    case OB_TYPE_NUMBER:
        check_start(b, n, "number");
        ob_buf_str(b, "typeof(");
        ob_sql_ident_quoted(b, n);
        ob_buf_str(b, ") IN ('integer', 'real', 'null'))");
        break;
    case OB_TYPE_DATE:
    case OB_TYPE_TIME:
    case OB_TYPE_DATETIME: {
        const char *fn = f->type == OB_TYPE_DATE ? "date" : f->type == OB_TYPE_TIME ? "time" : "datetime";
        check_start(b, n, fn);
        ob_buf_str(b, fn);
        ob_buf_char(b, '(');
        ob_sql_ident_quoted(b, n);
        ob_buf_str(b, ") IS ");
        ob_sql_ident_quoted(b, n);
        ob_buf_char(b, ')');
        break;
    }
    case OB_TYPE_YESNO:
        check_start(b, n, "yesno");
        ob_sql_ident_quoted(b, n);
        ob_buf_str(b, " IN (0, 1))");
        break;
    case OB_TYPE_CHOICE:
        if (f->choices && *f->choices) {
            check_start(b, n, "choice");
            ob_sql_ident_quoted(b, n);
            ob_buf_str(b, " IN (");
            if (choices_sql(b, f->choices) == OB_ERR_NOMEM) return OB_ERR_NOMEM;
            ob_buf_str(b, "))");
        }
        break;
    case OB_TYPE_TEXT:
        if (f->maxlen > 0) {
            check_start(b, n, "maxlen");
            ob_buf_str(b, "length(");
            ob_sql_ident_quoted(b, n);
            ob_buf_str(b, ") <= ");
            ob_buf_long(b, f->maxlen);
            ob_buf_char(b, ')');
        }
        break;
    case OB_TYPE_OBJECT:
        check_start(b, n, "object");
        ob_buf_str(b, "typeof(");
        ob_sql_ident_quoted(b, n);
        ob_buf_str(b, ") IN ('blob', 'null'))");
        break;
    default:
        break;
    }
    if (f->rule && *f->rule) {
        ob_buf_str(b, " CONSTRAINT ");
        if (f->rule_text && *f->rule_text) ob_sql_ident_quoted(b, f->rule_text);
        else {
            char name[OB_NAME_MAX + 8];
            snprintf(name, sizeof name, "%s:rule", n);
            ob_sql_ident_quoted(b, name);
        }
        ob_buf_str(b, " CHECK (");
        ob_buf_str(b, f->rule);
        ob_buf_char(b, ')');
    }
    if (f->type == OB_TYPE_OBJECT) {
        ob_buf_str(b, ",\n  ");
        add_ident(b, n, COMPANION_NAME);
        ob_buf_str(b, " TEXT,\n  ");
        add_ident(b, n, COMPANION_TYPE);
        ob_buf_str(b, " TEXT");
    }
    return b->failed;
}

int ob_table_create_sql(const ob_table *t, const char *as_name, ob_buf *b, ob_error *err)
{
    int i, k;
    ob_buf_reset(b);
    ob_buf_str(b, "CREATE TABLE ");
    ob_sql_ident_quoted(b, as_name ? as_name : t->name);
    ob_buf_str(b, " (\n  ");
    for (i = 0; i < t->nfields; i++) {
        if (i) ob_buf_str(b, ",\n  ");
        if (field_sql(b, &t->fields[i]) == OB_ERR_NOMEM) return ob_nomem(err);
    }
    if (t->nkey > 0) {
        ob_buf_str(b, ",\n  PRIMARY KEY (");
        for (k = 0; k < t->nkey; k++) {
            if (k) ob_buf_str(b, ", ");
            ob_sql_ident_quoted(b, t->key[k]);
        }
        ob_buf_char(b, ')');
    }
    for (i = 0; i < t->nrelations; i++) {
        const ob_relation *r = &t->relations[i];
        ob_buf_str(b, ",\n  FOREIGN KEY (");
        for (k = 0; k < r->nfields; k++) {
            if (k) ob_buf_str(b, ", ");
            ob_sql_ident_quoted(b, r->child_fields[k]);
        }
        ob_buf_str(b, ") REFERENCES ");
        /* a table's relationship to itself follows it when it is renamed */
        ob_sql_ident_quoted(b, (as_name && ob_casecmp(r->parent, t->name) == 0) ? t->name : r->parent);
        if (r->parent_fields[0]) {
            ob_buf_str(b, " (");
            for (k = 0; k < r->nfields; k++) {
                if (k) ob_buf_str(b, ", ");
                ob_sql_ident_quoted(b, r->parent_fields[k]);
            }
            ob_buf_char(b, ')');
        }
        ob_buf_str(b, " ON DELETE ");
        ob_buf_str(b, rel_sql(r->on_delete));
        if (r->on_update != OB_REL_REFUSE) {
            ob_buf_str(b, " ON UPDATE ");
            ob_buf_str(b, rel_sql(r->on_update));
        }
    }
    ob_buf_str(b, "\n)");
    if (b->failed == OB_ERR_LIMIT) return ob_fail(err, OB_ERR_LIMIT, "The table's design is too big.");
    if (b->failed) return ob_nomem(err);
    return OB_OK;
}

/* ---- Reading the project ------------------------------------------------------- */

/* The schema object's exact name, any case; 0 if there is none */
static int schema_name(ob_project *p, const char *type, const char *name, char *out, size_t size)
{
    sqlite3_stmt *st;
    int found = 0;
    if (sqlite3_prepare_v2(p->db,
                           type ? "SELECT name FROM sqlite_schema WHERE type=?2 AND name=?1 COLLATE NOCASE"
                                : "SELECT name FROM sqlite_schema WHERE name=?1 COLLATE NOCASE",
                           -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    if (type) sqlite3_bind_text(st, 2, type, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *n = (const char *)sqlite3_column_text(st, 0);
        if (n && strlen(n) < size) {
            strcpy(out, n);
            found = 1;
        }
    }
    sqlite3_finalize(st);
    return found;
}

int ob_table_exists(ob_project *p, const char *name)
{
    char exact[OB_NAME_MAX + 1];
    if (!name || ob_ncasecmp(name, "oadb_", 5) == 0 || ob_ncasecmp(name, "sqlite_", 7) == 0) return 0;
    return schema_name(p, "table", name, exact, sizeof exact);
}

static const char *col_text(sqlite3_stmt *st, int i) { return (const char *)sqlite3_column_text(st, i); }

static ob_type type_from_decl(const char *decl)
{
    /* SQLite's affinity rules (datatype3.html, 3.1) */
    char up[64];
    size_t i;
    if (!decl) decl = "";
    for (i = 0; decl[i] && i + 1 < sizeof up; i++) up[i] = (char)((decl[i] >= 'a' && decl[i] <= 'z') ? decl[i] - 32 : decl[i]);
    up[i] = 0;
    if (strstr(up, "INT")) return OB_TYPE_INTEGER;
    if (strstr(up, "CHAR") || strstr(up, "CLOB") || strstr(up, "TEXT")) return OB_TYPE_TEXT;
    if (!up[0]) return OB_TYPE_TEXT; /* no type: any value; Text is the nearest */
    if (strstr(up, "BLOB")) return OB_TYPE_OBJECT;
    if (strstr(up, "REAL") || strstr(up, "FLOA") || strstr(up, "DOUB")) return OB_TYPE_NUMBER;
    if (strstr(up, "DATETIME") || strstr(up, "TIMESTAMP")) return OB_TYPE_DATETIME;
    if (strstr(up, "DATE")) return OB_TYPE_DATE;
    if (strstr(up, "BOOL")) return OB_TYPE_YESNO;
    return OB_TYPE_NUMBER; /* NUMERIC affinity */
}

static int read_fields(ob_project *p, ob_table *t, ob_error *err)
{
    sqlite3_stmt *st = NULL, *meta = NULL;
    int rc, npk = 0, has_meta = 0;
    char pk[OB_KEY_MAX][OB_NAME_MAX + 1];
    int pkpos[OB_KEY_MAX];
    if ((rc = ob_prepare(p, "SELECT name, type, \"notnull\", dflt_value, pk FROM pragma_table_xinfo(?) WHERE hidden = 0",
                         &st, err)))
        return rc;
    if ((rc = ob_prepare(p, "SELECT type, label, format, choices, maxlen, rule, rule_text, lookup_table, lookup_show,"
                            " companion_of FROM oadb_fields WHERE tbl = ?1 AND field = ?2", &meta, err)))
        goto done;
    sqlite3_bind_text(st, 1, t->name, -1, SQLITE_STATIC);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        const char *name = col_text(st, 0);
        ob_field *f;
        int pkn = sqlite3_column_int(st, 4);
        ob_type type = type_from_decl(col_text(st, 1));
        if (!name) continue;
        sqlite3_reset(meta);
        sqlite3_bind_text(meta, 1, t->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(meta, 2, name, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(meta);
        if (rc == SQLITE_ROW) {
            has_meta = 1;
            if (sqlite3_column_type(meta, 9) != SQLITE_NULL) continue; /* a companion column */
            if (ob_type_from_key(col_text(meta, 0), &type)) type = type_from_decl(col_text(st, 1));
        } else if (rc != SQLITE_DONE) {
            rc = ob_sqlite_fail(p, rc, err, "reading the table");
            goto done;
        }
        if (pkn > 0 && pkn <= OB_KEY_MAX) {
            if (strlen(name) <= OB_NAME_MAX) {
                strcpy(pk[npk], name);
                pkpos[npk++] = pkn;
            }
        } else if (pkn > OB_KEY_MAX) {
            rc = ob_fail(err, OB_ERR_UNSUPPORTED, "The table's key has more than %d fields.", OB_KEY_MAX);
            goto done;
        }
        if (type == OB_TYPE_COUNTER && pkn == 0) type = OB_TYPE_INTEGER;
        if (!(f = ob_table_add_field(t, name, type, err))) {
            rc = err ? err->code : OB_ERR_NOMEM;
            goto done;
        }
        f->required = sqlite3_column_int(st, 2) != 0;
        if (col_text(st, 3) && ob_strset(&f->default_sql, col_text(st, 3), err)) goto nomem;
        if (rc == SQLITE_ROW) {
            if (ob_strset(&f->label, col_text(meta, 1), err) || ob_strset(&f->format, col_text(meta, 2), err) ||
                ob_strset(&f->choices, col_text(meta, 3), err) || ob_strset(&f->rule, col_text(meta, 5), err) ||
                ob_strset(&f->rule_text, col_text(meta, 6), err) || ob_strset(&f->lookup_table, col_text(meta, 7), err) ||
                ob_strset(&f->lookup_show, col_text(meta, 8), err))
                goto nomem;
            f->maxlen = sqlite3_column_int(meta, 4);
            if (f->maxlen < 0) f->maxlen = 0;
        }
        if (ob_strset(&f->was, name, err)) goto nomem;
    }
    if (rc != SQLITE_DONE) {
        rc = ob_sqlite_fail(p, rc, err, "reading the table");
        goto done;
    }
    t->adopted = !has_meta;
    /* the key, in its order */
    {
        int i, j;
        ob_field *c = npk == 1 ? ob_table_field(t, pk[0]) : NULL;
        if (c && (c->type == OB_TYPE_COUNTER || (t->adopted && c->type == OB_TYPE_INTEGER && !t->without_rowid))) {
            /* INTEGER PRIMARY KEY: the rowid; an adopted one becomes a Counter */
            c->type = OB_TYPE_COUNTER;
            c->required = 0;
        } else {
            if (c && c->type == OB_TYPE_COUNTER) c->type = OB_TYPE_INTEGER;
            for (i = 1; i <= npk; i++)
                for (j = 0; j < npk; j++)
                    if (pkpos[j] == i && ob_strset(&t->key[t->nkey++], pk[j], err)) goto nomem;
        }
        for (i = 0; i < t->nfields; i++)
            if (t->fields[i].type == OB_TYPE_COUNTER && !(c && c == &t->fields[i])) t->fields[i].type = OB_TYPE_INTEGER;
    }
    rc = OB_OK;
    goto done;
nomem:
    rc = OB_ERR_NOMEM;
done:
    sqlite3_finalize(st);
    sqlite3_finalize(meta);
    return rc;
}

static int read_indexes(ob_project *p, ob_table *t, ob_error *err)
{
    sqlite3_stmt *list = NULL, *info = NULL;
    int rc;
    if ((rc = ob_prepare(p, "SELECT name, \"unique\", origin, partial FROM pragma_index_list(?) ORDER BY name", &list, err)))
        return rc;
    if ((rc = ob_prepare(p, "SELECT cid, name FROM pragma_index_info(?) ORDER BY seqno", &info, err))) goto done;
    sqlite3_bind_text(list, 1, t->name, -1, SQLITE_STATIC);
    while ((rc = sqlite3_step(list)) == SQLITE_ROW) {
        const char *name = col_text(list, 0), *origin = col_text(list, 2);
        ob_index *ix;
        int usable = 1, n = 0;
        if (!name || !origin || sqlite3_column_int(list, 3) || strcmp(origin, "pk") == 0) continue;
        sqlite3_reset(info);
        sqlite3_bind_text(info, 1, name, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(info) == SQLITE_ROW) {
            if (sqlite3_column_int(info, 0) < 0 || !col_text(info, 1)) usable = 0;
            n++;
        }
        if (!usable || n == 0 || n > OB_INDEX_FIELDS) continue; /* kept by its SQL in a rebuild */
        if (!(ix = ob_table_add_index(t, NULL, sqlite3_column_int(list, 1), err))) goto nomem;
        if (strcmp(origin, "c") == 0 && ob_strset(&ix->name, name, err)) goto nomem;
        sqlite3_reset(info);
        while (sqlite3_step(info) == SQLITE_ROW)
            if (ob_table_add_index_field(ix, col_text(info, 1), err)) goto nomem;
    }
    rc = rc == SQLITE_DONE ? OB_OK : ob_sqlite_fail(p, rc, err, "reading the table's indexes");
    goto done;
nomem:
    rc = OB_ERR_NOMEM;
done:
    sqlite3_finalize(list);
    sqlite3_finalize(info);
    return rc;
}

static ob_relrule rule_from(const char *s)
{
    if (s && ob_casecmp(s, "CASCADE") == 0) return OB_REL_CASCADE;
    if (s && ob_casecmp(s, "SET NULL") == 0) return OB_REL_CLEAR;
    return OB_REL_REFUSE;
}

static int read_relations(ob_project *p, const char *table, ob_relation **out, int *count, ob_error *err)
{
    sqlite3_stmt *st;
    int rc, last = -1;
    ob_relation *r = NULL;
    if ((rc = ob_prepare(p, "SELECT id, seq, \"table\", \"from\", \"to\", on_update, on_delete"
                            " FROM pragma_foreign_key_list(?) ORDER BY id, seq", &st, err)))
        return rc;
    sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        int bad = 0;
        if (id != last || !r) {
            ob_relation *grown = (ob_relation *)ob_realloc(*out, sizeof(ob_relation) * (size_t)(*count + 1));
            if (!grown) goto nomem;
            *out = grown;
            r = &grown[(*count)++];
            memset(r, 0, sizeof *r);
            last = id;
            copy_str(&r->child, table, &bad);
            copy_str(&r->parent, col_text(st, 2), &bad);
            r->on_update = rule_from(col_text(st, 5));
            r->on_delete = rule_from(col_text(st, 6));
        }
        if (r->nfields >= OB_KEY_MAX) {
            rc = ob_fail(err, OB_ERR_UNSUPPORTED, "A relationship of %s has more than %d fields.", table, OB_KEY_MAX);
            sqlite3_finalize(st);
            return rc;
        }
        copy_str(&r->child_fields[r->nfields], col_text(st, 3), &bad);
        copy_str(&r->parent_fields[r->nfields], col_text(st, 4), &bad);
        r->nfields++;
        if (bad) goto nomem;
    }
    rc = rc == SQLITE_DONE ? OB_OK : ob_sqlite_fail(p, rc, err, "reading the relationships");
    sqlite3_finalize(st);
    return rc;
nomem:
    sqlite3_finalize(st);
    return ob_nomem(err);
}

int ob_table_read(ob_project *p, const char *name, ob_table **out, ob_error *err)
{
    char exact[OB_NAME_MAX + 1];
    ob_table *t;
    int rc;
    sqlite3_stmt *st;
    *out = NULL;
    if (!name || !ob_table_exists(p, name) || !schema_name(p, "table", name, exact, sizeof exact))
        return ob_fail(err, OB_ERR_NOT_FOUND, "There is no table called %s.", name ? name : "");
    if (!(t = ob_table_new(exact, 0, err))) return OB_ERR_NOMEM;
    if ((rc = ob_prepare(p, "SELECT sql FROM sqlite_schema WHERE type='table' AND name=?", &st, err))) goto fail;
    sqlite3_bind_text(st, 1, exact, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW && col_text(st, 0)) {
        const char *sql = col_text(st, 0);
        const char *close = strrchr(sql, ')');
        /* "...) WITHOUT ROWID" */
        if (close) {
            const char *q = close + 1;
            while (*q) {
                if (ob_ncasecmp(q, "WITHOUT", 7) == 0) t->without_rowid = 1;
                q++;
            }
        }
    }
    sqlite3_finalize(st);
    if ((rc = ob_strset(&t->was, exact, err))) goto fail;
    if ((rc = read_fields(p, t, err))) goto fail;
    if ((rc = read_indexes(p, t, err))) goto fail;
    if ((rc = read_relations(p, exact, &t->relations, &t->nrelations, err))) goto fail;
    *out = t;
    return OB_OK;
fail:
    ob_table_free(t);
    return rc == OB_ERR_NOMEM ? ob_nomem(err) : rc;
}

int ob_table_list(ob_project *p, char ***names, int *count, ob_error *err)
{
    sqlite3_stmt *st;
    int rc, n = 0;
    char **list = NULL;
    *names = NULL;
    *count = 0;
    if ((rc = ob_prepare(p, "SELECT name FROM sqlite_schema WHERE type='table' AND name NOT LIKE 'oadb\\_%' ESCAPE '\\'"
                            " AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY name COLLATE NOCASE", &st, err)))
        return rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        char **grown;
        if (n >= OB_TABLES_MAX) break;
        grown = (char **)ob_realloc(list, sizeof(char *) * (size_t)(n + 1));
        if (!grown) goto nomem;
        list = grown;
        if (!(list[n] = ob_strdup(col_text(st, 0) ? col_text(st, 0) : ""))) goto nomem;
        n++;
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        ob_names_free(list, n);
        return ob_sqlite_fail(p, rc, err, "listing the tables");
    }
    *names = list;
    *count = n;
    return OB_OK;
nomem:
    sqlite3_finalize(st);
    ob_names_free(list, n);
    return ob_nomem(err);
}

void ob_names_free(char **names, int count)
{
    int i;
    if (!names) return;
    for (i = 0; i < count; i++) ob_free(names[i]);
    ob_free(names);
}

/* ---- oadb_fields ------------------------------------------------------------------- */

int ob_fields_forget(ob_project *p, const char *table, ob_error *err)
{
    sqlite3_stmt *st;
    int rc = ob_prepare(p, "DELETE FROM oadb_fields WHERE tbl = ?", &st, err);
    if (rc) return rc;
    sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? OB_OK : ob_sqlite_fail(p, rc, err, "saving the design");
}

int ob_fields_write(ob_project *p, const ob_table *t, ob_error *err)
{
    sqlite3_stmt *st;
    int rc, i, pos = 0;
    if ((rc = ob_fields_forget(p, t->name, err))) return rc;
    if ((rc = ob_prepare(p, "INSERT INTO oadb_fields (tbl, field, position, type, label, format, choices, maxlen, rule,"
                            " rule_text, lookup_table, lookup_show, companion_of)"
                            " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)", &st, err)))
        return rc;
    for (i = 0; i < t->nfields; i++) {
        const ob_field *f = &t->fields[i];
        int c, parts = f->type == OB_TYPE_OBJECT ? 3 : 1;
        for (c = 0; c < parts; c++) {
            char comp[OB_NAME_MAX + 16];
            sqlite3_reset(st);
            sqlite3_clear_bindings(st);
            sqlite3_bind_text(st, 1, t->name, -1, SQLITE_STATIC);
            sqlite3_bind_int(st, 3, pos++);
            if (c == 0) {
                sqlite3_bind_text(st, 2, f->name, -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 4, ob_type_key(f->type), -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 5, f->label, -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 6, f->format, -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 7, f->choices, -1, SQLITE_STATIC);
                sqlite3_bind_int(st, 8, f->type == OB_TYPE_TEXT ? f->maxlen : 0);
                sqlite3_bind_text(st, 9, f->rule, -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 10, f->rule_text, -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 11, f->lookup_table, -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 12, f->lookup_show, -1, SQLITE_STATIC);
            } else {
                snprintf(comp, sizeof comp, "%s%s", f->name, c == 1 ? COMPANION_NAME : COMPANION_TYPE);
                sqlite3_bind_text(st, 2, comp, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 4, "text", -1, SQLITE_STATIC);
                sqlite3_bind_int(st, 8, 0);
                sqlite3_bind_text(st, 13, f->name, -1, SQLITE_STATIC);
            }
            if ((rc = sqlite3_step(st)) != SQLITE_DONE) {
                sqlite3_finalize(st);
                return ob_sqlite_fail(p, rc, err, "saving the design");
            }
        }
    }
    sqlite3_finalize(st);
    return OB_OK;
}

/* ---- Checking a design ---------------------------------------------------------------- */

static ob_field *counter_of(const ob_table *t)
{
    int i;
    for (i = 0; i < t->nfields; i++)
        if (t->fields[i].type == OB_TYPE_COUNTER) return &t->fields[i];
    return NULL;
}

/* Does `fields` name the parent's key, or a unique index of it? */
static int is_parent_key(const ob_table *parent, char *const *fields, int n)
{
    const ob_field *c = counter_of(parent);
    int i, k;
    if (n == 1 && c && ob_casecmp(fields[0], c->name) == 0) return 1;
    if (parent->nkey == n) {
        int all = 1;
        for (k = 0; k < n; k++) {
            int found = 0;
            for (i = 0; i < n; i++)
                if (ob_casecmp(parent->key[i], fields[k]) == 0) found = 1;
            all &= found;
        }
        if (all) return 1;
    }
    for (i = 0; i < parent->nindexes; i++) {
        const ob_index *ix = &parent->indexes[i];
        int all = ix->unique && ix->nfields == n;
        for (k = 0; all && k < n; k++) {
            int j, found = 0;
            for (j = 0; j < n; j++)
                if (ob_casecmp(ix->fields[j], fields[k]) == 0) found = 1;
            all &= found;
        }
        if (all) return 1;
    }
    return 0;
}

static int parent_key_count(const ob_table *parent) { return counter_of(parent) ? 1 : parent->nkey; }

static int validate(ob_project *p, ob_table *t, ob_error *err)
{
    int i, j, k, rc, columns = 0, counters = 0;
    if ((rc = ob_name_check(t->name, "table", err))) return rc;
    if (t->nfields < 1) return ob_fail(err, OB_ERR_INVALID, "The table needs at least one field.");
    for (i = 0; i < t->nfields; i++) {
        ob_field *f = &t->fields[i];
        if ((rc = ob_name_check(f->name, "field", err))) return rc;
        if ((unsigned)f->type >= OB_TYPE_COUNT) return ob_fail(err, OB_ERR_INVALID, "%s has no field type.", f->name);
        columns += f->type == OB_TYPE_OBJECT ? 3 : 1;
        for (j = 0; j < i; j++) {
            const ob_field *g = &t->fields[j];
            char comp[OB_NAME_MAX + 16];
            if (ob_casecmp(f->name, g->name) == 0)
                return ob_fail(err, OB_ERR_EXISTS, "Two fields are called %s.", f->name);
            for (k = 1; k <= 2; k++) {
                const ob_field *obj = f->type == OB_TYPE_OBJECT ? f : g->type == OB_TYPE_OBJECT ? g : NULL;
                const ob_field *other = obj == f ? g : f;
                if (!obj) break;
                snprintf(comp, sizeof comp, "%s%s", obj->name, k == 1 ? COMPANION_NAME : COMPANION_TYPE);
                if (ob_casecmp(comp, other->name) == 0)
                    return ob_fail(err, OB_ERR_EXISTS, "%s keeps %s's file name and type; choose another name.",
                                   comp, obj->name);
                if (f->type == OB_TYPE_OBJECT && g->type == OB_TYPE_OBJECT) break;
            }
        }
        if (f->type == OB_TYPE_OBJECT && strlen(f->name) + 5 > OB_NAME_MAX)
            return ob_fail(err, OB_ERR_INVALID, "%s's name is too long for a Datatype object.", f->name);
        if (f->type == OB_TYPE_COUNTER) {
            counters++;
            if (f->default_sql && *f->default_sql) return ob_fail(err, OB_ERR_INVALID, "A Counter can't have a default.");
            f->required = 0;
        }
        if (f->type == OB_TYPE_LOOKUP) {
            if (!f->lookup_table || !*f->lookup_table)
                return ob_fail(err, OB_ERR_INVALID, "The Lookup field %s needs a table to look in.", f->name);
            /* a Lookup is a relationship */
            for (j = 0; j < t->nrelations; j++)
                if (t->relations[j].nfields == 1 && ob_casecmp(t->relations[j].child_fields[0], f->name) == 0) break;
            if (j == t->nrelations && !ob_table_add_relation(t, f->name, f->lookup_table, OB_REL_REFUSE, err))
                return err ? err->code : OB_ERR_NOMEM;
        }
        if (f->maxlen < 0) f->maxlen = 0;
        if (f->choices && strlen(f->choices) > OB_CHOICES_MAX)
            return ob_fail(err, OB_ERR_LIMIT, "%s's choices are too long.", f->name);
    }
    if (columns > OB_FIELDS_MAX) return ob_fail(err, OB_ERR_LIMIT, "A table can have %d fields at most.", OB_FIELDS_MAX);
    if (counters > 1) return ob_fail(err, OB_ERR_INVALID, "A table can have only one Counter.");
    if (counters && t->nkey) return ob_fail(err, OB_ERR_INVALID, "The Counter is the table's key; it can't have another.");
    if (!counters && !t->nkey && !t->adopted)
        return ob_fail(err, OB_ERR_INVALID, "The table needs a key: a Counter, or fields chosen as its key.");
    for (k = 0; k < t->nkey; k++) {
        const ob_field *f = ob_table_field(t, t->key[k]);
        if (!f) return ob_fail(err, OB_ERR_NOT_FOUND, "The key's field %s isn't in the table.", t->key[k] ? t->key[k] : "");
        if (f->type == OB_TYPE_OBJECT || f->type == OB_TYPE_MEMO)
            return ob_fail(err, OB_ERR_INVALID, "A %s field can't be part of the key.", ob_type_name(f->type));
    }
    for (i = 0; i < t->nindexes; i++) {
        const ob_index *ix = &t->indexes[i];
        if (ix->nfields < 1) return ob_fail(err, OB_ERR_INVALID, "An index needs a field.");
        for (k = 0; k < ix->nfields; k++) {
            const ob_field *f = ob_table_field(t, ix->fields[k]);
            if (!f) return ob_fail(err, OB_ERR_NOT_FOUND, "The index's field %s isn't in the table.", ix->fields[k] ? ix->fields[k] : "");
            if (f->type == OB_TYPE_OBJECT) return ob_fail(err, OB_ERR_INVALID, "A Datatype object can't be indexed.");
        }
    }
    for (i = 0; i < t->nrelations; i++) {
        ob_relation *r = &t->relations[i];
        ob_table *parent = NULL;
        const ob_table *pt;
        if (r->nfields < 1 || r->nfields > OB_KEY_MAX || !r->parent)
            return ob_fail(err, OB_ERR_INVALID, "A relationship needs a field and a table.");
        if (ob_strset(&r->child, t->name, err)) return OB_ERR_NOMEM;
        for (k = 0; k < r->nfields; k++)
            if (!ob_table_field(t, r->child_fields[k]))
                return ob_fail(err, OB_ERR_NOT_FOUND, "The relationship's field %s isn't in the table.",
                               r->child_fields[k] ? r->child_fields[k] : "");
        if (ob_casecmp(r->parent, t->name) == 0 || (t->was && ob_casecmp(r->parent, t->was) == 0)) {
            pt = t;
        } else {
            if ((rc = ob_table_read(p, r->parent, &parent, err)))
                return rc == OB_ERR_NOT_FOUND ? ob_fail(err, rc, "%s is related to %s, which isn't a table.", t->name, r->parent) : rc;
            pt = parent;
        }
        rc = OB_OK;
        if (r->parent_fields[0]) {
            for (k = 0; k < r->nfields && !rc; k++)
                if (!r->parent_fields[k] || !ob_table_field(pt, r->parent_fields[k]))
                    rc = ob_fail(err, OB_ERR_NOT_FOUND, "%s has no field %s.", pt->name,
                                 r->parent_fields[k] ? r->parent_fields[k] : "");
            if (!rc && !is_parent_key(pt, r->parent_fields, r->nfields))
                rc = ob_fail(err, OB_ERR_INVALID, "A relationship must point to %s's key, or to fields with a unique index.",
                             pt->name);
        } else if (parent_key_count(pt) != r->nfields) {
            rc = ob_fail(err, OB_ERR_INVALID, "%s's key has %d field%s; the relationship has %d.", pt->name,
                         parent_key_count(pt), parent_key_count(pt) == 1 ? "" : "s", r->nfields);
        }
        ob_table_free(parent); /* after the words that name it */
        if (rc) return rc;
    }
    return OB_OK;
}

/* ---- Making a table ----------------------------------------------------------------- */

static int name_taken(ob_project *p, const char *name)
{
    char exact[OB_NAME_MAX + 1];
    return schema_name(p, NULL, name, exact, sizeof exact);
}

static int index_sql(ob_project *p, const char *table, ob_index *ix, ob_buf *b, ob_error *err)
{
    int k;
    if (!ix->name) {
        char name[OB_NAME_MAX * 2 + 16];
        size_t n = (size_t)snprintf(name, sizeof name, "%s", table);
        int tries;
        for (k = 0; k < ix->nfields && n < OB_NAME_MAX; k++)
            n += (size_t)snprintf(name + n, sizeof name - n, "_%s", ix->fields[k]);
        name[ob_utf8_cut(name, OB_NAME_MAX - 4)] = 0;
        n = strlen(name);
        for (tries = 2; name_taken(p, name) && tries < 1000; tries++) {
            name[n] = 0;
            snprintf(name + n, sizeof name - n, "_%d", tries);
        }
        if (ob_strset(&ix->name, name, err)) return OB_ERR_NOMEM;
    }
    ob_buf_reset(b);
    ob_buf_str(b, ix->unique ? "CREATE UNIQUE INDEX " : "CREATE INDEX ");
    ob_sql_ident_quoted(b, ix->name);
    ob_buf_str(b, " ON ");
    ob_sql_ident_quoted(b, table);
    ob_buf_str(b, " (");
    for (k = 0; k < ix->nfields; k++) {
        if (k) ob_buf_str(b, ", ");
        ob_sql_ident_quoted(b, ix->fields[k]);
    }
    ob_buf_char(b, ')');
    return b->failed ? ob_nomem(err) : OB_OK;
}

static int run(ob_project *p, const char *sql, ob_error *err, const char *what)
{
    int rc = sqlite3_exec(p->db, sql, NULL, NULL, NULL);
    return rc == SQLITE_OK ? OB_OK : ob_sqlite_fail(p, rc, err, what);
}

static int create_indexes(ob_project *p, ob_table *t, ob_buf *b, ob_error *err)
{
    int i, rc;
    for (i = 0; i < t->nindexes; i++) {
        if ((rc = index_sql(p, t->name, &t->indexes[i], b, err))) return rc;
        if ((rc = run(p, ob_buf_text(b), err, "making an index"))) return rc;
    }
    return OB_OK;
}

/* Inside a transaction the caller holds */
int ob_table_create_tx(ob_project *p, const ob_table *design, ob_error *err)
{
    ob_table *t;
    ob_buf b;
    int rc;
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if (!design) return ob_fail(err, OB_ERR_INVALID, "No table to make.");
    if (!(t = ob_table_copy(design, err))) return OB_ERR_NOMEM;
    t->adopted = 0;
    ob_buf_init(&b, 0);
    if ((rc = validate(p, t, err))) goto done;
    if (name_taken(p, t->name)) {
        rc = ob_fail(err, OB_ERR_EXISTS, "There is already a table, query or index called %s.", t->name);
        goto done;
    }
    if ((rc = ob_table_create_sql(t, NULL, &b, err))) goto done;
    if ((rc = run(p, ob_buf_text(&b), err, "making the table"))) goto done;
    if ((rc = create_indexes(p, t, &b, err))) goto done;
    rc = ob_fields_write(p, t, err);
done:
    ob_buf_free(&b);
    ob_table_free(t);
    return rc;
}

int ob_table_create(ob_project *p, const ob_table *design, ob_error *err)
{
    int rc;
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if ((rc = ob_begin(p, err))) return rc;
    if ((rc = ob_table_create_tx(p, design, err))) {
        ob_rollback(p);
        return rc;
    }
    return ob_commit(p, err);
}

/* The tables with relationships to `table`, other than itself */
static int children_of(ob_project *p, const char *table, char ***names, int *count, ob_error *err)
{
    char **all;
    int n, i, rc;
    *names = NULL;
    *count = 0;
    if ((rc = ob_table_list(p, &all, &n, err))) return rc;
    for (i = 0; i < n; i++) {
        ob_relation *r = NULL;
        int nr = 0, j, hit = 0;
        if (ob_casecmp(all[i], table) == 0) continue;
        if ((rc = read_relations(p, all[i], &r, &nr, err))) break;
        for (j = 0; j < nr; j++)
            if (r[j].parent && ob_casecmp(r[j].parent, table) == 0) hit = 1;
        ob_relations_free(r, nr);
        if (hit) {
            char **grown = (char **)ob_realloc(*names, sizeof(char *) * (size_t)(*count + 1));
            if (!grown) {
                rc = ob_nomem(err);
                break;
            }
            *names = grown;
            grown[(*count)++] = all[i];
            all[i] = NULL;
        }
    }
    ob_names_free(all, n);
    if (rc) {
        ob_names_free(*names, *count);
        *names = NULL;
        *count = 0;
    }
    return rc;
}

int ob_table_drop(ob_project *p, const char *name, ob_error *err)
{
    char exact[OB_NAME_MAX + 1];
    char **kids;
    int nkids, rc;
    ob_buf b;
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if (!name || !ob_table_exists(p, name) || !schema_name(p, "table", name, exact, sizeof exact))
        return ob_fail(err, OB_ERR_NOT_FOUND, "There is no table called %s.", name ? name : "");
    if ((rc = children_of(p, exact, &kids, &nkids, err))) return rc;
    if (nkids) {
        rc = ob_fail(err, OB_ERR_RULE, "%s has a relationship to %s; remove the relationship first.", kids[0], exact);
        ob_names_free(kids, nkids);
        return rc;
    }
    ob_buf_init(&b, 0);
    ob_buf_str(&b, "DROP TABLE ");
    ob_sql_ident_quoted(&b, exact);
    if (b.failed) {
        ob_buf_free(&b);
        return ob_nomem(err);
    }
    if ((rc = ob_begin(p, err))) goto done;
    if ((rc = run(p, ob_buf_text(&b), err, "removing the table")) || (rc = ob_fields_forget(p, exact, err))) {
        ob_rollback(p);
        goto done;
    }
    rc = ob_commit(p, err);
done:
    ob_buf_free(&b);
    return rc;
}

/* ---- Changing a table ------------------------------------------------------------------- */

static int same_str(const char *a, const char *b)
{
    if (!a || !*a) return !b || !*b;
    return b && strcmp(a, b) == 0;
}

/* "(x)" and "x" are the same default */
static int same_default(const char *a, const char *b)
{
    size_t na, nb;
    if (same_str(a, b)) return 1;
    if (!a || !b) return 0;
    na = strlen(a);
    nb = strlen(b);
    if (na == nb + 2 && a[0] == '(' && a[na - 1] == ')') return strncmp(a + 1, b, nb) == 0;
    if (nb == na + 2 && b[0] == '(' && b[nb - 1] == ')') return strncmp(b + 1, a, na) == 0;
    return 0;
}

/* Would this field's SQL differ? */
static int field_sql_differs(const ob_field *a, const ob_field *b)
{
    if (a->type != b->type || (a->type != OB_TYPE_COUNTER && a->required != b->required)) return 1;
    if (!same_default(a->default_sql, b->default_sql)) return 1;
    if (!same_str(a->rule, b->rule) || (a->rule && *a->rule && !same_str(a->rule_text, b->rule_text))) return 1;
    if (a->type == OB_TYPE_TEXT && a->maxlen != b->maxlen) return 1;
    if (a->type == OB_TYPE_CHOICE && !same_str(a->choices, b->choices)) return 1;
    if (a->type == OB_TYPE_LOOKUP && !(a->lookup_table && b->lookup_table && ob_casecmp(a->lookup_table, b->lookup_table) == 0))
        return 1;
    return 0;
}

static int relation_same(const ob_relation *a, const ob_relation *b)
{
    int k;
    if (a->nfields != b->nfields || a->on_delete != b->on_delete || a->on_update != b->on_update) return 0;
    if (!a->parent || !b->parent || ob_casecmp(a->parent, b->parent) != 0) return 0;
    if ((a->parent_fields[0] == NULL) != (b->parent_fields[0] == NULL)) return 0;
    for (k = 0; k < a->nfields; k++) {
        if (ob_casecmp(a->child_fields[k], b->child_fields[k]) != 0) return 0;
        if (a->parent_fields[0] && ob_casecmp(a->parent_fields[k], b->parent_fields[k]) != 0) return 0;
    }
    return 1;
}

static int relations_same(const ob_table *a, const ob_table *b)
{
    int i, j;
    if (a->nrelations != b->nrelations) return 0;
    for (i = 0; i < a->nrelations; i++) {
        for (j = 0; j < b->nrelations; j++)
            if (relation_same(&a->relations[i], &b->relations[j])) break;
        if (j == b->nrelations) return 0;
    }
    return 1;
}

static int keys_same(const ob_table *a, const ob_table *b)
{
    int k;
    if (a->nkey != b->nkey) return 0;
    for (k = 0; k < a->nkey; k++)
        if (ob_casecmp(a->key[k], b->key[k]) != 0) return 0;
    return 1;
}

static int index_same(const ob_index *a, const ob_index *b)
{
    int k;
    if (a->unique != b->unique || a->nfields != b->nfields) return 0;
    for (k = 0; k < a->nfields; k++)
        if (ob_casecmp(a->fields[k], b->fields[k]) != 0) return 0;
    return 1;
}

/* A field's new name, given a name the key, an index or a relationship
 * used: a field's name before the change (as ob_table_read gave it), or a
 * new field's name. Old names come first, so two fields can swap names. */
static const char *renamed(const ob_table *t, const char *name)
{
    int i;
    if (!name) return NULL;
    for (i = 0; i < t->nfields; i++)
        if (t->fields[i].was && ob_casecmp(t->fields[i].was, name) == 0) return t->fields[i].name;
    for (i = 0; i < t->nfields; i++)
        if (!t->fields[i].was && ob_casecmp(t->fields[i].name, name) == 0) return t->fields[i].name;
    return NULL;
}

/* Bring the key, indexes and relationships to the fields' new names; drop
 * the indexes and relationships of fields that went. */
static int follow_renames(ob_table *t, ob_change_result *res, ob_error *err)
{
    int i, k;
    for (k = 0; k < t->nkey; k++) {
        const char *n = renamed(t, t->key[k]);
        if (!n) return ob_fail(err, OB_ERR_NOT_FOUND, "The key's field %s isn't in the table.", t->key[k] ? t->key[k] : "");
        if (ob_strset(&t->key[k], n, err)) return OB_ERR_NOMEM;
    }
    for (i = 0; i < t->nindexes; i++) {
        ob_index *ix = &t->indexes[i];
        int gone = 0;
        for (k = 0; k < ix->nfields; k++) {
            const char *n = renamed(t, ix->fields[k]);
            if (!n) gone = 1;
            else if (ob_strset(&ix->fields[k], n, err)) return OB_ERR_NOMEM;
        }
        if (gone) {
            index_clear(ix);
            memmove(ix, ix + 1, sizeof(ob_index) * (size_t)(t->nindexes - i - 1));
            t->nindexes--;
            i--;
            res->dropped_indexes++;
        }
    }
    for (i = 0; i < t->nrelations; i++) {
        ob_relation *r = &t->relations[i];
        int gone = 0;
        for (k = 0; k < r->nfields; k++) {
            const char *n = renamed(t, r->child_fields[k]);
            if (!n) gone = 1;
            else if (ob_strset(&r->child_fields[k], n, err)) return OB_ERR_NOMEM;
        }
        /* a relationship to itself follows renames of its own key */
        if (!gone && r->parent && t->was && ob_casecmp(r->parent, t->was) == 0) {
            if (ob_strset(&r->parent, t->name, err)) return OB_ERR_NOMEM;
            for (k = 0; k < r->nfields && r->parent_fields[0]; k++) {
                const char *n = renamed(t, r->parent_fields[k]);
                if (!n) gone = 1;
                else if (ob_strset(&r->parent_fields[k], n, err)) return OB_ERR_NOMEM;
            }
        }
        if (gone) {
            ob_relation_clear(r);
            memmove(r, r + 1, sizeof(ob_relation) * (size_t)(t->nrelations - i - 1));
            t->nrelations--;
            i--;
            res->dropped_relations++;
        }
    }
    return OB_OK;
}

/* oadb_fields follows a field's new name (and an object's companions) */
static int rename_meta(ob_project *p, const char *table, const char *from, const char *to, ob_error *err)
{
    sqlite3_stmt *st;
    int rc, k;
    static const char *const sql[2] = { "UPDATE oadb_fields SET field = ?3 WHERE tbl = ?1 AND field = ?2",
                                        "UPDATE oadb_fields SET companion_of = ?3 WHERE tbl = ?1 AND companion_of = ?2" };
    for (k = 0; k < 2; k++) {
        if ((rc = ob_prepare(p, sql[k], &st, err))) return rc;
        sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, from, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, to, -1, SQLITE_STATIC);
        rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) return ob_sqlite_fail(p, rc, err, "saving the design");
    }
    return OB_OK;
}

static int alter_rename_column(ob_project *p, const char *table, const char *from, const char *to, ob_buf *b, ob_error *err)
{
    int rc;
    if ((rc = rename_meta(p, table, from, to, err))) return rc;
    ob_buf_reset(b);
    ob_buf_str(b, "ALTER TABLE ");
    ob_sql_ident_quoted(b, table);
    ob_buf_str(b, " RENAME COLUMN ");
    ob_sql_ident_quoted(b, from);
    ob_buf_str(b, " TO ");
    ob_sql_ident_quoted(b, to);
    if (b->failed) return ob_nomem(err);
    return run(p, ob_buf_text(b), err, "renaming a field");
}

/* Renames each renamed field (and an object's companions), through
 * temporary names, so that swapping two names works. */
static int rename_fields(ob_project *p, const char *table, const ob_table *want, const ob_table *cur, ob_buf *b, ob_error *err)
{
    int i, c, rc, step;
    char tmp[OB_NAME_MAX + 32], from[OB_NAME_MAX + 16], to[OB_NAME_MAX + 16];
    static const char *const sfx[3] = { "", COMPANION_NAME, COMPANION_TYPE };
    for (step = 0; step < 2; step++)
        for (i = 0; i < want->nfields; i++) {
            const ob_field *f = &want->fields[i], *old;
            if (!f->was || strcmp(f->was, f->name) == 0) continue;
            old = ob_table_field(cur, f->was);
            if (!old) continue;
            for (c = 0; c < (old->type == OB_TYPE_OBJECT ? 3 : 1); c++) {
                snprintf(tmp, sizeof tmp, "ob__rename_%d%s", i, sfx[c]);
                snprintf(from, sizeof from, "%s%s", f->was, sfx[c]);
                snprintf(to, sizeof to, "%s%s", f->name, sfx[c]);
                if ((rc = alter_rename_column(p, table, step ? tmp : from, step ? to : tmp, b, err))) return rc;
            }
        }
    return OB_OK;
}

typedef struct {
    char *type, *name, *sql, *tbl;
} saved_sql;

static void saved_free(saved_sql *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        ob_free(s[i].type);
        ob_free(s[i].name);
        ob_free(s[i].sql);
        ob_free(s[i].tbl);
    }
    ob_free(s);
}

static int save_sql(ob_project *p, const char *where, const char *bind, saved_sql **out, int *count, ob_error *err)
{
    sqlite3_stmt *st;
    char sql[256];
    int rc;
    *out = NULL;
    *count = 0;
    snprintf(sql, sizeof sql, "SELECT type, name, sql, tbl_name FROM sqlite_schema WHERE sql IS NOT NULL AND %s ORDER BY rowid", where);
    if ((rc = ob_prepare(p, sql, &st, err))) return rc;
    if (bind) sqlite3_bind_text(st, 1, bind, -1, SQLITE_STATIC);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        saved_sql *grown = (saved_sql *)ob_realloc(*out, sizeof(saved_sql) * (size_t)(*count + 1));
        int bad = 0;
        if (!grown) break;
        *out = grown;
        memset(&grown[*count], 0, sizeof(saved_sql));
        copy_str(&grown[*count].type, col_text(st, 0), &bad);
        copy_str(&grown[*count].name, col_text(st, 1), &bad);
        copy_str(&grown[*count].sql, col_text(st, 2), &bad);
        copy_str(&grown[*count].tbl, col_text(st, 3), &bad);
        (*count)++;
        if (bad) break;
    }
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return OB_OK;
    saved_free(*out, *count);
    *out = NULL;
    *count = 0;
    return rc == SQLITE_ROW ? ob_nomem(err) : ob_sqlite_fail(p, rc, err, "reading the design");
}

/* The first rows (rowids) of `table` breaking a relationship */
static int fk_problems(ob_project *p, const char *table, long *count, long *first, char *parent, size_t psize, ob_error *err)
{
    sqlite3_stmt *st;
    int rc;
    if ((rc = ob_prepare(p, "SELECT rowid, parent FROM pragma_foreign_key_check(?)", &st, err))) return rc;
    sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        if (!*count) {
            *first = (long)sqlite3_column_int64(st, 0);
            snprintf(parent, psize, "%s", col_text(st, 1) ? col_text(st, 1) : "?");
        }
        (*count)++;
    }
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? OB_OK : ob_sqlite_fail(p, rc, err, "checking the relationships");
}

static int check_relations(ob_project *p, const char *table, ob_error *err)
{
    long count = 0, first = 0;
    char parent[OB_NAME_MAX + 1];
    char **kids;
    int nkids, i, rc;
    if ((rc = fk_problems(p, table, &count, &first, parent, sizeof parent, err))) return rc;
    if (count)
        return ob_fail(err, OB_ERR_RULE, "%ld record%s in %s point%s to records that aren't in %s (the first is record %ld).",
                       count, count == 1 ? "" : "s", table, count == 1 ? "s" : "", parent, first);
    if ((rc = children_of(p, table, &kids, &nkids, err))) return rc;
    for (i = 0; i < nkids && !rc; i++) {
        count = 0;
        rc = fk_problems(p, kids[i], &count, &first, parent, sizeof parent, err);
        if (!rc && count)
            rc = ob_fail(err, OB_ERR_RULE, "%ld record%s in %s would point to records that aren't in %s (the first is record %ld).",
                         count, count == 1 ? "" : "s", kids[i], table, first);
    }
    ob_names_free(kids, nkids);
    return rc;
}

static int conv_expr(ob_buf *b, const ob_field *f, ob_type from, int clear, const ob_formats *fm)
{
    if (from == f->type) return ob_sql_ident_quoted(b, f->name);
    ob_buf_str(b, "ob_convert(");
    ob_sql_ident_quoted(b, f->name);
    ob_buf_str(b, ", '");
    ob_buf_str(b, ob_type_key(from));
    ob_buf_str(b, "', '");
    ob_buf_str(b, ob_type_key(f->type));
    ob_buf_str(b, clear ? "', 1, " : "', 0, ");
    ob_buf_long(b, fm->date_order);
    ob_buf_str(b, fm->decimal == ',' ? ", ',')" : ", '.')");
    return b->failed;
}

/* The copy step of a rebuild: INSERT INTO new (...) SELECT ... FROM old */
static int column_exists(ob_project *p, const char *table, const char *column)
{
    sqlite3_stmt *st;
    int found = 0;
    if (sqlite3_prepare_v2(p->db, "SELECT 1 FROM pragma_table_xinfo(?1) WHERE name = ?2 COLLATE NOCASE", -1, &st, NULL) !=
        SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, column, -1, SQLITE_STATIC);
    found = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return found;
}

static int copy_sql(ob_project *p, ob_buf *b, const char *newname, const char *old, const ob_table *want, const ob_table *cur,
                    int clear, const ob_formats *fm, int ignore, size_t key_size, char *key_expr)
{
    ob_buf cols, vals;
    const ob_field *counter = counter_of(want);
    int i, first = 1;
    ob_buf_init(&cols, 0);
    ob_buf_init(&vals, 0);
    /* which old value becomes each new row's rowid */
    if (!counter) {
        ob_buf_str(&cols, "rowid");
        ob_buf_str(&vals, "rowid");
        first = 0;
        snprintf(key_expr, key_size, "rowid");
    }
    for (i = 0; i < want->nfields; i++) {
        const ob_field *f = &want->fields[i];
        const ob_field *o = f->was ? ob_table_field(cur, f->name) : NULL;
        if (!o && f->type != OB_TYPE_COUNTER) continue; /* new: its default */
        if (!first) {
            ob_buf_str(&cols, ", ");
            ob_buf_str(&vals, ", ");
        }
        first = 0;
        ob_sql_ident_quoted(&cols, f->name);
        if (!o) {
            ob_buf_str(&vals, "rowid"); /* a new Counter numbers the rows as they are */
            if (f == counter) snprintf(key_expr, key_size, "rowid");
            continue;
        }
        if (f == counter) {
            ob_buf tmp;
            ob_buf_init(&tmp, 4096);
            conv_expr(&tmp, f, o->type, clear, fm);
            snprintf(key_expr, key_size, "%s", ob_buf_text(&tmp));
            ob_buf_free(&tmp);
        }
        conv_expr(&vals, f, o->type, clear, fm);
        if (f->type == OB_TYPE_OBJECT && o->type == OB_TYPE_OBJECT) {
            int c;
            for (c = 1; c <= 2; c++) {
                char comp[OB_NAME_MAX + 16];
                snprintf(comp, sizeof comp, "%s%s", f->name, c == 1 ? COMPANION_NAME : COMPANION_TYPE);
                if (!column_exists(p, old, comp)) continue; /* a BLOB OpenBase didn't make */
                ob_buf_str(&cols, ", ");
                add_ident(&cols, f->name, c == 1 ? COMPANION_NAME : COMPANION_TYPE);
                ob_buf_str(&vals, ", ");
                add_ident(&vals, f->name, c == 1 ? COMPANION_NAME : COMPANION_TYPE);
            }
        }
    }
    ob_buf_reset(b);
    ob_buf_str(b, ignore ? "INSERT OR IGNORE INTO " : "INSERT INTO ");
    ob_sql_ident_quoted(b, newname);
    ob_buf_str(b, " (");
    ob_buf_str(b, ob_buf_text(&cols));
    ob_buf_str(b, ") SELECT ");
    ob_buf_str(b, ob_buf_text(&vals));
    ob_buf_str(b, " FROM ");
    ob_sql_ident_quoted(b, old);
    if (!ignore) ob_buf_str(b, " WHERE rowid = ?1");
    i = cols.failed || vals.failed || b->failed;
    ob_buf_free(&cols);
    ob_buf_free(&vals);
    return i ? OB_ERR_NOMEM : OB_OK;
}

/* Values that won't become their field's new type */
static int scan_conversions(ob_project *p, const char *table, const ob_table *want, const ob_table *cur, unsigned flags,
                            ob_change_result *res, ob_buf *b, ob_error *err)
{
    int i, rc;
    const ob_formats *fm = &p->formats;
    for (i = 0; i < want->nfields; i++) {
        const ob_field *f = &want->fields[i];
        const ob_field *o = f->was ? ob_table_field(cur, f->name) : NULL;
        sqlite3_stmt *st;
        long long n = 0, first = 0;
        if (!o || o->type == f->type) continue;
        ob_buf_reset(b);
        ob_buf_str(b, "SELECT count(*), min(rowid) FROM ");
        ob_sql_ident_quoted(b, table);
        ob_buf_str(b, " WHERE NOT ob_convert_ok(");
        ob_sql_ident_quoted(b, f->name);
        ob_buf_str(b, ", '");
        ob_buf_str(b, ob_type_key(o->type));
        ob_buf_str(b, "', '");
        ob_buf_str(b, ob_type_key(f->type));
        ob_buf_str(b, "', ");
        ob_buf_long(b, fm->date_order);
        ob_buf_str(b, fm->decimal == ',' ? ", ',')" : ", '.')");
        if (b->failed) return ob_nomem(err);
        if ((rc = ob_prepare(p, ob_buf_text(b), &st, err))) return rc;
        rc = sqlite3_step(st);
        if (rc == SQLITE_ROW) {
            n = sqlite3_column_int64(st, 0);
            first = sqlite3_column_int64(st, 1);
        }
        sqlite3_finalize(st);
        if (rc != SQLITE_ROW) return ob_sqlite_fail(p, rc, err, "checking the values");
        if (!n) continue;
        if (!res->bad_values) {
            res->first_bad_row = (long)first;
            snprintf(res->first_bad_field, sizeof res->first_bad_field, "%s", f->name);
        }
        res->bad_values += (long)n;
        if (flags & OB_CHANGE_CLEAR_BAD) res->cleared += (long)n;
        else
            return ob_fail(err, OB_ERR_RULE, "%lld record%s in %s hold%s a value that can't become %s %s (the first is record %lld).",
                           n, n == 1 ? "" : "s", f->name, n == 1 ? "s" : "",
                           (f->type == OB_TYPE_INTEGER || f->type == OB_TYPE_OBJECT) ? "an" : "a", ob_type_name(f->type), first);
    }
    return OB_OK;
}

static int check_schema(ob_project *p, const char *table, ob_buf *b, ob_error *err)
{
    char check[OB_NAME_MAX + 16];
    long long others = 0;
    int i;
    if (ob_sql_long(p, "SELECT count(*) FROM sqlite_schema WHERE type IN ('view', 'trigger')", &others, err)) return OB_ERR_SQL;
    if (!others) return OB_OK;
    for (i = 0;; i++) {
        snprintf(check, sizeof check, "ob_check_%d", i);
        if (!name_taken(p, check) || i > 100) break;
    }
    ob_buf_reset(b);
    ob_buf_str(b, "ALTER TABLE ");
    ob_sql_ident_quoted(b, table);
    ob_buf_str(b, " RENAME TO ");
    ob_sql_ident_quoted(b, check);
    ob_buf_str(b, "; ALTER TABLE ");
    ob_sql_ident_quoted(b, check);
    ob_buf_str(b, " RENAME TO ");
    ob_sql_ident_quoted(b, table);
    if (b->failed) return ob_nomem(err);
    if (sqlite3_exec(p->db, ob_buf_text(b), NULL, NULL, NULL) != SQLITE_OK)
        return ob_fail(err, OB_ERR_RULE, "A view or trigger uses something this change takes away (%s).", sqlite3_errmsg(p->db));
    return OB_OK;
}

static int rebuild(ob_project *p, const char *table, ob_table *want, const ob_table *cur, unsigned flags,
                   ob_change_result *res, ob_buf *b, ob_error *err)
{
    saved_sql *keep = NULL, *extra = NULL;
    int nkeep = 0, nextra = 0, rc, i;
    char newname[OB_NAME_MAX + 16], key_expr[4096];
    long long n_old = 0, n_new = 0;
    if (cur->without_rowid)
        return ob_fail(err, OB_ERR_UNSUPPORTED, "%s is a WITHOUT ROWID table; OpenBase can't rebuild it.", table);
    if (cur->adopted) {
        /* rules OpenBase didn't write would be lost */
        sqlite3_stmt *st;
        int has_check = 0;
        if ((rc = ob_prepare(p, "SELECT sql FROM sqlite_schema WHERE type='table' AND name=?", &st, err))) return rc;
        sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW && col_text(st, 0)) {
            const char *s;
            for (s = col_text(st, 0); *s; s++)
                if (ob_ncasecmp(s, "CHECK", 5) == 0 || ob_ncasecmp(s, "GENERATED", 9) == 0 || ob_ncasecmp(s, "COLLATE", 7) == 0)
                    has_check = 1;
        }
        sqlite3_finalize(st);
        if (has_check)
            return ob_fail(err, OB_ERR_UNSUPPORTED, "%s has rules OpenBase didn't write; change it with SQL.", table);
    }
    res->rebuilt = 1;
    if ((rc = scan_conversions(p, table, want, cur, flags, res, b, err))) return rc;
    /* the table's own triggers go with it; views and other triggers stay */
    if ((rc = save_sql(p, "type = 'trigger' AND tbl_name = ?1", table, &keep, &nkeep, err))) return rc;
    /* indexes OpenBase can't describe (on expressions, partial) */
    if ((rc = save_sql(p, "type = 'index' AND tbl_name = ?1", table, &extra, &nextra, err))) goto done;
    for (i = 0; i < nextra; i++) {
        int k, known = 0;
        for (k = 0; k < cur->nindexes; k++)
            if (cur->indexes[k].name && ob_casecmp(cur->indexes[k].name, extra[i].name) == 0) known = 1;
        if (known) { /* rebuilt from the design */
            ob_free(extra[i].sql);
            extra[i].sql = NULL;
        }
    }
    for (i = 0;; i++) {
        snprintf(newname, sizeof newname, i ? "ob_new_%d" : "ob_new", i);
        if (!name_taken(p, newname)) break;
        if (i > 100) {
            rc = ob_fail(err, OB_ERR_EXISTS, "OpenBase's working names are all taken.");
            goto done;
        }
    }
    if ((rc = ob_table_create_sql(want, newname, b, err))) goto done;
    if ((rc = run(p, ob_buf_text(b), err, "making the new table"))) goto done;
    key_expr[0] = 0;
    if (copy_sql(p, b, newname, table, want, cur, (flags & OB_CHANGE_CLEAR_BAD) != 0, &p->formats, 1, sizeof key_expr, key_expr)) {
        rc = ob_nomem(err);
        goto done;
    }
    if ((rc = run(p, ob_buf_text(b), err, "copying the records"))) goto done;
    ob_buf_reset(b);
    ob_buf_str(b, "SELECT (SELECT count(*) FROM ");
    ob_sql_ident_quoted(b, table);
    ob_buf_str(b, "), (SELECT count(*) FROM ");
    ob_sql_ident_quoted(b, newname);
    ob_buf_char(b, ')');
    {
        sqlite3_stmt *st;
        if (b->failed) {
            rc = ob_nomem(err);
            goto done;
        }
        if ((rc = ob_prepare(p, ob_buf_text(b), &st, err))) goto done;
        if ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            n_old = sqlite3_column_int64(st, 0);
            n_new = sqlite3_column_int64(st, 1);
            rc = OB_OK;
        } else
            rc = ob_sqlite_fail(p, rc, err, "counting the records");
        sqlite3_finalize(st);
        if (rc) goto done;
    }
    res->rows = (long)n_old;
    if (n_old != n_new) {
        /* which row didn't fit, and why: copy it alone, without IGNORE */
        long long missing = 0;
        sqlite3_stmt *st;
        ob_error why;
        int found = 0;
        memset(&why, 0, sizeof why);
        ob_buf_reset(b);
        ob_buf_str(b, "SELECT rowid FROM ");
        ob_sql_ident_quoted(b, table);
        ob_buf_str(b, " WHERE (");
        ob_buf_str(b, key_expr);
        ob_buf_str(b, ") NOT IN (SELECT rowid FROM ");
        ob_sql_ident_quoted(b, newname);
        ob_buf_str(b, ") ORDER BY rowid LIMIT 1");
        if (!b->failed && ob_prepare(p, ob_buf_text(b), &st, NULL) == OB_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                missing = sqlite3_column_int64(st, 0);
                found = 1;
            }
            sqlite3_finalize(st);
        }
        if (found && copy_sql(p, b, newname, table, want, cur, (flags & OB_CHANGE_CLEAR_BAD) != 0, &p->formats, 0,
                              sizeof key_expr, key_expr) == OB_OK &&
            ob_prepare(p, ob_buf_text(b), &st, NULL) == OB_OK) {
            int src;
            sqlite3_bind_int64(st, 1, missing);
            src = sqlite3_step(st);
            if (src != SQLITE_DONE) ob_sqlite_fail(p, src, &why, "copying a record");
            sqlite3_finalize(st);
        }
        if (found && why.text[0])
            rc = ob_fail(err, OB_ERR_RULE, "%s %lld record%s can't be kept (the first is record %lld).", why.text,
                         n_old - n_new, n_old - n_new == 1 ? "" : "s", missing);
        else
            rc = ob_fail(err, OB_ERR_RULE, "%lld record%s don't fit the new design.", n_old - n_new, n_old - n_new == 1 ? "" : "s");
        goto done;
    }
    ob_buf_reset(b);
    ob_buf_str(b, "DROP TABLE ");
    ob_sql_ident_quoted(b, table);
    if (b->failed || (rc = run(p, ob_buf_text(b), err, "removing the old table"))) {
        if (!rc) rc = ob_nomem(err);
        goto done;
    }
    /* Step 7 renames the new table to the old name. Nothing names the new
     * table, so the rename has nothing to rewrite: legacy_alter_table skips
     * SQLite's check of views and triggers, which would fail while the old
     * name is missing. They are checked below instead. */
    ob_buf_reset(b);
    ob_buf_str(b, "PRAGMA legacy_alter_table=ON; ALTER TABLE ");
    ob_sql_ident_quoted(b, newname);
    ob_buf_str(b, " RENAME TO ");
    ob_sql_ident_quoted(b, want->name);
    if (b->failed) {
        rc = ob_nomem(err);
        goto done;
    }
    i = sqlite3_exec(p->db, ob_buf_text(b), NULL, NULL, NULL);
    ob_exec_quiet(p, "PRAGMA legacy_alter_table=OFF");
    if (i != SQLITE_OK) {
        rc = ob_sqlite_fail(p, i, err, "renaming the new table");
        goto done;
    }
    if ((rc = create_indexes(p, want, b, err))) goto done;
    for (i = 0; i < nextra; i++)
        if (extra[i].sql && sqlite3_exec(p->db, extra[i].sql, NULL, NULL, NULL) != SQLITE_OK) res->dropped_indexes++;
    for (i = 0; i < nkeep; i++)
        if (sqlite3_exec(p->db, keep[i].sql, NULL, NULL, NULL) != SQLITE_OK) {
            rc = ob_fail(err, OB_ERR_RULE, "The trigger %s uses something this change takes away (%s).", keep[i].name,
                         sqlite3_errmsg(p->db));
            goto done;
        }
    /* SQLite doesn't check a view's or a trigger's names when it is made;
     * a rename does, of every one. So: a rename there and back. */
    if ((rc = check_schema(p, want->name, b, err))) goto done;
    rc = OB_OK;
done:
    saved_free(keep, nkeep);
    saved_free(extra, nextra);
    return rc;
}

/* Fields ALTER TABLE ADD COLUMN can add as they are */
static int addable(const ob_field *f)
{
    if (f->type == OB_TYPE_COUNTER || f->type == OB_TYPE_LOOKUP) return 0;
    if (f->required && !(f->default_sql && *f->default_sql)) return 0;
    if (f->default_sql && *f->default_sql && (f->default_sql[0] == '(' || !default_is_literal(f->default_sql)))
        return 0; /* not a constant */
    return 1;
}

static int add_columns(ob_project *p, const char *table, const ob_table *want, ob_buf *b, ob_error *err)
{
    int i, rc;
    for (i = 0; i < want->nfields; i++) {
        const ob_field *f = &want->fields[i];
        ob_buf col;
        if (f->was) continue;
        ob_buf_init(&col, 0);
        field_sql(&col, f);
        ob_buf_reset(b);
        if (f->type == OB_TYPE_OBJECT) {
            /* three columns, one ALTER each */
            ob_buf_str(b, "ALTER TABLE ");
            ob_sql_ident_quoted(b, table);
            ob_buf_str(b, " ADD COLUMN ");
            ob_buf_add(b, ob_buf_text(&col), strcspn(ob_buf_text(&col), ","));
            ob_buf_str(b, "; ALTER TABLE ");
            ob_sql_ident_quoted(b, table);
            ob_buf_str(b, " ADD COLUMN ");
            add_ident(b, f->name, COMPANION_NAME);
            ob_buf_str(b, " TEXT; ALTER TABLE ");
            ob_sql_ident_quoted(b, table);
            ob_buf_str(b, " ADD COLUMN ");
            add_ident(b, f->name, COMPANION_TYPE);
            ob_buf_str(b, " TEXT");
        } else {
            ob_buf_str(b, "ALTER TABLE ");
            ob_sql_ident_quoted(b, table);
            ob_buf_str(b, " ADD COLUMN ");
            ob_buf_str(b, ob_buf_text(&col));
        }
        rc = col.failed || b->failed ? ob_nomem(err) : run(p, ob_buf_text(b), err, "adding a field");
        ob_buf_free(&col);
        if (rc) return rc;
    }
    return OB_OK;
}

/* Index changes that need no rebuild */
static int sync_indexes(ob_project *p, const char *table, ob_table *want, const ob_table *cur, ob_buf *b, ob_error *err)
{
    int i, j, rc;
    for (i = 0; i < cur->nindexes; i++) {
        const ob_index *o = &cur->indexes[i];
        int keep = 0;
        if (!o->name) continue; /* a UNIQUE constraint of an adopted table */
        for (j = 0; j < want->nindexes; j++)
            if (want->indexes[j].name && ob_casecmp(want->indexes[j].name, o->name) == 0 && index_same(o, &want->indexes[j]))
                keep = 1;
        if (keep) continue;
        ob_buf_reset(b);
        ob_buf_str(b, "DROP INDEX ");
        ob_sql_ident_quoted(b, o->name);
        if (b->failed) return ob_nomem(err);
        if ((rc = run(p, ob_buf_text(b), err, "removing an index"))) return rc;
    }
    for (j = 0; j < want->nindexes; j++) {
        ob_index *ix = &want->indexes[j];
        int have = 0;
        for (i = 0; i < cur->nindexes; i++)
            if (index_same(&cur->indexes[i], ix) &&
                ((!ix->name && !cur->indexes[i].name) ||
                 (ix->name && cur->indexes[i].name && ob_casecmp(cur->indexes[i].name, ix->name) == 0)))
                have = 1;
        if (have) continue;
        if (ix->name && name_taken(p, ix->name))
            return ob_fail(err, OB_ERR_EXISTS, "There is already something called %s.", ix->name);
        if ((rc = index_sql(p, table, ix, b, err))) return rc;
        if ((rc = run(p, ob_buf_text(b), err, "making an index"))) return rc;
    }
    return OB_OK;
}

int ob_table_change(ob_project *p, const char *name, const ob_table *design, unsigned flags, ob_change_result *res,
                    ob_error *err)
{
    ob_change_result local;
    ob_table *cur = NULL, *now = NULL, *want = NULL;
    ob_buf b;
    int rc, i, fk_on, need_rebuild = 0;
    char table[OB_NAME_MAX + 1];
    if (!res) res = &local;
    memset(res, 0, sizeof *res);
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if (!design) return ob_fail(err, OB_ERR_INVALID, "No design to change to.");
    if ((rc = ob_table_read(p, name, &cur, err))) return rc;
    ob_buf_init(&b, 0);
    if (!(want = ob_table_copy(design, err))) {
        rc = OB_ERR_NOMEM;
        goto out;
    }
    if (!want->name && ob_strset(&want->name, cur->name, err)) goto nomem;
    if (ob_strset(&want->was, cur->name, err)) goto nomem;
    want->adopted = cur->adopted;
    want->without_rowid = cur->without_rowid;
    for (i = 0; i < want->nfields; i++) {
        ob_field *f = &want->fields[i];
        if (f->was && !ob_table_field(cur, f->was)) {
            rc = ob_fail(err, OB_ERR_NOT_FOUND, "%s has no field %s to change.", cur->name, f->was);
            goto out;
        }
    }
    if ((rc = follow_renames(want, res, err))) goto out;
    /* a table without a key, not made by OpenBase, may stay that way */
    if (!counter_of(cur) && !cur->nkey && !counter_of(want) && !want->nkey) want->adopted = 1;
    if ((rc = validate(p, want, err))) goto out;
    if (ob_casecmp(want->name, cur->name) != 0 && name_taken(p, want->name)) {
        rc = ob_fail(err, OB_ERR_EXISTS, "There is already a table, query or index called %s.", want->name);
        goto out;
    }
    {
        long long on = 1;
        ob_sql_long(p, "PRAGMA foreign_keys", &on, NULL);
        fk_on = on != 0;
    }
    if (fk_on) ob_exec_quiet(p, "PRAGMA foreign_keys=OFF");
    if ((rc = ob_begin(p, err))) goto restore;
    snprintf(table, sizeof table, "%s", cur->name);
    /* renames first, by ALTER TABLE, so views and triggers follow them */
    if (strcmp(want->name, cur->name) != 0) {
        ob_buf_reset(&b);
        ob_buf_str(&b, "ALTER TABLE ");
        ob_sql_ident_quoted(&b, cur->name);
        ob_buf_str(&b, " RENAME TO ");
        ob_sql_ident_quoted(&b, want->name);
        if (b.failed) {
            rc = ob_nomem(err);
            goto rollback;
        }
        if ((rc = run(p, ob_buf_text(&b), err, "renaming the table"))) goto rollback;
        snprintf(table, sizeof table, "%s", want->name);
        /* Lookups elsewhere name it */
        {
            sqlite3_stmt *st;
            if ((rc = ob_prepare(p, "UPDATE oadb_fields SET lookup_table = ?1 WHERE lookup_table = ?2", &st, err))) goto rollback;
            sqlite3_bind_text(st, 1, want->name, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, cur->name, -1, SQLITE_STATIC);
            rc = sqlite3_step(st);
            sqlite3_finalize(st);
            if (rc != SQLITE_DONE) {
                rc = ob_sqlite_fail(p, rc, err, "saving the design");
                goto rollback;
            }
        }
        {
            sqlite3_stmt *st;
            if ((rc = ob_prepare(p, "UPDATE oadb_fields SET tbl = ?1 WHERE tbl = ?2", &st, err))) goto rollback;
            sqlite3_bind_text(st, 1, want->name, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, cur->name, -1, SQLITE_STATIC);
            rc = sqlite3_step(st);
            sqlite3_finalize(st);
            if (rc != SQLITE_DONE) {
                rc = ob_sqlite_fail(p, rc, err, "saving the design");
                goto rollback;
            }
        }
    }
    if ((rc = rename_fields(p, table, want, cur, &b, err))) goto rollback;
    /* the table as it is now */
    if ((rc = ob_table_read(p, table, &now, err))) goto rollback;
    for (i = 0; i < now->nfields; i++)
        if (!ob_table_field(want, now->fields[i].name)) need_rebuild = 1; /* a field goes */
    for (i = 0; i < want->nfields && !need_rebuild; i++) {
        const ob_field *f = &want->fields[i], *o = f->was ? ob_table_field(now, f->name) : NULL;
        if (o) {
            if (field_sql_differs(f, o)) need_rebuild = 1;
        } else if (!addable(f))
            need_rebuild = 1;
    }
    if (!keys_same(want, now) || !relations_same(want, now)) need_rebuild = 1;
    if (need_rebuild) {
        if ((rc = rebuild(p, table, want, now, flags, res, &b, err))) goto rollback;
    } else {
        long long rows = 0;
        if ((rc = add_columns(p, table, want, &b, err))) goto rollback;
        if ((rc = sync_indexes(p, table, want, now, &b, err))) goto rollback;
        ob_buf_reset(&b);
        ob_buf_str(&b, "SELECT count(*) FROM ");
        ob_sql_ident_quoted(&b, table);
        if (!b.failed && ob_sql_long(p, ob_buf_text(&b), &rows, NULL) == OB_OK) res->rows = (long)rows;
    }
    if ((rc = ob_fields_write(p, want, err))) goto rollback;
    if (fk_on && (rc = check_relations(p, table, err))) goto rollback;
    if ((rc = ob_commit(p, err))) goto restore;
    goto restore;
nomem:
    rc = ob_nomem(err);
    goto out;
rollback:
    ob_rollback(p);
restore:
    if (fk_on) ob_exec_quiet(p, "PRAGMA foreign_keys=ON");
out:
    ob_buf_free(&b);
    ob_table_free(cur);
    ob_table_free(now);
    ob_table_free(want);
    return rc;
}

/* ---- Relationships and indexes in a project ---------------------------------------------- */

int ob_relation_add(ob_project *p, const ob_relation *r, ob_error *err)
{
    ob_table *t;
    ob_relation *n;
    int rc, k, bad = 0;
    if (!r || !r->child || !r->parent || r->nfields < 1 || r->nfields > OB_KEY_MAX)
        return ob_fail(err, OB_ERR_INVALID, "A relationship needs a table, its fields, and the table they point to.");
    if ((rc = ob_table_read(p, r->child, &t, err))) return rc;
    for (k = 0; k < t->nrelations; k++)
        if (t->relations[k].nfields == r->nfields && ob_casecmp(t->relations[k].child_fields[0], r->child_fields[0]) == 0) {
            ob_table_free(t);
            return ob_fail(err, OB_ERR_EXISTS, "%s's field %s already has a relationship.", r->child, r->child_fields[0]);
        }
    n = (ob_relation *)ob_realloc(t->relations, sizeof(ob_relation) * (size_t)(t->nrelations + 1));
    if (!n) {
        ob_table_free(t);
        return ob_nomem(err);
    }
    t->relations = n;
    relation_copy(&n[t->nrelations++], r, &bad);
    if (bad) {
        ob_table_free(t);
        return ob_nomem(err);
    }
    rc = ob_table_change(p, t->name, t, 0, NULL, err);
    ob_table_free(t);
    return rc;
}

int ob_relation_remove(ob_project *p, const char *child, const char *child_field, ob_error *err)
{
    ob_table *t;
    int rc;
    if ((rc = ob_table_read(p, child, &t, err))) return rc;
    if (ob_table_remove_relation(t, child_field ? child_field : "")) {
        ob_table_free(t);
        return ob_fail(err, OB_ERR_NOT_FOUND, "%s's field %s has no relationship.", child, child_field ? child_field : "");
    }
    {
        /* a Lookup without its relationship is an Integer */
        ob_field *f = ob_table_field(t, child_field);
        if (f && f->type == OB_TYPE_LOOKUP) f->type = OB_TYPE_INTEGER;
    }
    rc = ob_table_change(p, t->name, t, 0, NULL, err);
    ob_table_free(t);
    return rc;
}

void ob_relations_free(ob_relation *r, int count)
{
    int i;
    if (!r) return;
    for (i = 0; i < count; i++) ob_relation_clear(&r[i]);
    ob_free(r);
}

int ob_relation_list(ob_project *p, ob_relation **out, int *count, ob_error *err)
{
    char **names;
    int n, i, rc;
    *out = NULL;
    *count = 0;
    if ((rc = ob_table_list(p, &names, &n, err))) return rc;
    for (i = 0; i < n && !rc; i++) rc = read_relations(p, names[i], out, count, err);
    ob_names_free(names, n);
    if (rc) {
        ob_relations_free(*out, *count);
        *out = NULL;
        *count = 0;
    }
    return rc;
}

int ob_index_add(ob_project *p, const char *table, const ob_index *ix, ob_error *err)
{
    ob_table *t;
    ob_index copy;
    ob_buf b;
    int rc, k, bad = 0;
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if (!ix || ix->nfields < 1 || ix->nfields > OB_INDEX_FIELDS) return ob_fail(err, OB_ERR_INVALID, "An index needs a field.");
    if ((rc = ob_table_read(p, table, &t, err))) return rc;
    memset(&copy, 0, sizeof copy);
    copy.unique = ix->unique;
    copy.nfields = ix->nfields;
    copy_str(&copy.name, ix->name, &bad);
    for (k = 0; k < ix->nfields; k++) {
        const ob_field *f = ob_table_field(t, ix->fields[k]);
        if (!f) {
            rc = ob_fail(err, OB_ERR_NOT_FOUND, "%s has no field %s.", t->name, ix->fields[k] ? ix->fields[k] : "");
            goto done;
        }
        if (f->type == OB_TYPE_OBJECT) {
            rc = ob_fail(err, OB_ERR_INVALID, "A Datatype object can't be indexed.");
            goto done;
        }
        copy_str(&copy.fields[k], f->name, &bad);
    }
    if (bad) {
        rc = ob_nomem(err);
        goto done;
    }
    if (copy.name && ((rc = ob_name_check(copy.name, "index", err)) || name_taken(p, copy.name))) {
        if (!rc) rc = ob_fail(err, OB_ERR_EXISTS, "There is already something called %s.", copy.name);
        goto done;
    }
    ob_buf_init(&b, 0);
    if (!(rc = index_sql(p, t->name, &copy, &b, err))) rc = run(p, ob_buf_text(&b), err, "making the index");
    ob_buf_free(&b);
done:
    index_clear(&copy);
    ob_table_free(t);
    return rc;
}

int ob_index_remove(ob_project *p, const char *name, ob_error *err)
{
    sqlite3_stmt *st;
    int rc, ok = 0;
    ob_buf b;
    char exact[OB_NAME_MAX + 1];
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if (!name || !schema_name(p, "index", name, exact, sizeof exact))
        return ob_fail(err, OB_ERR_NOT_FOUND, "There is no index called %s.", name ? name : "");
    if ((rc = ob_prepare(p, "SELECT tbl_name, sql FROM sqlite_schema WHERE type='index' AND name=?", &st, err))) return rc;
    sqlite3_bind_text(st, 1, exact, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW && col_text(st, 1) && col_text(st, 0) && ob_ncasecmp(col_text(st, 0), "oadb_", 5) != 0)
        ok = 1;
    sqlite3_finalize(st);
    if (!ok) return ob_fail(err, OB_ERR_INVALID, "%s belongs to its table's design; change the table instead.", exact);
    ob_buf_init(&b, 0);
    ob_buf_str(&b, "DROP INDEX ");
    ob_sql_ident_quoted(&b, exact);
    rc = b.failed ? ob_nomem(err) : run(p, ob_buf_text(&b), err, "removing the index");
    ob_buf_free(&b);
    return rc;
}
