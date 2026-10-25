/* OpenBase core: SQL quoting, statements, and SQLite's errors in plain words.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "ob_internal.h"

#include <stdio.h>
#include <string.h>

/* SQLite's keywords (sqlite.org/lang_keywords.html). Kept here rather than
 * asked of the library, so the SQL shown is the same on every build. */
static const char *const keywords[] = {
    "ABORT", "ACTION", "ADD", "AFTER", "ALL", "ALTER", "ALWAYS", "ANALYZE", "AND", "AS",
    "ASC", "ATTACH", "AUTOINCREMENT", "BEFORE", "BEGIN", "BETWEEN", "BY", "CASCADE",
    "CASE", "CAST", "CHECK", "COLLATE", "COLUMN", "COMMIT", "CONFLICT", "CONSTRAINT",
    "CREATE", "CROSS", "CURRENT", "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP",
    "DATABASE", "DEFAULT", "DEFERRABLE", "DEFERRED", "DELETE", "DESC", "DETACH",
    "DISTINCT", "DO", "DROP", "EACH", "ELSE", "END", "ESCAPE", "EXCEPT", "EXCLUDE",
    "EXCLUSIVE", "EXISTS", "EXPLAIN", "FAIL", "FILTER", "FIRST", "FOLLOWING", "FOR",
    "FOREIGN", "FROM", "FULL", "GENERATED", "GLOB", "GROUP", "GROUPS", "HAVING", "IF",
    "IGNORE", "IMMEDIATE", "IN", "INDEX", "INDEXED", "INITIALLY", "INNER", "INSERT",
    "INSTEAD", "INTERSECT", "INTO", "IS", "ISNULL", "JOIN", "KEY", "LAST", "LEFT", "LIKE",
    "LIMIT", "MATCH", "MATERIALIZED", "NATURAL", "NO", "NOT", "NOTHING", "NOTNULL",
    "NULL", "NULLS", "OF", "OFFSET", "ON", "OR", "ORDER", "OTHERS", "OUTER", "OVER",
    "PARTITION", "PLAN", "PRAGMA", "PRECEDING", "PRIMARY", "QUERY", "RAISE", "RANGE",
    "RECURSIVE", "REFERENCES", "REGEXP", "REINDEX", "RELEASE", "RENAME", "REPLACE",
    "RESTRICT", "RETURNING", "RIGHT", "ROLLBACK", "ROW", "ROWID", "ROWS", "SAVEPOINT",
    "SELECT", "SET", "TABLE", "TEMP", "TEMPORARY", "THEN", "TIES", "TO", "TRANSACTION",
    "TRIGGER", "UNBOUNDED", "UNION", "UNIQUE", "UPDATE", "USING", "VACUUM", "VALUES",
    "VIEW", "VIRTUAL", "WHEN", "WHERE", "WINDOW", "WITH", "WITHOUT"
};

int ob_sql_keyword(const char *w, size_t n)
{
    size_t i;
    for (i = 0; i < sizeof keywords / sizeof keywords[0]; i++)
        if (strlen(keywords[i]) == n && ob_ncasecmp(keywords[i], w, n) == 0) return 1;
    return 0;
}

