/* OpenBase host tests: CSV reading, guessing, import, export, round trips.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "obtest.h"

#include <sqlite3.h>
#include <stdlib.h>

/* A stream giving one byte a call, to find mistakes at block edges */
typedef struct { const char *data; long size, pos; } drip;

static long drip_read(void *ctx, void *buf, long size)
{
    drip *d = (drip *)ctx;
    (void)size;
    if (d->pos >= d->size) return 0;
    *(char *)buf = d->data[d->pos++];
    return 1;
}

static long failing_read(void *ctx, void *buf, long size)
{
    long *calls = (long *)ctx;
    if ((*calls)++ > 0) return -1;
    memcpy(buf, "a,b\n1,2\n", size < 8 ? (size_t)size : 8);
    return size < 8 ? size : 8;
}

/* Every record as "f1|f2|..." lines; bad records as "BAD(why)" */
static char *read_all(const char *data, long size, const ob_csv_options *o, int one_byte)
{
    ob_mem_source m;
    drip d;
    ob_csv_reader *r;
    ob_error e;
    ob_buf out;
    int n, i, got;
    char **f;
    m.data = data;
    m.size = size;
    m.pos = 0;
    d.data = data;
    d.size = size;
    d.pos = 0;
    r = one_byte ? ob_csv_reader_new(drip_read, &d, o, &e) : ob_csv_reader_new(ob_mem_read, &m, o, &e);
    ob_buf_init(&out, 0);
    while (r && (got = ob_csv_next(r, &n, &f, &e)) != OB_CSV_END) {
        char line[24];
        snprintf(line, sizeof line, "%ld:", ob_csv_line(r));
        ob_buf_str(&out, line);
        if (got == OB_CSV_FAIL) {
            ob_buf_str(&out, "FAIL\n");
            break;
        }
        if (got == OB_CSV_BAD) {
            ob_buf_str(&out, "BAD(");
            ob_buf_str(&out, ob_csv_reader_why(r));
            ob_buf_str(&out, ")\n");
            continue;
        }
        for (i = 0; i < n; i++) {
            if (i) ob_buf_char(&out, '|');
            if (ob_csv_quoted(r, i)) ob_buf_char(&out, '^');
            ob_buf_str(&out, f[i]);
        }
        ob_buf_char(&out, '\n');
    }
    ob_csv_reader_free(r);
    {
        char *s = malloc(out.len + 1);
        memcpy(s, ob_buf_text(&out), out.len + 1);
        ob_buf_free(&out);
        return s;
    }
}

static void expect_read(const char *data, const ob_csv_options *o, const char *want)
{
    int one;
    for (one = 0; one < 2; one++) {
        char *got = read_all(data, (long)strlen(data), o, one);
        obt_checks++;
        if (strcmp(got, want) != 0) {
            obt_failures++;
            fprintf(stderr, "csv read%s of [%s]:\n got [%s]\nwant [%s]\n", one ? " (byte by byte)" : "", data, got, want);
        }
        free(got);
    }
}

