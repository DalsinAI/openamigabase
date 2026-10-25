# OpenBase

OpenBase is the OpenAmiga relational database and application builder: tables, relationships, visual queries, forms, reports, Datatypes, ARexx automation, and OpenPrint/OpenWrite integration in a native Amiga interface.

## Status

**Phase B1, the portable core, is being written.** The design is
[`docs/gfoffice/Design-OpenBase.md`](https://github.com/DalsinAI/gfoffice/blob/main/docs/gfoffice/Design-OpenBase.md)
in DalsinAI/gfoffice (the GFOffice Suite's shared pieces are in
`Design-Suite-Shared.md` beside it). OpenWrite's gate is met (that design,
section 1.1).

| Phase | What | State |
| --- | --- | --- |
| B0 | SQLite's soft-float flavour in openamigasqlite; locking, journal and speed on the lab's 68040 | Not started (needs the stove and a lab) |
| B1 | `.oadb` projects, field types, tables changed the copy-and-rename way, relationships, CSV in and out, the query model and its SQL. No window | In progress: `src/core/`, host-tested |
| B2 | The program: object browser, table designer, datasheets, auto forms, relationships view, Datatype object fields | Not started |
| B3, B4 | Designers and reports; ARexx and runtime mode | Not started |

## Building and testing the core

The core is plain C99 with no Amiga calls, built and tested on any
computer with a C compiler and SQLite:

```sh
make test     # the host tests
make check    # the same under AddressSanitizer and UBSan
```

They use the computer's own SQLite (`pkg-config sqlite3`). To use the
SQLite openamigasqlite pins, with its build options, instead:

```sh
scripts/fetch-sqlite.sh third_party/sqlite
make check SQLITE_SRC=third_party/sqlite/sqlite-amalgamation-3530400
```

SQLite is public domain and used as a library: it is fetched at build time
and never committed here. CI (`.github/workflows/host-tests.yml`) runs both.

| Where | What |
| --- | --- |
| `include/` | The core's headers: `openbase_core.h` includes them all |
| `src/core/` | Projects (`ob_project.c`), field types (`ob_types.c`), tables and relationships (`ob_schema.c`), CSV (`ob_csv.c`), queries (`ob_query.c`) |
| `tests/` | The host tests; `tests/sql/` holds the query generator's golden files |
| `app/` | The Amiga program, from phase B2 |

## Direction

OpenBase is data-first rather than chrome-first:

- compact, system-themed OpenGadTools interface
- hideable/resizable object browser
- dense native controls
- persistent status bar
- OpenRTG used for rich design surfaces and previews
- SQLite-backed `.oadb` projects
- visual table, relationship, query, form and report designers
- Datatype Object fields
- OpenWrite for rich text/document composition
- OpenPrint for reports, labels, invoices and PDF/print output
- ARexx automation
- application/runtime mode for finished database front ends

## Repository

Related projects: `DalsinAI/gfoffice` (OpenWrite and the suite's designs),
`DalsinAI/openamigasqlite` (SQLite for AmigaOS).

## Contributors

OpenBase is created and maintained by [SacredTrees](https://github.com/SacredTrees) with The AmigaChrome Team, copyright AmigaChrome Team. Everyone whose work it includes is credited in [`CONTRIBUTORS.md`](CONTRIBUTORS.md).
