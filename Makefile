CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -O2
LDFLAGS =

PKG_CONFIG = pkg-config
PKGS = xcb xcb-keysyms xcb-randr

CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDFLAGS += $(shell $(PKG_CONFIG) --libs $(PKGS)) -lm

SRC = tinawm.c client.c keys.c layout.c bar.c monitor.c ewmh.c util.c
OBJ = $(SRC:.c=.o)
BIN = tinawm

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
