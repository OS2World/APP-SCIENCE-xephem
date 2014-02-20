/* stuff for the eyepiece dialog */

#include <stdio.h>
#include <math.h>
#include <string.h>

#if defined(__STDC__)
#include <stdlib.h>
#endif


#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/Scale.h>
#include <Xm/SelectioB.h>
#include <Xm/ToggleB.h>
#include <Xm/PushB.h>
#include <Xm/TextF.h>

#include "P_.h"
#include "astro.h"
#include "circum.h"
#include "preferences.h"
#include "skyeyep.h"

extern Widget toplevel_w;
extern Colormap xe_cm;

extern Now *mm_get_now P_((void));
extern char *getXRes P_((char *name, char *def));
extern void get_xmstring P_((Widget w, char *resource, char **txtp));
extern void hlp_dialog P_((char *tag, char *deflt[], int ndeflt));
extern void prompt_map_cb P_((Widget w, XtPointer client, XtPointer call));
extern void set_something P_((Widget w, char *resource, XtArgVal value));
extern void set_xmstring P_((Widget w, char *resource, char *txtp));
extern void setXRes P_((char *name, char *value));
extern void sr_reg P_((Widget w, char *res, char *cat, int autosav));
extern void wtip P_((Widget w, char *tip));
extern void xe_msg P_((char *msg, int show));

/* a list of EyePieces */
static EyePiece *eyep;		/* malloced list of eyepieces */
static int neyep;		/* number of entries off eyep */

#define	MXSCALE degrad(90)	/* max angle to show */
#define	LOSCALE	degrad(1./60)	/* rads/step at low end */
#define	HISCALE	degrad(10./60)	/* rads/step at hi end */
#define	CHSCALE degrad(10.)	/* where they switch */
#define	SCALEMAX	((int)(CHSCALE/LOSCALE + (MXSCALE-CHSCALE)/HISCALE))

/* favorites are managed in an array which is never shuffled. Entries are
 * reassigned when needed from unused entries or the array is expanded.
 * callbacks have the array index in their client param.
 */
#define	MAXEPNM	20		/* maximum name for eyepiece (including \0) */
typedef struct {
    int inuse;			/* whether this entry is in use */
    char name[MAXEPNM];		/* use's name */
    double w, h;		/* size, rads */
    int isE;			/* whether Elliptical or Rectangular */
    int isS;			/* whether Solid or Outline */
    Widget row_w;		/* widget with GUI controls for this entry */
    Widget name_w;		/* TF with name */
} Favorite;
static Favorite *favs;		/* malloced array of favorite eyepieces */
static int nfavs;		/* number of entries (regardless of inuse) */
static Widget favrc_w;		/* RC for listing favorites */

/* XEphem.feyeprn is the persistent storage for eyepieces */
static char feyeprn[] = "FavEyepieces";

static Widget eyep_w;		/* overall eyepiece dialog */
static Widget eyepws_w;		/* eyepiece width scale */
static Widget eyephs_w;		/* eyepiece height scale */
static Widget eyepwl_w;		/* eyepiece width label */
static Widget eyephl_w;		/* eyepiece height label */
static Widget eyer_w;		/* eyepiece Round TB */
static Widget eyes_w;		/* eyepiece Square TB */
static Widget eyef_w;		/* eyepiece filled TB */
static Widget eyeb_w;		/* eyepiece border TB */
static Widget telrad_w;		/* telrad on/off TB */
static Widget delep_w;		/* the delete all PB */
static Widget lock_w;		/* lock scales TB */
static Widget sa1_w;		/* sky angle L for formula 1 results */
static Widget sa2_w;		/* sky angle L for formula 2 results */
static Widget fl_w;		/* focal length TF */
static Widget fp_w;		/* focal plane length TF */
static Widget afov_w;		/* apparent eyepiece fov TF */
static Widget efl_w;		/* eyepiece focal length TF */
static Widget mfl_w;		/* mirror focal length TF */

static void se_create_eyep_w P_((void));
static void se_eyepsz P_((double *wp, double *hp, int *rp, int *fp));
static void se_scale_fmt P_((Widget s_w, Widget l_w));
static void se_telrad_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_skyW_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_skyH_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_wscale_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_hscale_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_delall_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_close_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_addfav_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_savfav_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_delfav_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_usefav_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_help_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_calc1_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_calc1 P_((void));
static void se_calc2_cb P_((Widget w, XtPointer client, XtPointer call));
static void se_calc2 P_((void));
static void se_rmall P_((void));
static double se_getScale P_((Widget w));
static void se_setScale P_((Widget w, double x));
static void se_addFavorite P_((Favorite *newfp));
static void se_readFav P_((void));
static void se_saveFav P_((void));

/* telrad circle diameters, degrees */
static double telrad_sz[] = {.5, 2., 4.};

static char skyepcategory[] = "Sky View -- Eyepieces";

void 
se_manage()
{
	if (!eyep_w)
	    se_create_eyep_w();
	XtManageChild (eyep_w);
}

void 
se_unmanage()
{
	if (eyep_w)
	    XtUnmanageChild (eyep_w);
}

