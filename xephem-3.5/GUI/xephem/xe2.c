/* Fetch the desired region of xe2-formatted catalog stars.
 * This format supports proper motion. The format is:
 *
 *  Note 1: all multibyte quantities are big-endian, aka, network byte order
 *  Note 2: header consists of ASCII version number followed by newline.
 *  Byte Bits  Description
 *  0    2..0  Name code:
 *              0: PPM
 *              1: SAO
 *              2: HD
 *              3: Tycho 2
 *              4: HIP
 *              5: GSC22
 *              6..7: reserved
 *  0    5..3  Type code:
 *              0: star
 *              1: star-like
 *              2: double
 *              3..7: reserved
 *  0    7..6  Reserved
 *  1..4       Name value is just the number except for Tycho 2:
 *                TYC1 = (B[3]<<6)|(B[4]>>2)
 *                TYC2 = (B[1]<<8)|(B[2])
 *                TYC3 = (B[4]&3)
 *  5..7       RA, J2000, rads, 0 .. 2*PI mapped to 0 .. 1<<24
 *  8..10      Dec, J2000, rads, -PI/2 .. PI/2 mapped to 0 .. (1<<24)-1
 *  11         Magnitude, -2..17 mapped to 0..255
 *  12..13     Spectral class, ASCII characters, left justified, blank filled
 *  14..15     Proper motion in RA, 2*(mas/yr)*cos(dec) + 32768
 *  16..17     Proper motion in Dec, 2*(mas/yr) + 32768
 */

#include <stdio.h>
#include <math.h>

#if defined(__STDC__)
#include <stdlib.h>
#include <string.h>
#else
extern void *malloc(), *realloc();
#endif

#ifndef SEEK_SET
#define	SEEK_SET 0
#define	SEEK_END 2
#endif

#include "P_.h"
#include "astro.h"
#include "circum.h"

extern FILE *fopenh P_((char *name, char *how));
extern Now *mm_get_now P_((void));
extern char *syserrstr P_((void));
extern void pm_set P_((int percentage));
extern void zero_mem P_((void *loc, unsigned len));

#define	XE2PKTSZ	18		/* packet size - DO NOT CHANGE */

typedef unsigned char UC;
typedef unsigned long UL;
typedef UC PKT[XE2PKTSZ];


/* an array of ObjF which can be grown efficiently in mults of NINC */
typedef struct {
    ObjF *mem;          /* malloced array */
    int max;            /* max number of cells in mem[] or -1 to just count */
    int used;           /* number actually in use */
} ObjFArray;

#define NINC    32      /* grow mem mem this many at a time */

static FILE *xe2open P_((char *file, char msg[]));
static void unpackObj P_((PKT pkt, Now *np, Obj *op));
static int addOneObjF P_((ObjFArray *ap, ObjF *fop));
static void unpack P_((PKT pkt, double *ra, double *dec, double *pma,
    double *pmd, double *mag, char name[MAXNM], char *spect, int *type));

/* return 0 if file looks like a xe2, else -1
 */
int
xe2chkfile (file, msg)
char *file;
char *msg;
{
	FILE *fp;

	fp = xe2open (file, msg);
	if (!fp)
	    return (-1);
	fclose (fp);
	return (0);
}

/* malloc a collection of XE2 stars around the given location at *opp.
 * if trouble fill msg[] and return -1, else return count.
 * N.B. memory is only actually malloced at *opp if we return > 0.
 * N.B. we very much rely on file being sorted by increasing dec.
 */
