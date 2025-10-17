# Makefile for XSC userspace tools

CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=gnu11
LDFLAGS := -lpthread

TOOLS := xsc-doorbell-probe

all: $(TOOLS)

xsc-doorbell-probe: xsc-doorbell-probe.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TOOLS) *.o

install: $(TOOLS)
	install -D -m 0755 xsc-doorbell-probe $(DESTDIR)/usr/bin/xsc-doorbell-probe

.PHONY: all clean install
