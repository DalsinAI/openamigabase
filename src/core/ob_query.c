/* OpenBase core: the query model and its SQL generator
 * (Design-OpenBase.md, 5). Grids in, SQL out; the grid is kept as text
 * beside its SQL in oadb_queries, and only SQL OpenBase wrote is read
 * back into a grid.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#include "ob_internal.h"

#include <stdio.h>
#include <string.h>

/* ---- The model ------------------------------------------------------------ */

ob_query *ob_query_new(void)
{
    ob_query *q = (ob_query *)ob_zalloc(sizeof *q);
    if (q) ob_formats_defaults(&q->formats);
    return q;
}

static void column_clear(ob_qcolumn *c)
{
    int r;
    ob_free(c->field);
    ob_free(c->name);
    for (r = 0; r < OB_QUERY_ORS; r++) ob_free(c->criteria[r]);
    memset(c, 0, sizeof *c);
}

void ob_query_free(ob_query *q)
{
    int i;
    if (!q) return;
    for (i = 0; i < q->ntables; i++) {
        ob_free(q->tables[i].name);
        ob_free(q->tables[i].alias);
    }
    for (i = 0; i < q->njoins; i++) {
        ob_free(q->joins[i].left_field);
        ob_free(q->joins[i].right_field);
    }
    for (i = 0; i < q->ncolumns; i++) column_clear(&q->columns[i]);
    ob_free(q);
}

int ob_query_add_table(ob_query *q, const char *name, const char *alias, ob_error *err)
{
    ob_qtable *t;
    int rc;
    if (q->ntables >= OB_QUERY_TABLES) return ob_fail(err, OB_ERR_LIMIT, "A query can use %d tables at most.", OB_QUERY_TABLES);
    if ((rc = ob_name_check(name, "table", err))) return rc;
    if (alias && *alias && (rc = ob_name_check(alias, "table's other name", err))) return rc;
    t = &q->tables[q->ntables];
    memset(t, 0, sizeof *t);
    if (ob_strset(&t->name, name, err) || (alias && *alias && ob_strset(&t->alias, alias, err))) {
        ob_free(t->name);
        t->name = NULL;
        return OB_ERR_NOMEM;
    }
    return q->ntables++, OB_OK;
}

int ob_query_add_join(ob_query *q, int left, const char *left_field, int right, const char *right_field, ob_jointype type,
                      ob_error *err)
{
    ob_qjoin *j;
    if (q->njoins >= OB_QUERY_TABLES * 2) return ob_fail(err, OB_ERR_LIMIT, "The query has too many joins.");
    if (left < 0 || left >= q->ntables || right < 0 || right >= q->ntables || left == right)
        return ob_fail(err, OB_ERR_INVALID, "A join needs two of the query's tables.");
    if (!left_field || !*left_field || !right_field || !*right_field)
        return ob_fail(err, OB_ERR_INVALID, "A join needs a field in each table.");
    if ((unsigned)type > OB_JOIN_RIGHT) return ob_fail(err, OB_ERR_INVALID, "That isn't a kind of join.");
    j = &q->joins[q->njoins];
    memset(j, 0, sizeof *j);
    j->left = left;
    j->right = right;
    j->type = type;
    if (ob_strset(&j->left_field, left_field, err) || ob_strset(&j->right_field, right_field, err)) {
        ob_free(j->left_field);
        ob_free(j->right_field);
        j->left_field = j->right_field = NULL;
        return OB_ERR_NOMEM;
    }
    return q->njoins++, OB_OK;
}

int ob_query_add_column(ob_query *q, int table, const char *field, ob_error *err)
{
    ob_qcolumn *c;
    if (q->ncolumns >= OB_QUERY_COLUMNS) return ob_fail(err, OB_ERR_LIMIT, "A query can have %d columns at most.", OB_QUERY_COLUMNS), -1;
    if (table < -1 || table >= q->ntables) return ob_fail(err, OB_ERR_INVALID, "The column's table isn't in the query."), -1;
    if (!field || !*field) return ob_fail(err, OB_ERR_INVALID, "The column needs a field or an expression."), -1;
    if (strlen(field) > 4096) return ob_fail(err, OB_ERR_LIMIT, "The expression is too long."), -1;
    c = &q->columns[q->ncolumns];
    memset(c, 0, sizeof *c);
    c->table = table;
    c->show = 1;
    c->type = table < 0 ? OB_TYPE_UNKNOWN : OB_TYPE_TEXT;
    if (ob_strset(&c->field, field, err)) return -1;
    return q->ncolumns++;
}

int ob_query_set_criteria(ob_query *q, int column, int row, const char *text, ob_error *err)
{
    if (column < 0 || column >= q->ncolumns || row < 0 || row >= OB_QUERY_ORS)
        return ob_fail(err, OB_ERR_INVALID, "There is no such criteria cell.");
    if (text && strlen(text) > 1024) return ob_fail(err, OB_ERR_LIMIT, "The criteria are too long.");
    return ob_strset(&q->columns[column].criteria[row], text && *text ? text : NULL, err);
}

int ob_query_set_name(ob_query *q, int column, const char *name, ob_error *err)
{
    int rc;
    if (column < 0 || column >= q->ncolumns) return ob_fail(err, OB_ERR_INVALID, "There is no such column.");
    if (name && *name && (rc = ob_name_check(name, "column", err))) return rc;
    return ob_strset(&q->columns[column].name, name && *name ? name : NULL, err);
}

/* ---- Criteria ---------------------------------------------------------- */

static int ci_word(const char *s, const char *w)
{
    size_t n = strlen(w);
    return ob_ncasecmp(s, w, n) == 0 && (s[n] == 0 || ob_is_space((unsigned char)s[n]) || s[n] == '(');
}

static int add_param(ob_params *pr, const char *prompt, size_t n, ob_error *err)
{
    int i;
    char tmp[80];
    if (n >= sizeof tmp) n = ob_utf8_cut(prompt, sizeof tmp - 1) < n ? ob_utf8_cut(prompt, sizeof tmp - 1) : n;
    memcpy(tmp, prompt, n);
    tmp[n] = 0;
    if (!pr) return ob_fail(err, OB_ERR_INVALID, "Parameters can't be used here."), -1;
    for (i = 0; i < pr->count; i++)
        if (ob_casecmp(pr->prompt[i], tmp) == 0) return i + 1;
    if (pr->count >= OB_QUERY_PARAMS) return ob_fail(err, OB_ERR_LIMIT, "A query can ask %d questions at most.", OB_QUERY_PARAMS), -1;
    memcpy(pr->prompt[pr->count], tmp, n + 1);
    return ++pr->count;
}

