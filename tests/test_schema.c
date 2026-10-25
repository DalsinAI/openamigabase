/* OpenBase host tests: tables, field types, changes that keep every row,
 * and relationships.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "obtest.h"

#include <sqlite3.h>
#include <stdlib.h>

#define ROWS 500

/* The words an SQL statement fails with */
static int exec_words(ob_project *p, const char *sql, const char *words)
{
    ob_error e;
    int rc;
    memset(&e, 0, sizeof e);
    rc = ob_exec(p, sql, &e);
    if (rc == OB_OK || !strstr(e.text, words)) {
        fprintf(stderr, "  %s\n  -> %d \"%s\", wanted \"%s\"\n", sql, rc, e.text, words);
        return 0;
    }
    return 1;
}

static int same_rows(ob_project *p, const char *sql, const char *before)
{
    char *now = obt_rows(p, sql);
    int same = strcmp(now, before) == 0;
    if (!same) fprintf(stderr, "  rows of %s changed\n", sql);
    free(now);
    return same;
}

static int healthy(ob_project *p)
{
    ob_error e;
    return ob_project_check(p, 1, &e) == OB_OK && obt_long(p, "SELECT count(*) FROM pragma_foreign_key_check") == 0;
}

static char *schema_sql(ob_project *p)
{
    return obt_rows(p, "SELECT type, name, sql FROM sqlite_schema ORDER BY name");
}

static ob_table *stock_design(void)
{
    ob_error e;
    ob_table *t = ob_table_new("Stock", 1, &e);
    ob_field *f;
    f = ob_table_add_field(t, "Code", OB_TYPE_TEXT, &e);
    f->maxlen = 12;
    f->required = 1;
    ob_strset(&f->label, "Stock code", &e);
    ob_table_add_field(t, "Notes", OB_TYPE_MEMO, &e);
    f = ob_table_add_field(t, "Qty", OB_TYPE_INTEGER, &e);
    ob_strset(&f->default_sql, "0", &e);
    ob_table_add_field(t, "Weight", OB_TYPE_NUMBER, &e);
    f = ob_table_add_field(t, "Price", OB_TYPE_CURRENCY, &e);
    ob_strset(&f->rule, "Price >= 0", &e);
    ob_strset(&f->rule_text, "Price can't be below 0", &e);
    f = ob_table_add_field(t, "Added", OB_TYPE_DATE, &e);
    ob_strset(&f->default_sql, "(date('now','localtime'))", &e);
    ob_table_add_field(t, "At", OB_TYPE_TIME, &e);
    ob_table_add_field(t, "Updated", OB_TYPE_DATETIME, &e);
    f = ob_table_add_field(t, "Active", OB_TYPE_YESNO, &e);
    ob_strset(&f->default_sql, "1", &e);
    f = ob_table_add_field(t, "Colour", OB_TYPE_CHOICE, &e);
    ob_strset(&f->choices, "Red\nGreen\nBlue\nO'Grady green", &e);
    ob_table_add_field(t, "Photo", OB_TYPE_OBJECT, &e);
    ob_table_add_field(t, "Size", OB_TYPE_TEXT, &e);
    ob_table_add_index(t, "Code", 1, &e);
    return t;
}