static void reader(void)
{
    ob_csv_options o;
    char *got, big[300];
    ob_error e;
    ob_csv_defaults(&o);
    o.charset = OB_CHARSET_UTF8;
    expect_read("a,b,c\r\n1,2,3\r\n", &o, "1:a|b|c\n2:1|2|3\n");
    expect_read("a,b\n1,2", &o, "1:a|b\n2:1|2\n");
    expect_read("a,b\r1,2\r", &o, "1:a|b\n2:1|2\n");
    expect_read("\"x, y\",\"say \"\"hi\"\"\",\"\"\n", &o, "1:^x, y|^say \"hi\"|^\n");
    expect_read("\"two\r\nlines\",z\nnext,1\n", &o, "1:^two\r\nlines|z\n3:next|1\n");
    expect_read("a,,c,\n,\n", &o, "1:a||c|\n2:|\n");
    expect_read("\n\na\n\n\nb\n", &o, "3:a\n6:b\n");
    expect_read("\xEF\xBB\xBF" "a,b\n", &o, "1:a|b\n");
    expect_read("\"a\"  ,b\n", &o, "1:^a|b\n");
    expect_read("ab\"c,d\n", &o, "1:ab\"c|d\n");
    expect_read("\"a\"x,b\nc,d\n", &o, "1:BAD(there is text after a closing quote)\n2:c|d\n");
    expect_read("a,\"open\nstill open", &o, "1:BAD(a quoted value isn't closed before the end of the file)\n");
    expect_read("a,b\nc\0d,e\nf,g\n", &o, "1:a|b\n2:c\n"); /* strlen stops at the NUL: see below */
    {
        static const char nul[] = "a,b\nc\0d,e\nf,g\n";
        got = read_all(nul, (long)sizeof nul - 1, &o, 0);
        CHECK_STR(got, "1:a|b\n2:BAD(it holds a zero byte)\n3:f|g\n");
        free(got);
    }
    o.max_fields = 3;
    expect_read("1,2,3,4\n5,6\n", &o, "1:BAD(it has more than 3 values)\n2:5|6\n");
    o.max_fields = 0;
    o.max_record = 10;
    expect_read("short\n\"this one is much too long, and has, commas\nand a line\",x\nok\n", &o,
                "1:short\n2:BAD(it is longer than 10 bytes)\n4:ok\n");
    o.max_record = 0;
    o.separator = ';';
    expect_read("a;\"b;c\";d,e\n", &o, "1:a|^b;c|d,e\n");
    o.separator = '\t';
    o.quote = 0;
    expect_read("a\t\"b\"\tc\n", &o, "1:a|\"b\"|c\n");
    o.separator = ',';
    o.quote = '"';
    o.skip_rows = 2;
    expect_read("Title line\r\nanother\r\na,b\r\n", &o, "3:a|b\n");
    o.skip_rows = 0;
    /* character sets */
    o.charset = OB_CHARSET_LATIN1;
    expect_read("Caf\xE9,\xA3" "5\n", &o, "1:Caf\xC3\xA9|\xC2\xA3" "5\n");
    o.charset = OB_CHARSET_UTF8;
    expect_read("Caf\xE9\nok\n", &o, "1:BAD(it isn't UTF-8 text (try ISO-8859-1))\n2:ok\n");
    o.charset = OB_CHARSET_AUTO;
    {
        /* UTF-8 at the start; one record later in ISO-8859-1 */
        ob_buf in, want;
        int k;
        ob_buf_init(&in, 0);
        ob_buf_init(&want, 0);
        for (k = 0; k < 800; k++) {
            char line[32];
            ob_buf_str(&in, "Caf\xC3\xA9\n");
            snprintf(line, sizeof line, "%d:Caf\xC3\xA9\n", k + 1);
            ob_buf_str(&want, line);
        }
        ob_buf_str(&in, "na\xEFve\n");
        ob_buf_str(&want, "801:na\xC3\xAFve\n");
        expect_read(ob_buf_text(&in), &o, ob_buf_text(&want));
        ob_buf_free(&in);
        ob_buf_free(&want);
    }
    expect_read("Caf\xE9\n", &o, "1:Caf\xC3\xA9\n");
    /* the separator and quote must differ */
    o.quote = ',';
    CHECK(ob_csv_reader_new(ob_mem_read, NULL, &o, &e) == NULL && e.code == OB_ERR_INVALID);
    o.quote = '"';
    /* a read error stops it */
    {
        long calls = 0;
        ob_csv_reader *r = ob_csv_reader_new(failing_read, &calls, &o, &e);
        int n, got2 = 0, k;
        char **f;
        for (k = 0; k < 5; k++) {
            got2 = ob_csv_next(r, &n, &f, &e);
            if (got2 == OB_CSV_FAIL || got2 == OB_CSV_END) break;
        }
        CHECK(got2 == OB_CSV_FAIL && e.code == OB_ERR_IO);
        ob_csv_reader_free(r);
    }
    /* a record across many blocks */
    memset(big, 'x', sizeof big);
    big[sizeof big - 1] = 0;
    {
        ob_buf b;
        int i;
        ob_buf_init(&b, 0);
        for (i = 0; i < 40; i++) {
            ob_buf_str(&b, "\"");
            ob_buf_str(&b, big);
            ob_buf_str(&b, "\",");
        }
        ob_buf_str(&b, "end\n");
        got = read_all(b.data, (long)b.len, &o, 0);
        CHECK(strlen(got) == 2 + 40 * (sizeof big - 1 + 2) + 4);
        free(got);
        ob_buf_free(&b);
    }
}

