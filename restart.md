# Restart: OpenBase

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

OpenBase, the Open relational database and application builder: tables, visual queries, forms, reports, ARexx, OpenPrint and OpenWrite integration.

## Where it stands

_Updated 25 October 2026 by Team 5._ Dale said to start (25 October, 02:55)
and accepted the design's recommended answers: `docs/gfoffice/Design-OpenBase.md`
in DalsinAI/gfoffice. Phase B1, the portable core, is in `src/core/` with host
tests in `tests/` (`make test`, `make check`), merged (#8). Phase B2, the
program, is `app/openbase.c` on the core, built by `build-amiga.sh` with
Cradle's os32-gcc16 stove and checked in a lab (project, tables, records,
a Lookup that refuses an orphan and a delete, a picture field, quit and
reopen). Records and Datatype objects for the program are in
`src/core/ob_record.c`.

## Open pull requests

- Team 5's B2 (branch `team5/openbase-b2-20261025`).

## Next step

1. B0's rest in the lab: `DELETE` against `TRUNCATE` journals, the RAM:
   issue, the size and speed of design section 11.3 (the soft-float,
   single-thread SQLite and the semaphore in place of file locks are in
   `build-os3.sh` and `app/openbase.c`). Answers go into `ob_config`
   (`include/ob_core.h`), the one place they are set.
2. B2's rest: a Lookup shown by the record it points to (not its number),
   CSV in and out from the menus, a hideable browser, the datasheet
   sorted by a column.
3. B3: the query, form and report designers.

## Waiting on @SacredTrees

- Nothing.

## Who owns it

Team 5 (B1, B2).

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenWrite_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.
