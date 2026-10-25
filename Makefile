# OpenBase's portable core (src/core) and its host tests, on this computer.
# The Amiga program (app/) comes in phase B2.
#   make          build/host/libobcore.a and the test program
#   make test     the tests
#   make check    the tests under AddressSanitizer and UBSan
#
# SQLite: the host's libsqlite3 (pkg-config sqlite3) by default. To build
# against the amalgamation openamigasqlite pins instead, with its options
# (never committed here):
#   scripts/fetch-sqlite.sh third_party/sqlite
#   make check SQLITE_SRC=third_party/sqlite/sqlite-amalgamation-3530400

CC ?= cc
CFLAGS ?= -O2
WARN = -std=c99 -Wall -Wextra -Wpedantic
SAN = -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
ifdef SQLITE_SRC
OUT = build/host-pinned
SQLITE_CFLAGS = -I$(SQLITE_SRC)
# openamigasqlite's build.sh options, single-threaded as OpenBase's flavour
# will be (Design-OpenBase.md, 11.2)
SQLITE_DEFS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0 \
	-DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED -DSQLITE_DQS=0
SQLITE_LIBS = -lm
SQLITE_OBJ = $(OUT)/sqlite3.o
SQLITE_OBJ_ASAN = $(OUT)/sqlite3-asan.o
else
OUT = build/host
SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS := $(shell pkg-config --libs sqlite3 2>/dev/null || echo -lsqlite3)
SQLITE_OBJ =
SQLITE_OBJ_ASAN =
endif

CORE_SRC = $(wildcard src/core/*.c)
CORE_H = $(wildcard include/*.h) src/core/ob_internal.h
TEST_SRC = $(wildcard tests/*.c)
TEST_H = tests/obtest.h
INC = -Iinclude -Isrc/core $(SQLITE_CFLAGS)

all: $(OUT)/libobcore.a $(OUT)/test-openbase

$(OUT)/libobcore.a: $(CORE_SRC) $(CORE_H)
	mkdir -p $(OUT)/obj
	cd $(OUT)/obj && $(CC) $(WARN) $(CFLAGS) $(addprefix -I$(CURDIR)/,include src/core) $(SQLITE_CFLAGS) -c $(addprefix $(CURDIR)/,$(CORE_SRC))
	ar rcs $@ $(addprefix $(OUT)/obj/,$(notdir $(CORE_SRC:.c=.o)))

$(OUT)/sqlite3.o: $(SQLITE_SRC)/sqlite3.c
	mkdir -p $(OUT)
	$(CC) -O2 $(SQLITE_DEFS) -c $< -o $@

# SQLite itself under AddressSanitizer only; UBSan watches OpenBase's code
$(OUT)/sqlite3-asan.o: $(SQLITE_SRC)/sqlite3.c
	mkdir -p $(OUT)
	$(CC) -O1 -g -fsanitize=address -fno-omit-frame-pointer $(SQLITE_DEFS) -c $< -o $@

$(OUT)/test-openbase: $(CORE_SRC) $(CORE_H) $(TEST_SRC) $(TEST_H) $(SQLITE_OBJ)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) $(INC) -Itests $(CORE_SRC) $(TEST_SRC) $(SQLITE_OBJ) $(SQLITE_LIBS) -o $@

$(OUT)/test-openbase-asan: $(CORE_SRC) $(CORE_H) $(TEST_SRC) $(TEST_H) $(SQLITE_OBJ_ASAN)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SAN) $(INC) -Itests $(CORE_SRC) $(TEST_SRC) $(SQLITE_OBJ_ASAN) $(SQLITE_LIBS) -o $@

test: $(OUT)/test-openbase
	$(OUT)/test-openbase all --sql tests/sql

check: $(OUT)/test-openbase-asan
	$(OUT)/test-openbase-asan all --sql tests/sql

# Rewrites the SQL golden files from what the generator writes now; read
# the diff before committing it.
golden-update: $(OUT)/test-openbase
	OB_GOLDEN_UPDATE=1 $(OUT)/test-openbase golden --sql tests/sql

clean:
	rm -rf build

.PHONY: all test check golden-update clean
