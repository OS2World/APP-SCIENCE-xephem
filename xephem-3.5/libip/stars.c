#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "ip.h"

#define	BRB		8		/* trash border to skip */
#define	GAUSL		16		/* guassian row length */
#define	MINSEP		5		/* min pix sep between legit stars */
#define	NREG		3		/* NREG*NREG sep local noise regions */

static double gauss2d (Gaussian *hgp, Gaussian *vgp, int x, int y);

/* extract some "stars" from fip, no guarantee of perfection or completeness.
 * return malloced arrays of locations sorted by brightest pixel.
 * N.B. caller must /always/ free *xpp and *ypp regardless of return value.
 */
int
quickStars (FImage *fip, int burnt, double std, double **xpp, double **ypp)
{
	int thresh[NREG][NREG];	/* local star thresholds */
	CamPix *ip = (CamPix *)fip->image;
	int w = fip->sw;	/* handy */
	int h = fip->sh;	/* handy */
	CamPix *br;		/* temp array to sort by brightness */
	int walk[8];		/* fast way to walk around a pixel */
	ImRegion imr;		/* for stats and walking to bright spots */
	int rw, rh;		/* region size */
	double *xp, *yp;
	int x, y;
	int i, nis;

	/* prime the arrays */
	xp = (double *) malloc (10*sizeof(double));
	yp = (double *) malloc (10*sizeof(double));
	br = (CamPix *) malloc (10*sizeof(CamPix));
	if (!xp || !yp || !br) {
	    printf ("Out of memory to find stars\n");
	    exit(1);
	}
	nis = 0;

	/* determine min threshold in each region */
	imr.rw = rw = (w-2*BRB)/NREG + NREG;	/* round up */
	imr.rh = rh = (h-2*BRB)/NREG + NREG;	/* round up */
	imr.im = ip;
	imr.iw = w;
	imr.ih = h;
	for (y = 0; y < NREG; y++) {
	    imr.ry = BRB + y*rh;
	    for (x = 0; x < NREG; x++) {
		ImStats ims;
		imr.rx = BRB + x*rw;
		regionStats (&imr, &ims);
		thresh[y][x] = (int)(ims.median + std*ims.std);
	    }
	}

	/* set up the neighbor walk */
	walk[0] =  1;
	walk[1] =  1 - w;
	walk[2] =    - w;
	walk[3] = -1 - w;
	walk[4] = -1;
	walk[5] = -1 + w;
	walk[6] =      w;
	walk[7] =  1 + w;

	/* scan inside border for local maxima */
	ip += BRB*w + BRB;
	for (y = BRB; y < h-BRB; y++) {
	    int *trow = thresh[(y-BRB)/rh];
	    for (x = BRB; x < w-BRB; x++) {
		int p = *ip;

		/* must be above threshold and not burned out */
		if (p < trow[(x-BRB)/rw] || p >= burnt)
		    goto no;

		/* must be true peak */
		for (i = 0; i < 8; i++)
		    if (p < ip[walk[i]] || ip[walk[i]] <= ip[2*walk[i]])
			goto no;

		/* must be unique to within MINSEP */
		for (i = 0; i < nis; i++)
		    if (fabs(xp[i]-x)<MINSEP && fabs(yp[i]-y)<MINSEP)
			goto no;
		
		/* ok! insert in order of decreasing brightness */
		xp = (double*) realloc ((char *)xp, (nis+1)*sizeof(double));
		yp = (double*) realloc ((char *)yp, (nis+1)*sizeof(double));
		br = (CamPix*) realloc ((char *)br, (nis+1)*sizeof(CamPix));
		for (i = nis; i > 0 && p > br[i-1]; --i) {
		    br[i] = br[i-1];
		    xp[i] = xp[i-1];
		    yp[i] = yp[i-1];
		}
		br[i] = p;
		xp[i] = x;
		yp[i] = y;
		nis++;

	      no:

		ip++;
	    }
	    ip += 2*BRB;
	}

	free ((char *)br);
	*xpp = xp;
	*ypp = yp;
	return (nis);
}

/* given a position in fip to just integer precision, refine IN PLACE by
 * using intersection of gaussian in each direction. if trouble, leave
 * original position unchanged.
 * we just use fip to get the pixels and size.
 */