/* Text with * and ? as a LIKE pattern */
static int like_pattern(ob_buf *b, const char *s, size_t n)
{
    ob_buf pat;
    size_t i;
    int escaped = 0, rc;
    ob_buf_init(&pat, 0);
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '*') ob_buf_char(&pat, '%');
        else if (c == '?') ob_buf_char(&pat, '_');
        else if (c == '%' || c == '_' || c == '\\') {
            ob_buf_char(&pat, '\\');
            ob_buf_char(&pat, c);
            escaped = 1;
        } else
            ob_buf_char(&pat, c);
    }
    ob_sql_text(b, ob_buf_text(&pat));
    if (escaped) ob_buf_str(b, " ESCAPE '\\'");
    rc = pat.failed;
    ob_buf_free(&pat);
    return rc;
}

/* One value, as SQL for a field of type t */
static int value_sql(const char *s, size_t n, ob_type t, const ob_formats *f, ob_params *pr, ob_buf *out, ob_error *err)
{
    char buf[200], *text;
    ob_value v;
    int quoted = 0, rc = OB_OK;
    while (n && ob_is_space((unsigned char)*s)) s++, n--;
    n = ob_trim_len(s, n);
    if (!n) return ob_fail(err, OB_ERR_INVALID, "A value is missing.");
    if (s[0] == '[') {
        int at;
        if (s[n - 1] != ']' || n < 3) return ob_fail(err, OB_ERR_INVALID, "A question in [ ] isn't closed.");
        if ((at = add_param(pr, s + 1, n - 2, err)) < 0) return err ? err->code : OB_ERR_INVALID;
        ob_buf_char(out, '?');
        return ob_buf_long(out, at);
    }
    if (n >= 2 && (s[0] == '\'' || s[0] == '"') && s[n - 1] == s[0]) {
        /* quoted: doubled quotes inside are one */
        size_t i, k = 0;
        text = (char *)ob_alloc(n);
        if (!text) return ob_nomem(err);
        for (i = 1; i + 1 < n; i++) {
            if (s[i] == s[0] && s[i + 1] == s[0] && i + 2 < n) i++;
            text[k++] = s[i];
        }
        text[k] = 0;
        quoted = 1;
    } else if (!(text = ob_strndup(s, n)))
        return ob_nomem(err);
    if (t == OB_TYPE_DATE && !quoted && ob_casecmp(text, "today") == 0) {
        ob_buf_str(out, "date('now', 'localtime')");
    } else if (t == OB_TYPE_DATETIME && !quoted && ob_casecmp(text, "now") == 0) {
        ob_buf_str(out, "datetime('now', 'localtime')");
    } else if (t == OB_TYPE_TIME && !quoted && ob_casecmp(text, "now") == 0) {
        ob_buf_str(out, "time('now', 'localtime')");
    } else if (t == OB_TYPE_OBJECT) {
        rc = ob_fail(err, OB_ERR_INVALID, "A Datatype object can only be found as empty or not empty.");
    } else if (t == OB_TYPE_UNKNOWN) {
        if (!quoted && !ob_parse_number(text, f, buf, sizeof buf)) ob_buf_str(out, buf);
        else ob_sql_text(out, text);
    } else if (ob_type_is_text(t)) {
        ob_sql_text(out, text);
    } else if (ob_value_from_text(t, text, f, &v, buf, sizeof buf) || v.kind == SQLITE_NULL) {
        rc = ob_fail(err, OB_ERR_INVALID, "\"%s\" isn't a %s.", text, ob_type_name(t));
    } else if (v.kind == SQLITE_INTEGER) {
        ob_buf_long(out, v.i);
    } else if (t == OB_TYPE_NUMBER) {
        ob_buf_str(out, v.text);
    } else
        ob_sql_text(out, v.text);
    ob_free(text);
    return rc;
}

/* Where the next " and " of "between A and B" is, outside quotes and brackets */
static const char *find_and(const char *s)
{
    char q = 0;
    int depth = 0;
    for (; *s; s++) {
        if (q) {
            if (*s == q) q = 0;
            continue;
        }
        if (*s == '\'' || *s == '"') q = *s;
        else if (*s == '[') depth++;
        else if (*s == ']' && depth) depth--;
        else if (!depth && ob_is_space((unsigned char)*s) && ob_ncasecmp(s + 1, "and", 3) == 0 &&
                 ob_is_space((unsigned char)s[4]))
            return s;
    }
    return NULL;
}

static int has_wildcards(const char *s)
{
    return s[0] != '[' && s[0] != '\'' && s[0] != '"' && (strchr(s, '*') || strchr(s, '?'));
}

