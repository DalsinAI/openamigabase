/* OpenBase host tests: .oadb projects.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "obtest.h"

#include <sqlite3.h>
#include <stdlib.h>

static void create_and_open(void)
{
    ob_project *p = NULL;
    ob_error e;
    char title[64];
    const char *path = obt_path("shop.oadb");
    long size;
    unsigned char *data;
    OK(ob_project_create(path, "Shop", NULL, &p, E));
    CHECK(obt_long(p, "PRAGMA application_id") == 0x4F414442L);
    CHECK(obt_long(p, "PRAGMA user_version") == 1);
    CHECK(obt_long(p, "PRAGMA foreign_keys") == 1);
    CHECK(obt_long(p, "SELECT count(*) FROM sqlite_schema WHERE name LIKE 'oadb\\_%' ESCAPE '\\'") == 7);
    OK(ob_meta_set(p, "start_form", "Orders", E));
    CHECK(ob_meta_set(p, "format", "9", &e) == OB_ERR_INVALID);
    ob_project_close(p);
    /* it can't be made twice */
    CHECK(ob_project_create(path, "Shop", NULL, &p, &e) == OB_ERR_EXISTS && !p);
    OK(ob_project_open(path, 0, NULL, &p, E));
    OK(ob_meta_get(p, "title", title, sizeof title, E));
    CHECK_STR(title, "Shop");
    OK(ob_meta_get(p, "start_form", title, sizeof title, E));
    CHECK_STR(title, "Orders");
    OK(ob_meta_get(p, "nothing", title, sizeof title, E));
    CHECK_STR(title, "");
    OK(ob_project_check(p, 1, E));
    ob_project_close(p);
    /* the header names it */
    data = (unsigned char *)obt_read_file(path, &size);
    CHECK(data && ob_header_check(data, (size_t)size, size, &e) == OB_OK);
    CHECK(data && ob_header_check(data, 100, -1, &e) == OB_OK);
    free(data);
}

static void not_projects(void)
{
    ob_project *p = NULL;
    ob_error e;
    sqlite3 *db;
    const char *plain = obt_path("plain.db"), *text = obt_path("notes.txt"), *newer = obt_path("newer.oadb");
    /* a plain SQLite database */
    sqlite3_open(plain, &db);
    sqlite3_exec(db, "CREATE TABLE t(x)", NULL, NULL, NULL);
    sqlite3_close(db);
    CHECK(ob_project_open(plain, 0, NULL, &p, &e) == OB_ERR_NOT_PROJECT && !p);
    {
        static const char hello[] = "Hello, this is not a database at all, but it is long enough to have a "
                                    "header-sized start, and some more.";
        obt_write_file(text, hello, (long)strlen(hello));
    }
    CHECK(ob_project_open(text, 0, NULL, &p, &e) == OB_ERR_NOT_PROJECT && !p);
    CHECK(ob_project_open(obt_path("missing.oadb"), 0, NULL, &p, &e) == OB_ERR_IO && !p);
    obt_write_file(text, "", 0);
    CHECK(ob_project_open(text, 0, NULL, &p, &e) == OB_ERR_NOT_PROJECT && !p);
    obt_write_file(text, "SQLite fo", 9);
    CHECK(ob_project_open(text, 0, NULL, &p, &e) == OB_ERR_DAMAGED && !p);
    /* made by a newer OpenBase */
    OK(ob_project_create(newer, "", NULL, &p, E));
    ob_exec(p, "PRAGMA user_version=2", &e);
    ob_project_close(p);
    p = NULL;
    CHECK(ob_project_open(newer, 0, NULL, &p, &e) == OB_ERR_NEWER && !p);
}

static void readonly_and_copies(void)
{
    ob_project *p = NULL, *c = NULL;
    ob_error e;
    ob_table *t;
    const char *path = obt_path("ro.oadb"), *copy = obt_path("ro-copy.oadb");
    OK(ob_project_create(path, "RO", NULL, &p, E));
    t = ob_table_new("People", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    OK(ob_table_create(p, t, E));
    OK(ob_exec(p, "INSERT INTO People(Name) VALUES ('Ann'), ('Bob')", E));
    OK(ob_project_save_copy(p, copy, E));
    CHECK(ob_project_save_copy(p, copy, &e) == OB_ERR_EXISTS);
    OK(ob_project_compact(p, E));
    ob_project_close(p);
    OK(ob_project_open(copy, 0, NULL, &c, E));
    CHECK(c && obt_long(c, "SELECT count(*) FROM People") == 2);
    ob_project_close(c);
    OK(ob_project_open(path, OB_OPEN_READONLY, NULL, &p, E));
    CHECK(p && ob_project_readonly(p));
    CHECK(ob_exec(p, "INSERT INTO People(Name) VALUES ('Cy')", &e) != OB_OK);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_READONLY);
    ob_project_close(p);
    ob_table_free(t);
}

static void formats_and_config(void)
{
    ob_project *p = NULL;
    ob_formats f;
    ob_config cfg;
    ob_error e;
    const char *path = obt_path("formats.oadb");
    ob_config_defaults(&cfg);
    CHECK(cfg.vfs == NULL && cfg.journal_mode == NULL && cfg.max_object_kib == 4096);
    CHECK(ob_config_cache_for(8UL << 20) == 256);
    CHECK(ob_config_cache_for(16UL << 20) == 1024);
    CHECK(ob_config_cache_for(32UL << 20) == 2048);
    cfg.journal_mode = "TRUNCATE";
    cfg.cache_kib = 256;
    cfg.temp_dir = obt_dir;
    OK(ob_project_create(path, "", &cfg, &p, E));
    ob_formats_defaults(&f);
    f.date_order = OB_DATE_DMY;
    f.decimal = ',';
    f.thousands = '.';
    OK(ob_project_set_formats(p, &f, E));
    f.thousands = ',';
    CHECK(ob_project_set_formats(p, &f, &e) == OB_ERR_INVALID);
    ob_project_close(p);
    OK(ob_project_open(path, 0, &cfg, &p, E));
    CHECK(p && obt_long(p, "PRAGMA cache_size") == -256);
    CHECK(sqlite3_temp_directory && !strcmp(sqlite3_temp_directory, obt_dir));
    /* a sort too big for the cache spills to the temporary folder */
    CHECK(p && obt_long(p, "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 20000)"
                           " SELECT count(*) FROM (SELECT DISTINCT randomblob(64) FROM n)") == 20000);
    CHECK(p && ob_project_formats(p)->date_order == OB_DATE_DMY && ob_project_formats(p)->decimal == ',' &&
          ob_project_formats(p)->thousands == '.');
    ob_project_close(p);
    cfg.journal_mode = "DELETE; DROP TABLE x";
    CHECK(ob_project_open(path, 0, &cfg, &p, &e) == OB_ERR_INVALID && !p);
}

void suite_project(void)
{
    create_and_open();
    not_projects();
    readonly_and_copies();
    formats_and_config();
}