static void fill_stock(ob_project *p)
{
    sqlite3_stmt *st;
    int i;
    sqlite3_prepare_v2(ob_project_db(p),
                       "INSERT INTO Stock (Code, Notes, Qty, Weight, Price, Added, At, Updated, Active, Colour, Photo,"
                       " Photo_name, Photo_type, Size) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                       -1, &st, NULL);
    for (i = 1; i <= ROWS; i++) {
        char code[16], notes[64], date[16], at[16], upd[40], size[16];
        snprintf(code, sizeof code, "C%05d", i);
        if (i % 7) snprintf(notes, sizeof notes, "Note \xC3\xA9 %d, \"quoted\"\nline 2", i);
        else notes[0] = 0;
        snprintf(date, sizeof date, "2026-%02d-%02d", 1 + i % 12, 1 + i % 28);
        snprintf(at, sizeof at, "%02d:%02d:00", i % 24, i % 60);
        snprintf(upd, sizeof upd, "%s %s", date, at);
        snprintf(size, sizeof size, "%d", i * 3);
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, code, -1, SQLITE_TRANSIENT);
        if (i % 11) sqlite3_bind_text(st, 2, notes, -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(st, 2);
        sqlite3_bind_int64(st, 3, i == 1 ? 9223372036854775807LL : i - 250);
        if (i % 5) {
            char w[16];
            snprintf(w, sizeof w, "%d.%02d", i, i % 100);
            sqlite3_bind_text(st, 4, w, -1, SQLITE_TRANSIENT); /* REAL affinity */
        } else
            sqlite3_bind_null(st, 4);
        sqlite3_bind_int64(st, 5, (long long)i * 199);
        sqlite3_bind_text(st, 6, date, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 7, at, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 8, upd, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 9, i % 2);
        sqlite3_bind_text(st, 10, i % 3 == 0 ? "Red" : i % 3 == 1 ? "Blue" : "O'Grady green", -1, SQLITE_STATIC);
        if (i % 4 == 0) {
            unsigned char blob[32];
            int k;
            for (k = 0; k < 32; k++) blob[k] = (unsigned char)(i + k);
            sqlite3_bind_blob(st, 11, blob, 32, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 12, "pic.iff", -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 13, "ilbm", -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(st, 11);
            sqlite3_bind_null(st, 12);
            sqlite3_bind_null(st, 13);
        }
        sqlite3_bind_text(st, 14, size, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) != SQLITE_DONE) fprintf(stderr, "fill: %s\n", sqlite3_errmsg(ob_project_db(p)));
    }
    sqlite3_finalize(st);
}

static void types_and_words(ob_project *p)
{
    ob_error e;
    ob_table *t = stock_design(), *back = NULL;
    ob_buf b;
    ob_field *f;
    ob_buf_init(&b, 0);
    OK(ob_table_create_sql(t, NULL, &b, E));
    CHECK(strstr(ob_buf_text(&b), "\"id\" INTEGER PRIMARY KEY") != NULL);
    CHECK(strstr(ob_buf_text(&b), "CONSTRAINT \"Code:maxlen\" CHECK (length(\"Code\") <= 12)") != NULL);
    CHECK(strstr(ob_buf_text(&b), "\"Colour\" IN ('Red', 'Green', 'Blue', 'O''Grady green')") != NULL);
    CHECK(strstr(ob_buf_text(&b), "\"Photo_name\" TEXT") != NULL);
    CHECK(strstr(ob_buf_text(&b), "CONSTRAINT \"Price can't be below 0\" CHECK (Price >= 0)") != NULL);
    ob_buf_free(&b);
    OK(ob_table_create(p, t, E));
    CHECK(ob_table_create(p, t, &e) == OB_ERR_EXISTS);
    /* SQLite's refusals in plain words */
    CHECK(exec_words(p, "INSERT INTO Stock(Code, Qty) VALUES ('A', 'abc')", "Qty must be a whole number."));
    CHECK(exec_words(p, "INSERT INTO Stock(Qty) VALUES (1)", "Code must be filled in."));
    CHECK(exec_words(p, "INSERT INTO Stock(Code) VALUES ('ABCDEFGHIJKLMN')", "Code is longer than its most characters."));
    CHECK(exec_words(p, "INSERT INTO Stock(Code, Colour) VALUES ('A', 'Pink')", "Colour must be one of its choices."));
    CHECK(exec_words(p, "INSERT INTO Stock(Code, Added) VALUES ('A', '2026-02-30')", "Added must be a date."));
    CHECK(exec_words(p, "INSERT INTO Stock(Code, At) VALUES ('A', '9:00')", "At must be a time."));
    CHECK(exec_words(p, "INSERT INTO Stock(Code, Updated) VALUES ('A', '2026-01-01')", "Updated must be a date and time."));
    CHECK(exec_words(p, "INSERT INTO Stock(Code, Active) VALUES ('A', 2)", "Active must be Yes or No."));
    CHECK(exec_words(p, "INSERT INTO Stock(Code, Weight) VALUES ('A', 'heavy')", "Weight must be a number."));
    CHECK(exec_words(p, "INSERT INTO Stock(Code, Price) VALUES ('A', 1.5)", "Price must be an amount of money."));
    CHECK(exec_words(p, "INSERT INTO Stock(Code, Price) VALUES ('A', -1)", "Price can't be below 0"));
    CHECK(exec_words(p, "INSERT INTO Stock(Code, Photo) VALUES ('A', 'text')", "Photo must be a Datatype object."));
    CHECK(exec_words(p, "INSERT INTO Stock(id, Code) VALUES ('x', 'A')", "whole number"));
    OK(ob_exec(p, "INSERT INTO Stock(Code) VALUES ('DUP')", E));
    CHECK(exec_words(p, "INSERT INTO Stock(Code) VALUES ('DUP')", "Another record already has this Code."));
    OK(ob_exec(p, "DELETE FROM Stock", E));
    /* the defaults */
    OK(ob_exec(p, "INSERT INTO Stock(Code) VALUES ('DEF')", E));
    CHECK(obt_long(p, "SELECT Qty = 0 AND Active = 1 AND Added = date('now','localtime') FROM Stock") == 1);
    OK(ob_exec(p, "DELETE FROM Stock", E));
    /* read back */
    OK(ob_table_read(p, "stock", &back, E));
    if (back) {
        CHECK_STR(back->name, "Stock");
        CHECK(back->nfields == t->nfields && !back->adopted && back->nkey == 0);
        f = ob_table_field(back, "Code");
        CHECK(f && f->type == OB_TYPE_TEXT && f->maxlen == 12 && f->required && f->label && !strcmp(f->label, "Stock code"));
        f = ob_table_field(back, "Colour");
        CHECK(f && f->type == OB_TYPE_CHOICE && f->choices && strstr(f->choices, "Green"));
        f = ob_table_field(back, "Price");
        CHECK(f && f->type == OB_TYPE_CURRENCY && f->rule && f->rule_text);
        CHECK(ob_table_field(back, "Photo") && ob_table_field(back, "Photo")->type == OB_TYPE_OBJECT);
        CHECK(ob_table_field(back, "Photo_name") == NULL); /* a companion, not a field */
        CHECK(ob_table_field(back, "id")->type == OB_TYPE_COUNTER);
        CHECK(back->nindexes == 1 && back->indexes[0].unique && !strcmp(back->indexes[0].fields[0], "Code"));
        CHECK(ob_table_field(back, "Qty")->default_sql && !strcmp(ob_table_field(back, "Qty")->default_sql, "0"));
    }
    ob_table_free(back);
    ob_table_free(t);
}

static void bad_designs(ob_project *p)
{
    ob_error e;
    ob_table *t;
    t = ob_table_new("Twice", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    ob_table_add_field(t, "name", OB_TYPE_TEXT, &e);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_EXISTS);
    ob_table_free(t);
    t = ob_table_new("Counters", 1, &e);
    ob_table_add_field(t, "n2", OB_TYPE_COUNTER, &e);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_INVALID);
    ob_table_free(t);
    t = ob_table_new("Keyless", 0, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_INVALID && strstr(e.text, "key"));
    ob_table_free(t);
    t = ob_table_new("oadb_mine", 1, &e);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_INVALID);
    ob_table_free(t);
    t = ob_table_new("Looks", 1, &e);
    ob_table_add_field(t, "who", OB_TYPE_LOOKUP, &e);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_INVALID);
    ob_strset(&ob_table_field(t, "who")->lookup_table, "Nowhere", &e);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_NOT_FOUND);
    ob_table_free(t);
    t = ob_table_new("Pics", 1, &e);
    ob_table_add_field(t, "Photo", OB_TYPE_OBJECT, &e);
    ob_table_add_field(t, "Photo_type", OB_TYPE_TEXT, &e);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_EXISTS);
    ob_table_free(t);
    t = ob_table_new("Bad\tname", 1, &e);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_INVALID);
    ob_table_free(t);
    t = ob_table_new("Two keys", 0, &e);
    ob_table_add_field(t, "A", OB_TYPE_TEXT, &e);
    ob_table_add_field(t, "B", OB_TYPE_INTEGER, &e);
    t->nkey = 2;
    ob_strset(&t->key[0], "A", &e);
    ob_strset(&t->key[1], "C", &e);
    CHECK(ob_table_create(p, t, &e) == OB_ERR_NOT_FOUND);
    ob_strset(&t->key[1], "B", &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
}