int ob_criterion_sql(const char *typed, const char *target, ob_type t, const ob_formats *f, ob_params *pr, ob_buf *out,
                     ob_error *err)
{
    ob_formats tmp;
    const char *s;
    size_t n;
    int neg = 0, rc;
    if (!f) {
        ob_formats_defaults(&tmp);
        f = &tmp;
    }
    s = ob_skip_space(typed ? typed : "");
    n = ob_trim_len(s, strlen(s));
    if (!n) return ob_fail(err, OB_ERR_INVALID, "The criteria are empty.");
    if ((n == 5 && ob_ncasecmp(s, "empty", 5) == 0) || (n == 7 && ob_ncasecmp(s, "is null", 7) == 0) ||
        (n == 8 && ob_ncasecmp(s, "is empty", 8) == 0)) {
        ob_buf_str(out, target);
        return ob_buf_str(out, " IS NULL");
    }
    if ((n == 9 && ob_ncasecmp(s, "not empty", 9) == 0) || (n == 11 && ob_ncasecmp(s, "is not null", 11) == 0) ||
        (n == 12 && ob_ncasecmp(s, "is not empty", 12) == 0)) {
        ob_buf_str(out, target);
        return ob_buf_str(out, " IS NOT NULL");
    }
    if (ci_word(s, "not")) {
        neg = 1;
        s = ob_skip_space(s + 3);
        n = ob_trim_len(s, strlen(s));
        if (!n) return ob_fail(err, OB_ERR_INVALID, "\"not\" needs something after it.");
    }
    if (ci_word(s, "between")) {
        const char *a = ob_skip_space(s + 7), *and_at = find_and(a);
        if (!and_at) return ob_fail(err, OB_ERR_INVALID, "\"between\" needs two values: between 1 and 5.");
        ob_buf_str(out, target);
        ob_buf_str(out, neg ? " NOT BETWEEN " : " BETWEEN ");
        if ((rc = value_sql(a, (size_t)(and_at - a), t, f, pr, out, err))) return rc;
        ob_buf_str(out, " AND ");
        return value_sql(and_at + 5, (size_t)(s + n - (and_at + 5)), t, f, pr, out, err);
    }
    if (ci_word(s, "in")) {
        const char *p = ob_skip_space(s + 2), *end = s + n;
        int first = 1;
        if (*p != '(' || end[-1] != ')') return ob_fail(err, OB_ERR_INVALID, "\"in\" needs a list: in (Leeds, York).");
        p++;
        end--;
        ob_buf_str(out, target);
        ob_buf_str(out, neg ? " NOT IN (" : " IN (");
        while (p < end) {
            const char *e = p;
            char q = 0;
            while (e < end && (q || *e != ',')) {
                if (q && *e == q) q = 0;
                else if (!q && (*e == '\'' || *e == '"')) q = *e;
                e++;
            }
            if (!first) ob_buf_str(out, ", ");
            first = 0;
            if ((rc = value_sql(p, (size_t)(e - p), t, f, pr, out, err))) return rc;
            p = e < end ? e + 1 : e;
        }
        if (first) return ob_fail(err, OB_ERR_INVALID, "The list after \"in\" is empty.");
        return ob_buf_char(out, ')');
    }
    if (ci_word(s, "like")) {
        const char *v = ob_skip_space(s + 4);
        size_t vn = ob_trim_len(v, strlen(v));
        if (vn >= 2 && (v[0] == '\'' || v[0] == '"') && v[vn - 1] == v[0]) v++, vn -= 2;
        ob_buf_str(out, target);
        ob_buf_str(out, neg ? " NOT LIKE " : " LIKE ");
        return like_pattern(out, v, vn);
    }
    {
        static const char *const ops[] = { "<=", ">=", "<>", "!=", "=", "<", ">" };
        size_t i;
        for (i = 0; i < sizeof ops / sizeof ops[0]; i++) {
            size_t k = strlen(ops[i]);
            if (strncmp(s, ops[i], k) == 0) {
                const char *op = strcmp(ops[i], "!=") == 0 ? "<>" : ops[i];
                if (neg) ob_buf_str(out, "NOT (");
                ob_buf_str(out, target);
                ob_buf_char(out, ' ');
                ob_buf_str(out, op);
                ob_buf_char(out, ' ');
                if ((rc = value_sql(s + k, n - k, t, f, pr, out, err))) return rc;
                return neg ? ob_buf_char(out, ')') : OB_OK;
            }
        }
    }
    if ((ob_type_is_text(t) || ob_type_is_dated(t) || t == OB_TYPE_UNKNOWN) && has_wildcards(s)) {
        ob_buf_str(out, target);
        ob_buf_str(out, neg ? " NOT LIKE " : " LIKE ");
        return like_pattern(out, s, n);
    }
    ob_buf_str(out, target);
    ob_buf_str(out, neg ? " <> " : " = ");
    return value_sql(s, n, t, f, pr, out, err);
}

/* ---- The generator ---------------------------------------------------------- */

static const char *qualifier(const ob_query *q, int t) { return q->tables[t].alias ? q->tables[t].alias : q->tables[t].name; }

static int is_aggregate(ob_total t) { return t >= OB_TOTAL_SUM && t <= OB_TOTAL_MAX; }

/* A column's value without its total: Customers.Name, or (expression) */
static void column_ref(ob_buf *b, const ob_query *q, const ob_qcolumn *c, int bare_expr)
{
    if (c->table < 0) {
        if (strcmp(c->field, "*") == 0 || bare_expr) ob_buf_str(b, c->field);
        else {
            ob_buf_char(b, '(');
            ob_buf_str(b, c->field);
            ob_buf_char(b, ')');
        }
        return;
    }
    if (q->ntables > 1) {
        ob_sql_ident(b, qualifier(q, c->table));
        ob_buf_char(b, '.');
    }
    if (strcmp(c->field, "*") == 0) ob_buf_char(b, '*');
    else ob_sql_ident(b, c->field);
}

static const char *const agg_fn[] = { "", "", "sum", "count", "avg", "min", "max", "" };
static const char *const agg_name[] = { "", "", "SumOf", "CountOf", "AvgOf", "MinOf", "MaxOf", "" };

/* The column as it is selected, grouped on or sorted by */
static void column_value(ob_buf *b, const ob_query *q, const ob_qcolumn *c, int bare_expr)
{
    if (is_aggregate(c->total)) {
        ob_buf_str(b, agg_fn[c->total]);
        ob_buf_char(b, '(');
        if (c->total == OB_TOTAL_COUNT && strcmp(c->field, "*") == 0) ob_buf_char(b, '*');
        else column_ref(b, q, c, 1);
        ob_buf_char(b, ')');
    } else
        column_ref(b, q, c, bare_expr);
}

static void join_clause(ob_buf *b, const ob_query *q, int placed_mask_unused, const ob_qjoin *j)
{
    OB_UNUSED(placed_mask_unused);
    ob_sql_ident(b, qualifier(q, j->left));
    ob_buf_char(b, '.');
    ob_sql_ident(b, j->left_field);
    ob_buf_str(b, " = ");
    ob_sql_ident(b, qualifier(q, j->right));
    ob_buf_char(b, '.');
    ob_sql_ident(b, j->right_field);
}

static void table_ref(ob_buf *b, const ob_query *q, int t)
{
    ob_sql_ident(b, q->tables[t].name);
    if (q->tables[t].alias) {
        ob_buf_str(b, " AS ");
        ob_sql_ident(b, q->tables[t].alias);
    }
}

/* One part (WHERE or HAVING): the rows ORed, each row's criteria ANDed.
 * pick: 0 where, 1 having, 2 both */
