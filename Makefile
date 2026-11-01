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

# One program per test file. -Isrc lets the tests write #include "rng.h".
$(OUT)/tests/%: tests/%.c $(LIB_OBJ) | $(OUT)/tests
	$(CC) $(CFLAGS) -Isrc -MMD -MP $< $(LIB_OBJ) -o $@ $(LDLIBS)

-include $(OBJ:.o=.d) $(TEST_BIN:=.d)

$(OUT) $(OUT)/tests:
	mkdir -p $@

test:
	$(MAKE) MODE=test run-tests

# Runs every test program, even after one fails, and fails if any did.
run-tests: $(TEST_BIN)
	@failed=0; \
	for t in $(TEST_BIN); do ./$$t || failed=1; done; \
	exit $$failed

clean:
	rm -rf build

.PHONY: all release test run-tests clean
