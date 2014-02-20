/* implement the manual sky coords conversion tool */

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <time.h>

#if defined(__STDC__)
#include <stdlib.h>
#else
extern int exit();
#endif

#include <X11/Xlib.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/TextF.h>

#include "P_.h"
#include "astro.h"
#include "circum.h"

extern Widget toplevel_w;
extern XtAppContext xe_app;
#define XtD XtDisplay(toplevel_w)
extern Colormap xe_cm;

extern Now *mm_get_now P_((void));
extern int isUp P_((Widget shell));
extern void hlp_dialog P_((char *tag, char *deflt[], int ndeflt));
extern void set_something P_((Widget w, char *resource, XtArgVal value));
extern void set_xmstring P_((Widget w, char *resource, char *txt));
extern void sr_reg P_((Widget w, char *res, char *cat, int autosav));
extern void sv_getcenter P_((int *aamodep, double *fovp, double *altp,
    double *azp, double *rap, double *decp));
extern void sv_point P_((Obj *op));
extern void timestamp P_((Now *np, Widget w));
extern void wtip P_((Widget w, char *tip));

static void cc_create P_((void));
static void cc_point_cb P_((Widget w, XtPointer client, XtPointer call));
static void cc_getsky_cb P_((Widget w, XtPointer client, XtPointer call));
static void cc_canonFormat_cb P_((Widget w, XtPointer client, XtPointer call));
static void cc_vchg_cb P_((Widget w, XtPointer client, XtPointer call));
static void cc_help_cb P_((Widget w, XtPointer client, XtPointer call));
static void cc_close_cb P_((Widget w, XtPointer client, XtPointer call));
static void cc_popdown_cb P_((Widget w, XtPointer client, XtPointer call));
static void cc_newval P_((Widget w));
static void canonAll P_((void));

static Widget ccshell_w;	/* main shell */
static Widget ccalt_w;		/* TF */
static Widget ccaz_w;		/* TF */
static Widget ccra_w;		/* TF */
static Widget ccdec_w;		/* TF */
static Widget ccglat_w;		/* TF */
static Widget ccglng_w;		/* TF */
static Widget cceclat_w;	/* TF */
static Widget cceclng_w;	/* TF */
static Widget ccdt_w;		/* time stamp label */
static Widget ccral_w;		/* RA epoch label */

static int block_vchg;		/* set to block valueChanged callback */

static char cccategory[] = "Coordinate converter";     /* Save category */

/* bring up the Manual dialog */
void
cc_manage ()
{
	if (!ccshell_w) {
	    cc_create();
	    cc_newval (ccra_w);
	    cc_canonFormat_cb (NULL, NULL, NULL);
	}

	XtPopup (ccshell_w, XtGrabNone);
	set_something (ccshell_w, XmNiconic, (XtArgVal)False);
}

/* new main Now: hold ra/dec and update alt/az */
void
cc_update (np, how_much)
Now *np;
int how_much;
{
	if (!isUp(ccshell_w))
	    return;

	cc_newval (ccra_w);
}

/* called to put up or remove the watch cursor.  */
void
cc_cursor (c)
Cursor c;
{
	Window win;

	if (ccshell_w && (win = XtWindow(ccshell_w)) != 0) {
	    Display *dsp = XtDisplay(ccshell_w);
	    if (c)
		XDefineCursor (dsp, win, c);
	    else
		XUndefineCursor (dsp, win);
	}
}