static int criteria_part(ob_buf *b, const ob_query *q, int pick_where_cols, int pick_group_cols, int pick_agg_cols,
                         ob_params *pr, ob_error *err)
{
    ob_buf row, one;
    int r, i, rows_written = 0, rc = OB_OK, nrows = 0;
    /* how many rows will have something, for the brackets */
    for (r = 0; r < OB_QUERY_ORS; r++)
        for (i = 0; i < q->ncolumns; i++) {
            const ob_qcolumn *c = &q->columns[i];
            int is_where = c->total == OB_TOTAL_WHERE, is_agg = is_aggregate(c->total);
            int pick = is_where ? pick_where_cols : is_agg ? pick_agg_cols : pick_group_cols;
            if (pick && c->criteria[r] && *ob_skip_space(c->criteria[r])) {
                nrows++;
                break;
            }
        }
    ob_buf_init(&row, OB_QUERY_SQL_MAX);
    ob_buf_init(&one, OB_QUERY_SQL_MAX);
    for (r = 0; r < OB_QUERY_ORS && !rc; r++) {
        int conds = 0;
        ob_buf_reset(&row);
        for (i = 0; i < q->ncolumns && !rc; i++) {
            const ob_qcolumn *c = &q->columns[i];
            int is_where = c->total == OB_TOTAL_WHERE, is_agg = is_aggregate(c->total);
            int pick = is_where ? pick_where_cols : is_agg ? pick_agg_cols : pick_group_cols;
            ob_buf target;
            if (!pick || !c->criteria[r] || !*ob_skip_space(c->criteria[r])) continue;
            if (c->table >= 0 && strcmp(c->field, "*") == 0) {
                rc = ob_fail(err, OB_ERR_INVALID, "Criteria can't go under \"*\".");
                break;
            }
            ob_buf_init(&target, OB_QUERY_SQL_MAX);
            column_value(&target, q, c, 0);
            ob_buf_reset(&one);
            if (target.failed) {
                ob_buf_free(&target);
                rc = ob_nomem(err);
                break;
            }
            rc = ob_criterion_sql(c->criteria[r], ob_buf_text(&target), is_agg && c->total == OB_TOTAL_COUNT ? OB_TYPE_INTEGER : c->type,
                                  &q->formats, pr, &one, err);
            ob_buf_free(&target);
            if (rc) {
                if (err) {
                    char why[sizeof err->text];
                    snprintf(why, sizeof why, "%s", err->text);
                    ob_fail(err, rc, "%s: %s", c->name ? c->name : c->field, why);
                }
                break;
            }
            if (conds++) ob_buf_str(&row, " AND ");
            ob_buf_str(&row, ob_buf_text(&one));
        }
        if (rc || !conds) continue;
        if (rows_written++) ob_buf_str(b, " OR ");
        if (nrows > 1 && conds > 1) ob_buf_char(b, '(');
        ob_buf_str(b, ob_buf_text(&row));
        if (nrows > 1 && conds > 1) ob_buf_char(b, ')');
    }
    if (!rc && (row.failed || one.failed)) rc = ob_fail(err, OB_ERR_LIMIT, "The query is too long.");
    ob_buf_free(&row);
    ob_buf_free(&one);
    return rc;
}

static int any_criteria(const ob_query *q, int where_cols, int group_cols, int agg_cols)
{
    int i, r;
    for (i = 0; i < q->ncolumns; i++) {
        const ob_qcolumn *c = &q->columns[i];
        int is_where = c->total == OB_TOTAL_WHERE, is_agg = is_aggregate(c->total);
        int pick = is_where ? where_cols : is_agg ? agg_cols : group_cols;
        if (!pick) continue;
        for (r = 0; r < OB_QUERY_ORS; r++)
            if (c->criteria[r] && *ob_skip_space(c->criteria[r])) return 1;
    }
    return 0;
}

