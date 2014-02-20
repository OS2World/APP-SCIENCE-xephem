CC = cc
CLDFLAGS = 
CFLAGS = $(CLDFLAGS) -O -I../.. -I../../../../libastro
LDFLAGS = $(CLDFLAGS)
LIBS = -lm

# Try using these libs for SVR4.
# LIBS = -lm -lsocket -lnsl

OBJS = gscload.o support.o ../../gsc.o

gscload: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

# For RCS Only -- Do Not Edit
# @(#) $RCSfile: Makefile,v $ $Date: 1998/02/27 19:36:36 $ $Revision: 1.1 $ $Name:  $
