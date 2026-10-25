/* OpenBase host tests: SQL golden files (Design-OpenBase.md, 5.5).
 * For each tests/sql/NAME.query (a grid, as oadb_queries keeps it):
 * the SQL it writes is compared with NAME.sql; then that SQL is run on
 * the fixture project (made here through the API, rows from fixture.sql),
 * with NAME.args as its answers if there is one, and the rows are
 * compared with NAME.rows. OB_GOLDEN_UPDATE=1 rewrites .sql and .rows.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#define _POSIX_C_SOURCE 200809L
#include "obtest.h"

#include <dirent.h>
#include <sqlite3.h>
#include <stdlib.h>

static ob_project *fixture(const char *dir)
{
    ob_project *p = obt_memory_project();
    ob_table *t;
    ob_field *f;
    ob_error e;
    char path[1024], *rows;
    t = ob_table_new("Customers", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    ob_table_add_field(t, "Town", OB_TYPE_TEXT, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    t = ob_table_new("Products", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    ob_table_add_field(t, "Price", OB_TYPE_CURRENCY, &e);
    f = ob_table_add_field(t, "Group", OB_TYPE_CHOICE, &e);
    ob_strset(&f->choices, "Tools\nFood", &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    t = ob_table_new("Orders", 1, &e);
    f = ob_table_add_field(t, "customer", OB_TYPE_LOOKUP, &e);
    ob_strset(&f->lookup_table, "Customers", &e);
    ob_table_add_field(t, "Day", OB_TYPE_DATE, &e);
    ob_table_add_field(t, "Paid", OB_TYPE_YESNO, &e);
    ob_table_add_field(t, "Total", OB_TYPE_CURRENCY, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    t = ob_table_new("Order Lines", 1, &e);
    f = ob_table_add_field(t, "order", OB_TYPE_LOOKUP, &e);
    ob_strset(&f->lookup_table, "Orders", &e);
    f = ob_table_add_field(t, "product", OB_TYPE_LOOKUP, &e);
    ob_strset(&f->lookup_table, "Products", &e);
    ob_table_add_field(t, "Qty", OB_TYPE_INTEGER, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    snprintf(path, sizeof path, "%s/fixture.sql", dir);
    rows = obt_read_file(path, NULL);
    CHECK(rows != NULL);
    if (rows) OK(ob_exec(p, rows, E));
    free(rows);
    return p;
}

static char *trim_end(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
    return s;
}

/* The column names, then the rows */
static char *run(ob_project *p, const char *sql, const char *args, ob_error *err)
{
    sqlite3_stmt *st;
    ob_buf out;
    int i, n, rc;
    char *res;
    ob_buf_init(&out, 0);
    if (sqlite3_prepare_v2(ob_project_db(p), sql, -1, &st, NULL) != SQLITE_OK) {
        snprintf(err->text, sizeof err->text, "%s", sqlite3_errmsg(ob_project_db(p)));
        return NULL;
    }
    if (args) {
        const char *a = args;
        for (i = 1; *a; i++) {
            const char *e = strchr(a, '\n');
            size_t len = e ? (size_t)(e - a) : strlen(a);
            sqlite3_bind_text(st, i, a, (int)len, SQLITE_TRANSIENT);
            a += len + (e ? 1 : 0);
        }
    }
    n = sqlite3_column_count(st);
    for (i = 0; i < n; i++) {
        if (i) ob_buf_char(&out, '\t');
        ob_buf_str(&out, sqlite3_column_name(st, i));
    }
    ob_buf_char(&out, '\n');
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        for (i = 0; i < n; i++) {
            if (i) ob_buf_char(&out, '\t');
            if (sqlite3_column_type(st, i) == SQLITE_NULL) ob_buf_str(&out, "NULL");
            else ob_buf_str(&out, (const char *)sqlite3_column_text(st, i));
        }
        ob_buf_char(&out, '\n');
    }
    if (rc != SQLITE_DONE) snprintf(err->text, sizeof err->text, "%s", sqlite3_errmsg(ob_project_db(p)));
    sqlite3_finalize(st);
    res = malloc(out.len + 1);
    memcpy(res, ob_buf_text(&out), out.len + 1);
    ob_buf_free(&out);
    return rc == SQLITE_DONE ? res : (free(res), (char *)NULL);
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void one(ob_project *p, const char *dir, const char *name, int update)
{
    char path[1024], *grid, *want_sql, *want_rows, *args, *rows;
    ob_query *q = NULL;
    ob_buf sql;
    ob_params pr;
    ob_error e;
    memset(&e, 0, sizeof e);
    snprintf(path, sizeof path, "%s/%s.query", dir, name);
    grid = obt_read_file(path, NULL);
    snprintf(path, sizeof path, "%s/%s.args", dir, name);
    args = obt_read_file(path, NULL);
    ob_buf_init(&sql, 0);
    obt_checks++;
    if (!grid || ob_query_from_text(grid, &q, &e) || ob_query_sql(q, &sql, &pr, &e)) {
        obt_failures++;
        fprintf(stderr, "golden %s: %s\n", name, grid ? e.text : "no .query file");
        goto done;
    }
    rows = run(p, ob_buf_text(&sql), args, &e);
    if (!rows) {
        obt_failures++;
        fprintf(stderr, "golden %s: the SQL doesn't run: %s\n  %s\n", name, e.text, ob_buf_text(&sql));
        goto done;
    }
    if (update) {
        char *line = malloc(sql.len + 2);
        sprintf(line, "%s\n", ob_buf_text(&sql));
        snprintf(path, sizeof path, "%s/%s.sql", dir, name);
        obt_write_file(path, line, (long)strlen(line));
        snprintf(path, sizeof path, "%s/%s.rows", dir, name);
        obt_write_file(path, rows, (long)strlen(rows));
        free(line);
    } else {
        snprintf(path, sizeof path, "%s/%s.sql", dir, name);
        want_sql = obt_read_file(path, NULL);
        snprintf(path, sizeof path, "%s/%s.rows", dir, name);
        want_rows = obt_read_file(path, NULL);
        if (!want_sql || strcmp(trim_end(want_sql), ob_buf_text(&sql)) != 0) {
            obt_failures++;
            fprintf(stderr, "golden %s: the SQL differs\n  got  %s\n  want %s\n", name, ob_buf_text(&sql), want_sql ? want_sql : "(none)");
        }
        obt_checks++;
        if (!want_rows || strcmp(want_rows, rows) != 0) {
            obt_failures++;
            fprintf(stderr, "golden %s: the rows differ\n--- got\n%s--- want\n%s", name, rows, want_rows ? want_rows : "(none)\n");
        }
        free(want_sql);
        free(want_rows);
    }
    free(rows);
done:
    ob_buf_free(&sql);
    ob_query_free(q);
    free(grid);
    free(args);
}

void suite_golden(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *ent;
    char **names = NULL;
    int n = 0, i, update = getenv("OB_GOLDEN_UPDATE") && *getenv("OB_GOLDEN_UPDATE") == '1';
    ob_project *p;
    if (!d) {
        obt_failures++;
        fprintf(stderr, "golden: no folder %s\n", dir);
        return;
    }
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len > 6 && strcmp(ent->d_name + len - 6, ".query") == 0) {
            names = realloc(names, sizeof(char *) * (size_t)(n + 1));
            names[n] = malloc(len - 5);
            memcpy(names[n], ent->d_name, len - 6);
            names[n][len - 6] = 0;
            n++;
        }
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(char *), by_name);
    p = fixture(dir);
    CHECK(n >= 10);
    for (i = 0; i < n; i++) {
        one(p, dir, names[i], update);
        free(names[i]);
    }
    free(names);
    ob_project_close(p);
}