int ob_query_sql(const ob_query *q, ob_buf *sql, ob_params *params, ob_error *err)
{
    ob_params local;
    int i, j, k, rc, shown = 0, totals = 0, first;
    int placed[OB_QUERY_TABLES];
    if (!params) params = &local;
    memset(params, 0, sizeof *params);
    ob_buf_reset(sql);
    if (!q || q->ntables < 1) return ob_fail(err, OB_ERR_INVALID, "The query needs a table.");
    for (i = 0; i < q->ntables; i++)
        for (j = 0; j < i; j++)
            if (ob_casecmp(qualifier(q, i), qualifier(q, j)) == 0)
                return ob_fail(err, OB_ERR_INVALID, "%s is in the query twice; give one of them another name.", qualifier(q, i));
    for (i = 0; i < q->ncolumns; i++) {
        const ob_qcolumn *c = &q->columns[i];
        if (!c->field || c->table < -1 || c->table >= q->ntables || (unsigned)c->total > OB_TOTAL_WHERE ||
            (unsigned)c->sort > OB_SORT_DESC)
            return ob_fail(err, OB_ERR_INVALID, "Column %d isn't complete.", i + 1);
        if (c->total != OB_TOTAL_NONE) totals = 1;
        if (c->show && c->total != OB_TOTAL_WHERE) shown++;
    }
    for (i = 0; i < q->njoins; i++) {
        const ob_qjoin *jn = &q->joins[i];
        if (jn->left < 0 || jn->left >= q->ntables || jn->right < 0 || jn->right >= q->ntables || jn->left == jn->right ||
            !jn->left_field || !jn->right_field || (unsigned)jn->type > OB_JOIN_RIGHT)
            return ob_fail(err, OB_ERR_INVALID, "Join %d isn't complete.", i + 1);
    }
    if (!shown) return ob_fail(err, OB_ERR_INVALID, "Show at least one column.");
    /* SELECT */
    ob_buf_str(sql, q->distinct ? "SELECT DISTINCT " : "SELECT ");
    first = 1;
    for (i = 0; i < q->ncolumns; i++) {
        const ob_qcolumn *c = &q->columns[i];
        if (!c->show || c->total == OB_TOTAL_WHERE) continue;
        if (!first) ob_buf_str(sql, ", ");
        first = 0;
        column_value(sql, q, c, 1);
        if (c->name) {
            ob_buf_str(sql, " AS ");
            ob_sql_ident(sql, c->name);
        } else if (is_aggregate(c->total)) {
            char name[OB_NAME_MAX + 16];
            snprintf(name, sizeof name, "%s%s", agg_name[c->total],
                     c->table >= 0 && strcmp(c->field, "*") != 0 ? c->field : "All");
            ob_buf_str(sql, " AS ");
            ob_sql_ident(sql, name);
        } else if (c->table < 0 && strcmp(c->field, "*") != 0) {
            char name[16];
            snprintf(name, sizeof name, "Expr%d", i + 1);
            ob_buf_str(sql, " AS ");
            ob_sql_ident(sql, name);
        }
    }
    /* FROM: each table joined to those before it */
    ob_buf_str(sql, " FROM ");
    table_ref(sql, q, 0);
    memset(placed, 0, sizeof placed);
    placed[0] = 1;
    for (i = 1; i < q->ntables; i++) {
        int conds = 0;
        const ob_qjoin *lead = NULL;
        for (j = 0; j < q->njoins; j++) {
            const ob_qjoin *jn = &q->joins[j];
            if ((jn->right == i && placed[jn->left]) || (jn->left == i && placed[jn->right])) {
                lead = jn;
                break;
            }
        }
        if (!lead) {
            ob_buf_str(sql, ", ");
            table_ref(sql, q, i);
        } else {
            ob_jointype kind = lead->type;
            if (lead->left == i && kind != OB_JOIN_INNER) kind = kind == OB_JOIN_LEFT ? OB_JOIN_RIGHT : OB_JOIN_LEFT;
            ob_buf_str(sql, kind == OB_JOIN_LEFT ? " LEFT JOIN " : kind == OB_JOIN_RIGHT ? " RIGHT JOIN " : " JOIN ");
            table_ref(sql, q, i);
            ob_buf_str(sql, " ON ");
            for (j = 0; j < q->njoins; j++) {
                const ob_qjoin *jn = &q->joins[j];
                if ((jn->right == i && placed[jn->left]) || (jn->left == i && placed[jn->right])) {
                    if (conds++) ob_buf_str(sql, " AND ");
                    join_clause(sql, q, 0, jn);
                }
            }
        }
        placed[i] = 1;
    }
    /* WHERE and HAVING */
    if (!totals) {
        if (any_criteria(q, 1, 1, 1)) {
            ob_buf_str(sql, " WHERE ");
            if ((rc = criteria_part(sql, q, 1, 1, 1, params, err))) return rc;
        }
    } else {
        int agg = any_criteria(q, 0, 0, 1);
        /* with no criteria on totals, the grouped fields' criteria filter
         * records before grouping: the same result, sooner */
        if (any_criteria(q, 1, !agg, 0)) {
            ob_buf_str(sql, " WHERE ");
            if ((rc = criteria_part(sql, q, 1, !agg, 0, params, err))) return rc;
        }
    }
    if (totals) {
        first = 1;
        for (i = 0; i < q->ncolumns; i++) {
            const ob_qcolumn *c = &q->columns[i];
            if (c->total != OB_TOTAL_GROUP && c->total != OB_TOTAL_NONE) continue;
            if (strcmp(c->field, "*") == 0) continue;
            ob_buf_str(sql, first ? " GROUP BY " : ", ");
            first = 0;
            column_ref(sql, q, c, 0);
        }
        if (any_criteria(q, 0, 0, 1)) {
            ob_buf_str(sql, " HAVING ");
            if ((rc = criteria_part(sql, q, 0, 1, 1, params, err))) return rc;
        }
    }
    first = 1;
    for (k = 0; k < q->ncolumns; k++) {
        const ob_qcolumn *c = &q->columns[k];
        if (c->sort == OB_SORT_NONE || c->total == OB_TOTAL_WHERE || strcmp(c->field, "*") == 0) continue;
        ob_buf_str(sql, first ? " ORDER BY " : ", ");
        first = 0;
        column_value(sql, q, c, 0);
        if (c->sort == OB_SORT_DESC) ob_buf_str(sql, " DESC");
    }
    if (q->limit > 0) {
        ob_buf_str(sql, " LIMIT ");
        ob_buf_long(sql, q->limit);
    }
    if (sql->failed == OB_ERR_LIMIT) return ob_fail(err, OB_ERR_LIMIT, "The query is too long.");
    if (sql->failed) return ob_nomem(err);
    return OB_OK;
}

/* ---- From the project ------------------------------------------------------ */

int ob_query_resolve(ob_project *p, ob_query *q, ob_error *err)
{
    ob_table *tables[OB_QUERY_TABLES];
    int i, rc = OB_OK;
    memset(tables, 0, sizeof tables);
    q->formats = p->formats;
    for (i = 0; i < q->ntables && !rc; i++) rc = ob_table_read(p, q->tables[i].name, &tables[i], err);
    for (i = 0; i < q->ncolumns && !rc; i++) {
        ob_qcolumn *c = &q->columns[i];
        const ob_field *f;
        if (c->table < 0 || c->table >= q->ntables) {
            c->type = OB_TYPE_UNKNOWN;
            continue;
        }
        if (strcmp(c->field, "*") == 0) continue;
        if (!(f = ob_table_field(tables[c->table], c->field))) {
            rc = ob_fail(err, OB_ERR_NOT_FOUND, "%s has no field %s.", q->tables[c->table].name, c->field);
            break;
        }
        c->type = f->type;
    }
    for (i = 0; i < q->njoins && !rc; i++) {
        const ob_qjoin *j = &q->joins[i];
        if (j->left >= q->ntables || j->right >= q->ntables) continue;
        if (!ob_table_field(tables[j->left], j->left_field))
            rc = ob_fail(err, OB_ERR_NOT_FOUND, "%s has no field %s.", q->tables[j->left].name, j->left_field);
        else if (!ob_table_field(tables[j->right], j->right_field))
            rc = ob_fail(err, OB_ERR_NOT_FOUND, "%s has no field %s.", q->tables[j->right].name, j->right_field);
    }
    for (i = 0; i < q->ntables; i++) ob_table_free(tables[i]);
    return rc;
}

int ob_query_auto_joins(ob_project *p, ob_query *q, ob_error *err)
{
    ob_relation *rel = NULL;
    int n = 0, i, a, b, rc;
    if ((rc = ob_relation_list(p, &rel, &n, err))) return rc;
    for (i = 0; i < n && !rc; i++) {
        const ob_relation *r = &rel[i];
        if (r->nfields != 1) continue;
        for (a = 0; a < q->ntables && !rc; a++)
            for (b = 0; b < q->ntables && !rc; b++) {
                int k, have = 0;
                char key[OB_NAME_MAX + 1];
                if (a == b || ob_casecmp(q->tables[a].name, r->child) != 0 || ob_casecmp(q->tables[b].name, r->parent) != 0)
                    continue;
                for (k = 0; k < q->njoins; k++)
                    if ((q->joins[k].left == a && q->joins[k].right == b) || (q->joins[k].left == b && q->joins[k].right == a))
                        have = 1;
                if (have) continue;
                if (r->parent_fields[0]) snprintf(key, sizeof key, "%s", r->parent_fields[0]);
                else {
                    ob_table *pt;
                    int j;
                    if ((rc = ob_table_read(p, r->parent, &pt, err))) break;
                    key[0] = 0;
                    for (j = 0; j < pt->nfields; j++)
                        if (pt->fields[j].type == OB_TYPE_COUNTER) snprintf(key, sizeof key, "%s", pt->fields[j].name);
                    if (!key[0] && pt->nkey == 1) snprintf(key, sizeof key, "%s", pt->key[0]);
                    ob_table_free(pt);
                    if (!key[0]) continue;
                }
                /* parent first, as the designer draws it */
                rc = ob_query_add_join(q, b, key, a, r->child_fields[0], OB_JOIN_INNER, err);
            }
    }
    ob_relations_free(rel, n);
    return rc;
}

