/* OpenBase host tests: the runner and shared helpers.
 *   test-openbase [suite...] [--sql DIR]
 * Suites: types project schema csv query golden fuzz alloc (all: every one).
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#define _POSIX_C_SOURCE 200809L
#include "obtest.h"

#include <sqlite3.h>
#include <stdlib.h>
#include <unistd.h>

int obt_checks, obt_failures;
const char *obt_dir;

char *obt_path(const char *name)
{
    static char paths[4][1024];
    static int at;
    char *p = paths[at++ & 3];
    snprintf(p, 1024, "%s/%s", obt_dir, name);
    return p;
}

ob_project *obt_memory_project(void)
{
    ob_project *p = NULL;
    ob_error e;
    if (ob_project_create(":memory:", "Test", NULL, &p, &e) != OB_OK) {
        fprintf(stderr, "can't make a project in memory: %s\n", e.text);
        exit(2);
    }
    return p;
}

long long obt_long(ob_project *p, const char *sql)
{
    long long v = -999999;
    ob_error e;
    if (ob_sql_long(p, sql, &v, &e) != OB_OK) fprintf(stderr, "obt_long(%s): %s\n", sql, e.text);
    return v;
}

char *obt_text(ob_project *p, const char *sql)
{
    sqlite3_stmt *st;
    char *out;
    const char *v = "";
    if (sqlite3_prepare_v2(ob_project_db(p), sql, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "obt_text(%s): %s\n", sql, sqlite3_errmsg(ob_project_db(p)));
        out = malloc(1);
        out[0] = 0;
        return out;
    }
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0)) v = (const char *)sqlite3_column_text(st, 0);
    out = malloc(strlen(v) + 1);
    strcpy(out, v);
    sqlite3_finalize(st);
    return out;
}

char *obt_rows(ob_project *p, const char *sql)
{
    sqlite3_stmt *st;
    size_t cap = 256, len = 0;
    char *out = malloc(cap);
    out[0] = 0;
    if (sqlite3_prepare_v2(ob_project_db(p), sql, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "obt_rows(%s): %s\n", sql, sqlite3_errmsg(ob_project_db(p)));
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        int i, n = sqlite3_column_count(st);
        for (i = 0; i < n; i++) {
            const char *v = sqlite3_column_type(st, i) == SQLITE_NULL ? "NULL"
                            : sqlite3_column_type(st, i) == SQLITE_BLOB ? "BLOB"
                                                                        : (const char *)sqlite3_column_text(st, i);
            size_t k = strlen(v) + 2;
            while (len + k + 1 > cap) cap *= 2, out = realloc(out, cap);
            memcpy(out + len, v, k - 2);
            len += k - 2;
            out[len++] = i + 1 < n ? '\t' : '\n';
            out[len] = 0;
        }
    }
    sqlite3_finalize(st);
    return out;
}

char *obt_read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)n + 1);
    if (fread(data, 1, (size_t)n, f) != (size_t)n) n = 0;
    data[n] = 0;
    fclose(f);
    if (size) *size = n;
    return data;
}

int obt_write_file(const char *path, const void *data, long size)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) return -1;
    ok = fwrite(data, 1, (size_t)size, f) == (size_t)size;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static void rm_tree(const char *dir)
{
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stderr, "couldn't clear %s\n", dir);
}

int main(int argc, char **argv)
{
    static char dir[] = "/tmp/openbase-test-XXXXXX";
    const char *sql_dir = "tests/sql";
    int i, all = argc < 2, ran = 0;
    const char *tmp = getenv("OB_TEST_TMP");
    static char tmpl[1024];
    if (tmp && *tmp) {
        snprintf(tmpl, sizeof tmpl, "%s/openbase-test-XXXXXX", tmp);
        obt_dir = mkdtemp(tmpl);
    } else
        obt_dir = mkdtemp(dir);
    if (!obt_dir) {
        perror("mkdtemp");
        return 2;
    }
    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "--sql") == 0 && i + 1 < argc) sql_dir = argv[++i];
        else if (strcmp(argv[i], "all") == 0) all = 1;
    printf("OpenBase core tests, SQLite %s\n", sqlite3_libversion());
#define RUN(name, call)                                                                     \
    do {                                                                                    \
        int want = all, k;                                                                  \
        for (k = 1; k < argc; k++)                                                          \
            if (strcmp(argv[k], name) == 0) want = 1;                                       \
        if (want) {                                                                         \
            int before = obt_failures, checks = obt_checks;                                 \
            call;                                                                           \
            printf("  %-8s %5d checks, %d failed\n", name, obt_checks - checks, obt_failures - before); \
            ran++;                                                                          \
        }                                                                                   \
    } while (0)
    RUN("types", suite_types());
    RUN("project", suite_project());
    RUN("schema", suite_schema());
    RUN("csv", suite_csv());
    RUN("query", suite_query());
    RUN("record", suite_record());
    RUN("golden", suite_golden(sql_dir));
    RUN("fuzz", suite_fuzz());
    RUN("alloc", suite_alloc());
    rm_tree(obt_dir);
    if (!ran) {
        fprintf(stderr, "no such suite\n");
        return 2;
    }
    printf("%d checks, %d failed\n", obt_checks, obt_failures);
    return obt_failures ? 1 : 0;
}
