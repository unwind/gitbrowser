#
# Trivial Makefile to build the gitbrowser Geany plugin.
#

CFLAGS=`pkg-config --cflags geany` -fPIC -Wall -pedantic -g
LDLIBS=`pkg-config --libs geany`

.PHONY:		clean

# --------------------------------------------------------------

gitbrowser.so:	gitbrowser.o
		gcc -shared -o $@ $(LDLIBS) $<

gitbrowser.o:	gitbrowser.c

# --------------------------------------------------------------

clean:
	rm -f *.o *.so
