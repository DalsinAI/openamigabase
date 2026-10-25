/* OpenBase core: records (ob_record.h), for the program's datasheets and
 * forms. Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "ob_internal.h"
#include "ob_record.h"

#include <stdio.h>
#include <string.h>

#define COMPANION_NAME "_name"
#define COMPANION_TYPE "_type"

static int select_from(ob_buf *b, const char *what, const char *table)
{
    ob_buf_str(b, "SELECT ");
    ob_buf_str(b, what);
    ob_buf_str(b, " FROM ");
    ob_sql_ident_quoted(b, table);
    return b->failed;
}

int ob_record_count(ob_project *p, const char *table, long *count, ob_error *err)
{
    ob_buf b;
    long long n = 0;
    int rc;
    *count = 0;
    if (!ob_table_exists(p, table)) return ob_fail(err, OB_ERR_NOT_FOUND, "There is no table %s.", table);
    ob_buf_init(&b, 0);
    if (select_from(&b, "count(*)", table)) {
        ob_buf_free(&b);
        return ob_nomem(err);
    }
    rc = ob_sql_long(p, ob_buf_text(&b), &n, err);
    ob_buf_free(&b);
    *count = (long)n;
    return rc;
}

int ob_record_keys(ob_project *p, const char *table, long offset, int n, long long *keys, int *got, ob_error *err)
{
    ob_buf b;
    sqlite3_stmt *st = NULL;
    int rc;
    *got = 0;
    if (n <= 0 || offset < 0) return OB_OK;
    if (!ob_table_exists(p, table)) return ob_fail(err, OB_ERR_NOT_FOUND, "There is no table %s.", table);
    ob_buf_init(&b, 0);
    select_from(&b, "rowid", table);
    ob_buf_str(&b, " ORDER BY rowid LIMIT ?1 OFFSET ?2");
    if (b.failed) {
        ob_buf_free(&b);
        return ob_nomem(err);
    }
    rc = ob_prepare(p, ob_buf_text(&b), &st, err);
    ob_buf_free(&b);
    if (rc) return rc;
    sqlite3_bind_int(st, 1, n);
    sqlite3_bind_int64(st, 2, offset);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW && *got < n)
        keys[(*got)++] = sqlite3_column_int64(st, 0);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) rc = ob_sqlite_fail(p, rc, err, "reading the records");
    else rc = OB_OK;
    sqlite3_finalize(st);
    return rc;
}

void ob_record_free(char **values, int n)
{
    int i;
    if (!values) return;
    for (i = 0; i < n; i++) ob_free(values[i]);
    ob_free(values);
}

/* A stored value as the field's type shows it. */
static char *shown(ob_project *p, const ob_field *f, sqlite3_stmt *st, int col, sqlite3_stmt *names, int ncol)
{
    const ob_formats *fm = ob_project_formats(p);
    char buf[200];
    const char *text;
    int kind = sqlite3_column_type(st, col);
    if (kind == SQLITE_NULL) return ob_strdup("");
    switch (f->type) {
    case OB_TYPE_CURRENCY:
        if (kind == SQLITE_INTEGER && !ob_format_currency(sqlite3_column_int64(st, col), fm, 1, buf, sizeof buf))
            return ob_strdup(buf);
        break;
    case OB_TYPE_DATE:
        text = (const char *)sqlite3_column_text(st, col);
        if (text && !ob_format_date(text, fm, buf, sizeof buf)) return ob_strdup(buf);
        break;
    case OB_TYPE_YESNO:
        if (kind == SQLITE_INTEGER) return ob_strdup(sqlite3_column_int64(st, col) ? fm->yes : fm->no);
        break;
    case OB_TYPE_OBJECT:
        /* its file's name, else what it is */
        if (names && ncol >= 0 && sqlite3_column_type(names, ncol) == SQLITE_TEXT)
            return ob_strdup((const char *)sqlite3_column_text(names, ncol));
        snprintf(buf, sizeof buf, "(%d bytes)", sqlite3_column_bytes(st, col));
        return ob_strdup(buf);
    default:
        break;
    }
    text = (const char *)sqlite3_column_text(st, col);
    return ob_strdup(text ? text : "");
}

