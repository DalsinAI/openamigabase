/* OpenBase core: tables, fields, keys, indexes and relationships
 * (Design-OpenBase.md, 4), and changing a table the copy-and-rename way.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#ifndef OB_SCHEMA_H
#define OB_SCHEMA_H

#include "ob_types.h"

#define OB_NAME_MAX 64      /* bytes of UTF-8 in a table or field name */
#define OB_FIELDS_MAX 255   /* fields in a table (a Datatype object counts 3) */
#define OB_KEY_MAX 4        /* fields in a key or a relationship */
#define OB_INDEX_FIELDS 8   /* fields in an index */
#define OB_TABLES_MAX 1000  /* tables listed */
#define OB_CHOICES_MAX 4096 /* bytes of a choice list */

/* All strings below belong to their structure: set them with ob_strset. */
int ob_strset(char **dst, const char *src, ob_error *err);

typedef struct ob_field {
    char *name;          /* the SQL column */
    char *was;           /* in a change: the field's name before; NULL: new */
    ob_type type;
    char *label;         /* NULL: the name */
    char *format;        /* display format; NULL: the type's own */
    char *choices;       /* Choice: the values, one a line */
    char *default_sql;   /* SQL: a literal, or an expression in brackets (ob_default_sql) */
    char *rule;          /* a further CHECK, in SQL; NULL: none */
    char *rule_text;     /* what to say when the rule refuses: "Price can't be below 0" */
    int maxlen;          /* Text: the most characters; 0: no limit */
    int required;        /* NOT NULL */
    char *lookup_table;  /* Lookup: the other table (its key is stored) */
    char *lookup_show;   /* Lookup: the field shown in lists */
} ob_field;

typedef enum { OB_REL_REFUSE, OB_REL_CASCADE, OB_REL_CLEAR } ob_relrule;

/* A relationship: a FOREIGN KEY on the child table. */
typedef struct ob_relation {
    char *child;                     /* the table holding the key's copy */
    int nfields;
    char *child_fields[OB_KEY_MAX];
    char *parent;                    /* the table whose key it is */
    char *parent_fields[OB_KEY_MAX]; /* NULL: the parent's primary key */
    ob_relrule on_delete, on_update;
} ob_relation;

typedef struct ob_index {
    char *name;   /* NULL: made from the table and fields */
    int unique;
    int nfields;
    char *fields[OB_INDEX_FIELDS];
} ob_index;

typedef struct ob_table {
    char *name;
    char *was;            /* in a change: the name before */
    int nfields;
    ob_field *fields;
    int nkey;             /* 0: the Counter field is the key */
    char *key[OB_KEY_MAX];
    int nindexes;
    ob_index *indexes;
    int nrelations;       /* the relationships where this table is the child */
    ob_relation *relations;
    int adopted;          /* read from a table OpenBase didn't make */
    int without_rowid;    /* a WITHOUT ROWID table (not OpenBase's): can't be rebuilt */
} ob_table;

/* Building a design in memory */
ob_table *ob_table_new(const char *name, int with_counter, ob_error *err); /* the Counter is "id" */
ob_field *ob_table_add_field(ob_table *t, const char *name, ob_type type, ob_error *err);
ob_field *ob_table_field(const ob_table *t, const char *name); /* any case */
int ob_table_remove_field(ob_table *t, const char *name, ob_error *err);
ob_index *ob_table_add_index(ob_table *t, const char *field, int unique, ob_error *err);
int ob_table_add_index_field(ob_index *ix, const char *field, ob_error *err);
ob_relation *ob_table_add_relation(ob_table *t, const char *field, const char *parent,
                                   ob_relrule on_delete, ob_error *err);
int ob_table_remove_relation(ob_table *t, const char *field);
ob_table *ob_table_copy(const ob_table *t, ob_error *err);
void ob_table_free(ob_table *t);

/* The CREATE TABLE statement for a design (for tests and the SQL view) */
int ob_table_create_sql(const ob_table *t, const char *as_name, ob_buf *out, ob_error *err);
/* Names: 1 to OB_NAME_MAX bytes of UTF-8, no control characters, not
 * starting oadb_ or sqlite_. */
int ob_name_check(const char *name, const char *what, ob_error *err);

/* In a project */
int ob_table_create(ob_project *p, const ob_table *t, ob_error *err);
int ob_table_read(ob_project *p, const char *name, ob_table **out, ob_error *err);
int ob_table_exists(ob_project *p, const char *name);
int ob_table_drop(ob_project *p, const char *name, ob_error *err);
/* The user's tables, sorted by name; free with ob_names_free */
int ob_table_list(ob_project *p, char ***names, int *count, ob_error *err);
void ob_names_free(char **names, int count);

/* What a change found */
typedef struct ob_change_result {
    int rebuilt;            /* copied and renamed; 0: ALTER TABLE did it */
    long rows;              /* rows in the table, before and after */
    long bad_values;        /* values that didn't fit their field's new type */
    long first_bad_row;     /* its rowid */
    char first_bad_field[OB_NAME_MAX + 1];
    long cleared;           /* values cleared (OB_CHANGE_CLEAR_BAD) */
    int dropped_indexes;    /* indexes on fields that went */
    int dropped_relations;  /* relationships of fields that went */
} ob_change_result;

enum {
    OB_CHANGE_CLEAR_BAD = 1 /* clear values that don't convert, instead of refusing */
};

/* Changes a table to a new design. Each of the new design's fields names
 * its old field in `was` (NULL for a new one); fields left out go. The
 * key, indexes and relationships name fields as they were before the
 * change (as ob_table_read gave them), or by a new field's name. Renames
 * use ALTER TABLE; anything ALTER can't do (a type, a rule, a key, a
 * relationship, a field dropped) copies the rows into a new table and
 * renames it, in one transaction, with foreign keys checked after.
 * Nothing changes unless every row fits. */
int ob_table_change(ob_project *p, const char *name, const ob_table *design,
                    unsigned flags, ob_change_result *res, ob_error *err);

/* Relationships (the child table is changed) */
int ob_relation_add(ob_project *p, const ob_relation *r, ob_error *err);
int ob_relation_remove(ob_project *p, const char *child, const char *child_field, ob_error *err);
/* Every relationship in the project; free with ob_relations_free */
int ob_relation_list(ob_project *p, ob_relation **out, int *count, ob_error *err);
void ob_relations_free(ob_relation *r, int count);
void ob_relation_clear(ob_relation *r);

/* Indexes */
int ob_index_add(ob_project *p, const char *table, const ob_index *ix, ob_error *err);
int ob_index_remove(ob_project *p, const char *name, ob_error *err);

#endif
