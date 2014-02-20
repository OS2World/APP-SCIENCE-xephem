/* find best WCS.
 * N.B. this is not reentrant.
 */

/* TRACE:
 *  0: none
 *  1: 1 line per trial FITS setting
 *  2: key info and decisions
 *  3: excruciating detail
 */
#define TRACE	0

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "P_.h"
#include "astro.h"
#include "circum.h"
#include "ip.h"

#define	STEPSZ		0.05		/* step distance, fractional im width */
#define	STOPSZ		0.035		/* 2D dist: sqrt(2*sqr(STEPSZ/2)) */
#define	ROTR		PI		/* +- rotation range */
#define	SCALER		.1		/* 1 +- scale range */
#define	TRANSR		.3		/* +- translation range */
#define	MINSP		5		/* bare min # star pairs required */
#define	LSTOL		.005		/* lstsqr fractional fit */

static int try_fi(void);
static int medianFit (void);
static int lsFit (void);
static double chisqr(double p[]);
static int discardLSOutlyers(void);
static void arrangeResult (ObjF *mfs, double *mx, double *my);

/* global for use by lstsqr() solver function.
 * N.B. hence not reentrant!
 */
static ObjF *_fs;			/* field stars */
static int _nfs;			/* n FS */
static double *_fsx, *_fsy;		/* field star locations with _fi */
static double *_isx, *_isy;		/* image star locations */
static int _nis;			/* n IS */
static int *_isol;			/* flag the IS outlyers */
static int _nisol;			/* number of outlying IS */
static int *_clfs;			/* index of closest FS to IS[i]*/
static int *_fsu;			/* flag FS is any IS's closest */
static int *_ref;			/* flag when refinePos() called */
static double *_err;			/* IS-FS err during lstsqr */
static FImage _fi;			/* solution to be found */
static double _bsep;			/* best sep we can hope for, pixels */
static double _wsep;			/* worst sep we will accept, pixels */

/* given a guess for WCS already in fip, a list of field stars in the
 * vicinity sorted by brightness, and a list of coordinates for starlike
 * things in the image sorted by brightest, put a best fit solution into *fip.
 * call stopf() occasionally and abandon search if it ever returns -1.
 * call progress() occasionally with percentage of total scan performed.
 * if successful:
 *   fs and isx/y are reordered so matching pairs actually used are in front.
 *   return number of pairs used in solution.
 * else
 *   write excuse in msg[] and return -1
 */
