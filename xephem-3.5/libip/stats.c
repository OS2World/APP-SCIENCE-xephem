/* basic pixel statistics */

#include <stdlib.h>
#include <math.h>

#include "ip.h"

/* given an image and a region within, return some basic stats.
 * N.B. we do /not/ check that the region has finite area and lies within image.
 */
void
regionStats (ImRegion *rp, ImStats *sp)
{
	CamPix fastmedarray[100*100];		/* avoid malloc if can */
	CamPix *bigmedarray = NULL;
	CamPix *medarray, *map;
	CamPix *im = &rp->im[rp->ry*rp->iw + rp->rx];
	int atx = rp->rx, aty = rp->ry;
	double sum = 0, sum2 = 0;
	int wrap = rp->iw - rp->rw;
	int rn = rp->rw * rp->rh;
	int min = im[0], max = im[0];
	double v;
	int x, y;

	if (rn > sizeof(fastmedarray)/sizeof(fastmedarray[0]))
	    medarray = bigmedarray = (CamPix *) malloc (rn * sizeof(CamPix));
	else
	    medarray = fastmedarray;
	map = medarray;

	for (y = 0; y < rp->rh; y++) {
	    for (x = 0; x < rp->rw; x++) {
		CamPix p = *im++;
		*map++ = p;
		sum += (double)p;
		sum2 += (double)p*(double)p;
		if (p > max) {
		    max = p;
		    atx = rp->rx+x;
		    aty = rp->ry+y;
		}
		if (p < min)
		    min = p;
	    }
	    im += wrap;
	}

	sp->min = min;
	sp->max = max;
	sp->maxatx = atx;
	sp->maxaty = aty;
	if (rn > 1) {
	    sp->median = cmedian (medarray, rn);
	    sp->mean = sum/rn;
	    v = (sum2 - sum*sp->mean)/(rn-1);
	    sp->std = v <= 0.0 ? 0.0 : sqrt (v);
	} else {
	    sp->mean = sp->median = (min+max)/2;
	    sp->std = 0.0;
	}

	if (bigmedarray)
	    free ((char *)bigmedarray);
}

/* check and fix the given region so as to be all within the image.
 * return 0 if successful, -1 if region is _completely_ outside the image
 */
int
clampRegion (ImRegion *rp)
{
	if (rp->rx < 0) {
	    rp->rw += rp->rx;
	    rp->rx = 0;
	} else if (rp->rx + rp->rw > rp->iw) {
	    rp->rw = rp->iw - rp->rx;
	}
	if (rp->rw <= 0)
	    return (-1);

	if (rp->ry < 0) {
	    rp->rh += rp->ry;
	    rp->ry = 0;
	} else if (rp->ry + rp->rh > rp->ih) {
	    rp->rh = rp->ih - rp->ry;
	}
	if (rp->rh <= 0)
	    return (-1);

	return (0);
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: stats.c,v $ $Date: 2001/10/13 06:15:53 $ $Revision: 1.4 $ $Name:  $"};