/* ---- The grid as text ------------------------------------------------------------ */

#define GRID_HEADER "OpenBase query"
#define GRID_MAX (256L * 1024L)

static void put_esc(ob_buf *b, const char *s)
{
    ob_buf_char(b, '\t');
    if (!s) return;
    for (; *s; s++) {
        if (*s == '\\') ob_buf_str(b, "\\\\");
        else if (*s == '\t') ob_buf_str(b, "\\t");
        else if (*s == '\n') ob_buf_str(b, "\\n");
        else if (*s == '\r') ob_buf_str(b, "\\r");
        else ob_buf_char(b, *s);
    }
}

static void put_num(ob_buf *b, long long v)
{
    ob_buf_char(b, '\t');
    ob_buf_long(b, v);
}

static const char *const join_words[] = { "inner", "left", "right" };
static const char *const sort_words[] = { "none", "asc", "desc" };
static const char *const total_words[] = { "none", "group", "sum", "count", "avg", "min", "max", "where" };
static const char *const order_words[] = { "YMD", "DMY", "MDY", "AUTO" };

int ob_query_to_text(const ob_query *q, ob_buf *b, ob_error *err)
{
    int i, r;
    char sep[2] = { 0, 0 };
    ob_buf_reset(b);
    ob_buf_str(b, GRID_HEADER);
    put_num(b, 1);
    ob_buf_str(b, "\ndistinct");
    put_num(b, q->distinct ? 1 : 0);
    ob_buf_str(b, "\nlimit");
    put_num(b, q->limit > 0 ? q->limit : 0);
    ob_buf_str(b, "\nformats");
    put_esc(b, order_words[(unsigned)q->formats.date_order <= OB_DATE_AUTO ? q->formats.date_order : 0]);
    sep[0] = q->formats.decimal ? q->formats.decimal : '.';
    put_esc(b, sep);
    sep[0] = q->formats.thousands;
    put_esc(b, sep);
    for (i = 0; i < q->ntables; i++) {
        ob_buf_str(b, "\ntable");
        put_esc(b, q->tables[i].name);
        put_esc(b, q->tables[i].alias);
    }
    for (i = 0; i < q->njoins; i++) {
        const ob_qjoin *j = &q->joins[i];
        ob_buf_str(b, "\njoin");
        put_num(b, j->left);
        put_esc(b, j->left_field);
        put_num(b, j->right);
        put_esc(b, j->right_field);
        put_esc(b, join_words[(unsigned)j->type <= OB_JOIN_RIGHT ? j->type : 0]);
    }
    for (i = 0; i < q->ncolumns; i++) {
        const ob_qcolumn *c = &q->columns[i];
        ob_buf_str(b, "\ncolumn");
        put_num(b, c->table);
        put_esc(b, c->field);
        put_esc(b, c->name);
        put_num(b, c->show ? 1 : 0);
        put_esc(b, sort_words[(unsigned)c->sort <= OB_SORT_DESC ? c->sort : 0]);
        put_esc(b, total_words[(unsigned)c->total <= OB_TOTAL_WHERE ? c->total : 0]);
        put_esc(b, ob_type_key(c->type));
        for (r = 0; r < OB_QUERY_ORS; r++)
            if (c->criteria[r]) {
                ob_buf_str(b, "\ncriteria");
                put_num(b, i);
                put_num(b, r);
                put_esc(b, c->criteria[r]);
            }
    }
    ob_buf_str(b, "\nend\n");
    if (b->failed) return b->failed == OB_ERR_LIMIT ? ob_fail(err, OB_ERR_LIMIT, "The query is too big.") : ob_nomem(err);
    return OB_OK;
}

/* Splits one line into tab-separated, unescaped parts, in place */
static int split_line(char *line, char **parts, int max)
{
    int n = 0;
    char *p = line, *w;
    while (n < max) {
        parts[n++] = w = p;
        for (; *p && *p != '\t'; p++) {
            if (*p == '\\') {
                char c = p[1];
                if (c == '\\') *w++ = '\\';
                else if (c == 't') *w++ = '\t';
                else if (c == 'n') *w++ = '\n';
                else if (c == 'r') *w++ = '\r';
                else return -1;
                p++;
            } else
                *w++ = *p;
        }
        if (!*p) {
            *w = 0;
            return n;
        }
        p++;
        *w = 0;
    }
    return -1; /* too many parts */
}

static int word_index(const char *w, const char *const *words, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (strcmp(w, words[i]) == 0) return i;
    return -1;
}

static int small_number(const char *s, long lo, long hi, long *out)
{
    long long v;
    if (!s || !*s || ob_parse_integer(s, NULL, &v) || v < lo || v > hi) return 0;
    for (; *s; s++)
        if (!((*s >= '0' && *s <= '9') || *s == '-')) return 0;
    *out = (long)v;
    return 1;
}