int
fsmatch (FImage *fip, ObjF *fs, int nfs, double *isx, double *isy, int nis,
double best, double worst, int (*stopf)(void), void (*progress)(int percent),
char msg[])
{
	FImage tst;
	int step, nsteps;
	double dr, ds;
	int i, j, r, dx, dy;

	/* no message when finished means we return success */
	msg[0] = '\0';

	/* set up solver globals */
	_fs = fs;
	_nfs = nfs;
	_isx = isx;
	_isy = isy;
	_nis = nis;
	_fsx = (double *) malloc (_nfs * sizeof(double));
	_fsy = (double *) malloc (_nfs * sizeof(double));
	_isol = (int *) malloc (_nis * sizeof(int));
	_clfs = (int *) malloc (_nis * sizeof(int));
	_err = (double *) malloc (_nis * sizeof(double));
	_fsu = (int *) malloc (_nfs * sizeof(int));
	_ref = (int *) calloc (_nis, sizeof(int));
	_bsep = best;
	_wsep = worst;

	if (_nis < MINSP || _nfs < MINSP) {
	    sprintf (msg, "Need at least %d star pairs to work with", MINSP);
	    goto cleanup;
	}

#if TRACE > 0
	printf ("Starting with %d Image stars, %d Field stars\n", _nis, _nfs);
#endif

	/* brute-force scan for something close.
	 * rot and scale pattern spread out from initial values.
	 * translations spiral out from center.
	 */
	step = 0;
	nsteps = (int)(ROTR*2/STEPSZ * SCALER*2/STEPSZ * TRANSR*2/STEPSZ *
							 TRANSR*2/STEPSZ);
	tst = *fip;
	for (i = 0; i < ROTR*2/STEPSZ; i++) {
	    dr = ((i+1)/2)*STEPSZ*(i&1?-1:1);
	    tst.rot = fip->rot + raddeg(dr);
	    for (j = 0; j < SCALER*2/STEPSZ; j++) {
		/* scale goes .9 .. 1.1 */
		ds = ((j+1)/2)*STEPSZ*((j&1)?-1:1);
		tst.xinc = fip->xinc * (1+ds);
		tst.yinc = fip->yinc * (1+ds);
		/* translate spirals out to .30 of image size */
		for (r = 0; r <= TRANSR/STEPSZ; r++) {
		    (*progress) (100*step/nsteps);
		    if ((*stopf)() < 0) {
			strcpy (msg, "No solution: User stop");
			goto cleanup;
		    }
		    for (dx = -r; dx <= r; dx++) {
			for (dy = -r; dy <= r; dy += abs(dx)==r ? 1 : 2*r) {
			    tst.xrefpix = fip->xrefpix + fip->sw*STEPSZ*dx;
			    tst.yrefpix = fip->yrefpix + fip->sh*STEPSZ*dy;
			    _fi = tst;
			    if (try_fi () == 0)
				goto foundfit;
			    step++;
			}
		    }
		}
	    }
	}

	sprintf (msg, "No solution found");
	goto cleanup;

    foundfit:

	/* _fi is the answer */
	*fip = _fi;
	setRealFITS (fip,   "CDELT1", _fi.xinc, 10, "RA step right, degs/pix");
	setRealFITS (fip,   "CDELT2", _fi.yinc, 10, "Dec step down, degs/pix");
	setRealFITS (fip,   "CRPIX1", _fi.xrefpix, 10, "Reference RA pixel");
	setRealFITS (fip,   "CRPIX2", _fi.yrefpix, 10, "Reference Dec pixel");
	setRealFITS (fip,   "CRVAL1", _fi.xref, 10, "Reference RA, degs");
	setRealFITS (fip,   "CRVAL2", _fi.yref, 10, "Reference Dec, degs");
	setRealFITS (fip,   "CROTA2", _fi.rot, 10, "Rotation E from N, degs");
	setStringFITS (fip, "CTYPE1", "RA---TAN", "RA Projection");
	setStringFITS (fip, "CTYPE2", "DEC--TAN", "Dec Projection");

	/* fill fs[] and isx/y[] with pairs used in solution */
	arrangeResult(fs, isx, isy);

    cleanup:

	free ((char *)_ref);
	free ((char *)_fsu);
	free ((char *)_err);
	free ((char *)_clfs);
	free ((char *)_isol);
	free ((char *)_fsy);
	free ((char *)_fsx);
	return (msg[0] ? -1 : _nis-_nisol);
}

/* try _fi as a solution.
 * return 0 if looks good, else -1
 */
static int
try_fi()
{
	int i, lastnis;

#if TRACE > 0
	printf ("Try R= %9.5f dX/Y= %9.7f %9.7f X/Y= %7.2f %7.2f\n",
			_fi.rot, _fi.xinc, _fi.yinc, _fi.xrefpix, _fi.yrefpix);
#endif

	/* test for crude coincidence */
	if (medianFit() < 0)
	    return (-1);

	/* worth a closer look */

	/* refine positions of candidate stars */
	for (i = 0; i < _nis; i++) {
	    if (!_isol[i] && !_ref[i]) {
		refinePos (&_fi, &_isx[i], &_isy[i]); /* _fi just for size */
		_ref[i] = 1;
	    }
	}

	/* keep doing LSFIT fits until no more outlyers or too few remain */
	do {
	    lastnis = _nis - _nisol;

	    /* lstsqr's fit on good pairs */
	    if (lsFit () < 0)
		return (-1);			/* no convergence */

	    /* discard outlyers */
	    if (discardLSOutlyers() < 0)
		return (-1);			/* too few good ones left */
	} while (_nis - _nisol < lastnis);

	/* no more outlyers, check for max */
	for (i = 0; i < _nis; i++) {
	    if (!_isol[i] && _err[i] > _wsep) {
#if TRACE > 1
		printf ("Final solution still worse than %g\n", _wsep);
#endif
		return(-1);
	    }
	}

	/* ok! */
#if TRACE > 1
	printf ("Final solution used %d pairs.\n", _nis - _nisol);
#endif
	return (0);
}

