PREFIX ?= /usr/local
DESTDIR ?=

BINDIR := $(DESTDIR)$(PREFIX)/bin
DATADIR := $(DESTDIR)$(PREFIX)/share/wondermirror
APPDIR := $(DESTDIR)$(PREFIX)/share/applications
ICONDIR := $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps

.PHONY: install install-freerdp uninstall test

install:
	install -Dm755 src/wondermirror $(BINDIR)/wondermirror
	install -d $(DATADIR)/lib/wondermirror $(DATADIR)/guest
	install -m644 src/lib/wondermirror/*.py src/lib/wondermirror/*.qml $(DATADIR)/lib/wondermirror/
	install -m644 guest/*.ps1 $(DATADIR)/guest/
	install -Dm644 wondermirror.desktop $(APPDIR)/wondermirror.desktop
	install -Dm644 wondermirror-keyboard.desktop $(APPDIR)/wondermirror-keyboard.desktop
	install -Dm644 assets/wondermirror.svg $(ICONDIR)/wondermirror.svg
	install -Dm644 assets/wondermirror-keyboard.svg $(ICONDIR)/wondermirror-keyboard.svg

# FreeRDP's X11 client with patches/freerdp-*.patch, which Wonder Mirror uses
# instead of the system's (context menus of Windows apps need it). Needs git,
# cmake, ninja and what FreeRDP itself is built from; run it again whenever the
# system's FreeRDP is updated.
install-freerdp:
	patches/build-freerdp.sh $(DESTDIR)$(PREFIX)

# Only the program goes. With PREFIX=~/.local, $(DATADIR) is also where the
# Windows machine lives: it and the launchers of its apps belong to the user
# and stay; `wondermirror remove` deletes those.
uninstall:
	rm -f $(BINDIR)/wondermirror
	rm -rf $(DATADIR)/lib $(DATADIR)/guest
	rm -f $(DESTDIR)$(PREFIX)/lib/wondermirror/xfreerdp3
	rm -f $(APPDIR)/wondermirror.desktop $(ICONDIR)/wondermirror.svg
	rm -f $(APPDIR)/wondermirror-keyboard.desktop $(ICONDIR)/wondermirror-keyboard.svg

test:
	tests/run.sh
