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

$(OUT)/%.o: src/%.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT):
	mkdir -p $@

test: 
	@echo "no tests yet"

clean:
	rm -rf build

.PHONY: all release test clean
