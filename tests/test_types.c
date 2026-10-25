/* OpenBase host tests: field types and typed values.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "obtest.h"

static void integers(void)
{
    ob_formats f;
    long long v;
    ob_formats_defaults(&f);
    CHECK(ob_parse_integer("42", &f, &v) == OB_OK && v == 42);
    CHECK(ob_parse_integer(" -7 ", &f, &v) == OB_OK && v == -7);
    CHECK(ob_parse_integer("1,234,567", &f, &v) == OB_OK && v == 1234567);
    CHECK(ob_parse_integer("1,23", &f, &v) != OB_OK);
    CHECK(ob_parse_integer("1234,567", &f, &v) != OB_OK);
    CHECK(ob_parse_integer("12.0", &f, &v) != OB_OK);
    CHECK(ob_parse_integer("", &f, &v) != OB_OK);
    CHECK(ob_parse_integer("-", &f, &v) != OB_OK);
    CHECK(ob_parse_integer("9223372036854775807", &f, &v) == OB_OK && v == 9223372036854775807LL);
    CHECK(ob_parse_integer("-9223372036854775808", &f, &v) == OB_OK && v == (-9223372036854775807LL - 1));
    CHECK(ob_parse_integer("9223372036854775808", &f, &v) != OB_OK);
    CHECK(ob_parse_integer("99999999999999999999999", &f, &v) != OB_OK);
    f.thousands = '.';
    f.decimal = ',';
    CHECK(ob_parse_integer("1.234", &f, &v) == OB_OK && v == 1234);
}

static void numbers(void)
{
    ob_formats f;
    char out[64];
    ob_formats_defaults(&f);
    CHECK(ob_parse_number("3.25", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "3.25");
    CHECK(ob_parse_number("-0003.500", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "-3.5");
    CHECK(ob_parse_number(".5", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "0.5");
    CHECK(ob_parse_number("1,234.5", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "1234.5");
    CHECK(ob_parse_number("6.02e23", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "6.02e23");
    CHECK(ob_parse_number("1e-5", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "1e-5");
    CHECK(ob_parse_number("-0", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "0");
    CHECK(ob_parse_number("abc", &f, out, sizeof out) != OB_OK);
    CHECK(ob_parse_number("1.2.3", &f, out, sizeof out) != OB_OK);
    CHECK(ob_parse_number("inf", &f, out, sizeof out) != OB_OK);
    CHECK(ob_parse_number("1e", &f, out, sizeof out) != OB_OK);
    f.decimal = ',';
    f.thousands = '.';
    CHECK(ob_parse_number("1.234,5", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "1234.5");
    CHECK(ob_parse_number("3,25", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "3.25");
}

static void money(void)
{
    ob_formats f;
    long long p;
    char out[48];
    ob_formats_defaults(&f);
    CHECK(ob_parse_currency("1,234.50", &f, &p) == OB_OK && p == 123450);
    CHECK(ob_parse_currency("\xC2\xA3" "3", &f, &p) == OB_OK && p == 300);
    CHECK(ob_parse_currency("-\xC2\xA3" "3.05", &f, &p) == OB_OK && p == -305);
    CHECK(ob_parse_currency("\xC2\xA3-3.05", &f, &p) == OB_OK && p == -305);
    CHECK(ob_parse_currency("(2.10)", &f, &p) == OB_OK && p == -210);
    CHECK(ob_parse_currency("12.50-", &f, &p) == OB_OK && p == -1250);
    CHECK(ob_parse_currency("$0.5", &f, &p) == OB_OK && p == 50);
    CHECK(ob_parse_currency("1.000", &f, &p) == OB_OK && p == 100);
    CHECK(ob_parse_currency("1.005", &f, &p) != OB_OK); /* a part of a penny */
    CHECK(ob_parse_currency("1-2", &f, &p) != OB_OK);
    CHECK(ob_parse_currency("--3", &f, &p) != OB_OK);
    CHECK(ob_parse_currency("(2.10", &f, &p) != OB_OK);
    CHECK(ob_parse_currency("", &f, &p) != OB_OK);
    CHECK(ob_parse_currency("1e3", &f, &p) != OB_OK);
    CHECK(ob_format_currency(123450, &f, 1, out, sizeof out) == OB_OK);
    CHECK_STR(out, "1,234.50");
    CHECK(ob_format_currency(-5, &f, 1, out, sizeof out) == OB_OK);
    CHECK_STR(out, "-0.05");
    CHECK(ob_format_currency(123456789012LL, &f, 0, out, sizeof out) == OB_OK);
    CHECK_STR(out, "1234567890.12");
    f.decimal = ',';
    f.thousands = '.';
    CHECK(ob_parse_currency("4,5 \xE2\x82\xAC", &f, &p) == OB_OK && p == 450);
    CHECK(ob_format_currency(123450, &f, 1, out, sizeof out) == OB_OK);
    CHECK_STR(out, "1.234,50");
}

