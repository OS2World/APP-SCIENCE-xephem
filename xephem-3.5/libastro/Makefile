# Makefile for the basic astronomy routines.
# The idea is to compile and archive them into libastro.a

# flags assume gcc
CFLAGS= -O2 -ffast-math -Wall
CC = cc

OBJS =	\
	aa_hadec.o \
	aberration.o \
	actan.o \
	airmass.o \
	anomaly.o \
	ap_as.o \
	auxil.o \
	chap95.o \
	chap95_data.o \
	circum.o \
	comet.o \
	dbfmt.o \
	deep.o \
	deltat.o \
	earthsat.o \
	eq_ecl.o \
	eq_gal.o \
	formats.o \
	helio.o \
	libration.o \
	misc.o \
	mjd.o \
	moon.o \
	mooncolong.o \
	nutation.o \
	obliq.o \
	parallax.o \
	plans.o \
	precess.o \
	reduce.o \
	refract.o \
	rings.o \
	riset.o \
	riset_cir.o \
	sdp4.o \
	sgp4.o \
	sphcart.o \
	sun.o \
	thetag.o \
	utc_gst.o \
	twobody.o \
	vsop87.o \
	vsop87_data.o

libastro.a:	$(OBJS)
	ar rv $@ $?
	ranlib $@

libastro.so:    $(OBJS)
	$(CC) -shared -o $@ $(OBJS)

clobber:
	rm -f *.o libastro.a

# For RCS Only -- Do Not Edit
# @(#) $RCSfile: Makefile,v $ $Date: 2001/10/16 21:45:05 $ $Revision: 1.14 $ $Name:  $