int ob_record_read(ob_project *p, const ob_table *t, long long key, char ***values, ob_error *err)
{
    ob_buf b;
    sqlite3_stmt *st = NULL;
    char **out;
    int i, rc, nobj = 0;
    *values = NULL;
    ob_buf_init(&b, 0);
    ob_buf_str(&b, "SELECT ");
    for (i = 0; i < t->nfields; i++) {
        if (i) ob_buf_str(&b, ", ");
        ob_sql_ident_quoted(&b, t->fields[i].name);
    }
    /* the objects' names after the fields */
    for (i = 0; i < t->nfields; i++)
        if (t->fields[i].type == OB_TYPE_OBJECT) {
            char name[OB_NAME_MAX + 8];
            snprintf(name, sizeof name, "%s" COMPANION_NAME, t->fields[i].name);
            ob_buf_str(&b, ", ");
            ob_sql_ident_quoted(&b, name);
            nobj++;
        }
    if (!t->nfields) ob_buf_str(&b, "rowid");
    ob_buf_str(&b, " FROM ");
    ob_sql_ident_quoted(&b, t->name);
    ob_buf_str(&b, " WHERE rowid = ?1");
    if (b.failed) {
        ob_buf_free(&b);
        return ob_nomem(err);
    }
    rc = ob_prepare(p, ob_buf_text(&b), &st, err);
    ob_buf_free(&b);
    if (rc) return rc;
    sqlite3_bind_int64(st, 1, key);
    rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) {
        int code = rc == SQLITE_DONE ? ob_fail(err, OB_ERR_NOT_FOUND, "That record isn't there any more.")
                                     : ob_sqlite_fail(p, rc, err, "reading the record");
        sqlite3_finalize(st);
        return code;
    }
    out = ob_zalloc(sizeof *out * (size_t)(t->nfields ? t->nfields : 1));
    if (!out) {
        sqlite3_finalize(st);
        return ob_nomem(err);
    }
    nobj = 0;
    for (i = 0; i < t->nfields; i++) {
        int ncol = -1;
        if (t->fields[i].type == OB_TYPE_OBJECT) ncol = t->nfields + nobj++;
        if (!(out[i] = shown(p, &t->fields[i], st, i, st, ncol))) {
            ob_record_free(out, t->nfields);
            sqlite3_finalize(st);
            return ob_nomem(err);
        }
    }
    sqlite3_finalize(st);
    *values = out;
    return OB_OK;
}

static int bind_value(sqlite3_stmt *st, int at, const ob_value *v)
{
    switch (v->kind) {
    case SQLITE_INTEGER: return sqlite3_bind_int64(st, at, v->i);
    case SQLITE_TEXT: return sqlite3_bind_text(st, at, v->text, (int)v->n, SQLITE_TRANSIENT);
    default: return sqlite3_bind_null(st, at);
    }
}

static int writable(const ob_field *f)
{
    return f->type != OB_TYPE_COUNTER && f->type != OB_TYPE_OBJECT;
}

/* Did a relationship refuse the last statement? SQLite says so with
   SQLITE_CONSTRAINT_FOREIGNKEY, or, for ON DELETE RESTRICT, from the
   action it runs (SQLITE_CONSTRAINT_TRIGGER), in the same words. */
static int fk_failed(ob_project *p)
{
    sqlite3 *db = ob_project_db(p);
    int ext = sqlite3_extended_errcode(db);
    const char *msg = sqlite3_errmsg(db);
    return ext == SQLITE_CONSTRAINT_FOREIGNKEY ||
           ((ext & 0xFF) == SQLITE_CONSTRAINT && msg && strcmp(msg, "FOREIGN KEY constraint failed") == 0);
}

static const char *label_of(const ob_field *f)
{
    return f->label && *f->label ? f->label : f->name;
}

/* Is there a record in table whose field (NULL: its key) is v? */
static int parent_has(ob_project *p, const char *table, const char *field, const ob_value *v)
{
    ob_buf b;
    sqlite3_stmt *st = NULL;
    int found = 1;
    ob_buf_init(&b, 0);
    select_from(&b, "1", table);
    ob_buf_str(&b, " WHERE ");
    if (field) ob_sql_ident_quoted(&b, field);
    else ob_buf_str(&b, "rowid");
    ob_buf_str(&b, " = ?1 LIMIT 1");
    if (!b.failed && sqlite3_prepare_v2(ob_project_db(p), ob_buf_text(&b), -1, &st, NULL) == SQLITE_OK) {
        bind_value(st, 1, v);
        found = sqlite3_step(st) == SQLITE_ROW;
    }
    sqlite3_finalize(st);
    ob_buf_free(&b);
    return found;
}