static const char sample[] =
    "id,Name,Joined,Balance,Score,Visits,Active,Code,Phone,Notes,Empty,Name,,At\r\n"
    "1,Ann,25/10/2026,\"\xC2\xA3" "1,234.50\",3.5,12,yes,007,+44 113 496 0000,\"Likes \"\"tea\"\"\",,x,y,09:30\r\n"
    "2,Bob,01/02/2025,\xC2\xA3" "0.99,-2,0,no,010,+44 113 496 0001,\"two\r\nlines\",,x,y,17:05:10\r\n"
    "3,Cy,13/12/2024,-\xC2\xA3" "5,1e3,7,Yes,123,01134960002,,,x,y,\r\n";

static void guessing(void)
{
    ob_csv_options o;
    ob_csv_plan *plan = NULL;
    ob_mem_source m;
    ob_error e;
    ob_csv_defaults(&o);
    m.data = sample;
    m.size = (long)sizeof sample - 1;
    m.pos = 0;
    OK(ob_csv_guess(ob_mem_read, &m, &o, &plan, E));
    if (!plan) return;
    CHECK(plan->ncolumns == 14 && plan->rows_seen == 3);
    CHECK(plan->date_order == OB_DATE_DMY && plan->charset == OB_CHARSET_UTF8);
    CHECK(plan->key_column == 0 && plan->columns[0].type == OB_TYPE_COUNTER);
    CHECK(plan->columns[1].type == OB_TYPE_TEXT);
    CHECK(plan->columns[2].type == OB_TYPE_DATE);
    CHECK(plan->columns[3].type == OB_TYPE_CURRENCY);
    CHECK(plan->columns[4].type == OB_TYPE_NUMBER);
    CHECK(plan->columns[5].type == OB_TYPE_INTEGER);
    CHECK(plan->columns[6].type == OB_TYPE_YESNO);
    CHECK(plan->columns[7].type == OB_TYPE_TEXT); /* 007: a code */
    CHECK(plan->columns[8].type == OB_TYPE_TEXT);
    CHECK(plan->columns[9].type == OB_TYPE_MEMO);
    CHECK(plan->columns[10].type == OB_TYPE_TEXT && plan->columns[10].nonempty == 0);
    CHECK(plan->columns[13].type == OB_TYPE_TIME);
    CHECK_STR(plan->columns[11].name, "Name 2");
    CHECK_STR(plan->columns[12].name, "Field 13");
    ob_csv_plan_free(plan);
    /* month first, by evidence */
    {
        static const char mdy[] = "When\n02/13/2026\n12/01/2026\n";
        m.data = mdy;
        m.size = (long)sizeof mdy - 1;
        m.pos = 0;
        OK(ob_csv_guess(ob_mem_read, &m, &o, &plan, E));
        CHECK(plan && plan->date_order == OB_DATE_MDY && plan->columns[0].type == OB_TYPE_DATE);
        ob_csv_plan_free(plan);
    }
    /* a header that can't be read */
    {
        static const char bad[] = "\"open";
        m.data = bad;
        m.size = (long)sizeof bad - 1;
        m.pos = 0;
        CHECK(ob_csv_guess(ob_mem_read, &m, &o, &plan, &e) == OB_ERR_INVALID && !plan);
    }
}

