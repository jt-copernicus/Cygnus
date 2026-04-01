PREFIX ?= /usr/local
CC = gcc
CFLAGS = -Wall -O2 -std=c99
LDFLAGS = -lX11
XSESSIONDIR = /usr/share/xsessions

all: cygnus cygnus-fm cygnus-mount cygnus-shot cygnus-view cygnus-edit cygnus-clock cygnus-calc cygnus-media cygnus-open cygnus-cam cygnus-paint-build

cygnus-paint-build:
	$(MAKE) -C cygnus-paint

cygnus: cygnus.c
	$(CC) $(CFLAGS) cygnus.c -o cygnus $(LDFLAGS)

cygnus-cam: cygnus-cam.c
	$(CC) $(CFLAGS) cygnus-cam.c -o cygnus-cam $(shell pkg-config --cflags --libs sdl2) -lm

cygnus-fm: cygnus-fm.c
	$(CC) $(CFLAGS) cygnus-fm.c -o cygnus-fm $(LDFLAGS) -lm

cygnus-open: cygnus-open.c
	$(CC) $(CFLAGS) cygnus-open.c -o cygnus-open $(LDFLAGS)

cygnus-mount: cygnus-mount.c
	$(CC) $(CFLAGS) cygnus-mount.c -o cygnus-mount $(LDFLAGS)

cygnus-shot: cygnus-shot.c
	$(CC) $(CFLAGS) cygnus-shot.c -o cygnus-shot $(LDFLAGS)

cygnus-view: cygnus-view.c
	$(CC) $(CFLAGS) cygnus-view.c -o cygnus-view $(LDFLAGS) -lm

cygnus-edit: cygnus-edit.c
	$(CC) $(CFLAGS) cygnus-edit.c -o cygnus-edit $(LDFLAGS)

cygnus-clock: cygnus-clock.c
	$(CC) $(CFLAGS) cygnus-clock.c -o cygnus-clock $(LDFLAGS)

cygnus-calc: cygnus-calc.c
	$(CC) $(CFLAGS) cygnus-calc.c -o cygnus-calc $(LDFLAGS) -lm

cygnus-media: cygnus-media.c
	$(CC) $(CFLAGS) cygnus-media.c -o cygnus-media $(shell pkg-config --cflags --libs libavformat libavcodec libswscale libswresample libavutil sdl2 SDL2_ttf) -lm -lX11

install: cygnus cygnus-fm cygnus-mount cygnus-shot cygnus-view cygnus-edit cygnus-clock cygnus-calc cygnus-media cygnus-open cygnus-cam cygnus-paint-build
	install -D -m 755 cygnus $(DESTDIR)$(PREFIX)/bin/cygnus
	install -D -m 755 cygnus-fm $(DESTDIR)$(PREFIX)/bin/cygnus-fm
	install -D -m 755 cygnus-cam $(DESTDIR)$(PREFIX)/bin/cygnus-cam
	install -D -m 755 cygnus-paint/cygnus-paint $(DESTDIR)$(PREFIX)/bin/cygnus-paint
	install -D -m 755 cygnus-open $(DESTDIR)$(PREFIX)/bin/cygnus-open
	install -D -m 755 cygnus-mount $(DESTDIR)$(PREFIX)/bin/cygnus-mount
	install -D -m 755 cygnus-shot $(DESTDIR)$(PREFIX)/bin/cygnus-shot
	install -D -m 755 cygnus-view $(DESTDIR)$(PREFIX)/bin/cygnus-view
	install -D -m 755 cygnus-edit $(DESTDIR)$(PREFIX)/bin/cygnus-edit
	install -D -m 755 cygnus-clock $(DESTDIR)$(PREFIX)/bin/cygnus-clock
	install -D -m 755 cygnus-calc $(DESTDIR)$(PREFIX)/bin/cygnus-calc
	install -D -m 755 cygnus-media $(DESTDIR)$(PREFIX)/bin/cygnus-media
	install -D -m 644 cygnus.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus.1
	install -D -m 644 cygnus-cam.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-cam.1
	install -D -m 644 cygnus-paint/cygnus-paint.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-paint.1
	install -D -m 644 cygnus-shot.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-shot.1
	install -D -m 644 cygnus-view.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-view.1
	install -D -m 644 cygnus-edit.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-edit.1
	install -D -m 644 cygnus-calc.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-calc.1
	install -D -m 644 cygnus-media.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-media.1
	install -D -m 644 cygnus-fm.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-fm.1
	install -D -m 644 cygnus-open.1 $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-open.1
	install -D -m 644 cygnus.desktop $(DESTDIR)$(XSESSIONDIR)/cygnus.desktop


uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-fm
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-cam
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-paint
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-open
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-mount
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-shot
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-view
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-edit
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-clock
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-calc
	rm -f $(DESTDIR)$(PREFIX)/bin/cygnus-media
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-cam.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-paint.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-shot.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-view.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-edit.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-calc.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-media.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-fm.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cygnus-open.1
	rm -f $(DESTDIR)$(XSESSIONDIR)/cygnus.desktop

clean:
	rm -f cygnus cygnus-fm cygnus-cam cygnus-open cygnus-mount cygnus-shot cygnus-view cygnus-edit cygnus-clock cygnus-calc cygnus-media
	$(MAKE) -C cygnus-paint clean

.PHONY: all install uninstall clean
