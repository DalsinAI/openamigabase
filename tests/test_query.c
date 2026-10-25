/* OpenBase host tests: criteria, the query model's text, saved queries.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "obtest.h"

#include <stdlib.h>

static void crit(const char *typed, ob_type t, const ob_formats *f, const char *want)
{
    ob_buf b;
    ob_params pr;
    ob_error e;
    int rc;
    memset(&pr, 0, sizeof pr);
    memset(&e, 0, sizeof e);
    ob_buf_init(&b, 0);
    rc = ob_criterion_sql(typed, "x", t, f, &pr, &b, &e);
    obt_checks++;
    if (want && (rc != OB_OK || strcmp(ob_buf_text(&b), want) != 0)) {
        obt_failures++;
        fprintf(stderr, "criteria [%s] as %s: got [%s] (%s), want [%s]\n", typed, ob_type_name(t), ob_buf_text(&b), e.text, want);
    } else if (!want && rc == OB_OK) {
        obt_failures++;
        fprintf(stderr, "criteria [%s] as %s: want an error, got [%s]\n", typed, ob_type_name(t), ob_buf_text(&b));
    }
    ob_buf_free(&b);
}

static void criteria(void)
{
    ob_formats f, dmy;
    ob_buf b;
    ob_params pr;
    ob_error e;
    ob_formats_defaults(&f);
    dmy = f;
    dmy.date_order = OB_DATE_DMY;
    dmy.decimal = ',';
    dmy.thousands = '.';
    /* Design-OpenBase.md 5.2's table */
    crit("Leeds", OB_TYPE_TEXT, &f, "x = 'Leeds'");
    crit("> 10", OB_TYPE_INTEGER, &f, "x > 10");
    crit("<= 2026-01-01", OB_TYPE_DATE, &f, "x <= '2026-01-01'");
    crit("> Leeds", OB_TYPE_TEXT, &f, "x > 'Leeds'");
    crit("Smith*", OB_TYPE_TEXT, &f, "x LIKE 'Smith%'");
    crit("between 1 and 5", OB_TYPE_INTEGER, &f, "x BETWEEN 1 AND 5");
    crit("empty", OB_TYPE_TEXT, &f, "x IS NULL");
    crit("not empty", OB_TYPE_DATE, &f, "x IS NOT NULL");
    crit("[Which town?]", OB_TYPE_TEXT, &f, "x = ?1");
    /* and more */
    crit("  Leeds  ", OB_TYPE_TEXT, &f, "x = 'Leeds'");
    crit("O'Brien", OB_TYPE_TEXT, &f, "x = 'O''Brien'");
    crit("\"Leeds or York\"", OB_TYPE_TEXT, &f, "x = 'Leeds or York'");
    crit("'it''s'", OB_TYPE_TEXT, &f, "x = 'it''s'");
    crit("\"a*\"", OB_TYPE_TEXT, &f, "x = 'a*'");
    crit("not Leeds", OB_TYPE_TEXT, &f, "x <> 'Leeds'");
    crit("NOT Smith*", OB_TYPE_TEXT, &f, "x NOT LIKE 'Smith%'");
    crit("not between 1 and 5", OB_TYPE_INTEGER, &f, "x NOT BETWEEN 1 AND 5");
    crit("not > 3", OB_TYPE_INTEGER, &f, "NOT (x > 3)");
    crit("in (Leeds, York, 'Hull, East')", OB_TYPE_TEXT, &f, "x IN ('Leeds', 'York', 'Hull, East')");
    crit("not in (1, 2)", OB_TYPE_INTEGER, &f, "x NOT IN (1, 2)");
    crit("100%*", OB_TYPE_TEXT, &f, "x LIKE '100\\%%' ESCAPE '\\'");
    crit("A?C", OB_TYPE_TEXT, &f, "x LIKE 'A_C'");
    crit("like *son", OB_TYPE_TEXT, &f, "x LIKE '%son'");
    crit("2026-01*", OB_TYPE_DATE, &f, "x LIKE '2026-01%'");
    crit("!= 3", OB_TYPE_INTEGER, &f, "x <> 3");
    crit("> 10.50", OB_TYPE_CURRENCY, &f, "x > 1050");
    crit("\xC2\xA3" "5", OB_TYPE_CURRENCY, &f, "x = 500");
    crit("between 1.5 and 2,000", OB_TYPE_CURRENCY, &f, "x BETWEEN 150 AND 200000");
    crit("yes", OB_TYPE_YESNO, &f, "x = 1");
    crit("No", OB_TYPE_YESNO, &f, "x = 0");
    crit("1e3", OB_TYPE_NUMBER, &f, "x = 1e3");
    crit(">= 01/02/2026", OB_TYPE_DATE, &dmy, "x >= '2026-02-01'");
    crit("1,5", OB_TYPE_NUMBER, &dmy, "x = 1.5");
    crit("9:30", OB_TYPE_TIME, &f, "x = '09:30:00'");
    crit("< now", OB_TYPE_DATETIME, &f, "x < datetime('now', 'localtime')");
    crit("today", OB_TYPE_DATE, &f, "x = date('now', 'localtime')");
    crit("10", OB_TYPE_UNKNOWN, &f, "x = 10");
    crit("ten", OB_TYPE_UNKNOWN, &f, "x = 'ten'");
    crit("'10'", OB_TYPE_UNKNOWN, &f, "x = '10'");
    crit(">= [From?]", OB_TYPE_DATE, &f, "x >= ?1");
    /* refused */
    crit("abc", OB_TYPE_INTEGER, &f, NULL);
    crit("1.5", OB_TYPE_INTEGER, &f, NULL);
    crit("x", OB_TYPE_OBJECT, &f, NULL);
    crit("31/02/2026", OB_TYPE_DATE, &dmy, NULL);
    crit("between 1", OB_TYPE_INTEGER, &f, NULL);
    crit("in 1, 2", OB_TYPE_INTEGER, &f, NULL);
    crit("in ()", OB_TYPE_INTEGER, &f, NULL);
    crit("[unclosed", OB_TYPE_TEXT, &f, NULL);
    crit("[]", OB_TYPE_TEXT, &f, NULL);
    crit("", OB_TYPE_TEXT, &f, NULL);
    crit("not", OB_TYPE_TEXT, &f, NULL);
    crit("maybe", OB_TYPE_YESNO, &f, NULL);
    crit("> ", OB_TYPE_INTEGER, &f, NULL);
    /* one question asked once */
    memset(&pr, 0, sizeof pr);
    ob_buf_init(&b, 0);
    OK(ob_criterion_sql("between [From?] and [To?]", "d", OB_TYPE_DATE, &f, &pr, &b, E));
    OK(ob_criterion_sql("<> [from?]", "d", OB_TYPE_DATE, &f, &pr, &b, E));
    CHECK_STR(ob_buf_text(&b), "d BETWEEN ?1 AND ?2d <> ?1");
    CHECK(pr.count == 2 && !strcmp(pr.prompt[0], "From?") && !strcmp(pr.prompt[1], "To?"));
    ob_buf_reset(&b);
    CHECK(ob_criterion_sql("[Q]", "d", OB_TYPE_TEXT, &f, NULL, &b, &e) != OB_OK); /* no parameters here */
    ob_buf_free(&b);
}