/* A relationship refused a record: which one, in plain words. 1 when found. */
static int which_parent_missing(ob_project *p, const ob_table *t, const char *const *texts, ob_error *err)
{
    int r, i;
    for (r = 0; r < t->nrelations; r++) {
        const ob_relation *rel = &t->relations[r];
        for (i = 0; i < t->nfields; i++) {
            ob_value v;
            char buf[200];
            if (rel->nfields != 1 || ob_casecmp(t->fields[i].name, rel->child_fields[0])) continue;
            if (!texts || !texts[i] || !*texts[i]) continue;
            if (ob_value_from_text(t->fields[i].type, texts[i], ob_project_formats(p), &v, buf, sizeof buf)) continue;
            if (!parent_has(p, rel->parent, rel->parent_fields[0], &v)) {
                ob_fail(err, OB_ERR_RULE, "%s: %s doesn't match a record in %s.", label_of(&t->fields[i]), texts[i],
                        rel->parent);
                return 1;
            }
        }
    }
    return 0;
}

int ob_record_write(ob_project *p, const ob_table *t, long long *key, const char *const *texts, ob_error *err)
{
    ob_buf b;
    sqlite3_stmt *st = NULL;
    int i, n = 0, rc, at;
    if (ob_project_readonly(p)) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    ob_buf_init(&b, 0);
    for (i = 0; i < t->nfields; i++)
        if (writable(&t->fields[i]) && texts && texts[i]) n++;
    if (*key) {
        if (!n) return OB_OK;
        ob_buf_str(&b, "UPDATE ");
        ob_sql_ident_quoted(&b, t->name);
        ob_buf_str(&b, " SET ");
        for (i = 0, at = 0; i < t->nfields; i++) {
            if (!writable(&t->fields[i]) || !texts || !texts[i]) continue;
            if (at++) ob_buf_str(&b, ", ");
            ob_sql_ident_quoted(&b, t->fields[i].name);
            ob_buf_str(&b, " = ?");
            ob_buf_long(&b, at);
        }
        ob_buf_str(&b, " WHERE rowid = ?");
        ob_buf_long(&b, at + 1);
    } else {
        ob_buf_str(&b, "INSERT INTO ");
        ob_sql_ident_quoted(&b, t->name);
        if (!n) {
            ob_buf_str(&b, " DEFAULT VALUES");
        } else {
            ob_buf_str(&b, " (");
            for (i = 0, at = 0; i < t->nfields; i++) {
                if (!writable(&t->fields[i]) || !texts || !texts[i]) continue;
                if (at++) ob_buf_str(&b, ", ");
                ob_sql_ident_quoted(&b, t->fields[i].name);
            }
            ob_buf_str(&b, ") VALUES (");
            for (at = 1; at <= n; at++) {
                if (at > 1) ob_buf_str(&b, ", ");
                ob_buf_char(&b, '?');
                ob_buf_long(&b, at);
            }
            ob_buf_char(&b, ')');
        }
    }
    if (b.failed) {
        ob_buf_free(&b);
        return ob_nomem(err);
    }
    rc = ob_prepare(p, ob_buf_text(&b), &st, err);
    ob_buf_free(&b);
    if (rc) return rc;
    for (i = 0, at = 0; i < t->nfields; i++) {
        ob_value v;
        char buf[200];
        if (!writable(&t->fields[i]) || !texts || !texts[i]) continue;
        if (ob_value_from_text(t->fields[i].type, texts[i], ob_project_formats(p), &v, buf, sizeof buf)) {
            sqlite3_finalize(st);
            return ob_fail(err, OB_ERR_INVALID, "%s: \"%s\" isn't a %s.", t->fields[i].label ? t->fields[i].label
                           : t->fields[i].name, texts[i], ob_type_name(t->fields[i].type));
        }
        bind_value(st, ++at, &v);
    }
    if (*key) sqlite3_bind_int64(st, at + 1, *key);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        int fk = fk_failed(p);
        ob_error said;
        int code = ob_sqlite_fail(p, rc, &said, *key ? "changing the record" : "adding the record");
        if (fk && which_parent_missing(p, t, texts, err)) return OB_ERR_RULE;
        if (err) *err = said;
        return code;
    }
    if (!*key) *key = sqlite3_last_insert_rowid(ob_project_db(p));
    return OB_OK;
}

