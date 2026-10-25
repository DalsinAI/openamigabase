/* OpenBase core: .oadb projects (Design-OpenBase.md, 3).
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "ob_internal.h"

#include <stdio.h>
#include <string.h>

/* ---- The config point ------------------------------------------------------ */

void ob_config_defaults(ob_config *c)
{
    memset(c, 0, sizeof *c);
    c->vfs = NULL;          /* B0 */
    c->journal_mode = NULL; /* B0 */
    c->max_object_kib = 4096;
}

long ob_config_cache_for(unsigned long free_bytes)
{
    unsigned long mb = free_bytes / (1024UL * 1024UL);
    if (mb <= 8) return 256;
    if (mb <= 16) return 1024;
    return 2048;
}

void ob_formats_defaults(ob_formats *f)
{
    memset(f, 0, sizeof *f);
    f->date_order = OB_DATE_YMD;
    f->date_sep = '/';
    f->year_pivot = 50;
    f->decimal = '.';
    f->thousands = ',';
    strcpy(f->yes, "Yes");
    strcpy(f->no, "No");
}

/* ---- The header ------------------------------------------------------------ */

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

int ob_header_check(const unsigned char *h, size_t n, long long size, ob_error *err)
{
    static const char magic[16] = "SQLite format 3";
    unsigned long page, pages;
    if (n < 16 || memcmp(h, magic, 16) != 0) {
        if (n > 0 && n < 16 && memcmp(h, magic, n) == 0)
            return ob_fail(err, OB_ERR_DAMAGED, "The project file is cut short.");
        return ob_fail(err, OB_ERR_NOT_PROJECT, "This isn't an OpenBase project.");
    }
    if (n < 100) return ob_fail(err, OB_ERR_DAMAGED, "The project file is cut short.");
    page = ((unsigned long)h[16] << 8) | h[17];
    if (page == 1) page = 65536;
    if (page < 512 || page > 65536 || (page & (page - 1)))
        return ob_fail(err, OB_ERR_DAMAGED, "The project file is damaged (its page size).");
    if (be32(h + 68) != (unsigned long)OB_APPLICATION_ID)
        return ob_fail(err, OB_ERR_NOT_PROJECT,
                       "This is an SQLite database, but not an OpenBase project.");
    if (be32(h + 60) > OB_FORMAT_VERSION)
        return ob_fail(err, OB_ERR_NEWER, "This project was made by a newer OpenBase.");
    if (size >= 0) {
        if (size < (long long)page || size % (long long)page)
            return ob_fail(err, OB_ERR_DAMAGED, "The project file is cut short.");
        /* the size in the header counts when it was written with the
         * change counter it carries */
        pages = be32(h + 28);
        if (pages && be32(h + 92) == be32(h + 24) && (long long)pages * (long long)page > size)
            return ob_fail(err, OB_ERR_DAMAGED, "The project file is cut short.");
    }
    return OB_OK;
}

/* ---- The design tables ---------------------------------------------------- */

static const char design_sql[] =
    "CREATE TABLE oadb_meta (key TEXT PRIMARY KEY NOT NULL COLLATE NOCASE, value TEXT);"
    "CREATE TABLE oadb_fields ("
    " tbl TEXT NOT NULL COLLATE NOCASE, field TEXT NOT NULL COLLATE NOCASE,"
    " position INTEGER NOT NULL DEFAULT 0, type TEXT NOT NULL,"
    " label TEXT, format TEXT, choices TEXT, maxlen INTEGER NOT NULL DEFAULT 0,"
    " rule TEXT, rule_text TEXT, lookup_table TEXT, lookup_show TEXT, companion_of TEXT,"
    " PRIMARY KEY (tbl, field));"
    "CREATE TABLE oadb_queries (name TEXT PRIMARY KEY NOT NULL COLLATE NOCASE, grid TEXT,"
    " sql TEXT NOT NULL, view INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE oadb_forms (name TEXT PRIMARY KEY NOT NULL COLLATE NOCASE, layout TEXT NOT NULL);"
    "CREATE TABLE oadb_reports (name TEXT PRIMARY KEY NOT NULL COLLATE NOCASE, layout TEXT NOT NULL);"
    "CREATE TABLE oadb_menus (position INTEGER PRIMARY KEY, label TEXT NOT NULL,"
    " action TEXT NOT NULL, target TEXT);"
    "CREATE TABLE oadb_scripts (name TEXT PRIMARY KEY NOT NULL COLLATE NOCASE, body TEXT NOT NULL);";