static void dates(void)
{
    ob_formats f;
    char d[11], t[9], dt[20], out[32];
    ob_formats_defaults(&f);
    CHECK(ob_parse_date("2026-10-25", &f, d) == OB_OK);
    CHECK_STR(d, "2026-10-25");
    f.date_order = OB_DATE_DMY;
    CHECK(ob_parse_date("25/10/2026", &f, d) == OB_OK);
    CHECK_STR(d, "2026-10-25");
    CHECK(ob_parse_date("1.2.26", &f, d) == OB_OK);
    CHECK_STR(d, "2026-02-01");
    CHECK(ob_parse_date("1-2-99", &f, d) == OB_OK);
    CHECK_STR(d, "1999-02-01");
    CHECK(ob_parse_date("2026-10-25", &f, d) == OB_OK); /* ISO in any order */
    CHECK_STR(d, "2026-10-25");
    CHECK(ob_parse_date("29/02/2024", &f, d) == OB_OK);
    CHECK(ob_parse_date("29/02/2026", &f, d) != OB_OK);
    CHECK(ob_parse_date("31/04/2026", &f, d) != OB_OK);
    CHECK(ob_parse_date("25 Dec 2026", &f, d) == OB_OK);
    CHECK_STR(d, "2026-12-25");
    CHECK(ob_parse_date("Dec 25, 2026", &f, d) == OB_OK);
    CHECK_STR(d, "2026-12-25");
    CHECK(ob_parse_date("1st September 2026", &f, d) == OB_OK);
    CHECK_STR(d, "2026-09-01");
    CHECK(ob_parse_date("25-Sept-26", &f, d) == OB_OK);
    CHECK_STR(d, "2026-09-25");
    CHECK(ob_parse_date("25 Foo 2026", &f, d) != OB_OK);
    CHECK(ob_parse_date("2026-13-01", &f, d) != OB_OK);
    CHECK(ob_parse_date("25/10/202", &f, d) != OB_OK);
    CHECK(ob_parse_date("25/10/2026x", &f, d) != OB_OK);
    CHECK(ob_parse_date("", &f, d) != OB_OK);
    f.date_order = OB_DATE_MDY;
    CHECK(ob_parse_date("10/25/2026", &f, d) == OB_OK);
    CHECK_STR(d, "2026-10-25");
    CHECK(ob_parse_date("25/10/2026", &f, d) != OB_OK);
    f.date_order = OB_DATE_AUTO;
    CHECK(ob_parse_date("25/10/2026", &f, d) == OB_OK);
    CHECK_STR(d, "2026-10-25");
    CHECK(ob_parse_date("10/25/2026", &f, d) == OB_OK);
    CHECK_STR(d, "2026-10-25");
    CHECK(ob_date_order_evidence("13/01/2026") == OB_DATE_DMY);
    CHECK(ob_date_order_evidence("01/13/2026") == OB_DATE_MDY);
    CHECK(ob_date_order_evidence("01/02/2026") == -1);
    CHECK(ob_parse_time("9:05", t) == OB_OK);
    CHECK_STR(t, "09:05:00");
    CHECK(ob_parse_time("23:59:59", t) == OB_OK);
    CHECK(ob_parse_time("12:30 am", t) == OB_OK);
    CHECK_STR(t, "00:30:00");
    CHECK(ob_parse_time("1:15 p.m.", t) == OB_OK);
    CHECK_STR(t, "13:15:00");
    CHECK(ob_parse_time("10:00:00.000", t) == OB_OK);
    CHECK(ob_parse_time("10:00:00.5", t) != OB_OK);
    CHECK(ob_parse_time("24:00", t) != OB_OK);
    CHECK(ob_parse_time("13:00 pm", t) != OB_OK);
    CHECK(ob_parse_time("9", t) != OB_OK);
    f.date_order = OB_DATE_DMY;
    CHECK(ob_parse_datetime("25/10/2026 14:30", &f, dt) == OB_OK);
    CHECK_STR(dt, "2026-10-25 14:30:00");
    CHECK(ob_parse_datetime("2026-10-25T14:30:05", &f, dt) == OB_OK);
    CHECK_STR(dt, "2026-10-25 14:30:05");
    CHECK(ob_parse_datetime("2026-10-25", &f, dt) == OB_OK);
    CHECK_STR(dt, "2026-10-25 00:00:00");
    CHECK(ob_parse_datetime("2026-10-25 25:00", &f, dt) != OB_OK);
    CHECK(ob_format_date("2026-10-25", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "25/10/2026");
    CHECK(ob_format_date("2026-10-25 14:30:00", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "25/10/2026 14:30:00");
    f.date_order = OB_DATE_MDY;
    f.date_sep = '-';
    CHECK(ob_format_date("2026-10-25", &f, out, sizeof out) == OB_OK);
    CHECK_STR(out, "10-25-2026");
}

static void yes_no(void)
{
    ob_formats f;
    int v;
    ob_formats_defaults(&f);
    CHECK(ob_parse_yesno("Yes", &f, &v) == OB_OK && v == 1);
    CHECK(ob_parse_yesno(" false ", &f, &v) == OB_OK && v == 0);
    CHECK(ob_parse_yesno("-1", &f, &v) == OB_OK && v == 1);
    CHECK(ob_parse_yesno("maybe", &f, &v) != OB_OK);
    strcpy(f.yes, "Ja");
    strcpy(f.no, "Nein");
    CHECK(ob_parse_yesno("ja", &f, &v) == OB_OK && v == 1);
    CHECK(ob_parse_yesno("NEIN", &f, &v) == OB_OK && v == 0);
}

static void names_and_defaults(void)
{
    ob_type t;
    ob_buf b;
    ob_formats f;
    ob_error e;
    int i;
    ob_formats_defaults(&f);
    for (i = 0; i < OB_TYPE_COUNT; i++) {
        CHECK(ob_type_from_key(ob_type_key((ob_type)i), &t) == OB_OK && t == (ob_type)i);
        CHECK(ob_type_from_key(ob_type_name((ob_type)i), &t) == OB_OK && t == (ob_type)i);
    }
    CHECK(ob_type_from_key("nonsense", &t) != OB_OK);
    CHECK_STR(ob_type_sql(OB_TYPE_CURRENCY), "INTEGER");
    CHECK_STR(ob_type_sql(OB_TYPE_NUMBER), "REAL");
    CHECK_STR(ob_type_sql(OB_TYPE_OBJECT), "BLOB");
    ob_buf_init(&b, 0);
    CHECK(ob_default_sql(OB_TYPE_TEXT, "O'Brien", &f, &b, &e) == OB_OK);
    CHECK_STR(ob_buf_text(&b), "'O''Brien'");
    CHECK(ob_default_sql(OB_TYPE_CURRENCY, "2.50", &f, &b, &e) == OB_OK);
    CHECK_STR(ob_buf_text(&b), "250");
    CHECK(ob_default_sql(OB_TYPE_DATE, "today", &f, &b, &e) == OB_OK);
    CHECK_STR(ob_buf_text(&b), "(date('now','localtime'))");
    CHECK(ob_default_sql(OB_TYPE_YESNO, "yes", &f, &b, &e) == OB_OK);
    CHECK_STR(ob_buf_text(&b), "1");
    CHECK(ob_default_sql(OB_TYPE_NUMBER, "1,5", &f, &b, &e) != OB_OK);
    CHECK(ob_default_sql(OB_TYPE_COUNTER, "1", &f, &b, &e) != OB_OK);
    CHECK(ob_default_sql(OB_TYPE_TEXT, "", &f, &b, &e) == OB_OK && b.len == 0);
    ob_buf_free(&b);
}

static void buffers(void)
{
    ob_buf b;
    ob_buf_init(&b, 8);
    CHECK(ob_buf_str(&b, "12345678") == OB_OK);
    CHECK(ob_buf_char(&b, '9') == OB_ERR_LIMIT);
    CHECK(ob_buf_str(&b, "x") != OB_OK); /* stays failed */
    ob_buf_free(&b);
    ob_buf_init(&b, 0);
    CHECK(ob_buf_long(&b, -9223372036854775807LL - 1) == OB_OK);
    CHECK_STR(ob_buf_text(&b), "-9223372036854775808");
    ob_buf_free(&b);
}

void suite_types(void)
{
    integers();
    numbers();
    money();
    dates();
    yes_no();
    names_and_defaults();
    buffers();
}
