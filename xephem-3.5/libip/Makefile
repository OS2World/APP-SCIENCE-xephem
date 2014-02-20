# Makefile for image processing routines, libip.
# (C) 2001 Elwood Charles Downey

# These flags assume gcc.
CFLAGS= -I../libastro -O2 -ffast-math -Wall
CC = cc

OBJS =	\
	explodegif.o	\
	fits.o		\
	fsmatch.o	\
	gaussfit.o	\
	lstsqr.o	\
	median.o	\
	sqr.o		\
	stars.o		\
	stats.o		\
	walk.o		\
	wcs.o

libip.a:	$(OBJS)
	ar rv $@ $?
	ranlib $@

libip.so:    $(OBJS)
	gcc -shared -o $@ $(OBJS)

clobber:
	rm -f *.o libip.a

# For RCS Only -- Do Not Edit
# @(#) $RCSfile: Makefile,v $ $Date: 2001/10/09 22:09:53 $ $Revision: 1.3 $ $Name:  $