static const char *const design_tables[] = {
    "oadb_meta", "oadb_fields", "oadb_queries", "oadb_forms", "oadb_reports", "oadb_menus", "oadb_scripts"
};

int ob_schema_init(ob_project *p, ob_error *err)
{
    int rc = sqlite3_exec(p->db, design_sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) return ob_sqlite_fail(p, rc, err, "making the project");
    return OB_OK;
}

static int design_present(ob_project *p, ob_error *err)
{
    size_t i;
    for (i = 0; i < sizeof design_tables / sizeof design_tables[0]; i++) {
        sqlite3_stmt *st;
        int rc = ob_prepare(p, "SELECT 1 FROM sqlite_schema WHERE type='table' AND name=?", &st, err);
        if (rc) return rc;
        sqlite3_bind_text(st, 1, design_tables[i], -1, SQLITE_STATIC);
        rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc == SQLITE_DONE)
            return ob_fail(err, OB_ERR_DAMAGED, "The project is missing its design table %s.",
                           design_tables[i]);
        if (rc != SQLITE_ROW) return ob_sqlite_fail(p, rc, err, "reading the project");
    }
    return OB_OK;
}

/* ---- The connection ---------------------------------------------------------- */

static int pragma(ob_project *p, const char *sql, ob_error *err)
{
    int rc = sqlite3_exec(p->db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) return ob_sqlite_fail(p, rc, err, "setting up the project");
    return OB_OK;
}

