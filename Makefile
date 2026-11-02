CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L \
				 -Wall -Wextra -Wpedantic -Wshadow -Wconversion

LDLIBS = -lm

MODE ?=  debug
OUT = build/$(MODE)

ifeq ($(MODE), release)
CFLAGS += -O2
else
CFLAGS += -g -fsanitize=address,undefined
endif

# The test build is the debug build with every warning turned into an error.
ifeq ($(MODE), test)
CFLAGS += -Werror
endif

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=$(OUT)/%.o)
# Everything except main.o: what a test program links against.
LIB_OBJ = $(filter-out $(OUT)/main.o, $(OBJ))

TEST_SRC = $(wildcard tests/test_*.c)
TEST_BIN = $(TEST_SRC:tests/%.c=$(OUT)/tests/%)
# Tests too slow for every run: built the same way, run by test-slow.
SLOW_SRC = $(wildcard tests/slow_*.c)
SLOW_BIN = $(SLOW_SRC:tests/%.c=$(OUT)/tests/%)
# Code only the tests use, linked into every test program: the scripted
# brain. The program itself never contains it.
TEST_SUPPORT_OBJ = $(OUT)/tests/brain_script.o

all: $(OUT)/agon

release:
	$(MAKE) MODE=release

$(OUT)/agon: $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# -MMD also writes a .d file next to each object listing the headers the
# .c file included; -MP adds an empty rule per header so deleting one does
# not break the build. Including the .d files makes an object rebuild when
# any header it uses changes.
$(OUT)/%.o: src/%.c | $(OUT)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/tests/brain_script.o: tests/brain_script.c | $(OUT)/tests
	$(CC) $(CFLAGS) -Isrc -MMD -MP -c $< -o $@

# One program per test file. -Isrc lets the tests write #include "rng.h".
$(OUT)/tests/%: tests/%.c $(LIB_OBJ) $(TEST_SUPPORT_OBJ) | $(OUT)/tests
	$(CC) $(CFLAGS) -Isrc -MMD -MP $< $(LIB_OBJ) $(TEST_SUPPORT_OBJ) \
		-o $@ $(LDLIBS)

-include $(OBJ:.o=.d) $(TEST_BIN:=.d) $(SLOW_BIN:=.d) \
	$(TEST_SUPPORT_OBJ:.o=.d)

$(OUT) $(OUT)/tests:
	mkdir -p $@

test:
	$(MAKE) MODE=test run-tests

# Runs every test program, even after one fails, and fails if any did.
run-tests: $(TEST_BIN)
	@failed=0; \
	for t in $(TEST_BIN); do ./$$t || failed=1; done; \
	exit $$failed

test-slow:
	$(MAKE) MODE=test run-slow-tests

run-slow-tests: $(SLOW_BIN)
	@failed=0; \
	for t in $(SLOW_BIN); do ./$$t || failed=1; done; \
	exit $$failed

# Where make install puts things: PREFIX is the usual setting, and DESTDIR
# (empty unless a packager sets it) goes in front of it, to build a package
# without touching the real system.
PREFIX ?= /usr/local

install: release
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/doc/agon
	install -m 755 build/release/agon $(DESTDIR)$(PREFIX)/bin/agon
	install -m 644 agon.conf.example $(DESTDIR)$(PREFIX)/share/doc/agon/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/agon
	rm -f $(DESTDIR)$(PREFIX)/share/doc/agon/agon.conf.example
	-rmdir $(DESTDIR)$(PREFIX)/share/doc/agon

clean:
	rm -rf build

.PHONY: all release test run-tests test-slow run-slow-tests install uninstall \
	clean
