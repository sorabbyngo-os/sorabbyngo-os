# ============================================================
# libsorabbyngo Makefile
# ============================================================

CC      = gcc
AR      = ar
CFLAGS  = -Wall -Wextra -O2 -fPIC -I./include
LDFLAGS = -lssl -lcrypto
SRC_DIR = src
INC_DIR = include
OUT_DIR = build

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OUT_DIR)/%.o)

LIB_STATIC  = $(OUT_DIR)/libsorabbyngo.a
LIB_SHARED  = $(OUT_DIR)/libsorabbyngo.so

.PHONY: all clean install static shared

all: static shared

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

static: $(LIB_STATIC)

$(LIB_STATIC): $(OBJS)
	$(AR) rcs $@ $^
	@echo "Built: $@"

shared: $(LIB_SHARED)

$(LIB_SHARED): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

install: all
	install -d /usr/local/lib /usr/local/include
	install -m 644 $(LIB_STATIC) /usr/local/lib/
	install -m 755 $(LIB_SHARED) /usr/local/lib/
	install -m 644 $(INC_DIR)/sorabbyngo.h /usr/local/include/
	ldconfig
	@echo "Installed libsorabbyngo"

clean:
	rm -rf $(OUT_DIR)
