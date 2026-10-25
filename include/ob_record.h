/* OpenBase core: records, for the program's datasheets and forms (phase B2,
 * Design-OpenBase.md 5.4, 6.1, 6.3 and 4.3).
 *
 * A record is found by its key: its rowid, which is the Counter of every
 * table OpenBase makes. Values go both ways as text, by their field's type:
 * read for showing (Currency as 1,234.50, dates in the project's order,
 * Yes/No as the project's words, a Datatype object as its file's name) and
 * written from what was typed (the same parsers as CSV import and the
 * query grid). SQLite's refusals come back in plain words ("Price must be a
 * number.", a relationship's "...doesn't match a record in Customers.").
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#ifndef OB_RECORD_H
#define OB_RECORD_H

#include "ob_schema.h"

/* How many records a table holds. */
int ob_record_count(ob_project *p, const char *table, long *count, ob_error *err);

/* Up to n keys, in key order, from the offset-th record on: a datasheet's
 * page (5.4). *got says how many there were. */
int ob_record_keys(ob_project *p, const char *table, long offset, int n, long long *keys, int *got,
                   ob_error *err);

/* One record as text, a value for each of t's fields (t from ob_table_read),
 * "" for none; *values is an array of t->nfields strings, freed with
 * ob_record_free. OB_ERR_NOT_FOUND when there is no such record. */
int ob_record_read(ob_project *p, const ob_table *t, long long key, char ***values, ob_error *err);
void ob_record_free(char **values, int n);

/* Writes what was typed: texts[i] for t->fields[i]; NULL leaves a field as
 * it is (a new record: its default), "" empties it. The Counter and
 * Datatype objects are not written here. *key 0: a new record, and *key
 * gets its key; else that record is changed. Refusals in plain words
 * (OB_ERR_RULE), a value that isn't its type's (OB_ERR_INVALID) names its
 * field. */
int ob_record_write(ob_project *p, const ob_table *t, long long *key, const char *const *texts, ob_error *err);

/* Deletes a record; a relationship that refuses says so (OB_ERR_RULE). */
int ob_record_delete(ob_project *p, const char *table, long long key, ob_error *err);

/* A Datatype object field (4.3): its bytes, its file's name and its kind
 * ("picture", "sound"...), as the record's. size above the project's
 * max_object_kib is refused (OB_ERR_LIMIT). data NULL clears it. */
int ob_object_set(ob_project *p, const char *table, const char *field, long long key, const void *data,
                  long size, const char *name, const char *kind, ob_error *err);
/* Its bytes (ob_alloc'd, *data NULL and *size 0 for none) and name. */
int ob_object_get(ob_project *p, const char *table, const char *field, long long key, void **data, long *size,
                  char *name, size_t name_size, ob_error *err);

#endif
