# OpenBase

OpenBase is the OpenAmiga relational database and application builder: tables, relationships, visual queries, forms, reports, Datatypes, ARexx automation, and OpenPrint/OpenWrite integration in a native Amiga interface.

## Status

**Phase B2, the program, is in review; B1, the portable core, is done.** The design is
[`docs/gfoffice/Design-OpenBase.md`](https://github.com/DalsinAI/gfoffice/blob/main/docs/gfoffice/Design-OpenBase.md)
in DalsinAI/gfoffice (the GFOffice Suite's shared pieces are in
`Design-Suite-Shared.md` beside it). OpenWrite's gate is met (that design,
section 1.1).

| Phase | What | State |
| --- | --- | --- |
| B0 | SQLite's soft-float flavour; locking, journal and speed on the lab's 68040 | In part: `build-os3.sh` builds SQLite in soft float, one thread, temporary tables in memory; the unix-none VFS, with a public semaphore so one program at a time has a project. Journal modes and speed not measured yet |
| B1 | `.oadb` projects, field types, tables changed the copy-and-rename way, relationships, CSV in and out, the query model and its SQL. No window | Done: `src/core/`, host-tested |
| B2 | The program: object browser, table designer, datasheets, auto forms, relationships view, Datatype object fields | In review: `app/openbase.c`, checked in a lab |
| B3, B4 | Designers and reports; ARexx and runtime mode | Not started |

## Using OpenBase (B2)

One window: the toolbar, the object browser (the project's tables, then
Relationships) on the left, the table's datasheet with the record's form
under it, and the status bar.

- **Project > New Project...** makes a `.oadb` file; **Open Project...**
  (Amiga-O) opens one, as does a project given on the command line or as
  a Workbench argument (`OpenBase Work:Shop.oadb`). One OpenBase at a
  time has a project; a second is told it is open elsewhere.
  **Check Project** runs SQLite's integrity and key checks.
- **Table > New Table...** (Amiga-T) and **Design Table...**: the table's
  fields, each a Text, Memo, Whole number, Number, Currency, Date, Time,
  Date and time, Yes/No, Lookup or Datatype object, required or not. A
  Lookup field points to another table (Points to): a record that points
  to nothing is refused, and so is deleting a record others point to.
  Design Table changes a table and keeps its records.
- **Relationships** (Amiga-R): the tables as boxes, a line from each
  Lookup field to the key it points to, and each relationship in words.
- **Record**: New Record (Amiga-N), Save Record (Amiga-S), Undo Changes
  (Amiga-Z), Delete Record... (the toolbar). Cursor up and down move
  through the records. A value the field can't take is refused with a
  sentence naming the field.
- **Load Picture...** (Amiga-L) keeps any file a datatype reads in the
  record (a picture is drawn beside the form); Clear Picture takes it out.

## Building for the Amiga

```sh
sh build-amiga.sh                 # Cradle's stove_build (target .): fetches the pinned pieces, then:
OS32_GCC16=~/AmigaChrome/stoves/os32-gcc16 SQLITE_SRC=... SQLITE_PORT=... OGT=... sh build-os3.sh [OUT]
```

`build-amiga.sh` fetches SQLite by `scripts/sqlite.lock`, and
DalsinAI/openamigasqlite and DalsinAI/opengadtools at the commits in
`deps/`. OUT holds `OpenBase` and `Images/Toolbar/`: copy both into one
drawer. A 68040 with or without an FPU (GCC 16, with
`-fno-late-combine-instructions`).

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
| `app/` | The Amiga program: `openbase.c`, its stack (`ob_stack.c`) and the last drawer (`lastdir.h`) |
| `Images/Toolbar/` | The Chrome toolbar pictures (opengadtools' `tools/iconset`) |
| `deps/` | The openamigasqlite and opengadtools commits the Amiga build fetches |

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
