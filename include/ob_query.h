/* OpenBase core: the query model and its SQL generator
 * (Design-OpenBase.md, 5). The designer edits an ob_query; the generator
 * writes SQL from it; the grid is kept beside its SQL in oadb_queries.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#ifndef OB_QUERY_H
#define OB_QUERY_H

#include "ob_schema.h"

#define OB_QUERY_TABLES 16
#define OB_QUERY_COLUMNS 64
#define OB_QUERY_ORS 8       /* the Criteria row and up to seven "or" rows */
#define OB_QUERY_PARAMS 16
#define OB_QUERY_SQL_MAX (64L * 1024L)

typedef enum { OB_JOIN_INNER, OB_JOIN_LEFT, OB_JOIN_RIGHT } ob_jointype;
typedef enum { OB_SORT_NONE, OB_SORT_ASC, OB_SORT_DESC } ob_sort;
typedef enum {
    OB_TOTAL_NONE, OB_TOTAL_GROUP, OB_TOTAL_SUM, OB_TOTAL_COUNT, OB_TOTAL_AVG,
    OB_TOTAL_MIN, OB_TOTAL_MAX,
    OB_TOTAL_WHERE /* only filters records before grouping; never shown */
} ob_total;

typedef struct ob_qtable {
    char *name;
    char *alias; /* NULL: the name; a table used twice needs one */
} ob_qtable;

/* left.left_field = right.right_field; LEFT keeps every row of `left` */
typedef struct ob_qjoin {
    int left, right; /* tables */
    char *left_field, *right_field;
    ob_jointype type;
} ob_qjoin;

typedef struct ob_qcolumn {
    int table;      /* index into tables; -1: field is an SQL expression */
    char *field;    /* a field, "*", or the expression */
    char *name;     /* the result column's name; NULL: the field's */
    int show;
    ob_sort sort;
    ob_total total;
    ob_type type;   /* the field's type, which criteria follow (ob_query_resolve) */
    char *criteria[OB_QUERY_ORS];
} ob_qcolumn;

typedef struct ob_query {
    int ntables, njoins, ncolumns;
    ob_qtable tables[OB_QUERY_TABLES];
    ob_qjoin joins[OB_QUERY_TABLES * 2];
    ob_qcolumn columns[OB_QUERY_COLUMNS];
    int distinct;
    long limit;          /* 0: all rows */
    ob_formats formats;  /* how dates and numbers are typed into criteria */
} ob_query;

/* A query's parameters: [Which town?] in criteria; the same prompt twice
 * is one parameter. Bind value i to ?(i+1). */
typedef struct ob_params {
    int count;
    char prompt[OB_QUERY_PARAMS][80];
} ob_params;

ob_query *ob_query_new(void);
void ob_query_free(ob_query *q);
int ob_query_add_table(ob_query *q, const char *name, const char *alias, ob_error *err);
int ob_query_add_join(ob_query *q, int left, const char *left_field, int right,
                      const char *right_field, ob_jointype type, ob_error *err);
/* The new column's index, or -1 (err) */
int ob_query_add_column(ob_query *q, int table, const char *field, ob_error *err);
int ob_query_set_criteria(ob_query *q, int column, int row, const char *text, ob_error *err);
int ob_query_set_name(ob_query *q, int column, const char *name, ob_error *err);

/* From the project: each column's type, and joins from the relationships
 * between the query's tables where none were drawn. */
int ob_query_resolve(ob_project *p, ob_query *q, ob_error *err);
int ob_query_auto_joins(ob_project *p, ob_query *q, ob_error *err);

/* The generator. One criterion (5.2's table) as SQL, for a value typed
 * against a field of type t: */
int ob_criterion_sql(const char *typed, const char *target_sql, ob_type t,
                     const ob_formats *f, ob_params *params, ob_buf *out, ob_error *err);
/* The whole query */
int ob_query_sql(const ob_query *q, ob_buf *sql, ob_params *params, ob_error *err);

/* The grid as text (kept in oadb_queries.grid), and back. */
int ob_query_to_text(const ob_query *q, ob_buf *out, ob_error *err);
int ob_query_from_text(const char *text, ob_query **out, ob_error *err);

/* Saved queries. grid NULL: kept as SQL only. as_view: also a VIEW, so
 * other SQLite tools see it (not for queries with parameters). */
int ob_query_save(ob_project *p, const char *name, const ob_query *grid, const char *sql,
                  int as_view, ob_error *err);
/* *grid is NULL when the query is kept as SQL only (its SQL was edited
 * away from the grid's when it was saved), or the grid can't be read. */
int ob_query_load(ob_project *p, const char *name, ob_query **grid, char **sql, ob_error *err);
int ob_query_delete(ob_project *p, const char *name, ob_error *err);
int ob_query_list(ob_project *p, char ***names, int *count, ob_error *err);

/* SQL quoting, as the generator does it: an identifier is quoted only
 * when it must be (not a plain word, or an SQL keyword). */
int ob_sql_ident(ob_buf *b, const char *name);
int ob_sql_text(ob_buf *b, const char *text); /* 'O''Brien' */

#endif
