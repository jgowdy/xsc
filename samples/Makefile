# Simple Makefile for building XSC sample binaries.

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?=

PREFIX ?= ./bin

all: $(PREFIX)/xsc_ring_demo

$(PREFIX):
	mkdir -p $(PREFIX)

$(PREFIX)/xsc_ring_demo: xsc_ring_demo.c | $(PREFIX)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -rf $(PREFIX)

.PHONY: all clean
