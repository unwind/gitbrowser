#
# Top-level Makefile for the gitbrowser Geany plugin. Not much to see, here.
#

.PHONY:	clean install install-dev

# --------------------------------------------------------------

all:
	$(MAKE) -C src

# --------------------------------------------------------------

clean:
	$(MAKE) -C src clean

install:
	$(MAKE) -C src install

install-dev:
	$(MAKE) -C src install-dev