void
refinePos (FImage *fip, double *xp, double *yp)
{
	ImRegion imr;
	Star s;
	int x, y;

	/* take int claim seriously */
	x = (int)floor(*xp + .5);
	y = (int)floor(*yp + .5);

	/* confirm enough image for surrounding cuts */
	if (x<GAUSL/2 || x+GAUSL/2>=fip->sw || y<GAUSL/2 || y+GAUSL/2>=fip->sh)
	    return;

	/* build region */
	imr.im = (CamPix *) fip->image;
	imr.iw = fip->sw;
	imr.ih = fip->sh;
	imr.rx = x - GAUSL/2;
	imr.ry = y - GAUSL/2;
	imr.rw = GAUSL;
	imr.rh = GAUSL;

	/* find star better */
	if (getStar (&imr, &s) == 0) {
	    *xp = s.x;
	    *yp = s.y;
	}
}

/* given a region that presumably contains a single star, fill in sp
 * with best-fit 2d commensurate gaussian.
 * return 0 if ok, else -1
 * N.B. we assume rp has already been clamped.
 */
int
getStar (ImRegion *rp, Star *sp)
{
	CamPix *im = &rp->im[rp->ry*rp->iw + rp->rx];
	int wrap = rp->iw - rp->rw;
	int nr = rp->rw * rp->rh;
	double sum = 0, sum2 = 0;
	double v;
	int x, y;

	/* on guard */
	if (nr < 2) {
	    printf ("getStar called with region of 1\n");
	    return (-1);
	}

	/* find commensurate gaussians */
	if (gauss2fit (rp, &sp->hg, &sp->vg) < 0)
	    return (-1);

	/* position is with respect to corner of region */
	sp->x = rp->rx + sp->hg.m;
	sp->y = rp->ry + sp->vg.m;

	/* compare ideal to real to estimate noise */
	for (y = 0; y < rp->rh; y++) {
	    for (x = 0; x < rp->rw; x++) {
		double ideal = gauss2d (&sp->hg, &sp->vg, x, y);
		double real = (double)*im++;
		double err = ideal - real;
		sum += err;
		sum2 += sqr(err);
	    }
	    im += wrap;
	}

	/* error is STARSIGMA sigma noise */
	v = (sum2 - sum*sum/nr)/(nr-1);
	sp->err = v <= 0 ? 0 : STARSIGMA*sqrt(v);

	return (0);
}

/* find the magnitude of s1 wrt s0 and the error in the estimate.
 * gaussian volume taken to be proportional to height * sigmax * sigmay.
 * return 0 if ok, else -1 if numerical troubles.
 */
int
cmpStars (Star *s0, Star *s1, double *magp, double *errp)
{
	double v0, v1, r;

	/* magnitude is ratio of guassian volumes */
	v0 = s0->hg.A * s0->hg.s * s0->vg.s;
	v1 = s1->hg.A * s1->hg.s * s1->vg.s;
	if (v0 == 0 || (r = v1/v0) <= 0)
	    return (-1);
	*magp = -2.511886*log10(r);		/* + is dimmer */

	/* error = (log(largest ratio) - log(smallest ratio))/2.
	 *       = (log((largest ratio) / (smallest ratio)))/2.
	 * final /2 because we want to report as +/-
	 */
	v1 = (s1->hg.A + s1->err)/(s0->hg.A - s0->err);	/* largest */
	v0 = (s1->hg.A - s1->err)/(s0->hg.A + s0->err);	/* smallest */
	if (v0 == 0)
	    return (-1);
	*errp = (2.511886/2)*log10(fabs(v1/v0));

	/* made it */
	return (0);
}

/* compute value of 2d gaussian at [x,y] */
static double
gauss2d (Gaussian *hgp, Gaussian *vgp, int x, int y)
{
	return (hgp->B + hgp->A*exp(-.5*(sqr((x-hgp->m)/hgp->s)+
	                                 sqr((y-vgp->m)/vgp->s))));
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: stars.c,v $ $Date: 2001/10/13 05:00:39 $ $Revision: 1.4 $ $Name:  $"};