static int setup(ob_project *p, ob_error *err)
{
    char sql[96];
    int rc;
    sqlite3_extended_result_codes(p->db, 1);
#ifdef SQLITE_DBCONFIG_DEFENSIVE
    sqlite3_db_config(p->db, SQLITE_DBCONFIG_DEFENSIVE, 1, (int *)NULL);
#endif
#ifdef SQLITE_DBCONFIG_TRUSTED_SCHEMA
    sqlite3_db_config(p->db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, (int *)NULL);
#endif
#ifdef SQLITE_DBCONFIG_DQS_DML
    sqlite3_db_config(p->db, SQLITE_DBCONFIG_DQS_DML, 0, (int *)NULL);
#endif
    if (p->cfg.busy_ms > 0) sqlite3_busy_timeout(p->db, p->cfg.busy_ms);
    if (p->cfg.temp_dir && *p->cfg.temp_dir &&
        (!sqlite3_temp_directory || strcmp(sqlite3_temp_directory, p->cfg.temp_dir) != 0)) {
        /* one task, one connection at a time (11.2): setting it here is safe */
        char *dir = sqlite3_mprintf("%s", p->cfg.temp_dir);
        if (!dir) return ob_nomem(err);
        sqlite3_free(sqlite3_temp_directory);
        sqlite3_temp_directory = dir;
    }
    if (p->cfg.heap_limit_kib > 0) sqlite3_soft_heap_limit64((sqlite3_int64)p->cfg.heap_limit_kib * 1024);
    if ((rc = pragma(p, "PRAGMA foreign_keys=ON", err))) return rc;
    if ((rc = pragma(p, "PRAGMA cell_size_check=ON", err))) return rc;
    if (p->cfg.cache_kib > 0) {
        snprintf(sql, sizeof sql, "PRAGMA cache_size=-%ld", p->cfg.cache_kib);
        if ((rc = pragma(p, sql, err))) return rc;
    }
    if (p->cfg.journal_mode && !p->readonly) {
        const char *j = p->cfg.journal_mode;
        if (strspn(j, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != strlen(j) || strlen(j) > 16)
            return ob_fail(err, OB_ERR_INVALID, "The journal mode isn't one SQLite knows.");
        snprintf(sql, sizeof sql, "PRAGMA journal_mode=%s", j);
        if ((rc = pragma(p, sql, err))) return rc;
    }
    if (p->readonly && (rc = pragma(p, "PRAGMA query_only=1", err))) return rc;
    return ob_types_register(p, err);
}

static ob_project *project_new(const ob_config *cfg)
{
    ob_project *p = (ob_project *)ob_zalloc(sizeof *p);
    if (!p) return NULL;
    if (cfg) p->cfg = *cfg;
    else ob_config_defaults(&p->cfg);
    ob_formats_defaults(&p->formats);
    return p;
}

void ob_project_close(ob_project *p)
{
    if (!p) return;
    if (p->db) sqlite3_close(p->db);
    ob_free(p);
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int ob_project_create(const char *path, const char *title, const ob_config *cfg,
                      ob_project **out, ob_error *err)
{
    ob_project *p;
    int rc, memory;
    char sql[96];
    *out = NULL;
    if (!path || !*path) return ob_fail(err, OB_ERR_INVALID, "The project needs a file name.");
    memory = strcmp(path, ":memory:") == 0;
    if (!memory && file_exists(path))
        return ob_fail(err, OB_ERR_EXISTS, "There is already a file called %s.", path);
    p = project_new(cfg);
    if (!p) return ob_nomem(err);
    p->in_memory = memory;
    rc = sqlite3_open_v2(path, &p->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, p->cfg.vfs);
    if (rc != SQLITE_OK) {
        rc = ob_sqlite_fail(p, rc, err, "making the project");
        goto fail;
    }
    if (p->cfg.page_size > 0) {
        snprintf(sql, sizeof sql, "PRAGMA page_size=%d", p->cfg.page_size);
        if ((rc = pragma(p, sql, err))) goto fail;
    }
    snprintf(sql, sizeof sql, "PRAGMA application_id=%ld; PRAGMA user_version=%d",
             (long)OB_APPLICATION_ID, OB_FORMAT_VERSION);
    if ((rc = pragma(p, sql, err))) goto fail;
    if ((rc = setup(p, err))) goto fail;
    if ((rc = ob_begin(p, err))) goto fail;
    if ((rc = ob_schema_init(p, err))) goto fail_tx;
    {
        sqlite3_stmt *st;
        if ((rc = ob_prepare(p, "INSERT INTO oadb_meta(key, value) VALUES ('format', ?1),"
                                " ('title', ?2), ('made_by', ?3)", &st, err)))
            goto fail_tx;
        sqlite3_bind_int(st, 1, OB_FORMAT_VERSION);
        sqlite3_bind_text(st, 2, title ? title : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, OB_VERSION_TEXT, -1, SQLITE_STATIC);
        rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) {
            rc = ob_sqlite_fail(p, rc, err, "making the project");
            goto fail_tx;
        }
    }
    if ((rc = ob_commit(p, err))) goto fail;
    *out = p;
    return OB_OK;
fail_tx:
    ob_rollback(p);
fail:
    ob_project_close(p);
    if (!memory) remove(path); /* the file this call made */
    return rc;
}

/* ---- The project's formats ---------------------------------------------------- */

static int load_formats(ob_project *p, ob_error *err)
{
    char v[16];
    int rc;
    if ((rc = ob_meta_get(p, "date_order", v, sizeof v, err))) return rc;
    if (!ob_casecmp(v, "DMY")) p->formats.date_order = OB_DATE_DMY;
    else if (!ob_casecmp(v, "MDY")) p->formats.date_order = OB_DATE_MDY;
    else p->formats.date_order = OB_DATE_YMD;
    if ((rc = ob_meta_get(p, "decimal", v, sizeof v, err))) return rc;
    if (v[0] == ',' && !v[1]) p->formats.decimal = ',';
    if ((rc = ob_meta_get(p, "thousands", v, sizeof v, err))) return rc;
    if (!strcmp(v, "none")) p->formats.thousands = 0;
    else if (v[0] && !v[1] && strchr(",. '", v[0])) p->formats.thousands = v[0];
    if (p->formats.thousands == p->formats.decimal) p->formats.thousands = 0;
    return OB_OK;
}

const ob_formats *ob_project_formats(ob_project *p) { return &p->formats; }

int ob_project_set_formats(ob_project *p, const ob_formats *f, ob_error *err)
{
    static const char *const orders[] = { "YMD", "DMY", "MDY", "YMD" };
    char dec[2], th[8];
    int rc;
    if (!f || (f->decimal != '.' && f->decimal != ',') || (unsigned)f->date_order > OB_DATE_AUTO)
        return ob_fail(err, OB_ERR_INVALID, "Those formats can't be used.");
    if (f->thousands && (!strchr(",. '", f->thousands) || f->thousands == f->decimal))
        return ob_fail(err, OB_ERR_INVALID, "The thousands separator can't be used.");
    dec[0] = f->decimal;
    dec[1] = 0;
    if (f->thousands) th[0] = f->thousands, th[1] = 0;
    else strcpy(th, "none");
    if ((rc = ob_meta_set(p, "date_order", orders[f->date_order], err))) return rc;
    if ((rc = ob_meta_set(p, "decimal", dec, err))) return rc;
    if ((rc = ob_meta_set(p, "thousands", th, err))) return rc;
    p->formats = *f;
    if (p->formats.date_order == OB_DATE_AUTO) p->formats.date_order = OB_DATE_YMD;
    return OB_OK;
}

int ob_project_open(const char *path, unsigned flags, const ob_config *cfg, ob_project **out,
                    ob_error *err)
{
    ob_project *p;
    int rc;
    long long fmt = 0;
    *out = NULL;
    if (!path || !*path) return ob_fail(err, OB_ERR_INVALID, "Which project?");
    if (strcmp(path, ":memory:") != 0) {
        unsigned char head[100];
        size_t n;
        long long size = -1;
        char jpath[512];
        FILE *f = fopen(path, "rb");
        if (!f) return ob_fail(err, OB_ERR_IO, "The file %s can't be opened.", path);
        n = fread(head, 1, sizeof head, f);
        if (fseek(f, 0, SEEK_END) == 0) size = (long long)ftell(f);
        fclose(f);
        /* a hot journal puts the file right; let SQLite do that first */
        snprintf(jpath, sizeof jpath, "%s-journal", path);
        if (file_exists(jpath)) size = -1;
        if ((rc = ob_header_check(head, n, size, err))) return rc;
    }
    p = project_new(cfg);
    if (!p) return ob_nomem(err);
    p->readonly = (flags & OB_OPEN_READONLY) != 0;
    rc = sqlite3_open_v2(path, &p->db, p->readonly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE,
                         p->cfg.vfs);
    if (rc != SQLITE_OK) {
        rc = ob_sqlite_fail(p, rc, err, "opening the project");
        goto fail;
    }
    if ((rc = setup(p, err))) goto fail;
    {
        long long id = 0, ver = 0;
        if ((rc = ob_sql_long(p, "PRAGMA application_id", &id, err))) goto fail;
        if ((rc = ob_sql_long(p, "PRAGMA user_version", &ver, err))) goto fail;
        if (id != OB_APPLICATION_ID) {
            rc = ob_fail(err, OB_ERR_NOT_PROJECT, "This isn't an OpenBase project.");
            goto fail;
        }
        if (ver > OB_FORMAT_VERSION) {
            rc = ob_fail(err, OB_ERR_NEWER, "This project was made by a newer OpenBase.");
            goto fail;
        }
    }
    if (!(flags & OB_OPEN_NO_CHECK) && (rc = ob_project_check(p, 0, err))) goto fail;
    if ((rc = design_present(p, err))) goto fail;
    if ((rc = ob_sql_long(p, "SELECT CAST(value AS INTEGER) FROM oadb_meta WHERE key='format'", &fmt, err)))
        goto fail;
    if (fmt < 1) {
        rc = ob_fail(err, OB_ERR_DAMAGED, "The project's format version is missing.");
        goto fail;
    }
    if (fmt > OB_FORMAT_VERSION) {
        rc = ob_fail(err, OB_ERR_NEWER, "This project was made by a newer OpenBase.");
        goto fail;
    }
    if ((rc = load_formats(p, err))) goto fail;
    *out = p;
    return OB_OK;
fail:
    ob_project_close(p);
    return rc;
}

int ob_project_check(ob_project *p, int full, ob_error *err)
{
    sqlite3_stmt *st;
    int rc = ob_prepare(p, full ? "PRAGMA integrity_check(20)" : "PRAGMA quick_check(20)", &st, err);
    if (rc) return rc;
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        const char *r = (const char *)sqlite3_column_text(st, 0);
        if (r && strcmp(r, "ok") == 0) rc = OB_OK;
        else rc = ob_fail(err, OB_ERR_DAMAGED, "The project file is damaged (%s).", r ? r : "?");
    } else
        rc = ob_sqlite_fail(p, rc, err, "checking the project");
    sqlite3_finalize(st);
    return rc;
}