/* Change Stock to `t`; it must keep ROWS rows */
static int change(ob_project *p, ob_table *t, unsigned flags, ob_change_result *res)
{
    ob_error e;
    int rc;
    memset(&e, 0, sizeof e);
    rc = ob_table_change(p, "Stock", t, flags, res, &e);
    if (rc) fprintf(stderr, "  change: %s\n", e.text);
    return rc;
}

static void changes_keep_rows(ob_project *p)
{
    ob_error e;
    ob_table *t;
    ob_change_result res;
    char *before, *schema;
    fill_stock(p);
    CHECK(obt_long(p, "SELECT count(*) FROM Stock") == ROWS);
    OK(ob_exec(p, "CREATE VIEW Cheap AS SELECT Code, Price, Qty FROM Stock WHERE Price < 1000", E));
    OK(ob_exec(p, "CREATE TABLE Log (n INTEGER PRIMARY KEY, what TEXT);"
                  "CREATE TRIGGER StockAdded AFTER INSERT ON Stock BEGIN INSERT INTO Log(what) VALUES (new.Code); END", E));

    /* 1. renames only: ALTER TABLE, no copy */
    before = obt_rows(p, "SELECT id, Code, Notes, Qty, Weight FROM Stock ORDER BY id");
    OK(ob_table_read(p, "Stock", &t, E));
    ob_strset(&ob_table_field(t, "Notes")->name, "Remarks", &e);
    ob_strset(&ob_table_field(t, "Qty")->name, "Quantity", &e);
    CHECK(change(p, t, 0, &res) == OB_OK && !res.rebuilt && res.rows == ROWS);
    CHECK(same_rows(p, "SELECT id, Code, Remarks, Quantity, Weight FROM Stock ORDER BY id", before));
    CHECK(obt_long(p, "SELECT count(*) FROM Cheap") > 0); /* the view followed the rename */
    ob_table_free(t);
    free(before);

    /* 2. two fields swap names */
    before = obt_rows(p, "SELECT id, Code, Size FROM Stock ORDER BY id");
    OK(ob_table_read(p, "Stock", &t, E));
    ob_strset(&ob_table_field(t, "Code")->name, "Tmp", &e);
    ob_strset(&ob_table_field(t, "Size")->name, "Code", &e);
    ob_strset(&ob_table_field(t, "Tmp")->name, "Size", &e);
    /* Code was the indexed, required, 12-long one; it is now called Size */
    CHECK(change(p, t, 0, &res) == OB_OK && res.rows == ROWS);
    CHECK(same_rows(p, "SELECT id, Size, Code FROM Stock ORDER BY id", before));
    ob_table_free(t);
    free(before);
    OK(ob_table_read(p, "Stock", &t, E));
    CHECK(t && ob_table_field(t, "Size")->maxlen == 12 && t->nindexes == 1 && !strcmp(t->indexes[0].fields[0], "Size"));
    /* and back */
    ob_strset(&ob_table_field(t, "Code")->name, "Tmp", &e);
    ob_strset(&ob_table_field(t, "Size")->name, "Code", &e);
    ob_strset(&ob_table_field(t, "Tmp")->name, "Size", &e);
    CHECK(change(p, t, 0, &res) == OB_OK);
    ob_table_free(t);

    /* 3. new fields ALTER can add */
    before = obt_rows(p, "SELECT * FROM Stock ORDER BY id");
    OK(ob_table_read(p, "Stock", &t, E));
    ob_table_add_field(t, "Supplier", OB_TYPE_TEXT, &e);
    {
        ob_field *f = ob_table_add_field(t, "Reorder", OB_TYPE_INTEGER, &e);
        f->required = 1;
        ob_strset(&f->default_sql, "5", &e);
    }
    CHECK(change(p, t, 0, &res) == OB_OK && !res.rebuilt && res.rows == ROWS);
    CHECK(obt_long(p, "SELECT count(*) FROM Stock WHERE Supplier IS NULL AND Reorder = 5") == ROWS);
    ob_table_free(t);
    free(before);

    /* 4. a field goes: copied and renamed; views and triggers come back */
    before = obt_rows(p, "SELECT id, Code, Quantity, Weight, Price, Added, At, Updated, Active, Colour, Photo, Photo_name,"
                         " Photo_type, Size, Supplier, Reorder FROM Stock ORDER BY id");
    OK(ob_table_read(p, "Stock", &t, E));
    ob_table_remove_field(t, "Remarks", &e);
    CHECK(change(p, t, 0, &res) == OB_OK && res.rebuilt && res.rows == ROWS);
    CHECK(same_rows(p, "SELECT id, Code, Quantity, Weight, Price, Added, At, Updated, Active, Colour, Photo, Photo_name,"
                       " Photo_type, Size, Supplier, Reorder FROM Stock ORDER BY id", before));
    CHECK(obt_long(p, "SELECT count(*) FROM sqlite_schema WHERE name IN ('Cheap', 'StockAdded', 'Stock_Code')") == 3);
    CHECK(healthy(p));
    ob_table_free(t);
    free(before);

    /* 5. a type changes and back: Integer to Text to Integer */
    before = obt_rows(p, "SELECT id, Quantity FROM Stock ORDER BY id");
    OK(ob_table_read(p, "Stock", &t, E));
    ob_table_field(t, "Quantity")->type = OB_TYPE_TEXT;
    CHECK(change(p, t, 0, &res) == OB_OK && res.rebuilt);
    CHECK(obt_long(p, "SELECT count(*) FROM Stock WHERE typeof(Quantity) = 'text'") == ROWS);
    ob_table_field(t, "Quantity")->type = OB_TYPE_INTEGER;
    CHECK(change(p, t, 0, &res) == OB_OK);
    CHECK(obt_long(p, "SELECT count(*) FROM Stock WHERE typeof(Quantity) = 'integer'") == ROWS);
    CHECK(same_rows(p, "SELECT id, Quantity FROM Stock ORDER BY id", before));
    ob_table_free(t);
    free(before);

    /* 6. Number to Currency: refused while a value has a part of a penny,
     * and nothing changes; then with clearing */
    OK(ob_exec(p, "UPDATE Stock SET Weight = 1.234 WHERE id IN (3, 9)", E));
    before = obt_rows(p, "SELECT * FROM Stock ORDER BY id");
    schema = schema_sql(p);
    OK(ob_table_read(p, "Stock", &t, E));
    ob_table_field(t, "Weight")->type = OB_TYPE_CURRENCY;
    memset(&e, 0, sizeof e);
    CHECK(ob_table_change(p, "Stock", t, 0, &res, &e) == OB_ERR_RULE);
    CHECK(res.bad_values == 2 && res.first_bad_row == 3 && !strcmp(res.first_bad_field, "Weight"));
    CHECK(strstr(e.text, "2 records in Weight") && strstr(e.text, "record 3"));
    CHECK(same_rows(p, "SELECT * FROM Stock ORDER BY id", before));
    CHECK(same_rows(p, "SELECT type, name, sql FROM sqlite_schema ORDER BY name", schema));
    CHECK(obt_long(p, "PRAGMA foreign_keys") == 1);
    CHECK(change(p, t, OB_CHANGE_CLEAR_BAD, &res) == OB_OK && res.cleared == 2 && res.rows == ROWS);
    CHECK(obt_long(p, "SELECT count(*) FROM Stock WHERE id IN (3, 9) AND Weight IS NULL") == 2);
    CHECK(obt_long(p, "SELECT Weight FROM Stock WHERE id = 2") == 202); /* 2.02 is 202 pence */
    ob_table_free(t);
    free(before);
    free(schema);

    /* 7. Text to Date, read in the project's day-first order */
    {
        ob_formats f = *ob_project_formats(p);
        f.date_order = OB_DATE_DMY;
        OK(ob_project_set_formats(p, &f, E));
        OK(ob_exec(p, "UPDATE Stock SET Supplier = substr(Added, 9, 2) || '/' || substr(Added, 6, 2) || '/' || substr(Added, 1, 4)", E));
        OK(ob_table_read(p, "Stock", &t, E));
        ob_table_field(t, "Supplier")->type = OB_TYPE_DATE;
        CHECK(change(p, t, 0, &res) == OB_OK);
        CHECK(obt_long(p, "SELECT count(*) FROM Stock WHERE Supplier = Added") == ROWS);
        ob_table_free(t);
    }

    /* 8. required where some are empty: refused, nothing changes */
    before = obt_rows(p, "SELECT * FROM Stock ORDER BY id");
    schema = schema_sql(p);
    OK(ob_table_read(p, "Stock", &t, E));
    ob_table_field(t, "Weight")->required = 1;
    memset(&e, 0, sizeof e);
    CHECK(ob_table_change(p, "Stock", t, 0, &res, &e) == OB_ERR_RULE && strstr(e.text, "Weight must be filled in"));
    CHECK(strstr(e.text, "the first is record 3") != NULL);
    CHECK(same_rows(p, "SELECT * FROM Stock ORDER BY id", before));
    CHECK(same_rows(p, "SELECT type, name, sql FROM sqlite_schema ORDER BY name", schema));
    /* a maximum length some values pass */
    ob_table_field(t, "Weight")->required = 0;
    ob_table_field(t, "Size")->type = OB_TYPE_TEXT;
    ob_table_field(t, "Size")->maxlen = 3;
    CHECK(ob_table_change(p, "Stock", t, 0, &res, &e) == OB_ERR_RULE && strstr(e.text, "longer"));
    CHECK(same_rows(p, "SELECT * FROM Stock ORDER BY id", before));
    ob_table_free(t);
    free(before);
    free(schema);

    /* 9. a field a view uses can't go */
    before = obt_rows(p, "SELECT * FROM Stock ORDER BY id");
    OK(ob_table_read(p, "Stock", &t, E));
    ob_table_remove_field(t, "Quantity", &e);
    memset(&e, 0, sizeof e);
    CHECK(ob_table_change(p, "Stock", t, 0, &res, &e) == OB_ERR_RULE && strstr(e.text, "Cheap") && strstr(e.text, "Quantity"));
    CHECK(same_rows(p, "SELECT * FROM Stock ORDER BY id", before));
    ob_table_free(t);
    free(before);

    /* 10. the table's name, and a Datatype object's, change */
    before = obt_rows(p, "SELECT id, Photo, Photo_name, Photo_type FROM Stock ORDER BY id");
    OK(ob_table_read(p, "Stock", &t, E));
    ob_strset(&t->name, "Items", &e);
    ob_strset(&ob_table_field(t, "Photo")->name, "Picture", &e);
    memset(&e, 0, sizeof e);
    CHECK(ob_table_change(p, "Stock", t, 0, &res, &e) == OB_OK);
    CHECK(same_rows(p, "SELECT id, Picture, Picture_name, Picture_type FROM Items ORDER BY id", before));
    CHECK(obt_long(p, "SELECT count(*) FROM Cheap") > 0);
    CHECK(obt_long(p, "SELECT count(*) FROM oadb_fields WHERE tbl = 'Stock'") == 0);
    CHECK(obt_long(p, "SELECT count(*) FROM oadb_fields WHERE tbl = 'Items' AND companion_of = 'Picture'") == 2);
    OK(ob_exec(p, "INSERT INTO Items(Code) VALUES ('NEW')", E));
    CHECK(obt_long(p, "SELECT count(*) FROM Log WHERE what = 'NEW'") == 1); /* the trigger came along */
    CHECK(healthy(p));
    ob_table_free(t);
    free(before);
}