static ob_query *sample_query(void)
{
    ob_error e;
    ob_query *q = ob_query_new();
    int c;
    ob_query_add_table(q, "Customers", NULL, &e);
    ob_query_add_table(q, "Orders", "o", &e);
    ob_query_add_join(q, 0, "id", 1, "customer", OB_JOIN_LEFT, &e);
    c = ob_query_add_column(q, 0, "Name", &e);
    q->columns[c].sort = OB_SORT_ASC;
    q->columns[c].total = OB_TOTAL_GROUP;
    c = ob_query_add_column(q, 0, "Town", &e);
    q->columns[c].total = OB_TOTAL_GROUP;
    ob_query_set_criteria(q, c, 0, "Leeds", &e);
    ob_query_set_criteria(q, c, 1, "York\twith a tab\nand a line \\ end", &e);
    c = ob_query_add_column(q, 1, "Total", &e);
    q->columns[c].total = OB_TOTAL_SUM;
    q->columns[c].type = OB_TYPE_CURRENCY;
    ob_query_set_name(q, c, "Spent", &e);
    c = ob_query_add_column(q, -1, "length(Customers.Name)", &e);
    q->columns[c].show = 0;
    q->columns[c].total = OB_TOTAL_WHERE;
    ob_query_set_criteria(q, c, 0, "> 2", &e);
    q->limit = 10;
    return q;
}