/* find closest field star to each image star, using _fi to map to image coords.
 * if at least half of smaller set of stars are within STOPSZ:
 *   mark those that are not ("outlyers") in _isol[], set _nisol,
 *   save index of the closest FS to each IS in _clfs[].
 *   return 0
 * else
 *   return -1 to signal _fi is not yet a reasonable solution.
 */
static int
medianFit ()
{
	int i, j;

	/* get image coords of each field star with current _fi */
	for (i = 0; i < _nfs; i++) {
	    RADec2xy (&_fi, ((Obj*)&_fs[i])->f_RA, ((Obj*)&_fs[i])->f_dec,
							    &_fsx[i], &_fsy[i]);
#if TRACE > 2
	    printf ("FS %3d: %5.2fM (%7.3f°,%7.3f°)=[%7.1f %7.1f]\n", i,
					    ((Obj*)&_fs[i])->f_mag/MAGSCALE,
					    raddeg(((Obj*)&_fs[i])->f_RA),
					    raddeg(((Obj*)&_fs[i])->f_dec),
					    _fsx[i], _fsy[i]);
#endif
	}

	/* for each IS, find and log its unique closest FS, count failures */
	memset (_clfs, 0, _nis*sizeof(int));
	memset (_fsu, 0, _nfs*sizeof(int));
	memset (_isol, 0, _nis*sizeof(int));
	_nisol = 0;
	for (i = 0; i < _nis; i++) {
	    double d, mind = 1e10;
	    for (j = 0; j < _nfs; j++)  {
		if (_fsu[j])
		    continue;			/* no dups */
		d = sqr(_isx[i]-_fsx[j]) + sqr(_isy[i]-_fsy[j]);
		if (d < mind) {
		    mind = d;
		    _clfs[i] = j;
		}
	    }
	    mind = sqrt(mind)/_fi.sw;		/* fractional image width */
	    if (mind > STOPSZ) {
		/* consider it an outlyer */
		_isol[i] = 1;
		_nisol++;
#if TRACE > 2
		printf ("MEDFIT Dropping %3d: Err = %8.4f @ [%4.0f,%4.0f]\n",
						i, mind, _isx[i], _isy[i]);
#endif
	    } else {
		/* mark FS now in use */
		_fsu[_clfs[i]] = 1;
#if TRACE > 2
		printf ("MEDFIT Keeping  %3d: Err = %8.4f @ [%4.0f,%4.0f]\n",
						i, mind, _isx[i], _isy[i]);
#endif
	    }
	}
#if TRACE > 1
	printf ("MEDFIT dropped %d of %d for sep > %g\n", _nisol, _nis,
								_fi.sw*STOPSZ);
#endif

	/* reject _fi if it results in too few remaining good pairs */
	if (_nis - _nisol < MINSP)
	    return (-1);

	/* ok, _fi is good enough to warrant more investigation */
	return (0);
}

/* use least-squares method to find the _fi that best maps the good FS to IS.
 * return 0 if converged, else -1
 */
static int
lsFit ()
{
	double p0[5], p1[5];

	/* set up initial values in order expected by chisqr() */
	p0[0] = _fi.rot;
	p0[1] = _fi.xinc;
	p0[2] = _fi.yinc;
	p0[3] = _fi.xrefpix;
	p0[4] = _fi.yrefpix;
	p1[0] = _fi.rot + raddeg(STEPSZ/3);
	p1[1] = _fi.xinc + STEPSZ/3;
	p1[2] = _fi.yinc + STEPSZ/3;
	p1[3] = _fi.xrefpix + _fi.sw*STEPSZ/3;
	p1[4] = _fi.yrefpix + _fi.sh*STEPSZ/3;

	memset (_err, 0, _nis*sizeof(double));
	if (lstsqr (chisqr, p0, p1, 5, LSTOL) < 0)
	    return (-1);

	/* unpack answer back into _fi */
	_fi.rot = p0[0];
	_fi.xinc = p0[1];
	_fi.yinc = p0[2];
	_fi.xrefpix = p0[3];
	_fi.yrefpix = p0[4];
	return (0);
}

/* evaluate fit at p, where p is new trial values for WCS fields in _fi.
 * leave the error for each IS in _err[].
 */
