# aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.

OBJ = aesession.o
X11OBJ = aedesk.o lib/menu.o lib/util.o
WMOBJ = aewm_init.o aewm_client.o aewm_event.o aewm_manip.o
GTKOBJ = aemenu.o aepanel.o
ALLOBJ = $(OBJ) $(X11OBJ) $(WMOBJ) $(GTKOBJ)
WM_H = aewm.h

BIN = aesession
X11BIN = aedesk
WMBIN = aewm
GTKBIN = aemenu aepanel
ALLBIN = $(BIN) $(X11BIN) $(WMBIN) $(GTKBIN)

X11FLAGS = -I$(XROOT)/include
WMFLAGS = $(X11FLAGS) $(OPT_WMFLAGS)
GTKFLAGS = `pkg-config --cflags gtk+-2.0`
INC = -I./lib

X11LIB = -L$(XROOT)/lib -lX11
WMLIB = $(X11LIB) $(OPT_WMLIB)
GTKLIB = `pkg-config --libs gtk+-2.0`

AEMAN = aewm.1x aeclients.1x
AERC = aewmrc clientsrc

all: $(ALLBIN)
aesession: aesession.o
aedesk: aedesk.o lib/util.o
aemenu: aemenu.o lib/menu.o lib/util.o
aepanel: aepanel.o lib/menu.o lib/util.o
aewm: $(WMOBJ) lib/util.o

$(WMOBJ): $(WM_H)

install: all
	mkdir -p $(BINDIR) $(MANDIR) $(CFGDIR)
	install -s $(ALLBIN) $(BINDIR)
	for i in $(AEMAN); do \
	    install -m 644 doc/$$i $(MANDIR); \
	    gzip -9 $(MANDIR)/$$i; \
	done
	for i in $(AERC); do \
	    install -m 644 doc/$$i.ex $(CFGDIR)/$$i; \
	done
	for i in $(BIN) $(X11BIN) $(GTKBIN); do \
	    ln -sf aeclients.1x.gz $(MANDIR)/$$i.1x.gz; \
	done

clean:
	rm -f $(ALLBIN) $(ALLOBJ)

.PHONY: all install clean