int
xe2fetch (file, np, ra, dec, fov, mag, opp, msg)
char *file;
Now *np;
double ra, dec;
double fov, mag;
ObjF **opp;
char *msg;
{
	double rov;
	double cdec;
	double sdec;
	double crov;
	ObjFArray ofa;
	FILE *fp;
	long l0, l, u, m;
	double ldec, udec;
	Obj o;
	PKT pkt;

	/* initial setup */
	rov = fov/2;
	cdec = cos(dec);
	sdec = sin(dec);
	crov = cos(rov);

	/* bug if not given a place to save new stars */
	if (!opp) {
	    printf ("xe2fetch with opp == NULL\n");
	    exit(1);
	}

	/* open the XE2 file */
	fp = xe2open (file, msg);
	if (!fp)
	    return (-1);
	l0 = ftell(fp);

	/* binary search for star nearest lower dec */
	ldec = dec - rov;
	if (ldec < -PI/2)
	    ldec = -PI/2;
	l = 0;
	fseek (fp, 0, SEEK_END);
	u = (ftell(fp)-l0) / XE2PKTSZ - 1;
	while (l <= u) {
	    m = (l+u)/2;
	    if (fseek (fp, m*XE2PKTSZ+l0, SEEK_SET) < 0) {
		(void) sprintf (msg, "%s:\nseek error: %s", file, syserrstr());
		fclose (fp);
		return (-1);
	    }
	    if (fread (pkt, XE2PKTSZ, 1, fp) != 1) {
		(void) sprintf (msg, "%s:\nread error: %s", file, syserrstr());
		fclose (fp);
		return (-1);
	    }
	    unpackObj (pkt, np, &o);
	    if (ldec < o.f_dec)
		u = m-1;
	    else
		l = m+1;
	}

	/* add each entry from m up to upper dec */
	udec = dec + rov;
	if (udec > PI/2)
	    udec = PI/2;
	ofa.mem = NULL;
	ofa.max = 0;
	ofa.used = 0;
	while (o.f_dec <= udec) {
	    pm_set ((int)((o.f_dec - ldec)/(udec - ldec)*100));

	    if (get_mag(&o) <= mag && sin(o.f_dec)*sdec +
				    cos(o.f_dec)*cdec*cos(ra-o.f_RA) >= crov) {
		if (addOneObjF (&ofa, (ObjF *)&o) < 0) {
		    (void) sprintf (msg, "%s:\nno more memory", file);
		    fclose (fp);
		    if (ofa.mem)
			free ((void *)ofa.mem);
		    return (-1);
		}
	    }

	    if (fread (pkt, XE2PKTSZ, 1, fp) != 1) {
		if (feof(fp))
		    break;
		(void) sprintf (msg, "%s:\nread error: %s", file, syserrstr());
		fclose (fp);
		if (ofa.mem)
		    free ((void *)ofa.mem);
		return (-1);
	    }

	    unpackObj (pkt, np, &o);
	}

	fclose (fp);
	if (ofa.mem)
	    *opp = ofa.mem;
	return (ofa.used);
}

/* open the xe2 file.
 * return the FILE * positioned at first record if ok,
 * else NULL with a reason in msg[].
 */
static FILE *
xe2open (file, msg)
char *file;
char msg[];
{
	FILE *fp = fopenh (file, "rb");
	char header[32];

	if (!fp) {
	    (void) sprintf (msg, "%s:\n%s", file, syserrstr());
	    return (NULL);
	}
	if (fgets (header, sizeof(header), fp) == NULL) {
	    sprintf (msg, "%s:\nno header: %s", file, syserrstr());
	    fclose (fp);
	    return(NULL);
	}
	if (strncmp (header, "XE2.", 4)) {
	    sprintf (msg, "%s:\nnot an XE2 file", file);
	    fclose (fp);
	    return(NULL);
	}

	return (fp);
}

/* crack open pkt and fill in op, allowing for proper motion at np->n_mjd. */
static void
unpackObj (pkt, np, op)
PKT pkt;
Now *np;
Obj *op;
{
#define	MASRAD(mas)	degrad((mas)/(3600.*1000.))
	double ra;
	double dec;
	double pma;
	double pmd;
	double mag;
	char name[MAXNM];
	char spect[32];
	int type;

	unpack (pkt, &ra, &dec, &pma, &pmd, &mag, name, spect, &type);

	zero_mem ((void *)op, sizeof (*op));

	(void) strncpy (op->o_name, name, MAXNM-1);
	op->o_type = FIXED;
	op->f_class = type == 2 ? 'D' : type == 1 ? 'T' : 'S';
	op->f_spect[0] = spect[0] == ' ' ? '\0' : spect[0];
	op->f_spect[1] = spect[1] == ' ' ? '\0' : spect[1];
	op->f_dec = (float)(dec + MASRAD(pmd)*(mjd-J2000)/365.24);
	op->f_RA = (float)(ra + MASRAD(pma)/cos(op->f_dec)*(mjd-J2000)/365.24);
	op->f_epoch = (float)J2000;
	set_fmag (op, mag);
#undef MASRAD
}