/* called to put up or remove the watch cursor.  */
void
se_cursor (c)
Cursor c;
{
	Window win;

	if (eyep_w && (win = XtWindow(eyep_w)) != 0) {
	    Display *dsp = XtDisplay(eyep_w);
	    if (c)
		XDefineCursor (dsp, win, c);
	    else
		XUndefineCursor (dsp, win);
	}
}

/* add one eyepiece for the given location */
void
se_add (ra, dec, alt, az)
double ra, dec, alt, az;
{
	EyePiece *new;
	int telrad;
	int nnew;

	/* check for first time */
	if (!eyep_w)
	    se_create_eyep_w();

	/* increase allocation at eyep by 1 or 3 if adding a telrad */
	telrad = XmToggleButtonGetState (telrad_w);
	nnew = telrad ? 3 : 1;
	eyep = (EyePiece*)XtRealloc((void *)eyep,(neyep+nnew)*sizeof(EyePiece));

	/* new points to the one(s) we're adding */
	new = eyep+neyep;
	neyep += nnew;

	/* fill in the details */
	while (--nnew >= 0) {
	    new->ra = ra;
	    new->dec = dec;
	    new->alt = alt;
	    new->az = az;
	    if (telrad) {
		new->eyepw = degrad(telrad_sz[nnew]);
		new->eyeph = degrad(telrad_sz[nnew]);
		new->round = 1;
		new->solid = 0;
	    } else
		se_eyepsz (&new->eyepw, &new->eyeph, &new->round, &new->solid);
	    new++;
	}

	/* at least one to delete now */
	XtSetSensitive (delep_w, True);
}

/* return whether there are any eyepieces that cover the given location,
 * isradec determining the interpretation of the coords.
 */
int
se_isOneHere (azra, altdec, isradec)
double azra, altdec;
int isradec;
{
	double caltdec = cos(altdec);
	double saltdec = sin(altdec);
	int i;

	for (i = 0; i < neyep; i++) {
	    EyePiece *ep = &eyep[i];
	    double L, l, csep, maxr;

	    if (isradec) {
		L = ep->ra;
		l = ep->dec;
	    } else {
		L = ep->az;
		l = ep->alt;
	    }
	    solve_sphere (azra-L, PI/2-l, saltdec, caltdec, &csep, NULL);
	    maxr = (ep->eyepw < ep->eyeph ? ep->eyepw : ep->eyeph)/2;
	    if (acos(csep) < maxr)
		return (1);	/* yes, there is */
	}

	/* none found */
	return (0);
}

/* delete eyepiece that most closely covers the given location.
 * isradec determines how to interpret the coords.
 */
void
se_del (azra, altdec, isradec)
double azra, altdec;
int isradec;
{
	double caltdec, saltdec;
	double r, maxcsep;
	EyePiece *ep, *endep, *minep;

	/* scan for eyepiece closest to target position, leave in minep */
	caltdec = cos(altdec);
	saltdec = sin(altdec);
	maxcsep = 0;
	endep = &eyep[neyep];
	minep = NULL;
	for (ep = eyep; ep < endep; ep++) {
	    double L, l, csep;

	    if (isradec) {
		L = ep->ra;
		l = ep->dec;
	    } else {
		L = ep->az;
		l = ep->alt;
	    }
	    solve_sphere (azra-L, PI/2-l, saltdec, caltdec, &csep, NULL);
	    if (csep > maxcsep) {
		maxcsep = csep;
		minep = ep;
	    }
	}
	if (!minep)
	    return;

	/* if actually under eyepiece, remove it */
	r = (minep->eyepw < minep->eyeph ? minep->eyepw : minep->eyeph)/2;
	if (maxcsep > cos(r)) {
	    while (++minep < endep)
		minep[-1] = minep[0];

	    /* drop count, but leave array.. likely grows again anyway */
	    neyep--;

	    /* may have no more left now! */
	    XtSetSensitive (delep_w, neyep);
	}
}


/* return the list of current eyepieces, if interested, and the count.
 */
int
se_getlist (ep)
EyePiece **ep;
{
	if (ep)
	    *ep = eyep;
	return (neyep);
}

/* fetch the current eyepiece diameter, in rads, whether it is round, and
 * whether it is filled from the dialog.
 */
static void
se_eyepsz(wp, hp, rp, fp)
double *wp, *hp;
int *rp;
int *fp;
{
	if (!eyep_w)
	    se_create_eyep_w();

	*wp = se_getScale (eyepws_w);
	*hp = se_getScale (eyephs_w);

	*rp = XmToggleButtonGetState (eyer_w);
	*fp = XmToggleButtonGetState (eyef_w);
}

