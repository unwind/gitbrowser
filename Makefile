#
# Top-level Makefile for the gitbrowser Geany plugin. Not much to see, here.
#

.PHONY:	clean

# --------------------------------------------------------------

all:
	cd src && $(MAKE)

# --------------------------------------------------------------

clean:
	cd src && $(MAKE) clean
