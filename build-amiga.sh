#!/bin/sh
# OpenBase for Cradle's stove_build (target .): Cradle checks out this repository alone and sets STOVE and OUT,
# so this fetches what the build needs, each at the commit or checksum this repository pins, then runs
# build-os3.sh:
#   - SQLite's amalgamation (scripts/sqlite.lock: its URL and SHA-256), through scripts/fetch-sqlite.sh;
#   - DalsinAI/openamigasqlite at deps/openamigasqlite.ref: its AmigaDOS-names patch and compat files;
#   - DalsinAI/opengadtools at deps/opengadtools.ref: the toolbar, list, sections, status bar and theme,
#     and the soft-float specs.
# A local build can run build-os3.sh itself with OS32_GCC16, SQLITE_SRC, SQLITE_PORT and OGT set.
# MIT, Copyright (c) 2026 AmigaChrome Team (Team 5, 25 October 2026).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-$HERE/build/os3}
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
DEPS=${DEPS:-$OUT/deps}
GIT_BASE=${AMIGACHROME_GIT_BASE:-https://github.com/}
mkdir -p "$DEPS"

# fetch REPO REF: DalsinAI/REPO at REF (a commit, or a branch) into $DEPS/REPO
fetch() {
    if [ ! -d "$DEPS/$1/.git" ]; then
        git init -q "$DEPS/$1" &&
            git -C "$DEPS/$1" fetch -q --depth 1 "${GIT_BASE}DalsinAI/$1" "$2" &&
            git -C "$DEPS/$1" checkout -q FETCH_HEAD ||
            { echo "build-amiga.sh: couldn't fetch DalsinAI/$1 at $2" >&2; exit 1; }
    fi
    echo "build-amiga.sh: $1 at $(git -C "$DEPS/$1" rev-parse --short HEAD) ($2)"
}
ref() { head -n 1 "$HERE/deps/$1.ref" | tr -d '[:space:]'; }

fetch openamigasqlite "$(ref openamigasqlite)"
fetch opengadtools "$(ref opengadtools)"
SQLITE_SRC=$(sh "$HERE/scripts/fetch-sqlite.sh" "$DEPS/sqlite" | tail -n 1)
echo "build-amiga.sh: SQLite from $SQLITE_SRC"
OS32_GCC16=$STOVE SQLITE_SRC=$SQLITE_SRC SQLITE_PORT="$DEPS/openamigasqlite" OGT="$DEPS/opengadtools" \
    sh "$HERE/build-os3.sh" "$OUT"
rm -rf "$DEPS"   # the job's out/ keeps the program, not the sources
