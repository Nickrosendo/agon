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

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=$(OUT)/%.o)

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

-include $(OBJ:.o=.d)

$(OUT):
	mkdir -p $@

test: 
	@echo "no tests yet"

clean:
	rm -rf build

.PHONY: all release test clean