static double
chisqr (double p[5])
{
	FImage tst = _fi;
	double e2, sum2;
	int i;

	/* set up tst */
	tst.rot = p[0];
	tst.xinc = p[1];
	tst.yinc = p[2];
	tst.xrefpix = p[3];
	tst.yrefpix = p[4];

	/* handy image coords of each field star really used */
	for (i = 0; i < _nfs; i++)
	    if (_fsu[i])
		RADec2xy (&tst, ((Obj*)(&_fs[i]))->f_RA,
				((Obj*)(&_fs[i]))->f_dec, &_fsx[i], &_fsy[i]);

	/* sum of of distances^2 between each IS and its closest FS */
	for (sum2 = i = 0; i < _nis; i++) {
	    if (!_isol[i]) {
		e2 = sqr(_isx[i]-_fsx[_clfs[i]])+sqr(_isy[i]-_fsy[_clfs[i]]);
		sum2 += e2;
		_err[i] = sqrt(e2);
	    }
	}
#if TRACE > 2
	printf ("chisqr= %6.2f @ R= %9.5f dX/Y= %9.7f %9.7f X/Y= %7.2f %7.2f\n",
		sum2, tst.rot, tst.xinc, tst.yinc, tst.xrefpix, tst.yrefpix);
#endif
	return (sum2);
}

/* based on chisqr from last lstsqr, add outlyers to _isol. 
 * return -1 if too few good pairs remain.
 */
static int
discardLSOutlyers()
{
	double sum, sum2, mean, std;
	double maxerr;
	int i, n;

	/* find stats of errs */
	sum = sum2 = 0;
	for (i = 0; i < _nis; i++) {
	    if (!_isol[i]) {
		sum += _err[i];
		sum2 += sqr(_err[i]);
	    }
	}
	n = _nis - _nisol;
	mean = sum/n;
	std = sqrt((sum2 - sum*mean/n)/(n-1));

	/* set a cutoff.
	 * clamp min or we keep culling outlyers too far.
	 */
	maxerr = mean + 1.0*std;
	if (maxerr < _bsep)
	    maxerr = _bsep;
#if TRACE > 1
	printf ("LSFIT n= %d mean= %g std= %g=> cutting at %g\n", n, mean,
								std, maxerr);
#endif


	/* discard those too far out */
	for (i = 0; i < _nis; i++) {
	    if (!_isol[i]) {
		if (_err[i] > maxerr) {
		    _isol[i] = 1;
		    _nisol++;
		    _fsu[_clfs[i]] = 0;		/* FS no longer a neighbor */
#if TRACE > 1
		    printf ("LSFIT Dropping %3d: Err = %8.4f @ [%4.0f,%4.0f]\n",
						i, _err[i], _isx[i], _isy[i]);
		} else {
		    printf ("LSFIT Keeping  %3d: Err = %8.4f @ [%4.0f,%4.0f]\n",
						i, _err[i], _isx[i], _isy[i]);
#endif
		}
	    }
	}

	/* ok if enough remain */
#if TRACE > 1
	printf ("LSFIT %d remain, want %d\n", _nis - _nisol, MINSP);
#endif
	return (_nis - _nisol < MINSP ? -1 : 0);
}


/* fill fs from _fs and isx/y from _isx/y so they contain just the matching
 * pairs of the FS and IS used in the solution.
 */
static void
arrangeResult(ObjF *fs, double *isx, double *isy)
{
	int ngood = _nis - _nisol;
	ObjF *tmpo = (ObjF *) malloc (ngood * sizeof(ObjF));
	double *tmpx = (double *) malloc (ngood * sizeof(double));
	double *tmpy = (double *) malloc (ngood * sizeof(double));
	int i, j;

	/* build result in temp arrays.
	 * there's probably a clever way to do this in-place ...
	 */
	for (i = j = 0; i < _nis; i++) {
	    if (!_isol[i]) {
		tmpo[j] = _fs[_clfs[i]];
		tmpx[j] = _isx[i];
		tmpy[j] = _isy[i];
		j++;
	    }
	}

	/* copy back to originals */
	memcpy (fs, tmpo, ngood*sizeof(ObjF));
	memcpy (isx, tmpx, ngood*sizeof(double));
	memcpy (isy, tmpy, ngood*sizeof(double));

	/* clean up */
	free ((char *)tmpo);
	free ((char *)tmpx);
	free ((char *)tmpy);
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: fsmatch.c,v $ $Date: 2001/10/09 22:29:22 $ $Revision: 1.5 $ $Name:  $"};