/* create the Manual entry dialog */
static void
cc_create()
{
	typedef struct {
	    char *label;	/* label */
	    char *def;		/* default value */
	    Widget *wp;		/* TF widget */
	    char *tip;		/* widget tip */
	} CCMField;
	static CCMField ccfields[] = {
	    {"Altitude:",     "  00:00:00.00", &ccalt_w,
						"Altitude, D:M:S"},
	    {"Azimuth:",      "  00:00:00.00", &ccaz_w,
						"Azimuth, D:M:S E of N"},
	    {"RA @ Epoch:",   "  00:00:00.00", &ccra_w,
						"RA, H:M:S @ Epoch"},
	    {"Declination:",  "  00:00:00.00", &ccdec_w,
						"Dec, D:M:S @ Epoch"},
	    {"Galactic lat:", "  00:00:00.00", &ccglat_w,
						"Galactic latitude, D:M:S"},
	    {"Galactic lng:", "  00:00:00.00", &ccglng_w,
						"Galactic longitude, D:M:S"},
	    {"Ecliptic lat:", "  00:00:00.00", &cceclat_w,
						"Ecliptic latitude, D:M:S"},
	    {"Ecliptic lng:", "  00:00:00.00", &cceclng_w,
						"Ecliptic longitude, D:M:S"},
	};
	Widget w;
	Widget cc_w, rc_w;
	Arg args[20];
	int i;
	int n;

	n = 0;
	XtSetArg (args[n], XmNallowShellResize, True); n++;
	XtSetArg (args[n], XmNcolormap, xe_cm); n++;
	XtSetArg (args[n], XmNtitle, "xephem Coordinate converter"); n++;
	XtSetArg (args[n], XmNiconName, "Coords"); n++;
	XtSetArg (args[n], XmNdeleteResponse, XmUNMAP); n++;
	ccshell_w = XtCreatePopupShell ("CoordsConv", topLevelShellWidgetClass,
							toplevel_w, args, n);
	set_something (ccshell_w, XmNcolormap, (XtArgVal)xe_cm);
	XtAddCallback (ccshell_w, XmNpopdownCallback, cc_popdown_cb, 0);
	sr_reg (ccshell_w, "XEphem*CoordsConv.x", cccategory, 0);
	sr_reg (ccshell_w, "XEphem*CoordsConv.y", cccategory, 0);

	n = 0;
	XtSetArg (args[n], XmNmarginHeight, 10); n++;
	XtSetArg (args[n], XmNmarginWidth, 10); n++;
	XtSetArg (args[n], XmNverticalSpacing, 10); n++;
	XtSetArg (args[n], XmNfractionBase, 46); n++;
	cc_w = XmCreateForm (ccshell_w, "CCForm", args, n);
	XtAddCallback (cc_w, XmNhelpCallback, cc_help_cb, 0);
	XtManageChild (cc_w);

	/* main table in a RC */
	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNspacing, 5); n++;
	XtSetArg (args[n], XmNpacking, XmPACK_COLUMN); n++;
	XtSetArg (args[n], XmNorientation, XmHORIZONTAL); n++;
	XtSetArg (args[n], XmNnumColumns, (XtNumber(ccfields)+1)/2); n++;
	XtSetArg (args[n], XmNisAligned, False); n++;
	rc_w = XmCreateRowColumn (cc_w, "CCMRC", args, n);
	XtManageChild (rc_w);

	    /* add the field entries */
	    for (i = 0; i < XtNumber(ccfields); i++) {
		CCMField *ccp = &ccfields[i];

		n = 0;
		XtSetArg (args[n], XmNalignment, XmALIGNMENT_END); n++;
		w = XmCreateLabel (rc_w, "CCM", args, n);
		set_xmstring (w, XmNlabelString, ccp->label);
		XtManageChild (w);

		/* grab RA label so we can show epoch */
		if (ccp->wp == &ccra_w)
		    ccral_w = w;

		n = 0;
		XtSetArg (args[n], XmNcolumns, 13); n++;
		XtSetArg (args[n], XmNvalue, ccp->def); n++;
		w = XmCreateTextField (rc_w, "CCTF", args, n);
		XtAddCallback (w, XmNvalueChangedCallback, cc_vchg_cb, 0);
		XtAddCallback (w, XmNactivateCallback, cc_canonFormat_cb, 0);
		wtip (w, ccp->tip);
		XtManageChild (w);
		*(ccp->wp) = w;
	    }

	/* time stamp */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, rc_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNalignment, XmALIGNMENT_CENTER); n++;
	ccdt_w = XmCreateLabel (cc_w, "DT", args, n);
	wtip (ccdt_w, "Date and Time for which Alt/Az are computed");
	XtManageChild (ccdt_w);

	/* controls across bottom */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, ccdt_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 1); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 9); n++;
	w = XmCreatePushButton (cc_w, "Close", args, n);
	wtip (w, "Close this window");
	XtAddCallback (w, XmNactivateCallback, cc_close_cb, 0);
	XtManageChild (w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, ccdt_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 10); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 18); n++;
	w = XmCreatePushButton (cc_w, "Sky point", args, n);
	wtip (w, "Center the Sky View at these coordinates"); 
	XtManageChild (w);
	XtAddCallback (w, XmNactivateCallback, cc_point_cb, 0);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, ccdt_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 19); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 27); n++;
	w = XmCreatePushButton (cc_w, "Get sky", args, n);
	wtip (w, "Load coordinates of Sky View center"); 
	XtAddCallback (w, XmNactivateCallback, cc_getsky_cb, 0);
	XtManageChild (w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, ccdt_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 28); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 36); n++;
	w = XmCreatePushButton (cc_w, "Canonize", args, n);
	wtip (w, "Reformat all fields in a consistent manner");
	XtAddCallback (w, XmNactivateCallback, cc_canonFormat_cb, 0);
	XtManageChild (w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, ccdt_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 37); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 45); n++;
	w = XmCreatePushButton (cc_w, "Help", args, n);
	wtip (w, "More information about this window");
	XtAddCallback (w, XmNactivateCallback, cc_help_cb, 0);
	XtManageChild (w);
}