static void importing(void)
{
    ob_project *p = obt_memory_project();
    ob_csv_options o;
    ob_csv_plan *plan = NULL;
    ob_csv_result res;
    ob_report *rep = ob_report_new(0);
    ob_mem_source m;
    ob_error e;
    static const char data[] =
        "Name,Age,Paid,Born\n"
        "Ann,34,\xC2\xA3" "10.50,2001-02-03\n"
        "Bob,thirty,\xC2\xA3" "1,1990-01-01\n"  /* line 3: not a number */
        "Cy,40\n"                                /* line 4: too few */
        "Di,22,\xC2\xA3" "3,2003-03-03,,\n"      /* trailing empty values: fine */
        "Ed,1,2,3,4,5\n"                         /* line 6: too many */
        "\"Fay\",50,\xC2\xA3" "0,1975-12-31\n"
        "Gus,60,\xC2\xA3" "1,\"1966-07-30\n";    /* line 8: cut short */
    ob_csv_defaults(&o);
    m.data = data;
    m.size = (long)sizeof data - 1;
    m.pos = 0;
    OK(ob_csv_guess(ob_mem_read, &m, &o, &plan, E));
    CHECK(plan && plan->columns[1].type == OB_TYPE_TEXT); /* "thirty" */
    plan->columns[1].type = OB_TYPE_INTEGER;              /* the wizard changes it */
    plan->columns[2].type = OB_TYPE_CURRENCY;
    plan->columns[3].type = OB_TYPE_DATE;
    m.pos = 0;
    OK(ob_csv_import(p, ob_mem_read, &m, &o, plan, "People", rep, &res, E));
    CHECK(res.rows_read == 7 && res.rows_added == 3 && res.rows_bad == 4);
    CHECK(rep->count == 4 && rep->errors == 4);
    if (rep->count == 4) {
        CHECK(rep->notes[0].line == 3 && strstr(rep->notes[0].text, "Age") && strstr(rep->notes[0].text, "thirty"));
        CHECK(rep->notes[1].line == 4 && strstr(rep->notes[1].text, "has 2 values"));
        CHECK(rep->notes[2].line == 6);
        CHECK(rep->notes[3].line == 8 && strstr(rep->notes[3].text, "isn't closed"));
    }
    {
        char *rows = obt_rows(p, "SELECT id, Name, Age, Paid, Born FROM People ORDER BY id");
        CHECK_STR(rows, "1\tAnn\t34\t1050\t2001-02-03\n2\tDi\t22\t300\t2003-03-03\n3\tFay\t50\t0\t1975-12-31\n");
        free(rows);
    }
    /* the same into the table that now exists, emptying bad values */
    o.bad_value = OB_CSV_EMPTY_VALUE;
    m.pos = 0;
    ob_report_free(rep);
    rep = ob_report_new(0);
    OK(ob_csv_import(p, ob_mem_read, &m, &o, plan, "people", rep, &res, E));
    CHECK(res.rows_added == 4 && res.values_emptied == 1 && rep->warnings == 1);
    CHECK(obt_long(p, "SELECT count(*) FROM People WHERE Name = 'Bob' AND Age IS NULL") == 1);
    ob_report_free(rep);
    ob_csv_plan_free(plan);

    /* rules of the table: required, unique; no header, by place */
    {
        ob_table *t = ob_table_new("Codes", 0, &e);
        ob_field *f = ob_table_add_field(t, "Code", OB_TYPE_TEXT, &e);
        static const char codes[] = "A1,first\nA2,second\nA1,again\n,none\n";
        f->required = 1;
        ob_table_add_field(t, "Text", OB_TYPE_TEXT, &e);
        t->nkey = 1;
        ob_strset(&t->key[0], "Code", &e);
        OK(ob_table_create(p, t, E));
        ob_table_free(t);
        o.header = 0;
        o.bad_value = OB_CSV_SKIP_ROW;
        m.data = codes;
        m.size = (long)sizeof codes - 1;
        m.pos = 0;
        OK(ob_csv_guess(ob_mem_read, &m, &o, &plan, E));
        rep = ob_report_new(0);
        m.pos = 0;
        OK(ob_csv_import(p, ob_mem_read, &m, &o, plan, "Codes", rep, &res, E));
        CHECK(res.rows_added == 2 && res.rows_bad == 2);
        CHECK(rep->count == 2 && strstr(rep->notes[0].text, "Another record already has this Code") &&
              strstr(rep->notes[1].text, "Code must be filled in"));
        ob_report_free(rep);
        ob_csv_plan_free(plan);
        o.header = 1;
    }
    /* a read error changes nothing, not even the new table */
    {
        long calls = 0;
        static const char ab[] = "a,b\n1,2\n";
        m.data = ab;
        m.size = (long)sizeof ab - 1;
        m.pos = 0;
        OK(ob_csv_guess(ob_mem_read, &m, &o, &plan, E));
        CHECK(ob_csv_import(p, failing_read, &calls, &o, plan, "Never", NULL, &res, &e) == OB_ERR_IO);
        CHECK(!ob_table_exists(p, "Never") && res.rows_added == 0);
        ob_csv_plan_free(plan);
    }
    ob_project_close(p);
}