static int plain_word(const char *s)
{
    const char *p = s;
    if (!*p || !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_')) return 0;
    for (; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_'))
            return 0;
    return !ob_sql_keyword(s, strlen(s));
}

int ob_sql_ident(ob_buf *b, const char *name)
{
    const char *p;
    int rc;
    if (plain_word(name)) return ob_buf_str(b, name);
    rc = ob_buf_char(b, '"');
    for (p = name; *p && !rc; p++) rc = *p == '"' ? ob_buf_add(b, "\"\"", 2) : ob_buf_char(b, *p);
    return rc ? rc : ob_buf_char(b, '"');
}

/* Always quoted: for names written into the design's own statements, so
 * a later keyword can never break them. */
static int ident_quoted(ob_buf *b, const char *name)
{
    const char *p;
    int rc = ob_buf_char(b, '"');
    for (p = name; *p && !rc; p++) rc = *p == '"' ? ob_buf_add(b, "\"\"", 2) : ob_buf_char(b, *p);
    return rc ? rc : ob_buf_char(b, '"');
}

int ob_sql_text(ob_buf *b, const char *text)
{
    const char *p;
    int rc = ob_buf_char(b, '\'');
    for (p = text; *p && !rc; p++) rc = *p == '\'' ? ob_buf_add(b, "''", 2) : ob_buf_char(b, *p);
    return rc ? rc : ob_buf_char(b, '\'');
}

int ob_sql_ident_quoted(ob_buf *b, const char *name) { return ident_quoted(b, name); }

/* ---- Statements ---------------------------------------------------------- */

int ob_prepare(ob_project *p, const char *sql, sqlite3_stmt **st, ob_error *err)
{
    int rc = sqlite3_prepare_v2(p->db, sql, -1, st, NULL);
    if (rc != SQLITE_OK) {
        *st = NULL;
        return ob_sqlite_fail(p, rc, err, "reading the SQL");
    }
    return OB_OK;
}

int ob_exec(ob_project *p, const char *sql, ob_error *err)
{
    int rc;
    if (!p || !sql) return ob_fail(err, OB_ERR_INVALID, "Nothing to run.");
    rc = sqlite3_exec(p->db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) return ob_sqlite_fail(p, rc, err, "running the SQL");
    return OB_OK;
}

int ob_exec_quiet(ob_project *p, const char *sql) { return sqlite3_exec(p->db, sql, NULL, NULL, NULL); }

int ob_sql_long(ob_project *p, const char *sql, long long *out, ob_error *err)
{
    sqlite3_stmt *st;
    int rc = ob_prepare(p, sql, &st, err);
    if (rc) return rc;
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *out = sqlite3_column_int64(st, 0);
        rc = OB_OK;
    } else if (rc == SQLITE_DONE) {
        *out = 0;
        rc = OB_OK;
    } else
        rc = ob_sqlite_fail(p, rc, err, "running the SQL");
    sqlite3_finalize(st);
    return rc;
}

int ob_begin(ob_project *p, ob_error *err)
{
    int rc = sqlite3_exec(p->db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
    if (rc != SQLITE_OK) return ob_sqlite_fail(p, rc, err, "starting a change");
    return OB_OK;
}

int ob_commit(ob_project *p, ob_error *err)
{
    int rc = sqlite3_exec(p->db, "COMMIT", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        rc = ob_sqlite_fail(p, rc, err, "saving the change");
        ob_rollback(p);
        return rc;
    }
    return OB_OK;
}

void ob_rollback(ob_project *p)
{
    if (!sqlite3_get_autocommit(p->db)) sqlite3_exec(p->db, "ROLLBACK", NULL, NULL, NULL);
}

/* ---- SQLite's errors in plain words -------------------------------------- */

/* "T.F" -> "F" */
static const char *field_part(const char *s, size_t n, size_t *len)
{
    size_t i;
    for (i = n; i > 0; i--)
        if (s[i - 1] == '.') break;
    *len = n - i;
    return s + i;
}

static const struct { const char *kind, *words; } check_words[] = {
    { "integer", "%.*s must be a whole number." },
    { "number", "%.*s must be a number." },
    { "currency", "%.*s must be an amount of money." },
    { "date", "%.*s must be a date." },
    { "time", "%.*s must be a time." },
    { "datetime", "%.*s must be a date and time." },
    { "yesno", "%.*s must be Yes or No." },
    { "choice", "%.*s must be one of its choices." },
    { "maxlen", "%.*s is longer than its most characters." },
    { "object", "%.*s must be a Datatype object." },
    { "rule", "%.*s doesn't meet its rule." }
};

int ob_constraint_words(ob_project *p, const char *msg, char *out, size_t size)
{
    static const char ck[] = "CHECK constraint failed: ";
    static const char nn[] = "NOT NULL constraint failed: ";
    static const char uq[] = "UNIQUE constraint failed: ";
    OB_UNUSED(p);
    if (!msg) return 0;
    if (strncmp(msg, ck, sizeof ck - 1) == 0) {
        const char *name = msg + sizeof ck - 1;
        const char *colon = strrchr(name, ':');
        size_t i;
        if (colon) {
            for (i = 0; i < sizeof check_words / sizeof check_words[0]; i++)
                if (strcmp(colon + 1, check_words[i].kind) == 0) {
                    snprintf(out, size, check_words[i].words, (int)(colon - name), name);
                    return 1;
                }
        }
        /* a rule named by its own words */
        snprintf(out, size, "%s", name);
        if (size && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
        return 1;
    }
    if (strncmp(msg, nn, sizeof nn - 1) == 0) {
        const char *s = msg + sizeof nn - 1;
        size_t len;
        const char *f = field_part(s, strlen(s), &len);
        snprintf(out, size, "%.*s must be filled in.", (int)len, f);
        return 1;
    }
    if (strncmp(msg, uq, sizeof uq - 1) == 0) {
        /* "T.a, T.b" */
        const char *s = msg + sizeof uq - 1;
        char names[160];
        size_t used = 0;
        names[0] = 0;
        while (*s) {
            const char *e = strchr(s, ',');
            size_t n = e ? (size_t)(e - s) : strlen(s), len;
            const char *f = field_part(s, n, &len);
            if (used + len + 6 < sizeof names) {
                if (used) {
                    memcpy(names + used, " and ", 5);
                    used += 5;
                }
                memcpy(names + used, f, len);
                used += len;
                names[used] = 0;
            }
            if (!e) break;
            s = e + 1;
            while (*s == ' ') s++;
        }
        snprintf(out, size, "Another record already has this %s.", names);
        return 1;
    }
    if (strcmp(msg, "FOREIGN KEY constraint failed") == 0) {
        snprintf(out, size, "That would break a relationship: a record must point to one that "
                            "exists, and a record others point to can't go.");
        return 1;
    }
    return 0;
}

int ob_sqlite_fail(ob_project *p, int rc, ob_error *err, const char *what)
{
    const char *msg = p && p->db ? sqlite3_errmsg(p->db) : sqlite3_errstr(rc);
    char words[200];
    switch (rc & 0xFF) {
    case SQLITE_CONSTRAINT:
        if (ob_constraint_words(p, msg, words, sizeof words)) return ob_fail(err, OB_ERR_RULE, "%s", words);
        return ob_fail(err, OB_ERR_RULE, "A rule refused it (%s).", msg);
    case SQLITE_MISMATCH:
        return ob_fail(err, OB_ERR_RULE, "The key must be a whole number.");
    case SQLITE_NOMEM:
        return ob_nomem(err);
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB:
        return ob_fail(err, OB_ERR_DAMAGED, "The project file is damaged (%s).", msg);
    case SQLITE_FULL:
        return ob_fail(err, OB_ERR_IO, "The disk is full.");
    case SQLITE_IOERR:
        return ob_fail(err, OB_ERR_IO, "A disk error stopped %s (%s).", what, msg);
    case SQLITE_CANTOPEN:
        return ob_fail(err, OB_ERR_IO, "The file can't be opened.");
    case SQLITE_READONLY:
        return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return ob_fail(err, OB_ERR_BUSY, "The project is open in another program.");
    case SQLITE_TOOBIG:
        return ob_fail(err, OB_ERR_LIMIT, "A value is too big.");
    default:
        return ob_fail(err, OB_ERR_SQL, "SQLite refused %s: %s", what, msg);
    }
}
