#!/bin/sh
# Fetches the SQLite amalgamation pinned in scripts/sqlite.lock, checks its
# SHA-256 and unpacks it into DIR. Then:
#   make check SQLITE_SRC=DIR/sqlite-amalgamation-3530400
# Copyright (c) 2026 AmigaChrome Team. MIT licence.
set -eu
dir=${1:?usage: fetch-sqlite.sh DIR}
lock=$(dirname "$0")/sqlite.lock
get() { sed -n "s/^$1=//p" "$lock"; }
file=$(get file)
url=$(get url)
sum=$(get sha256)
mkdir -p "$dir"
if [ ! -f "$dir/$file" ]; then
    curl -fsSL -o "$dir/$file.part" "$url"
    mv "$dir/$file.part" "$dir/$file"
fi
echo "$sum  $dir/$file" | sha256sum -c -
rm -rf "$dir/${file%.zip}"
python3 -I -m zipfile -e "$dir/$file" "$dir"
echo "$dir/${file%.zip}"
