PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share

.PHONY: build install uninstall

TARGET_DIR = $(or $(CARGO_TARGET_DIR),target)

build:
	cargo build --release --locked

install:
	install -Dm755 $(TARGET_DIR)/release/wonderterm $(DESTDIR)$(BINDIR)/wonderterm
	install -Dm644 resources/wonderterm.desktop $(DESTDIR)$(DATADIR)/applications/wonderterm.desktop
	install -Dm644 resources/wonderterm.svg $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/wonderterm.svg

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/wonderterm
	rm -f $(DESTDIR)$(DATADIR)/applications/wonderterm.desktop
	rm -f $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/wonderterm.svg
