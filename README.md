# OpenBase

OpenBase is the OpenAmiga relational database and application builder: tables, relationships, visual queries, forms, reports, Datatypes, ARexx automation, and OpenPrint/OpenWrite integration in a native Amiga interface.

## Status

**Design complete. Implementation queued behind OpenWrite.**

OpenBase is intentionally not entering active implementation while `DalsinAI/openamigawrite` is still in its initial build phase. This keeps engineering focus on getting the shared OpenAmiga document/UI/printing stack stable first.

### Implementation gate

Begin OpenBase implementation once OpenWrite has a stable native baseline covering:

- native application/editor shell
- OpenGadTools UI conventions
- DOCX/ODT core workflow
- OpenPrint integration
- stable shared OpenAmiga build/tooling conventions

OpenBase should then reuse those proven conventions rather than inventing parallel infrastructure.

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

Implementation will begin here after the OpenWrite gate is met.

Related project: `DalsinAI/openamigawrite`.

## Contributors

OpenBase is created and maintained by [SacredTrees](https://github.com/SacredTrees) with the AmigaChrome agent team, copyright Dalsin Limited. Everyone whose work it includes is credited in [`CONTRIBUTORS.md`](CONTRIBUTORS.md).
