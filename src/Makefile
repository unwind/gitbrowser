#
#
#


CFLAGS=`pkg-config --cflags geany` -fPIC

.PHONY:		clean



gitbrowser.so:	gitbrowser.o
		gcc -shared -o $@ $<

gitbrowser.o:	gitbrowser.c



clean:
	rm -f *.o *.so