/* callback to Point sky at current coords */
static void
cc_point_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	Now *np = mm_get_now();
	double e = epoch == EOD ? mjd : epoch;
	double ra, dec;
	char *p;
	Obj o;

	p = XmTextFieldGetString(ccra_w);
	f_scansex (0.0, p, &ra);
	XtFree(p);
	ra = hrrad(ra);

	p = XmTextFieldGetString(ccdec_w);
	f_scansex (0.0, p, &dec);
	XtFree(p);
	dec = degrad(dec);

	if (epoch == EOD)
	    ap_as (np, mjd, &ra, &dec);

	strcpy (o.o_name, "Annonymous");
	o.o_type = FIXED;
	o.f_RA = (float)ra;
	o.f_dec = (float)dec;
	o.f_epoch = (float)e;

	obj_cir (np, &o);
	sv_point (&o);
}

/* how each field is formatted */
static void
canonFmt (buf, v)
char buf[];
double v;
{
	fs_sexa (buf, v, 5, 36000);
}

/* callback to load from current Sky View center */
static void
cc_getsky_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	double fov, alt, az, ra, dec;
	char buf[32];
	int aa;

	sv_getcenter (&aa, &fov, &alt, &az, &ra, &dec);

	if (aa) {
	    /* user is driving via alt/az */
	    canonFmt (buf, raddeg(alt));
	    XmTextFieldSetString (ccalt_w, buf);
	    canonFmt (buf, raddeg(az));
	    XmTextFieldSetString (ccaz_w, buf);
	    cc_newval (ccaz_w);
	} else {
	    /* user is driving via RA/Dec */
	    canonFmt (buf, radhr(ra));
	    XmTextFieldSetString (ccra_w, buf);
	    canonFmt (buf, raddeg(dec));
	    XmTextFieldSetString (ccdec_w, buf);
	    cc_newval (ccdec_w);
	}
}

/* redisplay TF's sexagesimal value prettied up */
static void
reFormat (w)
Widget w;
{
	char buf[32], *p;
	double x;

	p = XmTextFieldGetString (w);
	f_scansex (0.0, p, &x);
	XtFree(p);
	canonFmt (buf, x);
	XmTextFieldSetString (w, buf);
}

/* tidy up each coordinate display */
static void
canonAll()
{
	block_vchg++;
	reFormat (ccalt_w);
	reFormat (ccaz_w);
	reFormat (ccra_w);
	reFormat (ccdec_w);
	reFormat (ccglat_w);
	reFormat (ccglng_w);
	reFormat (cceclat_w);
	reFormat (cceclng_w);
	block_vchg--;
}

/* called from either the Reformat PB or Enter from any TF */
static void
cc_canonFormat_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	canonAll();
}

static void
cc_close_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	/* let popdown do all the work */
	XtPopdown (ccshell_w);
}

static void
cc_popdown_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
}

/* called when any character changes on the Manual dialog */
static void
cc_vchg_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	cc_newval (w);
}

/* callback from the overall Help button.
 */
/* ARGSUSED */
static void
cc_help_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	static char *msg[] = { "Type in any field and the others are updated",};

	hlp_dialog ("Coord conv", msg, sizeof(msg)/sizeof(msg[0]));
}

/* called to compute new values when the given TF widget changes.
 */
