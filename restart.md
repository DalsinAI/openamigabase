# Restart: OpenBase

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

OpenBase, the Open relational database and application builder: tables, visual queries, forms, reports, ARexx, OpenPrint and OpenWrite integration.

## Where it stands

_Updated 25 October 2026 by Team 5._ Dale said to start (25 October, 02:55)
and accepted the design's recommended answers: `docs/gfoffice/Design-OpenBase.md`
in DalsinAI/gfoffice. Phase B1, the portable core, is in `src/core/` with host
tests in `tests/` (`make test`, `make check`).

## Open pull requests

- Team 5's B1 draft (branch `team5/openbase-b1-20261025`).

## Next step

1. B0 (openamigasqlite and the lab): the soft-float, single-thread flavour;
   `unix-dotfile` locking; `DELETE` against `TRUNCATE` journals; the RAM:
   issue; the size and speed of design section 11.3. Its answers go into
   `ob_config` (`include/ob_core.h`), the one place they are set.
2. B2: the Amiga program in `app/` on the core.

## Waiting on @SacredTrees

- Nothing for B1. B0 needs the stove and a lab.

## Who owns it

Team 5 (B1).

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenWrite_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.
