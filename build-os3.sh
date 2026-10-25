#!/bin/sh
# OpenBase for AmigaOS 3.x, 68040, no FPU needed: SQLite (soft float, one
# thread), the core and the program, with the os32-gcc16 stove.
#
#   build-os3.sh [OUT]          (default build/os3)
#
#   OS32_GCC16   the stove's root, holding prefix/ (default ~/AmigaChrome/stoves/os32-gcc16)
#   SQLITE_SRC   the amalgamation's folder, as scripts/fetch-sqlite.sh unpacks it (scripts/sqlite.lock)
#   SQLITE_PORT  a DalsinAI/openamigasqlite checkout: its AmigaDOS-names patch and compat files
#   OGT          a DalsinAI/opengadtools checkout: lib/ (the toolbar, list, sections, status bar,
#                theme) and specs/m68k-soft-m68040.specs
#
# build-amiga.sh fetches all three at the commits deps/ names and runs this.
# SQLite is built as openamigasqlite builds it (no WAL, no mmap, no extension
# loading; the unix-none VFS registered by the program), with these changes
# for OpenBase (Design-OpenBase.md, 11.2): soft float, SQLITE_THREADSAFE=0
# (one connection, one task, no libpthread) and SQLITE_OMIT_JSON.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${1:-$HERE/build/os3}
S=${OS32_GCC16:-$HOME/AmigaChrome/stoves/os32-gcc16}
CC="$S/prefix/bin/m68k-amigaos-gcc"
: "${SQLITE_SRC:?set SQLITE_SRC to the SQLite amalgamation (scripts/fetch-sqlite.sh)}"
: "${SQLITE_PORT:?set SQLITE_PORT to an openamigasqlite checkout}"
: "${OGT:?set OGT to an opengadtools checkout}"
GCCV=$("$CC" -dumpversion 2>/dev/null || echo 0)
[ "${GCCV%%.*}" -ge 16 ] 2>/dev/null || { echo "$CC is GCC $GCCV: OpenBase builds with GCC 16 (os32-gcc16)" >&2; exit 2; }
SPECS="$OGT/specs/m68k-soft-m68040.specs"
# GCC 16: -fno-late-combine-instructions (the Team's rule for GCC 16 builds)
CPU="-noixemul -m68040 -specs=$SPECS"
SAFE="-fno-late-combine-instructions -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -fno-common"
OBJ="$OUT/obj"
mkdir -p "$OBJ"

# SQLite: the amalgamation with openamigasqlite's patch (AmigaDOS names kept as given)
rm -rf "$OBJ/sqlite"
mkdir -p "$OBJ/sqlite"
cp "$SQLITE_SRC/sqlite3.c" "$SQLITE_SRC/sqlite3.h" "$OBJ/sqlite/"
(cd "$OBJ/sqlite" && patch -s -p1 < "$SQLITE_PORT/patches/sqlite-3.53.4-amiga-paths.patch")
SQLITE_OPTS="-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0 \
    -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED -DSQLITE_DQS=0 -DSQLITE_OMIT_JSON -DSQLITE_TEMP_STORE=3 -DHAVE_USLEEP=1 \
    -DSQLITE_OS_UNIX=1 -D_DEFAULT_SOURCE=1 -D_POSIX_TIMERS=1 \
    -D_POSIX_REALTIME_SIGNALS=1"
# shellcheck disable=SC2086
"$CC" $CPU -O2 $SAFE $SQLITE_OPTS -include "$SQLITE_PORT/compat/sqlite_amiga_pre.h" \
    -c "$OBJ/sqlite/sqlite3.c" -o "$OBJ/sqlite3.o"
# shellcheck disable=SC2086
"$CC" $CPU -O2 $SAFE -c "$SQLITE_PORT/compat/sqlite_amiga.c" -o "$OBJ/sqlite_amiga.o"
echo "sqlite3.o: $(wc -c < "$OBJ/sqlite3.o") bytes"

# The core and the program
WARN="-std=gnu99 -Wall -Werror -Wno-pointer-sign"
INC="-I$HERE/include -I$HERE/src/core -I$OBJ/sqlite -I$HERE/app -I$OGT/lib"
OGT_SRC=""
for f in ogt_theme ogt_draw ogt_icons ogt_iconfile ogt_iconset ogt_toolbar ogt_list ogt_font ogt_status ogt_sections; do
    OGT_SRC="$OGT_SRC $OGT/lib/$f.c"
done
# shellcheck disable=SC2086
"$CC" $CPU -O2 $SAFE $WARN $INC \
    "$HERE"/src/core/*.c "$HERE"/app/*.c $OGT_SRC "$OBJ/sqlite3.o" "$OBJ/sqlite_amiga.o" -lm -lamiga \
    -o "$OUT/OpenBase"
echo "$OUT/OpenBase ($(wc -c < "$OUT/OpenBase") bytes)"
# the toolbar's pictures (PROGDIR:Images/Toolbar/; made by opengadtools' tools/iconset)
mkdir -p "$OUT/Images/Toolbar"
cp "$HERE"/Images/Toolbar/*.ilbm "$OUT/Images/Toolbar/"
rm -rf "$OBJ"