/* add one ObjF entry to ap[], growing if necessary.
 * return 0 if ok else return -1
 */
static int
addOneObjF (ap, fop)
ObjFArray *ap;
ObjF *fop;
{
	ObjF *newf;

	if (ap->used >= ap->max) {
	    /* add room for NINC more */
	    char *newmem = ap->mem ? realloc ((void *)ap->mem,
						(ap->max+NINC)*sizeof(ObjF))
				   : malloc (NINC*sizeof(ObjF));
	    if (!newmem)
		return (-1);
	    ap->mem = (ObjF *)newmem;
	    ap->max += NINC;
	}

	newf = &ap->mem[ap->used++];
	(void) memcpy ((void *)newf, (void *)fop, sizeof(ObjF));

	return (0);
}

/* unpack the raw PKT into its basic parts.
 */
static void
unpack (pkt, ra, dec, pma, pmd, mag, name, spect, type)
PKT pkt;
double *ra;
double *dec;
double *pma;
double *pmd;
double *mag;
char name[MAXNM];
char *spect;
int *type;
{
	UL t;

	/* type code */
	*type = (pkt[0]>>3) & 7;

	/* RA, rads */
	t = ((UL)pkt[5] << 16) | ((UL)pkt[6] << 8) | (UL)pkt[7];
	*ra = 2*PI*t/(1L<<24);

	/* Dec, rads */
	t = ((UL)pkt[8] << 16) | ((UL)pkt[9] << 8) | (UL)pkt[10];
	*dec = PI*t/((1L<<24)-1) - PI/2;

	/* mag */
	t = (UL)pkt[11];
	*mag = (19./255.)*t - 2;

	/* spect */
	spect[0] = pkt[12];
	spect[1] = pkt[13];

	/* pm ra, mas/yr*cos(dec) */
	t = ((UL)pkt[14] << 8) | (UL)pkt[15];
	*pma = (t-32768.)/2.;

	/* pm dec, mas/yr */
	t = ((UL)pkt[16] << 8) | (UL)pkt[17];
	*pmd = (t-32768.)/2.;

	/* name */
	if ((pkt[0]&7) == 3) {
	    /* tycho format */
	    UL tyc1 = ((UL)pkt[3] << 6) | ((UL)pkt[4] >> 2);
	    UL tyc2 = ((UL)pkt[1] << 8) | ((UL)pkt[2]);
	    UL tyc3 = ((UL)pkt[4] & 3);
	    sprintf (name, "Tyc %ld-%ld-%ld", tyc1, tyc2, tyc3); /* MAXNM! */
	} else {
	    /* just a number */
	    t = ((UL)pkt[1] << 24) | ((UL)pkt[2] << 16) | ((UL)pkt[3] << 8)
								| (UL)pkt[4];
	    switch (pkt[0]&7) {
	    case 0: sprintf (name, "PPM %ld", t); break;
	    case 1: sprintf (name, "SAO %ld", t); break;
	    case 2: sprintf (name, "HD %ld", t); break;
	    case 4: sprintf (name, "HIP %ld", t); break;
	    case 5: sprintf (name, "GSC2201 %c %ld", *dec<0?'S':'N', t); break;
	    default: sprintf (name, "<Bogus>"); break;
	    }
	}
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: xe2.c,v $ $Date: 2001/09/28 05:13:11 $ $Revision: 1.7 $ $Name:  $"};
