CC = gcc
# -I adds a folder to where #include "x.h" looks, so a file in one folder
# can include a header from another by its bare name.
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L \
				 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
				 -Isrc -Isrc/gfx -Isrc/window

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

# The code comes in groups, by what it may depend on.
# TERM: the terminal program's own files. CORE: everything shared by both
# programs (it never touches the terminal or SDL). GFX: pure drawing into a
# block of pixels. WIN: the window program's own files.
TERM_SRC = src/main.c src/term.c src/render.c src/cells_term.c
CORE_SRC = $(filter-out $(TERM_SRC), $(wildcard src/*.c))
GFX_SRC = $(wildcard src/gfx/*.c)
WIN_SRC = $(wildcard src/window/*.c)

CORE_OBJ = $(CORE_SRC:src/%.c=$(OUT)/%.o)
TERM_OBJ = $(TERM_SRC:src/%.c=$(OUT)/%.o)
GFX_OBJ = $(GFX_SRC:src/%.c=$(OUT)/%.o)
WIN_OBJ = $(WIN_SRC:src/%.c=$(OUT)/%.o)
OBJ = $(CORE_OBJ) $(TERM_OBJ)
# What a test program links against: everything except main.o.
LIB_OBJ = $(filter-out $(OUT)/main.o, $(OBJ)) $(GFX_OBJ)

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

# The window program is built only when asked for: everything else builds and
# tests with no SDL installed. = (not :=) means sdl2-config runs only when a
# rule actually uses these. -isystem is -I for headers that are not ours, so
# our strict warnings are not applied to SDL's own code.
SDL_CFLAGS = $(patsubst -I%,-isystem %,$(shell sdl2-config --cflags))
SDL_LIBS = $(shell sdl2-config --libs)

window: $(OUT)/agon-window

$(OUT)/agon-window: $(CORE_OBJ) $(GFX_OBJ) $(WIN_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS)

# Only this one file sees SDL's headers.
$(OUT)/window/platform_sdl.o: CFLAGS += $(SDL_CFLAGS)

# Runs both programs on SDL's dummy video driver: no screen needed.
test-window:
	$(MAKE) MODE=test run-window-smoke

run-window-smoke: $(OUT)/agon $(OUT)/agon-window
	sh tests/window_smoke.sh $(OUT)

$(OUT)/agon: $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# -MMD also writes a .d file next to each object listing the headers the
# .c file included; -MP adds an empty rule per header so deleting one does
# not break the build. Including the .d files makes an object rebuild when
# any header it uses changes.
$(OUT)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/tests/brain_script.o: tests/brain_script.c | $(OUT)/tests
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# One program per test file.
$(OUT)/tests/%: tests/%.c $(LIB_OBJ) $(TEST_SUPPORT_OBJ) | $(OUT)/tests
	$(CC) $(CFLAGS) -MMD -MP $< $(LIB_OBJ) $(TEST_SUPPORT_OBJ) \
		-o $@ $(LDLIBS)

-include $(OBJ:.o=.d) $(GFX_OBJ:.o=.d) $(WIN_OBJ:.o=.d) $(TEST_BIN:=.d) $(SLOW_BIN:=.d) \
	$(TEST_SUPPORT_OBJ:.o=.d)

$(OUT)/tests:
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

# Make deletes "intermediate" files it had to build on the way to something
# else; a bare .SECONDARY: keeps them, so objects survive between runs.
.SECONDARY:

.PHONY: all release window test-window run-window-smoke test run-tests test-slow run-slow-tests install uninstall \
	clean
