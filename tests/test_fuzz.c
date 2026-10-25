/* OpenBase host tests: every cut-short .oadb and CSV gives an error, never
 * a crash; and random byte changes never crash either (Design-OpenBase.md,
 * 13, B1). Under `make check` this runs with AddressSanitizer and UBSan.
 * OB_FUZZ_ROUNDS sets the number of byte-change rounds (300 by default).
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "obtest.h"

#include <stdlib.h>

static unsigned long seed = 0x0ADB2026UL;

static unsigned long rnd(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed & 0xFFFFFFFFUL;
}

static long rounds(void)
{
    const char *r = getenv("OB_FUZZ_ROUNDS");
    long n = r ? atol(r) : 0;
    return n > 0 ? n : 300;
}

/* A project with something in every design table OpenBase reads */
static void build_sample(const char *path)
{
    ob_project *p = NULL;
    ob_table *t;
    ob_field *f;
    ob_error e;
    ob_query *q;
    int i, c;
    OK(ob_project_create(path, "Fuzz", NULL, &p, E));
    if (!p) return;
    t = ob_table_new("Customers", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e)->maxlen = 40;
    ob_table_add_field(t, "Town", OB_TYPE_TEXT, &e);
    ob_table_add_index(t, "Town", 0, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    t = ob_table_new("Orders", 1, &e);
    f = ob_table_add_field(t, "customer", OB_TYPE_LOOKUP, &e);
    ob_strset(&f->lookup_table, "Customers", &e);
    ob_table_add_field(t, "Day", OB_TYPE_DATE, &e);
    ob_table_add_field(t, "Total", OB_TYPE_CURRENCY, &e);
    ob_table_add_field(t, "Paid", OB_TYPE_YESNO, &e);
    ob_strset(&ob_table_add_field(t, "State", OB_TYPE_CHOICE, &e)->choices, "New\nSent", &e);
    ob_table_add_field(t, "Scan", OB_TYPE_OBJECT, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    ob_exec(p, "BEGIN", &e);
    for (i = 0; i < 300; i++) {
        char sql[256];
        snprintf(sql, sizeof sql, "INSERT INTO Customers(Name, Town) VALUES ('Customer %d', '%s')", i,
                 i % 3 ? "Leeds" : "York");
        ob_exec(p, sql, &e);
        snprintf(sql, sizeof sql, "INSERT INTO Orders(customer, Day, Total, Paid, State, Scan) VALUES (%d, '2026-%02d-%02d', %d, %d, '%s', %s)",
                 1 + i % 300, 1 + i % 12, 1 + i % 28, i * 37, i % 2, i % 2 ? "New" : "Sent", i % 10 ? "NULL" : "zeroblob(600)");
        ob_exec(p, sql, &e);
    }
    ob_exec(p, "COMMIT", &e);
    q = ob_query_new();
    ob_query_add_table(q, "Customers", NULL, &e);
    ob_query_add_table(q, "Orders", NULL, &e);
    ob_query_add_join(q, 0, "id", 1, "customer", OB_JOIN_LEFT, &e);
    c = ob_query_add_column(q, 0, "Town", &e);
    q->columns[c].total = OB_TOTAL_GROUP;
    c = ob_query_add_column(q, 1, "Total", &e);
    q->columns[c].total = OB_TOTAL_SUM;
    q->columns[c].type = OB_TYPE_CURRENCY;
    ob_query_set_criteria(q, c, 0, "> 1.00", &e);
    OK(ob_query_save(p, "Towns", q, NULL, 1, E));
    q->columns[0].total = OB_TOTAL_NONE;
    q->columns[1].total = OB_TOTAL_NONE;
    ob_query_set_criteria(q, 0, 0, "[Which town?]", &e);
    OK(ob_query_save(p, "Asks", q, NULL, 0, E));
    ob_query_free(q);
    OK(ob_exec(p, "CREATE TRIGGER Stamp AFTER INSERT ON Orders BEGIN UPDATE Orders SET Day = date('now') WHERE id = new.id AND Day IS NULL; END", E));
    OK(ob_project_compact(p, E));
    ob_project_close(p);
}

/* Read everything a damaged project may hold, as the program would */
static void exercise(ob_project *p)
{
    ob_error e;
    char **names = NULL, title[64];
    int n = 0, i;
    ob_relation *rel = NULL;
    int nrel = 0;
    ob_csv_options o;
    ob_csv_defaults(&o);
    ob_meta_get(p, "title", title, sizeof title, &e);
    ob_project_check(p, 1, &e);
    if (ob_table_list(p, &names, &n, &e) == OB_OK) {
        for (i = 0; i < n; i++) {
            ob_table *t = NULL;
            ob_buf out;
            long rows;
            if (ob_table_read(p, names[i], &t, &e) == OB_OK) {
                ob_buf sql;
                ob_buf_init(&sql, 0);
                ob_table_create_sql(t, NULL, &sql, &e);
                ob_buf_free(&sql);
                if (i == 0) {
                    ob_table_add_field(t, "Added", OB_TYPE_TEXT, &e);
                    ob_table_change(p, names[i], t, 0, NULL, &e);
                }
            }
            ob_table_free(t);
            ob_buf_init(&out, 4L * 1024 * 1024);
            ob_csv_export_table(p, names[i], ob_buf_write, &out, &o, NULL, &rows, &e);
            ob_buf_free(&out);
        }
        ob_names_free(names, n);
    }
    if (ob_relation_list(p, &rel, &nrel, &e) == OB_OK) ob_relations_free(rel, nrel);
    if (ob_query_list(p, &names, &n, &e) == OB_OK) {
        for (i = 0; i < n; i++) {
            ob_query *q = NULL;
            char *sql = NULL;
            if (ob_query_load(p, names[i], &q, &sql, &e) == OB_OK && sql) {
                ob_buf out;
                ob_buf_init(&out, 4L * 1024 * 1024);
                ob_csv_export_sql(p, sql, NULL, 0, ob_buf_write, &out, &o, NULL, NULL, &e);
                ob_buf_free(&out);
            }
            if (q) ob_query_resolve(p, q, &e);
            ob_query_free(q);
            ob_free(sql);
        }
        ob_names_free(names, n);
    }
    {
        static const char csv[] = "Name,Town\nA,Leeds\nB,York\n";
        ob_mem_source m;
        ob_csv_plan *plan = NULL;
        m.data = csv;
        m.size = (long)sizeof csv - 1;
        m.pos = 0;
        if (ob_csv_guess(ob_mem_read, &m, &o, &plan, &e) == OB_OK) {
            m.pos = 0;
            ob_csv_import(p, ob_mem_read, &m, &o, plan, "Imported", NULL, NULL, &e);
        }
        ob_csv_plan_free(plan);
    }
}

static void oadb_cuts(void)
{
    const char *path = obt_path("sample.oadb"), *cut = obt_path("cut.oadb");
    char *data;
    long size = 0, len, refused = 0, tried = 0, page = 4096;
    build_sample(path);
    data = obt_read_file(path, &size);
    CHECK(data && size > 8 * 4096);
    if (!data) return;
    {
        unsigned char *h = (unsigned char *)data;
        page = ((long)h[16] << 8) | h[17];
        if (page == 1) page = 65536;
    }
    for (len = 0; len < size; len++) {
        ob_project *p = NULL;
        ob_error e;
        int rc;
        /* every length near the start and around each page boundary; a
         * stride through the rest */
        if (len > 512 && len % page > 2 && len % page < page - 2 && len % 97) continue;
        tried++;
        obt_write_file(cut, data, len);
        rc = ob_project_open(cut, len % 2 ? OB_OPEN_NO_CHECK : 0, NULL, &p, &e);
        obt_checks++;
        if (rc == OB_OK) {
            obt_failures++;
            fprintf(stderr, "a project cut to %ld of %ld bytes opened\n", len, size);
            exercise(p);
            ob_project_close(p);
        } else {
            refused++;
            if (len >= 100 && rc != OB_ERR_DAMAGED) {
                obt_failures++;
                fprintf(stderr, "cut to %ld: %s (%d), not \"damaged\"\n", len, e.text, rc);
            }
        }
    }
    printf("           .oadb cut short: %ld of %ld lengths tried, all refused: %s\n", tried, size, refused == tried ? "yes" : "NO");
    free(data);
}

static void oadb_flips(void)
{
    const char *path = obt_path("sample.oadb"), *bad = obt_path("flipped.oadb");
    char *data, *copy;
    long size = 0, r, n = rounds(), opened = 0;
    data = obt_read_file(path, &size);
    if (!data) return;
    copy = malloc((size_t)size);
    for (r = 0; r < n; r++) {
        ob_project *p = NULL;
        ob_error e;
        int k, flips = 1 + (int)(rnd() % 4);
        memcpy(copy, data, (size_t)size);
        for (k = 0; k < flips; k++) {
            long at = rnd() % 4 == 0 ? (long)(rnd() % 100) : (long)(rnd() % (unsigned long)size);
            if (rnd() % 2) copy[at] = (char)(rnd() & 0xFF);
            else copy[at] ^= (char)(1 << (rnd() % 8));
        }
        obt_write_file(bad, copy, size);
        if (ob_project_open(bad, r % 3 == 0 ? OB_OPEN_NO_CHECK : 0, NULL, &p, &e) == OB_OK) {
            opened++;
            exercise(p);
            ob_project_close(p);
        }
        obt_checks++; /* reaching here is the check: no crash */
    }
    printf("           .oadb bytes changed: %ld rounds, %ld opened and were read through\n", n, opened);
    free(copy);
    free(data);
}

static const char sample_csv[] =
    "id,Name,Joined,Balance,Notes\r\n"
    "1,Ann,2026-10-25,12.50,\"Likes \"\"tea\"\"\"\r\n"
    "2,Bob,2025-01-02,0.99,\"two\r\nlines\"\r\n"
    "3,\"Cy, Jr\",2024-12-13,-5,\r\n"
    "4,D\xC3\xAF,2023-03-04,1000000,\"\"\r\n"
    "5,Ed,2022-02-28,7,plain\r\n";

static int inside_quotes(const char *s, long n)
{
    long i;
    int in = 0;
    for (i = 0; i < n; i++)
        if (s[i] == '"') in = !in;
    return in;
}

static void import_text(const char *data, long size, long *added, long *bad, long *read, int *guessed)
{
    ob_project *p = obt_memory_project();
    ob_csv_options o;
    ob_csv_plan *plan = NULL;
    ob_csv_result res;
    ob_mem_source m;
    ob_error e;
    ob_csv_defaults(&o);
    m.data = data;
    m.size = size;
    m.pos = 0;
    *added = *bad = *read = 0;
    *guessed = ob_csv_guess(ob_mem_read, &m, &o, &plan, &e) == OB_OK;
    if (*guessed) {
        ob_report *rep = ob_report_new(5);
        m.pos = 0;
        memset(&res, 0, sizeof res);
        if (ob_csv_import(p, ob_mem_read, &m, &o, plan, "T", rep, &res, &e) == OB_OK) {
            *added = res.rows_added;
            *bad = res.rows_bad;
            *read = res.rows_read;
            CHECK(res.rows_added + res.rows_bad == res.rows_read);
            CHECK(obt_long(p, "SELECT count(*) FROM T") == res.rows_added);
            CHECK(rep->errors == res.rows_bad);
        }
        ob_report_free(rep);
    }
    ob_csv_plan_free(plan);
    ob_project_close(p);
}

static void csv_cuts(void)
{
    long size = (long)sizeof sample_csv - 1, len, full_added, bad, read, added, reported = 0, quoted_cuts = 0;
    int guessed;
    import_text(sample_csv, size, &full_added, &bad, &read, &guessed);
    CHECK(guessed && full_added == 5 && bad == 0);
    for (len = 0; len < size; len++) {
        import_text(sample_csv, len, &added, &bad, &read, &guessed);
        CHECK(added <= full_added);
        if (inside_quotes(sample_csv, len)) {
            quoted_cuts++;
            /* the cut-short record is reported: in the header, as an error; after it, as a bad row */
            obt_checks++;
            if (guessed && bad < 1) {
                obt_failures++;
                fprintf(stderr, "a CSV cut inside quotes at %ld wasn't reported\n", len);
            } else
                reported++;
        }
    }
    printf("           CSV cut short: %ld lengths; %ld cut inside quotes, all reported: %s\n", size, quoted_cuts,
           reported == quoted_cuts ? "yes" : "NO");
}

static void csv_flips(void)
{
    long size = (long)sizeof sample_csv - 1, r, n = rounds() * 3;
    char *copy = malloc((size_t)size);
    static const char specials[] = "\",\r\n\0\xFF\xC3;";
    for (r = 0; r < n; r++) {
        long added, bad, read;
        int k, guessed, flips = 1 + (int)(rnd() % 6);
        memcpy(copy, sample_csv, (size_t)size);
        for (k = 0; k < flips; k++) {
            long at = (long)(rnd() % (unsigned long)size);
            copy[at] = rnd() % 2 ? specials[rnd() % (sizeof specials - 1)] : (char)(rnd() & 0xFF);
        }
        import_text(copy, size, &added, &bad, &read, &guessed);
    }
    free(copy);
}

static void grid_text(void)
{
    static const char grid[] =
        "OpenBase query\t1\ndistinct\t0\nlimit\t5\nformats\tDMY\t.\t,\ntable\tCustomers\t\ntable\tOrders\to\n"
        "join\t0\tid\t1\tcustomer\tleft\ncolumn\t0\tName\t\t1\tasc\tgroup\ttext\n"
        "column\t1\tTotal\tSpent\t1\tnone\tsum\tcurrency\ncriteria\t1\t0\t> 1.50\ncriteria\t0\t1\t[Who?]\nend\n";
    long size = (long)sizeof grid - 1, len, r;
    char *buf = malloc((size_t)size + 1);
    ob_query *q;
    ob_error e;
    CHECK(ob_query_from_text(grid, &q, &e) == OB_OK);
    ob_query_free(q);
    for (len = 0; len < size; len++) {
        memcpy(buf, grid, (size_t)len);
        buf[len] = 0;
        if (ob_query_from_text(buf, &q, &e) == OB_OK) {
            ob_buf sql;
            ob_buf_init(&sql, 0);
            ob_query_sql(q, &sql, NULL, &e);
            ob_buf_free(&sql);
            ob_query_free(q);
            /* only cuts that lose nothing but the last newline may read */
            CHECK(len >= size - 1);
        }
    }
    for (r = 0; r < rounds() * 3; r++) {
        int k;
        memcpy(buf, grid, (size_t)size + 1);
        for (k = 0; k < 3; k++) {
            static const char picks[] = "\t\n\\-9 ";
            char c = rnd() % 2 ? picks[rnd() % 6] : (char)((rnd() & 0x7F) | 1);
            buf[rnd() % (unsigned long)size] = c;
        }
        if (ob_query_from_text(buf, &q, &e) == OB_OK) {
            ob_buf sql;
            ob_buf_init(&sql, 0);
            ob_query_sql(q, &sql, NULL, &e);
            ob_query_to_text(q, &sql, &e);
            ob_buf_free(&sql);
            ob_query_free(q);
        }
        obt_checks++;
    }
    free(buf);
}

void suite_fuzz(void)
{
    oadb_cuts();
    oadb_flips();
    csv_cuts();
    csv_flips();
    grid_text();
}