static void keys_change(ob_project *p)
{
    ob_error e;
    ob_table *t;
    ob_change_result res;
    char *before;
    /* the table with two key fields gets a Counter instead */
    OK(ob_exec(p, "INSERT INTO \"Two keys\" VALUES ('a', 1), ('a', 2), ('b', 1)", E));
    before = obt_rows(p, "SELECT A, B FROM \"Two keys\" ORDER BY rowid");
    OK(ob_table_read(p, "Two keys", &t, E));
    CHECK(t && t->nkey == 2);
    ob_free(t->key[0]);
    ob_free(t->key[1]);
    t->key[0] = t->key[1] = NULL;
    t->nkey = 0;
    ob_table_add_field(t, "Number", OB_TYPE_COUNTER, &e);
    memset(&e, 0, sizeof e);
    CHECK(ob_table_change(p, "Two keys", t, 0, &res, &e) == OB_OK && res.rows == 3);
    CHECK(same_rows(p, "SELECT A, B FROM \"Two keys\" ORDER BY Number", before));
    CHECK(obt_long(p, "SELECT sum(Number) FROM \"Two keys\"") == 6);
    ob_table_free(t);
    free(before);
    /* a key over a field with the same value twice is refused */
    OK(ob_table_read(p, "Two keys", &t, E));
    ob_table_remove_field(t, "Number", &e);
    t->nkey = 1;
    ob_strset(&t->key[0], "A", &e);
    memset(&e, 0, sizeof e);
    CHECK(ob_table_change(p, "Two keys", t, 0, &res, &e) == OB_ERR_RULE && strstr(e.text, "Another record already has this A"));
    CHECK(obt_long(p, "SELECT count(*) FROM \"Two keys\"") == 3);
    ob_table_free(t);
}

