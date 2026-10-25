/* OpenBase host tests: records and Datatype objects (ob_record.h), as the
 * program's datasheets and forms use them (phase B2).
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "obtest.h"

#include <stdlib.h>

/* The words a call failed with, and its code */
static int fails_with(int rc, int code, const ob_error *e, const char *words)
{
    if (rc != code || !strstr(e->text, words)) {
        fprintf(stderr, "  -> %d \"%s\", wanted %d \"%s\"\n", rc, e->text, code, words);
        return 0;
    }
    return 1;
}

/* Customers (a Datatype object among its fields) and Orders, whose
   customer field is a relationship that refuses an orphan */
static void make_tables(ob_project *p)
{
    ob_error e;
    ob_table *t = ob_table_new("Customers", 1, &e);
    ob_field *f = ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    f->required = 1;
    ob_table_add_field(t, "Joined", OB_TYPE_DATE, &e);
    ob_table_add_field(t, "Credit", OB_TYPE_CURRENCY, &e);
    f = ob_table_add_field(t, "Member", OB_TYPE_YESNO, &e);
    ob_strset(&f->default_sql, "0", &e);
    ob_table_add_field(t, "Photo", OB_TYPE_OBJECT, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    t = ob_table_new("Orders", 1, &e);
    f = ob_table_add_field(t, "Customer", OB_TYPE_LOOKUP, &e);
    ob_strset(&f->lookup_table, "Customers", &e);
    ob_strset(&f->label, "Customer number", &e);
    ob_table_add_field(t, "Total", OB_TYPE_CURRENCY, &e);
    ob_table_add_relation(t, "Customer", "Customers", OB_REL_REFUSE, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
}

static ob_table *read_table(ob_project *p, const char *name)
{
    ob_table *t = NULL;
    OK(ob_table_read(p, name, &t, E));
    return t;
}

static void test_write_read(ob_project *p)
{
    ob_table *c = read_table(p, "Customers");
    const char *ann[6] = { NULL, "Ann Smith", "2026-10-25", "1,234.50", "yes", NULL };
    const char *bob[6] = { NULL, "Bob", "", "-3", NULL, NULL };
    long long k1 = 0, k2 = 0, keys[8];
    char **v = NULL;
    long n = -1;
    int got = -1;
    ob_error e;
    if (!c) return;
    CHECK(c->nfields == 6);
    OK(ob_record_write(p, c, &k1, ann, E));
    OK(ob_record_write(p, c, &k2, bob, E));
    CHECK(k1 == 1 && k2 == 2);
    OK(ob_record_count(p, "Customers", &n, E));
    CHECK(n == 2);
    OK(ob_record_read(p, c, k1, &v, E));
    if (v) {
        CHECK_STR(v[0], "1");
        CHECK_STR(v[1], "Ann Smith");
        CHECK_STR(v[2], "2026-10-25");
        CHECK_STR(v[3], "1,234.50");
        CHECK_STR(v[4], "Yes");
        CHECK_STR(v[5], "");
        ob_record_free(v, c->nfields);
    }
    OK(ob_record_read(p, c, k2, &v, E));
    if (v) {
        CHECK_STR(v[2], "");        /* "" typed: nothing kept */
        CHECK_STR(v[3], "-3.00");
        CHECK_STR(v[4], "No");      /* not typed: its default */
        ob_record_free(v, c->nfields);
    }
    CHECK(obt_long(p, "SELECT Credit FROM Customers WHERE id = 1") == 123450);
    CHECK(obt_long(p, "SELECT typeof(Joined) = 'null' FROM Customers WHERE id = 2") == 1);

    /* a change: only the fields given */
    {
        const char *now[6] = { "99", "Bob Jones", NULL, NULL, "no", NULL };
        OK(ob_record_write(p, c, &k2, now, E));
        CHECK(k2 == 2);   /* the Counter isn't written */
        OK(ob_record_read(p, c, k2, &v, E));
        if (v) {
            CHECK_STR(v[1], "Bob Jones");
            CHECK_STR(v[3], "-3.00");
            ob_record_free(v, c->nfields);
        }
    }

    /* what was typed isn't the field's type: the field is named */
    {
        const char *bad[6] = { NULL, "Cy", "the 40th of May", NULL, NULL, NULL };
        long long k = 0;
        memset(&e, 0, sizeof e);
        CHECK(fails_with(ob_record_write(p, c, &k, bad, &e), OB_ERR_INVALID, &e, "Joined"));
        CHECK(k == 0);
    }
    /* a required field left empty, in plain words */
    {
        const char *none[6] = { NULL, "", NULL, NULL, NULL, NULL };
        long long k = 0;
        memset(&e, 0, sizeof e);
        CHECK(fails_with(ob_record_write(p, c, &k, none, &e), OB_ERR_RULE, &e, "Name must be filled in"));
    }

    /* pages of keys */
    {
        int i;
        for (i = 0; i < 20; i++) {
            const char *row[6] = { NULL, "Someone", NULL, NULL, NULL, NULL };
            long long k = 0;
            OK(ob_record_write(p, c, &k, row, E));
        }
    }
    OK(ob_record_keys(p, "Customers", 0, 8, keys, &got, E));
    CHECK(got == 8 && keys[0] == 1 && keys[7] == 8);
    OK(ob_record_keys(p, "Customers", 16, 8, keys, &got, E));
    CHECK(got == 6 && keys[0] == 17 && keys[5] == 22);
    OK(ob_record_keys(p, "Customers", 100, 8, keys, &got, E));
    CHECK(got == 0);
    OK(ob_record_keys(p, "Customers", 0, 0, keys, &got, E));
    CHECK(got == 0);
    memset(&e, 0, sizeof e);
    CHECK(ob_record_keys(p, "Nowhere", 0, 8, keys, &got, &e) == OB_ERR_NOT_FOUND);
    memset(&e, 0, sizeof e);
    CHECK(ob_record_count(p, "Nowhere", &n, &e) == OB_ERR_NOT_FOUND);

    /* a record that isn't there */
    memset(&e, 0, sizeof e);
    CHECK(ob_record_read(p, c, 9999, &v, &e) == OB_ERR_NOT_FOUND && !v);
    memset(&e, 0, sizeof e);
    CHECK(ob_record_delete(p, "Customers", 9999, &e) == OB_ERR_NOT_FOUND);
    ob_table_free(c);
}

static void test_relationship(ob_project *p)
{
    ob_table *o = read_table(p, "Orders");
    const char *orphan[3] = { NULL, "99", "10" };
    const char *good[3] = { NULL, "1", "12.5" };
    long long k = 0, kept = 0;
    char **v = NULL;
    ob_error e;
    if (!o) return;
    CHECK(o->nrelations == 1);
    /* an order for a customer who isn't there is refused, in plain words */
    memset(&e, 0, sizeof e);
    CHECK(fails_with(ob_record_write(p, o, &k, orphan, &e), OB_ERR_RULE, &e,
                     "Customer number: 99 doesn't match a record in Customers."));
    CHECK(k == 0 && obt_long(p, "SELECT count(*) FROM Orders") == 0);
    OK(ob_record_write(p, o, &kept, good, E));
    CHECK(kept == 1);
    OK(ob_record_read(p, o, kept, &v, E));
    if (v) {
        CHECK_STR(v[1], "1");
        CHECK_STR(v[2], "12.50");
        ob_record_free(v, o->nfields);
    }
    /* changed to point nowhere: refused, and the record is as it was */
    memset(&e, 0, sizeof e);
    CHECK(fails_with(ob_record_write(p, o, &kept, orphan, &e), OB_ERR_RULE, &e, "doesn't match a record in Customers"));
    CHECK(obt_long(p, "SELECT Customer FROM Orders WHERE id = 1") == 1);
    /* the customer an order points to can't go */
    memset(&e, 0, sizeof e);
    CHECK(fails_with(ob_record_delete(p, "Customers", 1, &e), OB_ERR_RULE, &e,
                     "Records in Orders point to this one"));
    CHECK(obt_long(p, "SELECT count(*) FROM Customers WHERE id = 1") == 1);
    /* the order first, then the customer */
    OK(ob_record_delete(p, "Orders", kept, E));
    OK(ob_record_delete(p, "Customers", 1, E));
    CHECK(obt_long(p, "SELECT count(*) FROM Customers WHERE id = 1") == 0);
    CHECK(obt_long(p, "SELECT count(*) FROM pragma_foreign_key_check") == 0);
    ob_table_free(o);
}

static void test_objects(ob_project *p)
{
    ob_table *c = read_table(p, "Customers");
    unsigned char pic[3000];
    void *data = NULL;
    long size = -1, i;
    char name[64], **v = NULL;
    ob_error e;
    if (!c) return;
    for (i = 0; i < (long)sizeof pic; i++) pic[i] = (unsigned char)(i * 7);
    pic[0] = 0;   /* bytes, not text: a zero inside */
    OK(ob_object_set(p, "Customers", "Photo", 2, pic, (long)sizeof pic, "Bob.iff", "picture", E));
    OK(ob_object_get(p, "Customers", "Photo", 2, &data, &size, name, sizeof name, E));
    CHECK(size == (long)sizeof pic && data && memcmp(data, pic, sizeof pic) == 0);
    CHECK_STR(name, "Bob.iff");
    ob_free(data);
    CHECK(obt_long(p, "SELECT Photo_type = 'picture' FROM Customers WHERE id = 2") == 1);
    /* a datasheet shows its file's name; a form's record writes leave it be */
    OK(ob_record_read(p, c, 2, &v, E));
    if (v) {
        CHECK_STR(v[5], "Bob.iff");
        ob_record_free(v, c->nfields);
    }
    {
        const char *row[6] = { NULL, "Bob J", NULL, NULL, NULL, "not an object" };
        long long k = 2;
        OK(ob_record_write(p, c, &k, row, E));
        CHECK(obt_long(p, "SELECT length(Photo) FROM Customers WHERE id = 2") == (long long)sizeof pic);
    }
    /* cleared */
    OK(ob_object_set(p, "Customers", "Photo", 2, NULL, 0, NULL, NULL, E));
    OK(ob_object_get(p, "Customers", "Photo", 2, &data, &size, name, sizeof name, E));
    CHECK(!data && size == 0 && name[0] == 0);
    CHECK(obt_long(p, "SELECT Photo IS NULL AND Photo_name IS NULL FROM Customers WHERE id = 2") == 1);
    /* an empty object is kept as one */
    OK(ob_object_set(p, "Customers", "Photo", 2, "", 0, "empty", "text", E));
    OK(ob_object_get(p, "Customers", "Photo", 2, &data, &size, name, sizeof name, E));
    CHECK(!data && size == 0);
    CHECK_STR(name, "empty");
    /* not an object field, no such record, no such table */
    memset(&e, 0, sizeof e);
    CHECK(ob_object_set(p, "Customers", "Name", 2, pic, 10, "x", "picture", &e) == OB_ERR_NOT_FOUND);
    memset(&e, 0, sizeof e);
    CHECK(ob_object_set(p, "Customers", "Photo", 9999, pic, 10, "x", "picture", &e) == OB_ERR_NOT_FOUND);
    memset(&e, 0, sizeof e);
    CHECK(ob_object_get(p, "Customers", "Photo", 9999, &data, &size, name, sizeof name, &e) == OB_ERR_NOT_FOUND);
    memset(&e, 0, sizeof e);
    CHECK(ob_object_get(p, "Nowhere", "Photo", 2, &data, &size, NULL, 0, &e) != OB_OK);
    memset(&e, 0, sizeof e);
    CHECK(ob_object_set(p, "Customers", "Photo", 2, pic, -5, "x", "picture", &e) == OB_ERR_INVALID);
    ob_table_free(c);
}

/* The Prefs limit on one object (4 MB to start; here 2 KB) */
static void test_object_limit(void)
{
    ob_project *p = NULL;
    ob_config cfg;
    static unsigned char big[3 * 1024];
    const char *row[6] = { NULL, "Ann", NULL, NULL, NULL, NULL };
    long long k = 0;
    ob_table *c;
    ob_error e;
    ob_config_defaults(&cfg);
    CHECK(cfg.max_object_kib == 4096);
    cfg.max_object_kib = 2;
    OK(ob_project_create(":memory:", "Limit", &cfg, &p, E));
    if (!p) return;
    make_tables(p);
    c = read_table(p, "Customers");
    if (c) {
        OK(ob_record_write(p, c, &k, row, E));
        memset(&e, 0, sizeof e);
        CHECK(fails_with(ob_object_set(p, "Customers", "Photo", k, big, (long)sizeof big, "big.iff", "picture", &e),
                         OB_ERR_LIMIT, &e, "2 KB at most"));
        OK(ob_object_set(p, "Customers", "Photo", k, big, 2048, "two.iff", "picture", E));
        ob_table_free(c);
    }
    ob_project_close(p);
}

/* A project opened read only refuses changes */
static void test_readonly(void)
{
    const char *path = obt_path("records-ro.oadb");
    ob_project *p = NULL;
    ob_table *c;
    const char *row[6] = { NULL, "Ann", NULL, NULL, NULL, NULL };
    long long k = 0;
    ob_error e;
    OK(ob_project_create(path, "RO", NULL, &p, E));
    if (!p) return;
    make_tables(p);
    c = read_table(p, "Customers");
    if (c) OK(ob_record_write(p, c, &k, row, E));
    ob_table_free(c);
    ob_project_close(p);
    p = NULL;
    OK(ob_project_open(path, OB_OPEN_READONLY, NULL, &p, E));
    if (!p) return;
    c = read_table(p, "Customers");
    if (c) {
        long long k2 = 0;
        char **v = NULL;
        memset(&e, 0, sizeof e);
        CHECK(ob_record_write(p, c, &k2, row, &e) == OB_ERR_READONLY);
        memset(&e, 0, sizeof e);
        CHECK(ob_record_delete(p, "Customers", k, &e) == OB_ERR_READONLY);
        memset(&e, 0, sizeof e);
        CHECK(ob_object_set(p, "Customers", "Photo", k, "x", 1, "x", "text", &e) == OB_ERR_READONLY);
        OK(ob_record_read(p, c, k, &v, E));
        if (v) {
            CHECK_STR(v[1], "Ann");
            ob_record_free(v, c->nfields);
        }
        ob_table_free(c);
    }
    ob_project_close(p);
}

/* Names that need quoting in SQL */
static void test_odd_names(ob_project *p)
{
    ob_error e;
    ob_table *t = ob_table_new("Bob's \"best\" list", 1, &e), *r;
    const char *row[3] = { NULL, "it's \"quoted\"", NULL };
    long long k = 0;
    char **v = NULL;
    long n = 0;
    ob_table_add_field(t, "Item name", OB_TYPE_TEXT, &e);
    ob_table_add_field(t, "Pic\"ture", OB_TYPE_OBJECT, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    r = read_table(p, "Bob's \"best\" list");
    if (!r) return;
    OK(ob_record_write(p, r, &k, row, E));
    OK(ob_record_count(p, "Bob's \"best\" list", &n, E));
    CHECK(n == 1);
    OK(ob_object_set(p, "Bob's \"best\" list", "Pic\"ture", k, "abc", 3, "a\"b.iff", "picture", E));
    OK(ob_record_read(p, r, k, &v, E));
    if (v) {
        CHECK_STR(v[1], "it's \"quoted\"");
        CHECK_STR(v[2], "a\"b.iff");
        ob_record_free(v, r->nfields);
    }
    OK(ob_record_delete(p, "Bob's \"best\" list", k, E));
    ob_table_free(r);
}

void suite_record(void)
{
    ob_project *p = obt_memory_project();
    make_tables(p);
    test_write_read(p);
    test_relationship(p);
    test_objects(p);
    test_odd_names(p);
    ob_project_close(p);
    test_object_limit();
    test_readonly();
}