/* A relationship refused deleting record key of table: the tables whose
   records point to it, in plain words. 1 when found. */
static int which_children_point(ob_project *p, const char *table, long long key, ob_error *err)
{
    ob_relation *rels = NULL;
    ob_buf names;
    int n = 0, r, found = 0;
    ob_error ignore;
    if (ob_relation_list(p, &rels, &n, &ignore)) return 0;
    ob_buf_init(&names, 400);
    for (r = 0; r < n; r++) {
        ob_buf b;
        sqlite3_stmt *st = NULL;
        if (rels[r].nfields != 1 || ob_casecmp(rels[r].parent, table)) continue;
        ob_buf_init(&b, 0);
        select_from(&b, "1", rels[r].child);
        ob_buf_str(&b, " WHERE ");
        ob_sql_ident_quoted(&b, rels[r].child_fields[0]);
        if (rels[r].parent_fields[0]) {
            /* the value of the parent's field it points by, in the record still there */
            ob_buf_str(&b, " = (SELECT ");
            ob_sql_ident_quoted(&b, rels[r].parent_fields[0]);
            ob_buf_str(&b, " FROM ");
            ob_sql_ident_quoted(&b, table);
            ob_buf_str(&b, " WHERE rowid = ?1) LIMIT 1");
        } else {
            ob_buf_str(&b, " = ?1 LIMIT 1");
        }
        if (!b.failed && sqlite3_prepare_v2(ob_project_db(p), ob_buf_text(&b), -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, key);
            if (sqlite3_step(st) == SQLITE_ROW) {
                if (found++) ob_buf_str(&names, " and ");
                ob_buf_str(&names, rels[r].child);
            }
        }
        sqlite3_finalize(st);
        ob_buf_free(&b);
    }
    if (found)
        ob_fail(err, OB_ERR_RULE, "Records in %s point to this one: delete or change them first.", ob_buf_text(&names));
    ob_buf_free(&names);
    ob_relations_free(rels, n);
    return found > 0;
}

int ob_record_delete(ob_project *p, const char *table, long long key, ob_error *err)
{
    ob_buf b;
    sqlite3_stmt *st = NULL;
    int rc;
    if (ob_project_readonly(p)) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    ob_buf_init(&b, 0);
    ob_buf_str(&b, "DELETE FROM ");
    ob_sql_ident_quoted(&b, table);
    ob_buf_str(&b, " WHERE rowid = ?1");
    if (b.failed) {
        ob_buf_free(&b);
        return ob_nomem(err);
    }
    rc = ob_prepare(p, ob_buf_text(&b), &st, err);
    ob_buf_free(&b);
    if (rc) return rc;
    sqlite3_bind_int64(st, 1, key);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        int fk = fk_failed(p);
        ob_error said;
        int code = ob_sqlite_fail(p, rc, &said, "deleting the record");
        if (fk && which_children_point(p, table, key, err)) return OB_ERR_RULE;
        if (err) *err = said;
        return code;
    }
    if (!sqlite3_changes(ob_project_db(p))) return ob_fail(err, OB_ERR_NOT_FOUND, "That record isn't there any more.");
    return OB_OK;
}

/* The object field of a table, checked. */
static int object_field(ob_project *p, const char *table, const char *field, ob_error *err)
{
    ob_table *t = NULL;
    const ob_field *f;
    int rc = ob_table_read(p, table, &t, err);
    if (rc) return rc;
    f = ob_table_field(t, field);
    if (!f || f->type != OB_TYPE_OBJECT)
        rc = ob_fail(err, OB_ERR_NOT_FOUND, "%s has no Datatype object field %s.", table, field);
    ob_table_free(t);
    return rc;
}

