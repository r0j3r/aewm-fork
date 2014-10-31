# aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.

# Change compile options here if desired
CC = gcc
CFLAGS = -g -O2 -fno-strict-aliasing -Wall

# If you are using X11R7, change this to /usr
XROOT = /usr/X11R6

# Where to install
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1
CFGDIR = $(PREFIX)/etc/aewm

# Uncomment to enable Shape extension support
#OPT_WMFLAGS += -DSHAPE
#OPT_WMLIB += -lXext

# Uncomment to add Xft support
#OPT_WMFLAGS += -DXFT `pkg-config --cflags xft`
#OPT_WMLIB += `pkg-config --libs xft` -lXext

# Uncomment to print debugging info
#OPT_WMFLAGS += -DDEBUG