/* create the eyepiece size dialog */
static void
se_create_eyep_w()
{
	Widget w, sep_w;
	Widget l_w, rb_w;
	Widget pb_w;
	Arg args[20];
	int n;

	/* create form */

	n = 0;
	XtSetArg(args[n], XmNautoUnmanage, False); n++;
	XtSetArg (args[n], XmNcolormap, xe_cm); n++;
	XtSetArg (args[n], XmNverticalSpacing, 5); n++;
	XtSetArg (args[n], XmNmarginHeight, 10); n++;
	XtSetArg (args[n], XmNmarginWidth, 10); n++;
	eyep_w = XmCreateFormDialog (svshell_w, "SkyEyep", args, n);
	set_something (eyep_w, XmNcolormap, (XtArgVal)xe_cm);
	XtAddCallback (eyep_w, XmNhelpCallback, se_help_cb, 0);
	XtAddCallback (eyep_w, XmNmapCallback, prompt_map_cb, NULL);

	/* set some stuff in the parent DialogShell.
	 * setting XmNdialogTitle in the Form didn't work..
	 */
	n = 0;
	XtSetArg (args[n], XmNtitle, "xephem Eyepiece Setup"); n++;
	XtSetValues (XtParent(eyep_w), args, n);

	/* title label */
	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	w = XmCreateLabel (eyep_w, "L", args, n);
	set_xmstring (w, XmNlabelString, "Set next eyepiece size, shape and style:");
	XtManageChild (w);

	/* w scale and its labels */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	eyepwl_w = XmCreateLabel (eyep_w, "EyepWL", args, n);
	XtManageChild (eyepwl_w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 20); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNorientation, XmHORIZONTAL); n++;
	XtSetArg (args[n], XmNshowValue, False); n++;
	XtSetArg (args[n], XmNscaleMultiple, 1); n++;
	XtSetArg (args[n], XmNmaximum, SCALEMAX); n++;
	eyepws_w = XmCreateScale (eyep_w, "EyepW", args, n);
	XtAddCallback (eyepws_w, XmNdragCallback, se_wscale_cb, 0);
	XtAddCallback (eyepws_w, XmNvalueChangedCallback, se_wscale_cb, 0);
	wtip (eyepws_w, "Slide to desired width of eyepiece, D:M");
	sr_reg (eyepws_w, NULL, skyepcategory, 0);
	se_scale_fmt (eyepws_w, eyepwl_w);
	XtManageChild (eyepws_w);

	/* h scale and its label */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, eyepws_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	eyephl_w = XmCreateLabel (eyep_w, "EyepHL", args, n);
	XtManageChild (eyephl_w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, eyepws_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 20); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNorientation, XmHORIZONTAL); n++;
	XtSetArg (args[n], XmNshowValue, False); n++;
	XtSetArg (args[n], XmNscaleMultiple, 1); n++;
	XtSetArg (args[n], XmNmaximum, SCALEMAX); n++;
	eyephs_w = XmCreateScale (eyep_w, "EyepH", args, n);
	XtAddCallback (eyephs_w, XmNdragCallback, se_hscale_cb, 0);
	XtAddCallback (eyephs_w, XmNvalueChangedCallback, se_hscale_cb, 0);
	wtip (eyephs_w, "Slide to desired height of eyepiece, D:M");
	sr_reg (eyephs_w, NULL, skyepcategory, 0);
	se_scale_fmt (eyephs_w, eyephl_w);
	XtManageChild (eyephs_w);

	/* lock TB */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, eyephs_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	lock_w = XmCreateToggleButton (eyep_w, "Lock", args, n);
	set_xmstring (lock_w, XmNlabelString, "Lock scales together");
	wtip (lock_w, "When on, width and height scales move as one");
	XtManageChild (lock_w);
	sr_reg (lock_w, NULL, skyepcategory, 0);

	/* telrad TB */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, lock_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	telrad_w = XmCreateToggleButton (eyep_w, "Telrad", args, n);
	XtAddCallback (telrad_w, XmNvalueChangedCallback, se_telrad_cb, NULL);
	set_xmstring (telrad_w, XmNlabelString, "Create a Telrad pattern");
	wtip (telrad_w, "When on, next eyepiece will be 3 open circles matching the Telrad.");
	XtManageChild (telrad_w);
	sr_reg (telrad_w, NULL, skyepcategory, 0);

	/* shape label */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, telrad_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	l_w = XmCreateLabel (eyep_w, "S", args, n);
	set_xmstring (l_w, XmNlabelString, "Shape:");
	XtManageChild (l_w);

	/* round or square Radio box */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, telrad_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNleftWidget, l_w); n++;
	XtSetArg (args[n], XmNleftOffset, 5); n++;
	rb_w = XmCreateRadioBox (eyep_w, "RSRB", args, n);
	XtManageChild (rb_w);

	    n = 0;
	    eyer_w = XmCreateToggleButton (rb_w, "Elliptical", args, n);
	    wtip (eyer_w, "When on, next eyepiece will be elliptical");
	    XtManageChild (eyer_w);
	    sr_reg (eyer_w, NULL, skyepcategory, 0);

	    n = 0;
	    eyes_w = XmCreateToggleButton (rb_w, "Rectangular", args, n);
	    wtip (eyes_w, "When on, next eyepiece will be rectangular");
	    XtManageChild (eyes_w);

	    /* "Elliptical" establishes truth setting */
	    XmToggleButtonSetState (eyes_w, !XmToggleButtonGetState(eyer_w), 0);

	/* style label */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, telrad_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 50); n++;
	l_w = XmCreateLabel (eyep_w, "St", args, n);
	set_xmstring (l_w, XmNlabelString, "Style:");
	XtManageChild (l_w);

	/* style Radio box */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, telrad_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNleftWidget, l_w); n++;
	XtSetArg (args[n], XmNleftOffset, 5); n++;
	rb_w = XmCreateRadioBox (eyep_w, "FBRB", args, n);
	XtManageChild (rb_w);

	    n = 0;
	    eyef_w = XmCreateToggleButton (rb_w, "Solid", args, n);
	    wtip (eyef_w, "When on, next eyepiece will be solid");
	    XtManageChild (eyef_w);
	    sr_reg (eyef_w, NULL, skyepcategory, 0);

	    n = 0;
	    eyeb_w = XmCreateToggleButton (rb_w, "Outline", args, n);
	    wtip (eyeb_w, "When on, next eyepiece will be just a border");
	    XtManageChild (eyeb_w);

	    /* "Solid" establishes truth setting */
	    XmToggleButtonSetState (eyeb_w, !XmToggleButtonGetState(eyef_w), 0);

	/* calculator */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, rb_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNseparatorType, XmDOUBLE_LINE); n++;
	sep_w = XmCreateSeparator (eyep_w, "Sep", args, n);
	XtManageChild (sep_w);

	    /* title */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, sep_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    l_w = XmCreateLabel (eyep_w, "EL", args, n);
	    set_xmstring(l_w,XmNlabelString,"Handy Field-of-View Calculators:");
	    XtManageChild (l_w);

	    /* formula # 1 */

	    /* labels */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, l_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 28); n++;
	    w = XmCreateLabel (eyep_w, "EL", args, n);
	    set_xmstring (w, XmNlabelString, "Focal length\n(mm) m in");
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, l_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 30); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 58); n++; /*first includes 10*/
	    w = XmCreateLabel (eyep_w, "EL", args, n);
	    set_xmstring (w, XmNlabelString, "F Plane Size\n(µm) mm in");
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, l_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 60); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 88); n++;
	    w = XmCreateLabel (eyep_w, "EL", args, n);
	    set_xmstring (w, XmNlabelString, "Sky angle\nD:M:S");
	    XtManageChild (w);

	    /* TFs */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 28); n++;
	    XtSetArg (args[n], XmNcolumns, 8); n++;
	    fl_w = XmCreateTextField (eyep_w, "FocalLength", args, n);
	    wtip (fl_w, "Enter effective focal length, specifying units of mm, m or inches");
	    sr_reg (fl_w, NULL, skyepcategory, 0);
	    XtManageChild (fl_w);
	    XtAddCallback (fl_w, XmNvalueChangedCallback, se_calc1_cb, NULL);
	    XtAddCallback (fl_w, XmNactivateCallback, se_calc1_cb, NULL);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 30); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 58); n++;
	    XtSetArg (args[n], XmNcolumns, 8); n++;
	    fp_w = XmCreateTextField (eyep_w, "FPlaneSize", args, n);
	    wtip (fp_w, "Enter size of object on focal plane, in microns, mm or inches");
	    sr_reg (fp_w, NULL, skyepcategory, 0);
	    XtManageChild (fp_w);
	    XtAddCallback (fp_w, XmNvalueChangedCallback, se_calc1_cb, NULL);
	    XtAddCallback (fp_w, XmNactivateCallback, se_calc1_cb, NULL);

	    /* formula #1 sky angle result label */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 60); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 88); n++;
	    sa1_w = XmCreateLabel (eyep_w, "SAL", args, n);
	    wtip (sa1_w, "Sky angle with given focal length and plane size");
	    set_xmstring (sa1_w, XmNlabelString, "xxx:xx:xx.x");
	    XtManageChild (sa1_w);

	    /* formula #1 set w and h */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, l_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 90); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    w = XmCreateLabel (eyep_w, "Set", args, n);
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, w); n++;
	    XtSetArg (args[n], XmNtopOffset, 1); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 90); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNmarginHeight, 0); n++;
	    w = XmCreatePushButton (eyep_w, "W", args, n);
	    wtip (w, "Set eyepiece width scale to this Sky angle");
	    XtAddCallback (w, XmNactivateCallback, se_skyW_cb,(XtPointer)sa1_w);
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, w); n++;
	    XtSetArg (args[n], XmNtopOffset, 1); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 90); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNmarginHeight, 0); n++;
	    w = XmCreatePushButton (eyep_w, "H", args, n);
	    wtip (w, "Set eyepiece height scale to this Sky angle");
	    XtAddCallback (w, XmNactivateCallback, se_skyH_cb,(XtPointer)sa1_w);
	    XtManageChild (w);

	    /* formula #2 */

	    /* labels */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, fp_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 19); n++;
	    l_w = XmCreateLabel (eyep_w, "EL", args, n);
	    set_xmstring (l_w, XmNlabelString, "Apparent\nFOV, °");
	    XtManageChild (l_w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, fp_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 21); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 38); n++; /*first includes 10*/
	    w = XmCreateLabel (eyep_w, "EL", args, n);
	    set_xmstring (w, XmNlabelString, "Eyepiece\nFL, mm");
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, fp_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 40); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 58); n++;
	    w = XmCreateLabel (eyep_w, "EL", args, n);
	    set_xmstring (w, XmNlabelString, "Mirror FL\n(mm) m in");
	    XtManageChild (w);

	    /* TFs */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, l_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 19); n++;
	    XtSetArg (args[n], XmNcolumns, 8); n++;
	    afov_w = XmCreateTextField (eyep_w, "ApparentFOV", args, n);
	    wtip (afov_w,
	       "Enter apparent field of view through the eyepiece, in degrees");
	    sr_reg (afov_w, NULL, skyepcategory, 0);
	    XtManageChild (afov_w);
	    XtAddCallback (afov_w, XmNvalueChangedCallback, se_calc2_cb, NULL);
	    XtAddCallback (afov_w, XmNactivateCallback, se_calc2_cb, NULL);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, l_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 21); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 38); n++;
	    XtSetArg (args[n], XmNcolumns, 8); n++;
	    efl_w = XmCreateTextField (eyep_w, "EyepieceFL", args, n);
	    wtip (efl_w, "Enter focal length of eyepiece, in millimeters");
	    sr_reg (efl_w, NULL, skyepcategory, 0);
	    XtManageChild (efl_w);
	    XtAddCallback (efl_w, XmNvalueChangedCallback, se_calc2_cb, NULL);
	    XtAddCallback (efl_w, XmNactivateCallback, se_calc2_cb, NULL);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, l_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 40); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 58); n++;
	    XtSetArg (args[n], XmNcolumns, 8); n++;
	    mfl_w = XmCreateTextField (eyep_w, "MirrorFL", args, n);
	    wtip (mfl_w, "Enter focal length of primary mirror, in units of mm, m or inches");
	    sr_reg (mfl_w, NULL, skyepcategory, 0);
	    XtManageChild (mfl_w);
	    XtAddCallback (mfl_w, XmNvalueChangedCallback, se_calc2_cb, NULL);
	    XtAddCallback (mfl_w, XmNactivateCallback, se_calc2_cb, NULL);

	    /* formula #2 sky angle result label */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, l_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 60); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNrightPosition, 88); n++;
	    sa2_w = XmCreateLabel (eyep_w, "SAL", args, n);
	    wtip (sa2_w, "Sky angle with given eyepiece and mirror");
	    set_xmstring (sa2_w, XmNlabelString, "xxx:xx:xx.x");
	    XtManageChild (sa2_w);

	    /* formula #2 set w and h */

	    n = 0;
	    XtSetArg (args[n],XmNbottomAttachment,XmATTACH_OPPOSITE_WIDGET);n++;
	    XtSetArg (args[n], XmNbottomWidget, l_w); n++;
	    XtSetArg (args[n], XmNbottomOffset, 0); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 90); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNmarginHeight, 0); n++;
	    w = XmCreatePushButton (eyep_w, "W", args, n);
	    wtip (w, "Set eyepiece width scale to this Sky angle");
	    XtAddCallback (w, XmNactivateCallback, se_skyW_cb,(XtPointer)sa2_w);
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, w); n++;
	    XtSetArg (args[n], XmNtopOffset, 1); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 90); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNmarginHeight, 0); n++;
	    w = XmCreatePushButton (eyep_w, "H", args, n);
	    wtip (w, "Set eyepiece height scale to this Sky angle");
	    XtAddCallback (w, XmNactivateCallback, se_skyH_cb,(XtPointer)sa2_w);
	    XtManageChild (w);

	/* favorites */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, mfl_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNseparatorType, XmDOUBLE_LINE); n++;
	sep_w = XmCreateSeparator (eyep_w, "Sep", args, n);
	XtManageChild (sep_w);

	    /* title */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, sep_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    l_w = XmCreateLabel (eyep_w, "EL", args, n);
	    set_xmstring (l_w, XmNlabelString, "Save Favorite Eyepieces:");
	    XtManageChild (l_w);

	    /* add */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, sep_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNleftWidget, l_w); n++;
	    XtSetArg (args[n], XmNleftOffset, 10); n++;
	    pb_w = XmCreatePushButton (eyep_w, "Add", args, n);
	    wtip (pb_w, "Add current settings to list of favorites");
	    XtAddCallback (pb_w, XmNactivateCallback, se_addfav_cb, NULL);
	    XtManageChild (pb_w);

	    /* all favorites in a RC */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, pb_w); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    favrc_w = XmCreateRowColumn (eyep_w, "EPRC", args, n);
	    XtManageChild (favrc_w);

	    /* empty until favorites get defined */


	/* delete PB */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, favrc_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNseparatorType, XmDOUBLE_LINE); n++;
	sep_w = XmCreateSeparator (eyep_w, "Sep", args, n);
	XtManageChild (sep_w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, sep_w); n++;
	XtSetArg (args[n], XmNtopOffset, 10); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 20); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 80); n++;
	delep_w = XmCreatePushButton (eyep_w, "DelE", args, n);
	XtAddCallback (delep_w, XmNactivateCallback, se_delall_cb, NULL);
	wtip (delep_w, "Delete all eyepieces now on Sky View");
	set_xmstring (delep_w, XmNlabelString, "Delete all Sky View eyepieces");
	XtSetSensitive (delep_w, False);	/* works when there are some */
	XtManageChild (delep_w);

	/* separator */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, delep_w); n++;
	XtSetArg (args[n], XmNtopOffset, 10); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	sep_w = XmCreateSeparator (eyep_w, "Sep", args, n);
	XtManageChild (sep_w);

	/* a close button */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, sep_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 20); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 40); n++;
	w = XmCreatePushButton (eyep_w, "Close", args, n);
	XtAddCallback (w, XmNactivateCallback, se_close_cb, NULL);
	wtip (w, "Close this dialog");
	XtManageChild (w);

	/* a help button */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, sep_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 60); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 80); n++;
	w = XmCreatePushButton (eyep_w, "Help", args, n);
	XtAddCallback (w, XmNactivateCallback, se_help_cb, NULL);
	wtip (w, "More info about this dialog");
	XtManageChild (w);

	/* engage side effects if telrad on initially */
	if (XmToggleButtonGetState(telrad_w)) {
	    XmToggleButtonSetState(telrad_w, False, True);
	    XmToggleButtonSetState(telrad_w, True, True);
	}

	/* calculate sky angles from default values */
	se_calc1();
	se_calc2();

	/* register eyepiece resource */
	sr_reg (0, feyeprn, skyepcategory, 1);

	/* load default favorites */
	se_readFav();
}

