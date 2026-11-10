PREFIX ?= /usr/local
DESTDIR ?=

BINDIR := $(DESTDIR)$(PREFIX)/bin
DATADIR := $(DESTDIR)$(PREFIX)/share/wonderupdate
UNITDIR := $(DESTDIR)$(PREFIX)/lib/systemd/user
APPDIR := $(DESTDIR)$(PREFIX)/share/applications
ICONDIR := $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps

# The checkout this is installed from: WonderUpdate updates the desktop from it.
SOURCE := $(abspath ../..)

.PHONY: install uninstall test

install:
	install -Dm755 src/wonderupdate $(BINDIR)/wonderupdate
	install -d $(DATADIR)/lib $(UNITDIR)
	install -m644 src/lib/*.sh $(DATADIR)/lib/
	install -m644 wonderupdate.conf.example $(DATADIR)/wonderupdate.conf.example
	if [ -e $(SOURCE)/.git ] && [ -z "$(DESTDIR)" ]; then echo $(SOURCE) > $(DATADIR)/source; fi
	sed 's|@BINDIR@|$(PREFIX)/bin|' systemd/wonderupdate.service > $(UNITDIR)/wonderupdate.service
	install -m644 systemd/wonderupdate.timer $(UNITDIR)/wonderupdate.timer
	install -Dm644 wonderupdate.desktop $(APPDIR)/wonderupdate.desktop
	install -Dm644 assets/wonderupdate.svg $(ICONDIR)/wonderupdate.svg

uninstall:
	rm -f $(BINDIR)/wonderupdate
	rm -rf $(DATADIR)
	rm -f $(UNITDIR)/wonderupdate.service $(UNITDIR)/wonderupdate.timer
	rm -f $(APPDIR)/wonderupdate.desktop $(ICONDIR)/wonderupdate.svg

test:
	tests/run.sh