int ob_query_from_text(const char *text, ob_query **out, ob_error *err)
{
    ob_query *q;
    char *copy, *line, *next;
    char *parts[10];
    int rc = OB_OK, lines = 0, ended = 0;
    *out = NULL;
    if (!text) return ob_fail(err, OB_ERR_INVALID, "No query.");
    if (strlen(text) > GRID_MAX) return ob_fail(err, OB_ERR_LIMIT, "The query is too big.");
    if (!(q = ob_query_new())) return ob_nomem(err);
    if (!(copy = ob_strdup(text))) {
        ob_query_free(q);
        return ob_nomem(err);
    }
    for (line = copy; line && *line && !rc && !ended; line = next) {
        int n;
        long a, b2;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        n = split_line(line, parts, 10);
        if (n < 1) {
            rc = OB_ERR_DAMAGED;
            break;
        }
        if (lines++ == 0) {
            if (n != 2 || strcmp(parts[0], GRID_HEADER) != 0 || !small_number(parts[1], 1, 1000000, &a)) rc = OB_ERR_DAMAGED;
            else if (a > 1) rc = OB_ERR_NEWER;
            continue;
        }
        if (!strcmp(parts[0], "end") && n == 1) ended = 1;
        else if (!strcmp(parts[0], "distinct") && n == 2 && small_number(parts[1], 0, 1, &a)) q->distinct = (int)a;
        else if (!strcmp(parts[0], "limit") && n == 2 && small_number(parts[1], 0, 2000000000L, &a)) q->limit = a;
        else if (!strcmp(parts[0], "formats") && n == 4) {
            int o = word_index(parts[1], order_words, 4);
            if (o < 0 || (strcmp(parts[2], ".") && strcmp(parts[2], ",")) || strlen(parts[3]) > 1) rc = OB_ERR_DAMAGED;
            else {
                q->formats.date_order = (ob_date_order)o;
                q->formats.decimal = parts[2][0];
                q->formats.thousands = parts[3][0];
            }
        } else if (!strcmp(parts[0], "table") && n == 3) {
            if (ob_query_add_table(q, parts[1], parts[2], NULL)) rc = OB_ERR_DAMAGED;
        } else if (!strcmp(parts[0], "join") && n == 6 && small_number(parts[1], 0, OB_QUERY_TABLES - 1, &a) &&
                   small_number(parts[3], 0, OB_QUERY_TABLES - 1, &b2)) {
            int t = word_index(parts[5], join_words, 3);
            if (t < 0 || ob_query_add_join(q, (int)a, parts[2], (int)b2, parts[4], (ob_jointype)t, NULL)) rc = OB_ERR_DAMAGED;
        } else if (!strcmp(parts[0], "column") && n == 8 && small_number(parts[1], -1, OB_QUERY_TABLES - 1, &a) &&
                   small_number(parts[4], 0, 1, &b2)) {
            int s = word_index(parts[5], sort_words, 3), t = word_index(parts[6], total_words, 8), c;
            ob_type type = OB_TYPE_UNKNOWN;
            if (s < 0 || t < 0 || (strcmp(parts[7], "unknown") && ob_type_from_key(parts[7], &type))) rc = OB_ERR_DAMAGED;
            else if ((c = ob_query_add_column(q, (int)a, parts[2], NULL)) < 0) rc = OB_ERR_DAMAGED;
            else {
                q->columns[c].show = (int)b2;
                q->columns[c].sort = (ob_sort)s;
                q->columns[c].total = (ob_total)t;
                q->columns[c].type = type;
                if (*parts[3] && ob_query_set_name(q, c, parts[3], NULL)) rc = OB_ERR_DAMAGED;
            }
        } else if (!strcmp(parts[0], "criteria") && n == 4 && small_number(parts[1], 0, OB_QUERY_COLUMNS - 1, &a) &&
                   small_number(parts[2], 0, OB_QUERY_ORS - 1, &b2)) {
            if (a >= q->ncolumns || ob_query_set_criteria(q, (int)a, (int)b2, parts[3], NULL)) rc = OB_ERR_DAMAGED;
        } else
            rc = OB_ERR_DAMAGED;
    }
    ob_free(copy);
    if (!rc && !ended) rc = OB_ERR_DAMAGED;
    if (!rc) {
        int i;
        /* everything points inside the query */
        for (i = 0; i < q->ncolumns; i++)
            if (q->columns[i].table >= q->ntables) rc = OB_ERR_DAMAGED;
        for (i = 0; i < q->njoins; i++)
            if (q->joins[i].left >= q->ntables || q->joins[i].right >= q->ntables) rc = OB_ERR_DAMAGED;
    }
    if (rc) {
        ob_query_free(q);
        if (rc == OB_ERR_NEWER) return ob_fail(err, rc, "This query was made by a newer OpenBase.");
        return ob_fail(err, OB_ERR_DAMAGED, "The query's grid is damaged.");
    }
    *out = q;
    return OB_OK;
}

/* ---- Saved queries --------------------------------------------------------------- */

/* The SQL must be one statement that only reads */
static int check_select(ob_project *p, const char *sql, ob_error *err)
{
    sqlite3_stmt *st;
    const char *tail = NULL;
    int rc = sqlite3_prepare_v2(p->db, sql, -1, &st, &tail);
    if (rc != SQLITE_OK) return ob_sqlite_fail(p, rc, err, "the query's SQL");
    if (!st) return ob_fail(err, OB_ERR_INVALID, "The query's SQL is empty.");
    rc = !sqlite3_stmt_readonly(st) || sqlite3_column_count(st) == 0;
    sqlite3_finalize(st);
    if (rc) return ob_fail(err, OB_ERR_INVALID, "A query must be one SELECT.");
    if (tail && *ob_skip_space(tail) && strcmp(ob_skip_space(tail), ";") != 0)
        return ob_fail(err, OB_ERR_INVALID, "A query must be one statement.");
    return OB_OK;
}