/* read the given scale and write it's value in the given label */
static void
se_scale_fmt (s_w, l_w)
Widget s_w, l_w;
{
	char buf[64];

	buf[0] = l_w == eyephl_w ? 'H' : 'W';
	fs_sexa (buf+1, raddeg(se_getScale(s_w)), 3, 60);
	set_xmstring (l_w, XmNlabelString, buf);
}

/* called when the telrad TB is activated */
static void
se_telrad_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	int set = XmToggleButtonGetState (w);

	if (set) {
	    XtSetSensitive (eyef_w, False);
	    XtSetSensitive (eyeb_w, False);
	    XtSetSensitive (eyer_w, False);
	    XtSetSensitive (eyes_w, False);
	    XtSetSensitive (lock_w, False);
	    XtSetSensitive (eyephs_w, False);
	    XtSetSensitive (eyepws_w, False);
	} else {
	    XtSetSensitive (eyef_w, True);
	    XtSetSensitive (eyeb_w, True);
	    XtSetSensitive (eyer_w, True);
	    XtSetSensitive (eyes_w, True);
	    XtSetSensitive (lock_w, True);
	    XtSetSensitive (eyephs_w, True);
	    XtSetSensitive (eyepws_w, True);
	}
}

/* drag callback from the height scale */
static void
se_hscale_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	se_scale_fmt (eyephs_w, eyephl_w);

	/* slave the w scale to the h scale */
	if (XmToggleButtonGetState(lock_w)) {
	    se_setScale (eyepws_w, se_getScale (eyephs_w));
	    se_scale_fmt (eyepws_w, eyepwl_w);
	    XmToggleButtonSetState (telrad_w, False, True);
	}
}