static void model_text(void)
{
    ob_query *q = sample_query(), *back = NULL;
    ob_buf a, b, s1, s2;
    ob_error e;
    char *t;
    ob_buf_init(&a, 0);
    ob_buf_init(&b, 0);
    ob_buf_init(&s1, 0);
    ob_buf_init(&s2, 0);
    OK(ob_query_to_text(q, &a, E));
    OK(ob_query_from_text(ob_buf_text(&a), &back, E));
    if (back) {
        OK(ob_query_to_text(back, &b, E));
        CHECK_STR(ob_buf_text(&b), ob_buf_text(&a));
        OK(ob_query_sql(q, &s1, NULL, E));
        OK(ob_query_sql(back, &s2, NULL, E));
        CHECK_STR(ob_buf_text(&s2), ob_buf_text(&s1));
        CHECK_STR(ob_buf_text(&s1),
                  "SELECT Customers.Name, Customers.Town, sum(o.Total) AS Spent FROM Customers LEFT JOIN Orders AS o ON "
                  "Customers.id = o.customer WHERE (Customers.Town = 'Leeds' AND (length(Customers.Name)) > 2) OR "
                  "Customers.Town = 'York\twith a tab\nand a line \\ end' GROUP BY Customers.Name, Customers.Town ORDER BY "
                  "Customers.Name LIMIT 10");
    }
    ob_query_free(back);
    /* damaged and newer texts */
    CHECK(ob_query_from_text("", &back, &e) == OB_ERR_DAMAGED);
    CHECK(ob_query_from_text("OpenBase query\t2\nend\n", &back, &e) == OB_ERR_NEWER);
    CHECK(ob_query_from_text("OpenBase query\t1\n", &back, &e) == OB_ERR_DAMAGED); /* no end */
    CHECK(ob_query_from_text("OpenBase query\t1\ncolumn\t3\tx\t\t1\tnone\tnone\ttext\nend\n", &back, &e) == OB_ERR_DAMAGED);
    CHECK(ob_query_from_text("OpenBase query\t1\ntable\tT\t\njoin\t0\ta\t0\tb\tinner\nend\n", &back, &e) == OB_ERR_DAMAGED);
    CHECK(ob_query_from_text("OpenBase query\t1\ntable\tT\t\ncriteria\t0\t0\tx\nend\n", &back, &e) == OB_ERR_DAMAGED);
    CHECK(ob_query_from_text("OpenBase query\t1\nbogus\nend\n", &back, &e) == OB_ERR_DAMAGED);
    CHECK(ob_query_from_text("OpenBase query\t1\ntable\tbad\\q\t\nend\n", &back, &e) == OB_ERR_DAMAGED);
    CHECK(ob_query_from_text("OpenBase query\t1\nlimit\t-1\nend\n", &back, &e) == OB_ERR_DAMAGED);
    t = malloc(300 * 1024);
    memset(t, 'a', 300 * 1024 - 1);
    t[300 * 1024 - 1] = 0;
    CHECK(ob_query_from_text(t, &back, &e) == OB_ERR_LIMIT);
    free(t);
    /* the generator refuses what it can't write */
    {
        ob_query *z = ob_query_new();
        CHECK(ob_query_sql(z, &s1, NULL, &e) == OB_ERR_INVALID);
        ob_query_add_table(z, "T", NULL, &e);
        ob_query_add_column(z, 0, "a", &e);
        z->columns[0].show = 0;
        CHECK(ob_query_sql(z, &s1, NULL, &e) == OB_ERR_INVALID);
        z->columns[0].show = 1;
        ob_query_add_table(z, "T", NULL, &e);
        CHECK(ob_query_sql(z, &s1, NULL, &e) == OB_ERR_INVALID && strstr(e.text, "twice"));
        ob_query_free(z);
    }
    CHECK(ob_query_add_join(q, 0, "a", 0, "b", OB_JOIN_INNER, &e) == OB_ERR_INVALID);
    CHECK(ob_query_add_column(q, 9, "a", &e) == -1);
    CHECK(ob_query_set_criteria(q, 0, OB_QUERY_ORS, "x", &e) == OB_ERR_INVALID);
    ob_buf_free(&a);
    ob_buf_free(&b);
    ob_buf_free(&s1);
    ob_buf_free(&s2);
    ob_query_free(q);
}