static void relationships(ob_project *p)
{
    ob_error e;
    ob_table *c = ob_table_new("Customers", 1, &e), *o = ob_table_new("Orders", 1, &e), *t;
    ob_field *f;
    ob_relation *rels = NULL, r;
    ob_change_result res;
    int n = 0, i, found = 0;
    char *before;
    ob_table_add_field(c, "Name", OB_TYPE_TEXT, &e);
    f = ob_table_add_field(o, "customer", OB_TYPE_LOOKUP, &e);
    ob_strset(&f->lookup_table, "Customers", &e);
    ob_strset(&f->lookup_show, "Name", &e);
    ob_table_add_field(o, "Total", OB_TYPE_CURRENCY, &e);
    OK(ob_table_create(p, c, E));
    OK(ob_table_create(p, o, E));
    OK(ob_exec(p, "INSERT INTO Customers(Name) VALUES ('Ann'), ('Bob'), ('Cy')", E));
    OK(ob_exec(p, "INSERT INTO Orders(customer, Total) VALUES (1, 1000), (1, 250), (2, 99)", E));
    CHECK(exec_words(p, "INSERT INTO Orders(customer) VALUES (99)", "relationship"));
    CHECK(exec_words(p, "DELETE FROM Customers WHERE id = 1", "relationship"));
    OK(ob_relation_list(p, &rels, &n, E));
    for (i = 0; i < n; i++)
        if (!strcmp(rels[i].child, "Orders") && !strcmp(rels[i].parent, "Customers") && rels[i].on_delete == OB_REL_REFUSE &&
            !strcmp(rels[i].child_fields[0], "customer"))
            found = 1;
    CHECK(found);
    ob_relations_free(rels, n);
    CHECK(ob_table_drop(p, "Customers", &e) == OB_ERR_RULE);

    /* the parent copied and renamed keeps its children's relationship */
    before = obt_rows(p, "SELECT * FROM Orders ORDER BY id");
    OK(ob_table_read(p, "Customers", &t, E));
    ob_table_field(t, "Name")->required = 1;
    ob_table_add_field(t, "Since", OB_TYPE_DATE, &e);
    memset(&e, 0, sizeof e);
    CHECK(ob_table_change(p, "Customers", t, 0, &res, &e) == OB_OK && res.rebuilt);
    CHECK(exec_words(p, "INSERT INTO Orders(customer) VALUES (99)", "relationship"));
    CHECK(same_rows(p, "SELECT * FROM Orders ORDER BY id", before));
    CHECK(healthy(p));
    ob_table_free(t);
    free(before);

    /* cascade */
    OK(ob_table_read(p, "Orders", &t, E));
    CHECK(t && t->nrelations == 1);
    t->relations[0].on_delete = OB_REL_CASCADE;
    OK(ob_table_change(p, "Orders", t, 0, NULL, E));
    OK(ob_exec(p, "DELETE FROM Customers WHERE id = 1", E));
    CHECK(obt_long(p, "SELECT count(*) FROM Orders") == 1);
    /* clear */
    t->relations[0].on_delete = OB_REL_CLEAR;
    OK(ob_table_change(p, "Orders", t, 0, NULL, E));
    OK(ob_exec(p, "DELETE FROM Customers WHERE id = 2", E));
    CHECK(obt_long(p, "SELECT count(*) FROM Orders WHERE customer IS NULL") == 1);
    ob_table_free(t);

    /* a relationship added where records already point nowhere */
    OK(ob_exec(p, "CREATE TABLE Notes (id INTEGER PRIMARY KEY, customer INTEGER, text TEXT)", E));
    OK(ob_exec(p, "INSERT INTO Notes(customer, text) VALUES (3, 'ok'), (7, 'orphan'), (8, 'orphan')", E));
    memset(&r, 0, sizeof r);
    r.child = (char *)"Notes";
    r.parent = (char *)"Customers";
    r.nfields = 1;
    r.child_fields[0] = (char *)"customer";
    memset(&e, 0, sizeof e);
    CHECK(ob_relation_add(p, &r, &e) == OB_ERR_RULE && strstr(e.text, "2 records in Notes") && strstr(e.text, "record 2"));
    CHECK(obt_long(p, "SELECT count(*) FROM pragma_foreign_key_list('Notes')") == 0);
    OK(ob_exec(p, "DELETE FROM Notes WHERE text = 'orphan'", E));
    OK(ob_relation_add(p, &r, E));
    CHECK(exec_words(p, "INSERT INTO Notes(customer) VALUES (99)", "relationship"));
    CHECK(ob_relation_add(p, &r, &e) == OB_ERR_EXISTS);
    /* a relationship must point to a key */
    r.parent_fields[0] = (char *)"Name";
    CHECK(ob_relation_add(p, &r, &e) != OB_OK);
    r.parent_fields[0] = NULL;
    OK(ob_relation_remove(p, "Notes", "customer", E));
    OK(ob_exec(p, "INSERT INTO Notes(customer) VALUES (99)", E));

    /* a table related to itself, renamed */
    t = ob_table_new("Staff", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    f = ob_table_add_field(t, "manager", OB_TYPE_LOOKUP, &e);
    ob_strset(&f->lookup_table, "Staff", &e);
    OK(ob_table_create(p, t, E));
    OK(ob_exec(p, "INSERT INTO Staff(Name, manager) VALUES ('Boss', NULL), ('Pat', 1)", E));
    CHECK(exec_words(p, "INSERT INTO Staff(Name, manager) VALUES ('X', 42)", "relationship"));
    ob_table_free(t);
    OK(ob_table_read(p, "Staff", &t, E));
    ob_strset(&t->name, "People", &e);
    ob_table_add_field(t, "Phone", OB_TYPE_TEXT, &e);
    ob_table_field(t, "Name")->required = 1; /* a rebuild */
    OK(ob_table_change(p, "Staff", t, 0, NULL, E));
    CHECK(obt_long(p, "SELECT count(*) FROM pragma_foreign_key_list('People') WHERE \"table\" = 'People'") == 1);
    CHECK(exec_words(p, "INSERT INTO People(Name, manager) VALUES ('X', 42)", "relationship"));
    ob_table_free(t);
    CHECK(healthy(p));

    /* dropping: the child first */
    OK(ob_table_drop(p, "Orders", E));
    OK(ob_table_drop(p, "Customers", E));
    CHECK(obt_long(p, "SELECT count(*) FROM oadb_fields WHERE tbl IN ('Orders', 'Customers')") == 0);
    ob_table_free(c);
    ob_table_free(o);
}

static void adopted_tables(ob_project *p)
{
    ob_error e;
    ob_table *t;
    ob_change_result res;
    OK(ob_exec(p, "CREATE TABLE raw (a INTEGER PRIMARY KEY, b VARCHAR(20), c DOUBLE, d BLOB, e)", E));
    OK(ob_exec(p, "INSERT INTO raw VALUES (1, 'x', 1.5, NULL, NULL), (2, 'y', NULL, x'00', 3)", E));
    OK(ob_table_read(p, "raw", &t, E));
    CHECK(t && t->adopted && ob_table_field(t, "a")->type == OB_TYPE_COUNTER && ob_table_field(t, "b")->type == OB_TYPE_TEXT &&
          ob_table_field(t, "c")->type == OB_TYPE_NUMBER);
    ob_table_remove_field(t, "e", &e);
    ob_table_field(t, "d")->type = OB_TYPE_OBJECT;
    CHECK(ob_table_change(p, "raw", t, 0, &res, &e) == OB_OK && res.rows == 2);
    ob_table_free(t);
    OK(ob_exec(p, "CREATE TABLE ruled (a INTEGER PRIMARY KEY, b INTEGER CHECK (b > 0))", E));
    OK(ob_table_read(p, "ruled", &t, E));
    ob_table_add_field(t, "c", OB_TYPE_TEXT, &e);
    ob_table_field(t, "b")->type = OB_TYPE_TEXT; /* a rebuild would lose b's rule */
    CHECK(ob_table_change(p, "ruled", t, 0, &res, &e) == OB_ERR_UNSUPPORTED);
    ob_table_free(t);
    /* a table without a key stays changeable */
    OK(ob_exec(p, "CREATE TABLE loose (a TEXT, b INTEGER); INSERT INTO loose VALUES ('x', 1), ('y', 2)", E));
    OK(ob_table_read(p, "loose", &t, E));
    ob_table_field(t, "b")->type = OB_TYPE_TEXT;
    OK(ob_table_change(p, "loose", t, 0, &res, E));
    ob_table_free(t);
    OK(ob_table_read(p, "loose", &t, E));
    CHECK(t && !t->adopted && t->nkey == 0);
    ob_table_add_field(t, "c", OB_TYPE_TEXT, &e);
    OK(ob_table_change(p, "loose", t, 0, &res, E));
    CHECK(res.rows == 2);
    ob_table_free(t);
    OK(ob_exec(p, "CREATE TABLE nr (k TEXT PRIMARY KEY, v) WITHOUT ROWID", E));
    OK(ob_table_read(p, "nr", &t, E));
    CHECK(t && t->without_rowid && t->nkey == 1);
    ob_table_field(t, "v")->type = OB_TYPE_INTEGER;
    CHECK(ob_table_change(p, "nr", t, 0, &res, &e) == OB_ERR_UNSUPPORTED);
    ob_table_free(t);
}

static void lists_and_indexes(ob_project *p)
{
    ob_error e;
    char **names;
    int n, i, has_stock = 0;
    ob_index ix;
    OK(ob_table_list(p, &names, &n, E));
    for (i = 0; i < n; i++) {
        if (!strcmp(names[i], "Items")) has_stock = 1;
        CHECK(strncmp(names[i], "oadb_", 5) != 0);
    }
    CHECK(has_stock);
    ob_names_free(names, n);
    memset(&ix, 0, sizeof ix);
    ix.nfields = 2;
    ix.fields[0] = (char *)"Colour";
    ix.fields[1] = (char *)"price";
    OK(ob_index_add(p, "Items", &ix, E));
    CHECK(obt_long(p, "SELECT count(*) FROM sqlite_schema WHERE name = 'Items_Colour_Price'") == 1);
    CHECK(ob_index_add(p, "Items", &ix, &e) == OB_OK); /* a second name is made */
    OK(ob_index_remove(p, "Items_Colour_Price", E));
    CHECK(ob_index_remove(p, "nothing", &e) == OB_ERR_NOT_FOUND);
    ix.unique = 1;
    ix.nfields = 1;
    ix.fields[0] = (char *)"Active";
    CHECK(ob_index_add(p, "Items", &ix, &e) == OB_ERR_RULE && strstr(e.text, "Another record already has this Active"));
}

void suite_schema(void)
{
    ob_project *p = obt_memory_project();
    types_and_words(p);
    bad_designs(p);
    changes_keep_rows(p);
    keys_change(p);
    relationships(p);
    adopted_tables(p);
    lists_and_indexes(p);
    CHECK(healthy(p));
    ob_project_close(p);
}