/* drag callback from the width scale */
static void
se_wscale_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	se_scale_fmt (eyepws_w, eyepwl_w);

	/* slave the w scale to the h scale */
	if (XmToggleButtonGetState(lock_w)) {
	    se_setScale (eyephs_w, se_getScale (eyepws_w));
	    se_scale_fmt (eyephs_w, eyephl_w);
	    XmToggleButtonSetState (telrad_w, False, True);
	}
}

/* callback from the delete-all eyepieces control.
 */
/* ARGSUSED */
static void
se_delall_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	if (eyep) {
	    se_rmall();
	    sv_all(mm_get_now());
	    XtSetSensitive (delep_w, False);
	}
}

/* callback from the close PB.
 */
/* ARGSUSED */
static void
se_close_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	XtUnmanageChild (eyep_w);
}

/* called when the help button is hit in the eyepiece dialog */
/* ARGSUSED */
static void
se_help_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	static char *msg[] = {
	    "Define eyepiece shapes and sizes."
	};

	hlp_dialog ("Sky View -- Eyepieces", msg, sizeof(msg)/sizeof(msg[0]));

}

/* called to take the sky angle from the calculator and set width scale.
 * client points to a label widget from which the angle is extracted.
 */
/* ARGSUSED */
static void
se_skyW_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	Widget angle_w = (Widget)client;
	char *str;
	double a;

	get_xmstring (angle_w, XmNlabelString, &str);
	f_scansex (0.0, str, &a);
	XtFree (str);
	se_setScale (eyepws_w, degrad(a));
	se_scale_fmt (eyepws_w, eyepwl_w);
	if (XmToggleButtonGetState (lock_w)) {
	    se_setScale (eyephs_w, degrad(a));
	    se_scale_fmt (eyephs_w, eyephl_w);
	}
}