int ob_object_set(ob_project *p, const char *table, const char *field, long long key, const void *data, long size,
                  const char *name, const char *kind, ob_error *err)
{
    ob_buf b;
    sqlite3_stmt *st = NULL;
    char cname[OB_NAME_MAX + 8], ctype[OB_NAME_MAX + 8];
    int rc;
    if (ob_project_readonly(p)) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if ((rc = object_field(p, table, field, err))) return rc;
    if (data && size < 0) return ob_fail(err, OB_ERR_INVALID, "A Datatype object can't have a negative size.");
    if (data && ob_project_config(p)->max_object_kib > 0 && size / 1024 > ob_project_config(p)->max_object_kib)
        return ob_fail(err, OB_ERR_LIMIT, "That file is %ld KB: a Datatype object may be %ld KB at most.",
                       size / 1024, ob_project_config(p)->max_object_kib);
    snprintf(cname, sizeof cname, "%s" COMPANION_NAME, field);
    snprintf(ctype, sizeof ctype, "%s" COMPANION_TYPE, field);
    ob_buf_init(&b, 0);
    ob_buf_str(&b, "UPDATE ");
    ob_sql_ident_quoted(&b, table);
    ob_buf_str(&b, " SET ");
    ob_sql_ident_quoted(&b, field);
    ob_buf_str(&b, " = ?1, ");
    ob_sql_ident_quoted(&b, cname);
    ob_buf_str(&b, " = ?2, ");
    ob_sql_ident_quoted(&b, ctype);
    ob_buf_str(&b, " = ?3 WHERE rowid = ?4");
    if (b.failed) {
        ob_buf_free(&b);
        return ob_nomem(err);
    }
    rc = ob_prepare(p, ob_buf_text(&b), &st, err);
    ob_buf_free(&b);
    if (rc) return rc;
    if (data) {
        sqlite3_bind_blob(st, 1, data, (int)size, SQLITE_STATIC);
        if (name) sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
        if (kind) sqlite3_bind_text(st, 3, kind, -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int64(st, 4, key);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ob_sqlite_fail(p, rc, err, "keeping the object");
    if (!sqlite3_changes(ob_project_db(p))) return ob_fail(err, OB_ERR_NOT_FOUND, "That record isn't there any more.");
    return OB_OK;
}

int ob_object_get(ob_project *p, const char *table, const char *field, long long key, void **data, long *size,
                  char *name, size_t name_size, ob_error *err)
{
    ob_buf b;
    sqlite3_stmt *st = NULL;
    char cname[OB_NAME_MAX + 8];
    int rc;
    *data = NULL;
    *size = 0;
    if (name && name_size) name[0] = 0;
    if ((rc = object_field(p, table, field, err))) return rc;
    snprintf(cname, sizeof cname, "%s" COMPANION_NAME, field);
    ob_buf_init(&b, 0);
    ob_buf_str(&b, "SELECT ");
    ob_sql_ident_quoted(&b, field);
    ob_buf_str(&b, ", ");
    ob_sql_ident_quoted(&b, cname);
    ob_buf_str(&b, " FROM ");
    ob_sql_ident_quoted(&b, table);
    ob_buf_str(&b, " WHERE rowid = ?1");
    if (b.failed) {
        ob_buf_free(&b);
        return ob_nomem(err);
    }
    rc = ob_prepare(p, ob_buf_text(&b), &st, err);
    ob_buf_free(&b);
    if (rc) return rc;
    sqlite3_bind_int64(st, 1, key);
    rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) {
        int code = rc == SQLITE_DONE ? ob_fail(err, OB_ERR_NOT_FOUND, "That record isn't there any more.")
                                     : ob_sqlite_fail(p, rc, err, "reading the object");
        sqlite3_finalize(st);
        return code;
    }
    if (sqlite3_column_type(st, 0) != SQLITE_NULL) {
        int n = sqlite3_column_bytes(st, 0);
        const void *bytes = sqlite3_column_blob(st, 0);
        if (n > 0 && bytes) {
            if (!(*data = ob_alloc((size_t)n))) {
                sqlite3_finalize(st);
                return ob_nomem(err);
            }
            memcpy(*data, bytes, (size_t)n);
            *size = n;
        }
    }
    if (name && name_size && sqlite3_column_type(st, 1) == SQLITE_TEXT)
        snprintf(name, name_size, "%s", (const char *)sqlite3_column_text(st, 1));
    sqlite3_finalize(st);
    return OB_OK;
}