static int query_row(ob_project *p, const char *name, char *exact, size_t size, int *view)
{
    sqlite3_stmt *st;
    int found = 0;
    if (sqlite3_prepare_v2(p->db, "SELECT name, view FROM oadb_queries WHERE name = ?", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *n = (const char *)sqlite3_column_text(st, 0);
        if (n && strlen(n) < size) {
            strcpy(exact, n);
            *view = sqlite3_column_int(st, 1);
            found = 1;
        }
    }
    sqlite3_finalize(st);
    return found;
}

static int drop_view(ob_project *p, const char *name, ob_error *err)
{
    ob_buf b;
    int rc;
    ob_buf_init(&b, 0);
    ob_buf_str(&b, "DROP VIEW IF EXISTS ");
    ob_sql_ident_quoted(&b, name);
    rc = b.failed ? ob_nomem(err) : ob_exec(p, ob_buf_text(&b), err);
    ob_buf_free(&b);
    return rc;
}

int ob_query_save(ob_project *p, const char *name, const ob_query *grid, const char *sql, int as_view, ob_error *err)
{
    ob_buf gen, text, b;
    ob_params params;
    const char *use_sql = sql;
    int rc, keep_grid = grid != NULL, old_view = 0;
    char exact[OB_NAME_MAX + 1];
    sqlite3_stmt *st;
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if ((rc = ob_name_check(name, "query", err))) return rc;
    if (!grid && !sql) return ob_fail(err, OB_ERR_INVALID, "The query is empty.");
    ob_buf_init(&gen, OB_QUERY_SQL_MAX);
    ob_buf_init(&text, GRID_MAX);
    ob_buf_init(&b, 0);
    memset(&params, 0, sizeof params);
    if (grid) {
        if ((rc = ob_query_sql(grid, &gen, &params, err))) goto done;
        if (!sql) use_sql = ob_buf_text(&gen);
        else if (strcmp(ob_skip_space(sql), ob_buf_text(&gen)) != 0) keep_grid = 0; /* edited: SQL only */
        if (keep_grid && (rc = ob_query_to_text(grid, &text, err))) goto done;
    }
    if ((rc = check_select(p, use_sql, err))) goto done;
    if (as_view) {
        sqlite3_stmt *probe;
        if (sqlite3_prepare_v2(p->db, use_sql, -1, &probe, NULL) == SQLITE_OK) {
            if (sqlite3_bind_parameter_count(probe) > 0) rc = ob_fail(err, OB_ERR_INVALID, "A query that asks questions can't be a view.");
            sqlite3_finalize(probe);
            if (rc) goto done;
        }
    }
    if ((rc = ob_begin(p, err))) goto done;
    if (query_row(p, name, exact, sizeof exact, &old_view) && old_view && (rc = drop_view(p, exact, err))) goto fail;
    if (as_view) {
        sqlite3_stmt *probe;
        int taken = 0;
        if ((rc = ob_prepare(p, "SELECT 1 FROM sqlite_schema WHERE name = ? COLLATE NOCASE", &probe, err))) goto fail;
        sqlite3_bind_text(probe, 1, name, -1, SQLITE_STATIC);
        taken = sqlite3_step(probe) == SQLITE_ROW;
        sqlite3_finalize(probe);
        if (taken) {
            rc = ob_fail(err, OB_ERR_EXISTS, "There is already a table, view or index called %s.", name);
            goto fail;
        }
        ob_buf_str(&b, "CREATE VIEW ");
        ob_sql_ident_quoted(&b, name);
        ob_buf_str(&b, " AS ");
        ob_buf_str(&b, use_sql);
        if (b.failed) {
            rc = ob_nomem(err);
            goto fail;
        }
        if ((rc = ob_exec(p, ob_buf_text(&b), err))) goto fail;
    }
    if ((rc = ob_prepare(p, "INSERT OR REPLACE INTO oadb_queries (name, grid, sql, view) VALUES (?, ?, ?, ?)", &st, err))) goto fail;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    if (keep_grid) sqlite3_bind_text(st, 2, ob_buf_text(&text), -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, use_sql, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 4, as_view ? 1 : 0);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        rc = ob_sqlite_fail(p, rc, err, "saving the query");
        goto fail;
    }
    rc = ob_commit(p, err);
    goto done;
fail:
    ob_rollback(p);
done:
    ob_buf_free(&gen);
    ob_buf_free(&text);
    ob_buf_free(&b);
    return rc;
}

int ob_query_load(ob_project *p, const char *name, ob_query **grid, char **sql, ob_error *err)
{
    sqlite3_stmt *st;
    int rc;
    if (grid) *grid = NULL;
    if (sql) *sql = NULL;
    if (!name) return ob_fail(err, OB_ERR_NOT_FOUND, "Which query?");
    if ((rc = ob_prepare(p, "SELECT grid, sql FROM oadb_queries WHERE name = ?", &st, err))) return rc;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(st);
        if (rc == SQLITE_DONE) return ob_fail(err, OB_ERR_NOT_FOUND, "There is no query called %s.", name);
        return ob_sqlite_fail(p, rc, err, "reading the query");
    }
    {
        const char *g = (const char *)sqlite3_column_text(st, 0);
        const char *s = (const char *)sqlite3_column_text(st, 1);
        ob_query *q = NULL;
        if (!s) {
            sqlite3_finalize(st);
            return ob_fail(err, OB_ERR_DAMAGED, "The query %s has no SQL.", name);
        }
        /* A grid is kept only with SQL it wrote (ob_query_save); a later
         * OpenBase may write that SQL a little differently, so it isn't
         * compared again here. A grid that can't be read is left out. */
        if (g && grid && ob_query_from_text(g, &q, NULL) != OB_OK) q = NULL;
        if (sql && !(*sql = ob_strdup(s))) {
            ob_query_free(q);
            sqlite3_finalize(st);
            return ob_nomem(err);
        }
        if (grid) *grid = q;
        else ob_query_free(q);
    }
    sqlite3_finalize(st);
    return OB_OK;
}

int ob_query_delete(ob_project *p, const char *name, ob_error *err)
{
    char exact[OB_NAME_MAX + 1];
    int view = 0, rc;
    sqlite3_stmt *st;
    if (p->readonly) return ob_fail(err, OB_ERR_READONLY, "The project can't be changed: it is read only.");
    if (!name || !query_row(p, name, exact, sizeof exact, &view))
        return ob_fail(err, OB_ERR_NOT_FOUND, "There is no query called %s.", name ? name : "");
    if ((rc = ob_begin(p, err))) return rc;
    if (view && (rc = drop_view(p, exact, err))) goto fail;
    if ((rc = ob_prepare(p, "DELETE FROM oadb_queries WHERE name = ?", &st, err))) goto fail;
    sqlite3_bind_text(st, 1, exact, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        rc = ob_sqlite_fail(p, rc, err, "removing the query");
        goto fail;
    }
    return ob_commit(p, err);
fail:
    ob_rollback(p);
    return rc;
}

int ob_query_list(ob_project *p, char ***names, int *count, ob_error *err)
{
    sqlite3_stmt *st;
    int rc, n = 0;
    char **list = NULL;
    *names = NULL;
    *count = 0;
    if ((rc = ob_prepare(p, "SELECT name FROM oadb_queries ORDER BY name COLLATE NOCASE", &st, err))) return rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW && n < 10000) {
        char **grown = (char **)ob_realloc(list, sizeof(char *) * (size_t)(n + 1));
        const char *v = (const char *)sqlite3_column_text(st, 0);
        if (!grown) break;
        list = grown;
        if (!(list[n] = ob_strdup(v ? v : ""))) break;
        n++;
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        ob_names_free(list, n);
        return ob_sqlite_fail(p, rc, err, "listing the queries");
    }
    if (rc == SQLITE_ROW && n < 10000) {
        ob_names_free(list, n);
        return ob_nomem(err);
    }
    *names = list;
    *count = n;
    return OB_OK;
}