/* called to take the sky angle from the calculator and set height scale.
 * client points to a label widget from which the angle is extracted.
 */
/* ARGSUSED */
static void
se_skyH_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	Widget angle_w = (Widget)client;
	char *str;
	double a;

	get_xmstring (angle_w, XmNlabelString, &str);
	f_scansex (0.0, str, &a);
	XtFree (str);
	se_setScale (eyephs_w, degrad(a));
	se_scale_fmt (eyephs_w, eyephl_w);
	if (XmToggleButtonGetState (lock_w)) {
	    se_setScale (eyepws_w, degrad(a));
	    se_scale_fmt (eyepws_w, eyepwl_w);
	}
}

/* called when any of the factors in formula 1 change */
/* ARGSUSED */
static void
se_calc1_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	se_calc1();
}

static void
se_calc1()
{
	char *tmp, *flstr, *fpstr, sastr[32];
	double fl, fp, sa;

	/* get focal length, convert to mm */
	flstr = XmTextFieldGetString (fl_w);
	fl = atof (flstr);
	if (fl == 0) {
	    XtFree (flstr);
	    return;
	}
	if ((tmp = strchr (flstr, 'm')) && strrchr (flstr, 'm') == tmp)
	    fl *= 1e3;		/* m to mm */
	else if (strchr (flstr, '"') || strchr (flstr, 'i'))
	    fl *= 25.4;		/* inches to mm */
	/* else assume mm */
	XtFree (flstr);

	/* get focal plane length, convert to um */
	fpstr = XmTextFieldGetString (fp_w);
	fp = atof (fpstr);
	if ((tmp = strchr (fpstr, 'm')) && strrchr(fpstr, 'm') != tmp)
	    fp *= 1e3;		/* mm to um */
	else if (strchr (fpstr, '"') || strchr (fpstr, 'i'))
	    fp *= 25.4e6;	/* inches to um */
	/* else assume um */
	XtFree (fpstr);

	/* compute and show sky angle */
	sa = 206*fp/fl;		/* arc seconds */
	fs_sexa (sastr, sa/3600., 3, 36000);
	set_xmstring (sa1_w, XmNlabelString, sastr);
}