static void
cc_newval (w)
Widget w;
{
	Now *np = mm_get_now();
	double e = (epoch == EOD) ? mjd : epoch;
	char buf[32], *p;
	double ra, dec, lst;

	/* avoid recursion */
	if (block_vchg)
	    return;
	block_vchg++;

	/* current time and eq epoch */
	timestamp (np, ccdt_w);
	if (epoch == EOD)
	    (void) strcpy (buf, "RA @ EOD:");
	else {
	    double tmp;
	    mjd_year (epoch, &tmp);
	    sprintf (buf, "RA @ %.1f:", tmp);
	}
	set_xmstring (ccral_w, XmNlabelString, buf);

	/* handy stuff */
	now_lst (np, &lst);
	lst = hrrad(lst);

	/* find astrometric ra/dec @ e from whatever changed */
	if (w == ccalt_w || w == ccaz_w) {
	    double alt, az, ha;

	    p = XmTextFieldGetString(ccalt_w);
	    f_scansex (0.0, p, &alt);
	    XtFree(p);
	    alt = degrad(alt);
	    p = XmTextFieldGetString(ccaz_w);
	    f_scansex (0.0, p, &az);
	    XtFree(p);
	    az = degrad(az);

	    unrefract (pressure, temp, alt, &alt);
	    aa_hadec (lat, alt, az, &ha, &dec);
	    ra = lst - ha;
	    range (&ra, 2*PI);
	    ap_as (np, e, &ra, &dec);

	} else if (w == ccglat_w || w == ccglng_w) {
	    double glat, glng;

	    p = XmTextFieldGetString(ccglat_w);
	    f_scansex (0.0, p, &glat);
	    XtFree(p);
	    glat = degrad(glat);
	    p = XmTextFieldGetString(ccglng_w);
	    f_scansex (0.0, p, &glng);
	    XtFree(p);
	    glng = degrad(glng);

	    gal_eq (e, glat, glng, &ra, &dec);

	} else if (w == cceclat_w || w == cceclng_w) {
	    double eclat, eclng;

	    p = XmTextFieldGetString(cceclat_w);
	    f_scansex (0.0, p, &eclat);
	    XtFree(p);
	    eclat = degrad(eclat);
	    p = XmTextFieldGetString(cceclng_w);
	    f_scansex (0.0, p, &eclng);
	    XtFree(p);
	    eclng = degrad(eclng);

	    ecl_eq (e, eclat, eclng, &ra, &dec);

	} else if (w == ccra_w || w == ccdec_w) {
	    p = XmTextFieldGetString(ccra_w);
	    f_scansex (0.0, p, &ra);
	    XtFree(p);
	    ra = hrrad(ra);
	    p = XmTextFieldGetString(ccdec_w);
	    f_scansex (0.0, p, &dec);
	    XtFree(p);
	    dec = degrad(dec);
	    if (epoch == EOD)
		ap_as (np, e, &ra, &dec);

	} else {
	    printf ("Bogus coords conv widget\n");
	    exit (1);
	}

	/* find all others from ra/dec that were not set */
	if (w != ccalt_w && w != ccaz_w) {
	    double apra = ra, apdec = dec;	/* require apparent */
	    double alt, az, ha;

	    as_ap (np, e, &apra, &apdec);
	    ha = lst - apra;
	    hadec_aa (lat, ha, apdec, &alt, &az);
	    refract (pressure, temp, alt, &alt);
	    canonFmt (buf, raddeg(alt));
	    XmTextFieldSetString (ccalt_w, buf);
	    canonFmt (buf, raddeg(az));
	    XmTextFieldSetString (ccaz_w, buf);
	}
	if (w != ccglat_w && w != ccglng_w) {
	    double glat, glng;

	    eq_gal (e, ra, dec, &glat, &glng);
	    canonFmt (buf, raddeg(glat));
	    XmTextFieldSetString (ccglat_w, buf);
	    canonFmt (buf, raddeg(glng));
	    XmTextFieldSetString (ccglng_w, buf);
	}
	if (w != cceclat_w && w != cceclng_w) {
	    double eclat, eclng;

	    eq_ecl (e, ra, dec, &eclat, &eclng);
	    canonFmt (buf, raddeg(eclat));
	    XmTextFieldSetString (cceclat_w, buf);
	    canonFmt (buf, raddeg(eclng));
	    XmTextFieldSetString (cceclng_w, buf);
	}
	if (w != ccra_w && w != ccdec_w) {
	    double era = ra, edec = dec;	/* want current epoch setting */

	    if (epoch == EOD)
		as_ap (np, e, &era, &edec);
	    canonFmt (buf, radhr(era));
	    XmTextFieldSetString (ccra_w, buf);
	    canonFmt (buf, raddeg(edec));
	    XmTextFieldSetString (ccdec_w, buf);
	}

	block_vchg--;
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: coordsmenu.c,v $ $Date: 2001/08/13 01:38:43 $ $Revision: 1.5 $ $Name:  $"};