int ob_project_save_copy(ob_project *p, const char *path, ob_error *err)
{
    sqlite3_stmt *st;
    int rc;
    if (!path || !*path) return ob_fail(err, OB_ERR_INVALID, "The copy needs a file name.");
    if (file_exists(path)) return ob_fail(err, OB_ERR_EXISTS, "There is already a file called %s.", path);
    if ((rc = ob_prepare(p, "VACUUM INTO ?", &st, err))) return rc;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ob_sqlite_fail(p, rc, err, "saving a copy");
    return OB_OK;
}

int ob_project_compact(ob_project *p, ob_error *err)
{
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    return ob_exec(p, "VACUUM", err);
}

int ob_meta_get(ob_project *p, const char *key, char *buf, size_t size, ob_error *err)
{
    sqlite3_stmt *st;
    int rc;
    if (!size) return ob_fail(err, OB_ERR_INVALID, "No room for the value.");
    buf[0] = 0;
    if ((rc = ob_prepare(p, "SELECT value FROM oadb_meta WHERE key=?", &st, err))) return rc;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        const char *v = (const char *)sqlite3_column_text(st, 0);
        if (v) {
            size_t n = ob_utf8_cut(v, size - 1);
            memcpy(buf, v, n);
            buf[n] = 0;
        }
        rc = OB_OK;
    } else if (rc == SQLITE_DONE)
        rc = OB_OK;
    else
        rc = ob_sqlite_fail(p, rc, err, "reading the project");
    sqlite3_finalize(st);
    return rc;
}

int ob_meta_set(ob_project *p, const char *key, const char *value, ob_error *err)
{
    sqlite3_stmt *st;
    int rc;
    if (!key || !*key) return ob_fail(err, OB_ERR_INVALID, "The setting needs a name.");
    if (ob_casecmp(key, "format") == 0)
        return ob_fail(err, OB_ERR_INVALID, "The format version is OpenBase's own.");
    if ((rc = ob_prepare(p, "INSERT OR REPLACE INTO oadb_meta(key, value) VALUES (?, ?)", &st, err)))
        return rc;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, value, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ob_sqlite_fail(p, rc, err, "saving the setting");
    return OB_OK;
}

struct sqlite3 *ob_project_db(ob_project *p) { return p ? p->db : NULL; }
const ob_config *ob_project_config(ob_project *p) { return &p->cfg; }
int ob_project_readonly(ob_project *p) { return p->readonly; }
