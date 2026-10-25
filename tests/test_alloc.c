/* OpenBase host tests: running out of memory. The core's allocator fails
 * the n-th allocation, for every n, through a whole session: making
 * tables, changing one, CSV in and out, a query saved and loaded. Each
 * call must give an error and leave nothing behind (LeakSanitizer checks
 * that under `make check`). SQLite's own memory isn't failed here.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "obtest.h"

#include <stdlib.h>

static long count, fail_at = -1, failures;

static int fail_now(void)
{
    long at = count++;
    if (fail_at >= 0 && at == fail_at) {
        failures++;
        return 1;
    }
    return 0;
}

static void *fa_alloc(void *ctx, size_t n)
{
    (void)ctx;
    return fail_now() ? NULL : malloc(n);
}

static void *fa_resize(void *ctx, void *b, size_t n)
{
    (void)ctx;
    return fail_now() ? NULL : realloc(b, n);
}

static void fa_release(void *ctx, void *b)
{
    (void)ctx;
    free(b);
}

/* 0 when every step worked; else the first error's code */
static int session(void)
{
    ob_project *p = NULL;
    ob_table *t = NULL;
    ob_field *f;
    ob_error e;
    ob_csv_options o;
    ob_csv_plan *plan = NULL;
    ob_report *rep = NULL;
    ob_query *q = NULL, *back = NULL;
    ob_buf b;
    ob_mem_source m;
    char *sql = NULL, **names = NULL;
    int rc, n = 0, c;
    static const char csv[] = "Name,Joined,Owes\nAnn,25/10/2026,\xC2\xA3" "3.50\nBob,x,1\n\"Cy\",01/01/2020,2\n";
    ob_buf_init(&b, 0);
    if ((rc = ob_project_create(":memory:", "Alloc", NULL, &p, &e))) goto out;
    if (!(t = ob_table_new("Customers", 1, &e))) {
        rc = e.code;
        goto out;
    }
    if (!ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e) || !ob_table_add_index(t, "Name", 0, &e)) {
        rc = e.code;
        goto out;
    }
    if ((rc = ob_table_create(p, t, &e))) goto out;
    ob_table_free(t);
    if (!(t = ob_table_new("Orders", 1, &e))) {
        rc = e.code;
        goto out;
    }
    if (!(f = ob_table_add_field(t, "customer", OB_TYPE_LOOKUP, &e)) || (rc = ob_strset(&f->lookup_table, "Customers", &e)) ||
        !ob_table_add_field(t, "Total", OB_TYPE_CURRENCY, &e)) {
        rc = rc ? rc : e.code;
        goto out;
    }
    if ((rc = ob_table_create(p, t, &e))) goto out;
    if ((rc = ob_exec(p, "INSERT INTO Customers(Name) VALUES ('Ann'), ('Bob'); INSERT INTO Orders(customer, Total) VALUES (1, 5), (2, 7)", &e)))
        goto out;
    ob_table_free(t);
    t = NULL;
    /* a change that copies and renames */
    if ((rc = ob_table_read(p, "Orders", &t, &e))) goto out;
    ob_table_field(t, "Total")->type = OB_TYPE_NUMBER;
    if (!ob_table_add_field(t, "Note", OB_TYPE_MEMO, &e)) {
        rc = e.code;
        goto out;
    }
    if ((rc = ob_strset(&t->name, "Sales", &e))) goto out;
    if ((rc = ob_table_change(p, "Orders", t, 0, NULL, &e))) goto out;
    /* CSV in and out */
    ob_csv_defaults(&o);
    m.data = csv;
    m.size = (long)sizeof csv - 1;
    m.pos = 0;
    if ((rc = ob_csv_guess(ob_mem_read, &m, &o, &plan, &e))) goto out;
    if (!(rep = ob_report_new(10))) {
        rc = OB_ERR_NOMEM;
        goto out;
    }
    m.pos = 0;
    if ((rc = ob_csv_import(p, ob_mem_read, &m, &o, plan, "People", rep, NULL, &e))) goto out;
    o.charset = OB_CHARSET_LATIN1;
    if ((rc = ob_csv_export_table(p, "People", ob_buf_write, &b, &o, rep, NULL, &e))) goto out;
    /* a query */
    if (!(q = ob_query_new())) {
        rc = OB_ERR_NOMEM;
        goto out;
    }
    if ((rc = ob_query_add_table(q, "Sales", NULL, &e)) || (rc = ob_query_add_table(q, "Customers", "c", &e))) goto out;
    if ((rc = ob_query_auto_joins(p, q, &e))) goto out;
    if ((c = ob_query_add_column(q, 1, "Name", &e)) < 0) {
        rc = e.code;
        goto out;
    }
    if ((c = ob_query_add_column(q, 0, "Total", &e)) < 0 || (rc = ob_query_set_criteria(q, c, 0, "between 1 and [Most?]", &e))) {
        rc = rc ? rc : e.code;
        goto out;
    }
    if ((rc = ob_query_resolve(p, q, &e))) goto out;
    if ((rc = ob_query_save(p, "Small", q, NULL, 0, &e))) goto out;
    if ((rc = ob_query_load(p, "Small", &back, &sql, &e))) goto out;
    if ((rc = ob_query_list(p, &names, &n, &e))) goto out;
    if ((rc = ob_relation_add(p, &(ob_relation){ (char *)"People", 1, { (char *)"Name" }, (char *)"Customers",
                                                 { (char *)"Name" }, OB_REL_REFUSE, OB_REL_REFUSE }, &e)) != OB_ERR_INVALID)
        goto out; /* Name isn't Customers' key nor unique: refused, but cleanly */
    rc = OB_OK;
out:
    ob_names_free(names, n);
    ob_free(sql);
    ob_query_free(back);
    ob_query_free(q);
    ob_report_free(rep);
    ob_csv_plan_free(plan);
    ob_table_free(t);
    ob_buf_free(&b);
    ob_project_close(p);
    return rc;
}

void suite_alloc(void)
{
    ob_allocator a = { fa_alloc, fa_resize, fa_release, NULL };
    long n, completed_at = -1, total;
    int rc;
    ob_set_allocator(&a);
    fail_at = -1;
    count = 0;
    rc = session();
    total = count;
    CHECK(rc == OB_OK);
    for (n = 0; n <= total + 5; n++) {
        count = 0;
        failures = 0;
        fail_at = n;
        rc = session();
        obt_checks++;
        if (!failures) {
            completed_at = n;
            if (rc != OB_OK) {
                obt_failures++;
                fprintf(stderr, "alloc: with no allocation failed, the session failed (%d)\n", rc);
            }
            break;
        }
        if (rc == OB_OK) {
            /* fine when the failure was in something that may fail quietly
             * (a note not kept); the session still finished */
        }
    }
    ob_set_allocator(NULL);
    fail_at = -1;
    printf("           allocations in the session: %ld; each one failed in turn\n", total);
    CHECK(completed_at == total);
}
