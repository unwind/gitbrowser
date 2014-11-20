#
# Trivial Makefile to build the gitbrowser Geany plugin.
#

BASENAME=gitbrowser

CFLAGS=`pkg-config --cflags geany` -fPIC -Wall -pedantic -g
LDLIBS=`pkg-config --libs geany`

PLUGINDIR=`pkg-config --variable=libdir geany`/geany

.PHONY:		clean install install-dev

# --------------------------------------------------------------

$(BASENAME).so:	$(BASENAME).o levenshtein.o
		$(CC) -shared -o $@ $(LDLIBS) $^

$(BASENAME).o:	$(BASENAME).c

# --------------------------------------------------------------

# Test program for playing with the Levenshtein implementation.
leventest:	leventest.c levenshtein.c levenshtein.h
	$(CC) `pkg-config --cflags glib-2.0` -o $@ $^ `pkg-config --libs glib-2.0`

# --------------------------------------------------------------

clean:
	rm -f *.o *.so


# Installation for "end users", who don't want to keep the source around.
install:
	cp $(BASENAME).so $(PLUGINDIR)


# Installation for developers, who want to be able to iterate quickly.
install-dev:
	ln --force --symbolic `pwd`/$(BASENAME).so $(PLUGINDIR)