static void saved_queries(void)
{
    ob_project *p = obt_memory_project();
    ob_table *t;
    ob_error e;
    ob_query *q, *back;
    ob_field *f;
    ob_buf sql;
    char *s = NULL, **names;
    int n, c;
    t = ob_table_new("Customers", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    ob_table_add_field(t, "Town", OB_TYPE_TEXT, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    t = ob_table_new("Orders", 1, &e);
    f = ob_table_add_field(t, "customer", OB_TYPE_LOOKUP, &e);
    ob_strset(&f->lookup_table, "Customers", &e);
    ob_table_add_field(t, "Total", OB_TYPE_CURRENCY, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    OK(ob_exec(p, "INSERT INTO Customers(Name, Town) VALUES ('Ann', 'Leeds'), ('Bob', 'York');"
                  "INSERT INTO Orders(customer, Total) VALUES (1, 1050), (1, 99), (2, 100000)", E));
    /* types and joins from the project */
    q = ob_query_new();
    ob_query_add_table(q, "Orders", NULL, &e);
    ob_query_add_table(q, "Customers", NULL, &e);
    ob_query_add_column(q, 1, "Name", &e);
    c = ob_query_add_column(q, 0, "Total", &e);
    ob_query_set_criteria(q, c, 0, "> 10.00", &e);
    OK(ob_query_resolve(p, q, E));
    CHECK(q->columns[c].type == OB_TYPE_CURRENCY && q->columns[0].type == OB_TYPE_TEXT);
    OK(ob_query_auto_joins(p, q, E));
    CHECK(q->njoins == 1);
    ob_buf_init(&sql, 0);
    OK(ob_query_sql(q, &sql, NULL, E));
    CHECK_STR(ob_buf_text(&sql), "SELECT Customers.Name, Orders.Total FROM Orders JOIN Customers ON Customers.id = "
                                 "Orders.customer WHERE Orders.Total > 1000");
    OK(ob_query_save(p, "Big orders", q, NULL, 0, E));
    OK(ob_query_load(p, "big ORDERS", &back, &s, E));
    CHECK(back != NULL && s && !strcmp(s, ob_buf_text(&sql)));
    ob_query_free(back);
    free(s);
    s = NULL;
    /* SQL edited by hand: kept as SQL only */
    OK(ob_query_save(p, "Big orders", q, "SELECT Total FROM Orders WHERE Total > 1000", 0, E));
    OK(ob_query_load(p, "Big orders", &back, &s, E));
    CHECK(back == NULL && s && !strcmp(s, "SELECT Total FROM Orders WHERE Total > 1000"));
    free(s);
    s = NULL;
    /* a view, so other SQLite tools see it */
    OK(ob_query_save(p, "Rich", q, NULL, 1, E));
    CHECK(obt_long(p, "SELECT count(*) FROM Rich") == 2);
    OK(ob_query_save(p, "Rich", q, NULL, 1, E)); /* saved again: the view is remade */
    CHECK(ob_query_save(p, "Customers", q, NULL, 1, &e) == OB_ERR_EXISTS);
    ob_query_set_criteria(q, c, 1, "[How much?]", &e);
    CHECK(ob_query_save(p, "Asks", q, NULL, 1, &e) == OB_ERR_INVALID);
    OK(ob_query_save(p, "Asks", q, NULL, 0, E));
    CHECK(ob_query_save(p, "Bad", NULL, "DELETE FROM Orders", 0, &e) == OB_ERR_INVALID);
    CHECK(ob_query_save(p, "Two", NULL, "SELECT 1; SELECT 2", 0, &e) == OB_ERR_INVALID);
    CHECK(ob_query_save(p, "Wrong", NULL, "SELECT nothing FROM Orders", 0, &e) == OB_ERR_SQL);
    OK(ob_query_list(p, &names, &n, E));
    CHECK(n == 3 && !strcmp(names[0], "Asks") && !strcmp(names[1], "Big orders") && !strcmp(names[2], "Rich"));
    ob_names_free(names, n);
    OK(ob_query_delete(p, "Rich", E));
    CHECK(obt_long(p, "SELECT count(*) FROM sqlite_schema WHERE name = 'Rich'") == 0);
    CHECK(ob_query_delete(p, "Rich", &e) == OB_ERR_NOT_FOUND);
    CHECK(ob_query_load(p, "Rich", &back, &s, &e) == OB_ERR_NOT_FOUND);
    /* a field that isn't there */
    ob_query_add_column(q, 1, "Twon", &e);
    CHECK(ob_query_resolve(p, q, &e) == OB_ERR_NOT_FOUND && strstr(e.text, "Twon"));
    ob_buf_free(&sql);
    ob_query_free(q);
    ob_project_close(p);
}

void suite_query(void)
{
    criteria();
    model_text();
    saved_queries();
}