/* called when any of the factors in formula 2 change */
/* ARGSUSED */
static void
se_calc2_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	se_calc2();
}

/* gather values and compute sky angle using formula 2 */
static void
se_calc2()
{
	char *tmp, *mflstr, sastr[32];
	double afov, efl, mfl, sa;

	/* get apparent field of view, assume degrees */
	tmp = XmTextFieldGetString (afov_w);
	afov = atof (tmp);
	XtFree (tmp);

	/* get eyepiece focal length, assume mm */
	tmp = XmTextFieldGetString (efl_w);
	efl = atof (tmp);
	XtFree (tmp);
	if (efl <= 0) {
	    XtFree(tmp);
	    return;
	}

	/* get focal length of primary, convert to mm */
	mflstr = XmTextFieldGetString (mfl_w);
	mfl = atof (mflstr);
	if ((tmp = strchr (mflstr, 'm')) && strrchr(mflstr, 'm') == tmp)
	    mfl *= 1e3;		/* m to mm */
	else if (strchr (mflstr, '"') || strchr (mflstr, 'i'))
	    mfl *= 25.4;	/* inches to mm */
	/* else assume mm */
	XtFree (mflstr);

	/* compute and show sky angle */
	sa = afov*efl/mfl;
	fs_sexa (sastr, sa, 3, 36000);
	set_xmstring (sa2_w, XmNlabelString, sastr);
}

/* delete the entire list of eyepieces */
static void
se_rmall()
{
	if (eyep) {
	    free ((void *)eyep);
	    eyep = NULL;
	}
	neyep = 0;
}

/* read the given Scale widget and return it's current setting, in rads */
static double
se_getScale (w)
Widget w;
{
	int v;
	double a;

	XmScaleGetValue (w, &v);

	a = v*LOSCALE;
	if (a > CHSCALE)
	    a = CHSCALE + (a - CHSCALE)/LOSCALE*HISCALE;
	return (a);
}

/* set the given Scale widget to the given setting, in rads */
static void
se_setScale (w, a)
Widget w;
double a;
{
	int v;

	if (a > CHSCALE)
	    a = (a - CHSCALE)*LOSCALE/HISCALE + CHSCALE;
	v = (int)(a/LOSCALE+.5);
	if (v > SCALEMAX) {
	    char msg[128];
	    sprintf (msg, "Sorry, scale only goes to %g", raddeg(MXSCALE));
	    xe_msg (msg, 1);
	} else {
	    if (v < 1)
		v = 1;
	    XmScaleSetValue (w, v);
	}
}

/* called to install "use" a Favorite.
 * client is index into favs[]
 */
/* ARGSUSED */
static void
se_usefav_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	Favorite *fp = &favs[(int)client];

	se_setScale (eyepws_w, fp->w);
	se_scale_fmt (eyepws_w, eyepwl_w);
	se_setScale (eyephs_w, fp->h);
	se_scale_fmt (eyephs_w, eyephl_w);
	XmToggleButtonSetState (fp->isE ? eyer_w : eyes_w, True, True);
	XmToggleButtonSetState (fp->isS ? eyef_w : eyeb_w, True, True);
}

/* called to delete a Favorite.
 * client is index into favs[]
 */
/* ARGSUSED */
static void
se_delfav_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	Favorite *fp = &favs[(int)client];
	XtDestroyWidget (fp->row_w);
	fp->inuse = 0;
	se_saveFav();
}

/* called to add a new Favorite */
/* ARGSUSED */
static void
se_addfav_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	Favorite newfav;

	memset (&newfav, 0, sizeof(newfav));
	se_eyepsz (&newfav.w, &newfav.h, &newfav.isE, &newfav.isS);
	sprintf (newfav.name, "My eyepiece #%d", nfavs+1);
	se_addFavorite (&newfav);
	se_saveFav();
}