static char *export_text(ob_project *p, const char *table, const ob_csv_options *o, ob_report *rep, long *rows)
{
    ob_buf b;
    ob_error e;
    char *s;
    ob_buf_init(&b, 16L * 1024 * 1024);
    memset(&e, 0, sizeof e);
    if (ob_csv_export_table(p, table, ob_buf_write, &b, o, rep, rows, &e) != OB_OK) fprintf(stderr, "export: %s\n", e.text);
    s = malloc(b.len + 1);
    memcpy(s, ob_buf_text(&b), b.len + 1);
    ob_buf_free(&b);
    return s;
}

static void exporting(void)
{
    ob_project *p = obt_memory_project();
    ob_table *t;
    ob_error e;
    ob_csv_options o;
    ob_report *rep = ob_report_new(0);
    char *text;
    long rows;
    t = ob_table_new("T", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    ob_table_add_field(t, "Price", OB_TYPE_CURRENCY, &e);
    ob_table_add_field(t, "Paid", OB_TYPE_YESNO, &e);
    ob_table_add_field(t, "When", OB_TYPE_DATE, &e);
    ob_table_add_field(t, "Size", OB_TYPE_NUMBER, &e);
    ob_table_add_field(t, "Pic", OB_TYPE_OBJECT, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    OK(ob_exec(p, "INSERT INTO T (Name, Price, Paid, \"When\", Size, Pic) VALUES"
                  " ('plain', 123450, 1, '2026-10-25', 2.5, x'0102'),"
                  " ('a, \"b\"', -5, 0, NULL, NULL, NULL),"
                  " ('', NULL, NULL, NULL, NULL, NULL),"
                  " (NULL, 0, 1, '2026-01-02', 1e20, NULL),"
                  " (' spaced ', 1, 0, NULL, NULL, NULL),"
                  " ('line\nbreak \xE2\x82\xAC', 2, 0, NULL, NULL, NULL)", E));
    ob_csv_defaults(&o);
    text = export_text(p, "T", &o, rep, &rows);
    CHECK(rows == 6);
    CHECK_STR(text, "id,Name,Price,Paid,When,Size\r\n"
                    "1,plain,1234.50,Yes,2026-10-25,2.5\r\n"
                    "2,\"a, \"\"b\"\"\",-0.05,No,,\r\n"
                    "3,\"\",,,,\r\n"
                    "4,,0.00,Yes,2026-01-02,1.0e+20\r\n"
                    "5,\" spaced \",0.01,No,,\r\n"
                    "6,\"line\nbreak \xE2\x82\xAC\",0.02,No,,\r\n");
    CHECK(rep->count == 1 && strstr(rep->notes[0].text, "Pic"));
    free(text);
    /* ISO-8859-1, semicolons, day first, decimal comma, LF */
    o.charset = OB_CHARSET_LATIN1;
    o.separator = ';';
    o.crlf = 0;
    o.header = 0;
    o.formats.date_order = OB_DATE_DMY;
    o.formats.decimal = ',';
    o.formats.thousands = '.';
    ob_report_free(rep);
    rep = ob_report_new(0);
    text = export_text(p, "T", &o, rep, &rows);
    CHECK_STR(text, "1;plain;1234,50;Yes;25/10/2026;2,5\n"
                    "2;\"a, \"\"b\"\"\";-0,05;No;;\n"
                    "3;\"\";;;;\n"
                    "4;;0,00;Yes;02/01/2026;1,0e+20\n"
                    "5;\" spaced \";0,01;No;;\n"
                    "6;\"line\nbreak ?\";0,02;No;;\n");
    CHECK(rep->warnings == 2); /* Pic, and the euro sign */
    free(text);
    /* any SELECT; not other statements */
    {
        ob_buf b;
        ob_buf_init(&b, 0);
        ob_csv_defaults(&o);
        OK(ob_csv_export_sql(p, "SELECT Name, length(Name) FROM T WHERE id < 3", NULL, 0, ob_buf_write, &b, &o, NULL, &rows, E));
        CHECK_STR(ob_buf_text(&b), "Name,length(Name)\r\nplain,5\r\n\"a, \"\"b\"\"\",6\r\n");
        CHECK(ob_csv_export_sql(p, "DELETE FROM T", NULL, 0, ob_buf_write, &b, &o, NULL, &rows, &e) == OB_ERR_INVALID);
        CHECK(obt_long(p, "SELECT count(*) FROM T") == 6);
        ob_buf_free(&b);
    }
    ob_report_free(rep);
    ob_project_close(p);
}

#define OB_UNUSED_E(x) ((void)(x))

/* Export a table, import it back into a new one with the same types, and
 * compare every row */
static void round_trip(ob_project *p, const char *table, const ob_csv_options *o, int keep_types)
{
    ob_csv_plan *plan = NULL;
    ob_csv_result res;
    ob_mem_source m;
    ob_table *t;
    char copy[80], sql[512], *text;
    long rows = 0;
    int i;
    snprintf(copy, sizeof copy, "%s copy %d", table, (int)o->separator);
    text = export_text(p, table, o, NULL, &rows);
    m.data = text;
    m.size = (long)strlen(text);
    m.pos = 0;
    OK(ob_csv_guess(ob_mem_read, &m, o, &plan, E));
    OK(ob_table_read(p, table, &t, E));
    if (!plan || !t) {
        ob_csv_plan_free(plan);
        ob_table_free(t);
        free(text);
        return;
    }
    CHECK(plan->ncolumns == t->nfields);
    for (i = 0; i < plan->ncolumns && i < t->nfields; i++) {
        if (keep_types) plan->columns[i].type = t->fields[i].type;
        else if (t->fields[i].type == OB_TYPE_CURRENCY && plan->columns[i].type == OB_TYPE_NUMBER)
            plan->columns[i].type = OB_TYPE_CURRENCY; /* money is written without its sign: the wizard says Currency */
        else if (plan->columns[i].type != t->fields[i].type) {
            obt_failures++;
            fprintf(stderr, "round trip of %s: %s guessed as %s, was %s\n", table, t->fields[i].name,
                    ob_type_name(plan->columns[i].type), ob_type_name(t->fields[i].type));
        }
    }
    m.pos = 0;
    OK(ob_csv_import(p, ob_mem_read, &m, o, plan, copy, NULL, &res, E));
    CHECK(res.rows_added == rows && res.rows_bad == 0);
    snprintf(sql, sizeof sql, "SELECT (SELECT count(*) FROM (SELECT * FROM \"%s\" EXCEPT SELECT * FROM \"%s\")) +"
                              " (SELECT count(*) FROM (SELECT * FROM \"%s\" EXCEPT SELECT * FROM \"%s\"))",
             table, copy, copy, table);
    CHECK(obt_long(p, sql) == 0);
    snprintf(sql, sizeof sql, "SELECT count(*) FROM \"%s\" a JOIN \"%s\" b ON a.id = b.id WHERE"
                              " a.Name IS NOT b.Name OR typeof(a.Name) <> typeof(b.Name)", table, copy);
    if (ob_table_field(t, "Name")) CHECK(obt_long(p, sql) == 0);
    ob_table_free(t);
    ob_csv_plan_free(plan);
    free(text);
}

static void round_trips(void)
{
    ob_project *p = obt_memory_project();
    ob_table *t;
    ob_error e;
    OB_UNUSED_E(e);
    ob_csv_options o;
    ob_buf longtext;
    sqlite3_stmt *st;
    int i;
    t = ob_table_new("All", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    ob_table_add_field(t, "Notes", OB_TYPE_MEMO, &e);
    ob_table_add_field(t, "Count", OB_TYPE_INTEGER, &e);
    ob_table_add_field(t, "Size", OB_TYPE_NUMBER, &e);
    ob_table_add_field(t, "Price", OB_TYPE_CURRENCY, &e);
    ob_table_add_field(t, "Day", OB_TYPE_DATE, &e);
    ob_table_add_field(t, "At", OB_TYPE_TIME, &e);
    ob_table_add_field(t, "Stamp", OB_TYPE_DATETIME, &e);
    ob_table_add_field(t, "Done", OB_TYPE_YESNO, &e);
    ob_strset(&ob_table_add_field(t, "Colour", OB_TYPE_CHOICE, &e)->choices, "Red\nGreen", &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    ob_buf_init(&longtext, 0);
    for (i = 0; i < 1000; i++) ob_buf_str(&longtext, "0123456789");
    sqlite3_prepare_v2(ob_project_db(p), "INSERT INTO \"All\" (Name, Notes, Count, Size, Price, Day, At, Stamp, Done, Colour)"
                                         " VALUES (?,?,?,?,?,?,?,?,?,?)", -1, &st, NULL);
    for (i = 0; i < 60; i++) {
        static const char *const names[] = { "plain", "", NULL, "comma, inside", "\"quoted\"", " spaces ",
                                             "line\r\nbreak", "caf\xC3\xA9 \xF0\x9F\x98\x80", "semi;colon", "tab\there" };
        static const char *const sizes[] = { "3.25", "0.1", "-1234.5", "1e-7", "0", "123456789.125" };
        char day[16], at[16], stamp[40];
        sqlite3_reset(st);
        if (names[i % 10]) sqlite3_bind_text(st, 1, names[i % 10], -1, SQLITE_STATIC);
        else sqlite3_bind_null(st, 1);
        if (i % 9 == 0) sqlite3_bind_text(st, 2, ob_buf_text(&longtext), -1, SQLITE_STATIC);
        else if (i % 3) sqlite3_bind_text(st, 2, "memo\nwith \"lines\"", -1, SQLITE_STATIC);
        else sqlite3_bind_null(st, 2);
        sqlite3_bind_int64(st, 3, i == 1 ? 9223372036854775807LL : i == 2 ? (-9223372036854775807LL - 1) : i * 1000 - 7);
        sqlite3_bind_text(st, 4, sizes[i % 6], -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 5, (long long)(i - 30) * 12345);
        snprintf(day, sizeof day, "20%02d-%02d-%02d", 10 + i % 20, 1 + i % 12, 1 + i % 28);
        snprintf(at, sizeof at, "%02d:%02d:%02d", i % 24, i % 60, (i * 7) % 60);
        snprintf(stamp, sizeof stamp, "%s %s", day, at);
        if (i % 4) sqlite3_bind_text(st, 6, day, -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(st, 6);
        sqlite3_bind_text(st, 7, at, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 8, stamp, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 9, i % 2);
        sqlite3_bind_text(st, 10, i % 2 ? "Red" : "Green", -1, SQLITE_STATIC);
        if (sqlite3_step(st) != SQLITE_DONE) fprintf(stderr, "round trip fill: %s\n", sqlite3_errmsg(ob_project_db(p)));
    }
    sqlite3_finalize(st);
    ob_buf_free(&longtext);
    ob_csv_defaults(&o);
    round_trip(p, "All", &o, 1);
    o.separator = '\t';
    o.crlf = 0;
    o.quote_all = 1;
    round_trip(p, "All", &o, 1);

    /* ISO-8859-1, semicolons, day first, decimal comma, LF */
    t = ob_table_new("Latin", 1, &e);
    ob_table_add_field(t, "Name", OB_TYPE_TEXT, &e);
    ob_table_add_field(t, "Size", OB_TYPE_NUMBER, &e);
    ob_table_add_field(t, "Price", OB_TYPE_CURRENCY, &e);
    ob_table_add_field(t, "Day", OB_TYPE_DATE, &e);
    ob_table_add_field(t, "Done", OB_TYPE_YESNO, &e);
    OK(ob_table_create(p, t, E));
    ob_table_free(t);
    OK(ob_exec(p, "INSERT INTO Latin (Name, Size, Price, Day, Done) VALUES"
                  " ('Stra\xC3\x9F" "e; \xC2\xA3" "5', 2.5, 123456, '2026-10-25', 1),"
                  " ('na\xC3\xAFve', -0.125, -1, '1999-12-31', 0),"
                  " ('', NULL, 0, NULL, NULL), (NULL, 1000000.5, 99, '2000-02-29', 1)", E));
    ob_csv_defaults(&o);
    o.charset = OB_CHARSET_LATIN1;
    o.separator = ';';
    o.crlf = 0;
    o.formats.date_order = OB_DATE_DMY;
    o.formats.decimal = ',';
    o.formats.thousands = '.';
    round_trip(p, "Latin", &o, 0); /* the guess finds the types too */
    ob_csv_defaults(&o);
    round_trip(p, "Latin", &o, 0);
    ob_project_close(p);
}

void suite_csv(void)
{
    reader();
    guessing();
    importing();
    exporting();
    round_trips();
}