/* called to save the current set of Favorites to the X database.
 * used to keep db up to date as user changes names.
 */
/* ARGSUSED */
static void
se_savfav_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	se_saveFav();
}

/* add (or reuse) a new (or unused) entry to favs[], create and show in favrc_w.
 */
static void
se_addFavorite (newfp)
Favorite *newfp;
{
	char buf[32], wstr[32], hstr[32];
	Widget w;
	Arg args[20];
	Favorite *fp;
	int n, fn;

	/* find/create a new entry */
	for (fn = 0; fn < nfavs; fn++)
	    if (!favs[fn].inuse)
		break;
	if (fn == nfavs)
	    favs = (Favorite *) XtRealloc ((char *)favs,
						    (++nfavs)*sizeof(Favorite));
	fp = &favs[fn];

	/* start filling with new */
	*fp = *newfp;
	fp->inuse = 1;

	/* add the widgets */
	n = 0;
	XtSetArg (args[n], XmNorientation, XmHORIZONTAL); n++;
	XtSetArg (args[n], XmNspacing, 3); n++;
	fp->row_w = XmCreateRowColumn (favrc_w, "EPF", args, n);
	XtManageChild (fp->row_w);

	    n = 0;
	    w = XmCreatePushButton (fp->row_w, "Del", args, n);
	    XtAddCallback (w, XmNactivateCallback, se_delfav_cb, (XtPointer)fn);
	    wtip (w, "Delete this eyepiece from the list of favorites");
	    XtManageChild (w);

	    n = 0;
	    w = XmCreatePushButton (fp->row_w, "Use", args, n);
	    XtAddCallback (w, XmNactivateCallback, se_usefav_cb, (XtPointer)fn);
	    wtip (w, "Install this eyepiece in the `next' settings above.");
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNcolumns, MAXEPNM-1); n++;
	    XtSetArg (args[n], XmNmaxLength, MAXEPNM-1); n++;
	    XtSetArg (args[n], XmNvalue, fp->name); n++;
	    w = XmCreateTextField (fp->row_w, "FTF", args, n);
	    XtAddCallback (w, XmNvalueChangedCallback, se_savfav_cb, NULL);
	    wtip (w, "Type in a name for this eyepiece configuration");
	    XtManageChild (w);
	    fp->name_w = w;

	    n = 0;
	    w = XmCreateLabel (fp->row_w, "FL", args, n);
	    fs_sexa (wstr, raddeg(fp->w), 2, 60);
	    fs_sexa (hstr, raddeg(fp->h), 2, 60);
	    sprintf (buf, "%s %s %c %c", wstr, hstr, fp->isE ? 'E' : 'R',
						     fp->isS ? 'S' : 'O');
	    set_xmstring (w, XmNlabelString, buf);
	    wtip (w, "Coded definition for this eyepiece: W H Shape Style");
	    XtManageChild (w);
}

/* save the current (used) entries in favs[] to the X resource */
static void
se_saveFav()
{
	char *buf;
	int l, i;

	/* null case avoids call to malloc 0 bytes */
	if (!nfavs) {
	    setXRes (feyeprn, "");
	    return;
	}

	/* get generous working space */
	buf = XtMalloc (nfavs*(MAXEPNM + 100));

	/* append each entry in use.
	 * use nl as separator (not terminator)
	 */
	for (l = i = 0; i < nfavs; i++) {
	    Favorite *fp = &favs[i];
	    char *nam;

	    if (!fp->inuse)
		continue;
	    nam = XmTextFieldGetString (fp->name_w);
	    l += sprintf (buf+l, "%8.6f %8.6f %c %c '%s'", fp->w, fp->h,
					fp->isE?'E':'R', fp->isS?'S':'O', nam);
	    XtFree (nam);
	    if (i < nfavs-1)
		l += sprintf (buf+l, "\\n");
	}

	/* store in resource */
	setXRes (feyeprn, buf);
	XtFree (buf);
}

/* read and replace the favorites list from the X resource */
static void
se_readFav()
{
	double w, h;
	char shape, style;
	char name[MAXEPNM];
	char *favbuf;
	int i;

	/* clear the favs[] array */
	for (i = 0; i < nfavs; i++)
	    if (favs[i].inuse)
		XtDestroyWidget (favs[i].row_w);
	XtFree ((char *)favs);
	favs = NULL;
	nfavs = 0;

	/* build anew from entries in feyeprn, separated by nl */
	favbuf = getXRes (feyeprn, NULL);
	while (favbuf &&
		sscanf (favbuf, "%lf %lf %c %c '%[^']'", &w, &h, &shape, &style,
								name) == 5) {
	    Favorite newf, *fp = &newf;

	    memset (fp, 0, sizeof(*fp));
	    strcpy (fp->name, name);
	    fp->isE = (shape == 'E');
	    fp->isS = (style == 'S');
	    fp->w = w;
	    fp->h = h;
	    se_addFavorite (fp);

	    favbuf = strchr (favbuf, '\n');
	    if (favbuf)
		favbuf++;
	}
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: skyeyep.c,v $ $Date: 2001/10/13 22:59:27 $ $Revision: 1.10 $ $Name:  $"};
