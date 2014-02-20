/* code to open local or fetch FITS files for skyview from STScI or ESO.
 */

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <X11/Xlib.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/DrawingA.h>
#include <Xm/RowColumn.h>
#include <Xm/ToggleB.h>
#include <Xm/TextF.h>
#include <Xm/Scale.h>
#include <Xm/FileSB.h>
#include <Xm/MessageB.h>
#include <Xm/PanedW.h>
#include <Xm/Text.h>


#include "P_.h"
#include "astro.h"
#include "circum.h"
#include "ip.h"
#include "skytoolbar.h"
#include "net.h"
#include "ps.h"
#include "patchlevel.h"

#define	MAXDSSFOV	(30.)	/* max field size we retreive, arcmins*/
#define	MINDSSFOV	(5.)	/* min field size we retreive, arcmins*/

extern Widget toplevel_w;
extern XtAppContext xe_app;
#define XtD XtDisplay(toplevel_w)
extern Colormap xe_cm;

extern Widget svshell_w;

extern FImage *si_getFImage P_((void));
extern char *expand_home P_((char *path));
extern char *getShareDir P_((void));
extern char *getPrivateDir P_((void));
extern char *getXRes P_((char *name, char *def));
extern char *syserrstr P_((void));
extern int confirm P_((void));
extern int existsh P_((char *filename));
extern int get_color_resource P_((Widget w, char *cname, Pixel *p));
extern int openh P_((char *name, int flags, ...));
extern int sv_ison P_((void));
extern int xy2RADec P_((FImage *fip, double x, double y, double *rap,
    double *decp));
extern void defaultTextFN P_((Widget w, int cols, char *x, char *y));
extern void get_something P_((Widget w, char *resource, XtArgVal value));
extern void hlp_dialog P_((char *tag, char *deflt[], int ndeflt));
extern void mm_newcaldate P_((double newmjd));
extern void msg_manage P_((void));
extern void pm_set P_((int percentage));
extern void pm_down P_((void));
extern void pm_up P_((void));
extern void prompt_map_cb P_((Widget w, XtPointer client, XtPointer call));
extern void query P_((Widget tw, char *msg, char *label1, char *label2,
    char *label3, void (*func1)(void), void (*func2)(void),
    void (*func3)(void)));
extern void set_something P_((Widget w, char *resource, XtArgVal value));
extern void set_xmstring P_((Widget w, char *resource, char *txt));
extern void si_create P_((void));
extern void si_newfim P_((FImage *fip, char *name));
extern void si_off P_((void));
extern void si_setContrast P_((FImage *fip));
extern void sr_reg P_((Widget w, char *res, char *cat, int autosav));
extern void stopd_up P_((void));
extern void stopd_down P_((void));
extern void sv_manage P_((void));
extern void sv_getcenter P_((int *aamode, double *fov,
    double *altp, double *azp, double *rap, double *decp));
extern void watch_cursor P_((int want));
extern void wtip P_((Widget w, char *tip));
extern void xe_msg P_((char *msg, int app_modal));

void sf_go_cb P_((Widget w, XtPointer client, XtPointer call));

static int sf_readFile P_((char *name));
static void sf_create P_((void));
static void initFSB P_((Widget w));
static void initPubShared P_((Widget rc_w, Widget fsb_w));
static void sf_save_cb P_((Widget w, XtPointer client, XtPointer call));
static void save_file P_((void));
static void sf_open_cb P_((Widget w, XtPointer client, XtPointer call));
static void sf_close_cb P_((Widget w, XtPointer client, XtPointer call));
static void sf_help_cb P_((Widget w, XtPointer client, XtPointer call));
static void sf_setdate_cb P_((Widget w, XtPointer client, XtPointer call));
static void sf_setSaveName P_((char *newfn, int usemysuf));
static void sf_addSaveSuffix P_((char *buf));
static char *bname P_((char *buf));

static void eso_fits P_((void));
static void stsci_fits P_((void));
static void fits_read_icb P_((XtPointer client, int *fd, XtInputId *id));
static int fitsObs P_((double *mjdp));
static void setObsDate P_((void));
static int prepOpen P_((char fn[], char errmsg[]));
static XtInputId read_iid;	/* set while working on reading from socket  */

static Widget sf_w;		/* main dialog */
static Widget savefn_w;		/* TF for save filename */
static Widget stsci_w;		/* TB for STScI, else ESO */
static Widget fsb_w;		/* FSB for opening a file */
static Widget hdr_w;		/* ScrolledText for the FITS header */
static Widget autoname_w;	/* TB for whether to auto set save filename */ 
static Widget obsdate_w;	/* label for obs date string */
static Widget setobsdate_w;	/* PB to set main to obs date */
static Widget dss1_w;		/* TB set to use DSS 1 */
static Widget dss2r_w;		/* TB set to use DSS 2 red */
static Widget dss2b_w;		/* TB set to use DSS 2 blue */


#define	FWDT	1234		/* FITS file poll interval, ms */
static int fw_isFifo P_((char *name));
static void fw_to P_((XtPointer client, XtIntervalId *id));
static void fw_icb P_((XtPointer client, int *fd, XtInputId *id));
static void fw_cb P_((Widget w, XtPointer client, XtPointer call));
static void fw_on P_((int whether));
static XtIntervalId fw_tid;	/* used to poll for file naming FITS file */
static XtInputId fw_iid;	/* used to monitor FIFO for name of FITS file */
static Widget fwfn_w;		/* TF holding Watch file name */
static Widget fw_w;		/* TB whether to watch for FITS file */
static int fw_fd;		/* file watch fifo id */

/* which survey */
typedef enum {
    DSS_1, DSS_2R, DSS_2B
} Survey;
static Survey whichSurvey P_((void));

static char fitsp[] = "FITSpattern";	/* resource name of FITS file pattern */
#if defined (__NUTC__)
static char gexe[] = "gunzip.exe";	/* gunzip executable */
#else
static char gexe[] = "gunzip";		/* gunzip executable */
#endif
static char gcmd[] = "gunzip";		/* gunzip command's argv[0] */

static char skyfitscategory[] = "Sky View -- FITS";	/* Save category */

/* called to manage the fits dialog.
 */
void
sf_manage()
{
	if (!sf_w) {
	    sf_create();
	    si_create();
	}

	XtManageChild(sf_w);
}

/* called to unmanage the fits dialog.
 */
void
sf_unmanage()
{
	if (!sf_w)
	    return;
	XtUnmanageChild (sf_w);
}

/* return 1 if dialog is up, else 0.
 */
int
sf_ismanaged()
{
	return (sf_w && XtIsManaged(sf_w));
}

/* called to put up or remove the watch cursor.  */
void
sf_cursor (c)
Cursor c;
{
	Window win;

	if (sf_w && (win = XtWindow(sf_w)) != 0) {
	    Display *dsp = XtDisplay(sf_w);
	    if (c)
		XDefineCursor (dsp, win, c);
	    else
		XUndefineCursor (dsp, win);
	}
}

/* fill the hdr_w scrolled text with the FITS header entries.
 * keep hdrl up to date.
 */
void
sf_showHeader (fip)
FImage *fip;
{
	char *header;
	int i;

	/* room for each FITS line, with nl and a final END and \0 */
	header = malloc ((fip->nvar+1)*(FITS_HCOLS+1) + 1);
	if (!header) {
	    xe_msg ("No memory to display FITS header", 0);
	    return;
	}

	/* copy from fip->var to header, adding \n after each line */
	for (i = 0; i < fip->nvar; i++) {
	    memcpy(header + i*(FITS_HCOLS+1), fip->var[i], FITS_HCOLS);
	    header[(i+1)*(FITS_HCOLS+1)-1] = '\n';
	}
	
	/* add END and '\0' to make it a real string */
	(void) sprintf (&header[i*(FITS_HCOLS+1)], "END");

	XmTextSetString (hdr_w, header);
	free (header);

	/* scroll to the top */
	XmTextShowPosition (hdr_w, (XmTextPosition)0);
}

/* return copies of the current filename and OBJECT or TARGET keywords.
 * if either can not be determined, the returned string will be 0 length.
 * N.B. we assume the caller supplies "enough" space.
 */
void
sf_getName (fn, on)
char *fn;	/* filename */
char *on;	/* object name */
{
	FImage *fip;
	char *savefn;
	int i, n;

	fip = si_getFImage ();
	if (!fip) {
	    *fn = *on = '\0';
	    return;
	}

	savefn = XmTextFieldGetString (savefn_w);
	n = strlen (savefn);

	for (i = n-1; i >= 0 && savefn[i] != '/' && savefn[i] != '\\'; --i)
	    continue;
	strcpy (fn, &savefn[i+1]);
	XtFree (savefn);

	if (getStringFITS (fip, "OBJECT", on) < 0 &&
				getStringFITS (fip, "TARGET", on) < 0)
	    *on = '\0';
}

/* t00fri: include possibility to read .fth compressed files */
static int
prepOpen (fn, errmsg)
char fn[];
char errmsg[];
{
	int fd;
	int l;

	l = strlen (fn);
	if (l < 4 || strcmp(fn+l-4, ".fth")) {
	    /* just open directly */
	    fd = openh (fn, O_RDONLY);
	    if (fd < 0)
		strcpy (errmsg, syserrstr());
	} else {
	    /* ends with .fth so need to run through fdecompress
	     * TODO: this is a really lazy way to do it --
	     */
	    char cmd[2048];
	    char tmp[128];
	    int s;

	    tmpnam (tmp);
	    strcat (tmp, ".fth");
	    sprintf (cmd, "cp %s %s; fdecompress -r %s", fn, tmp, tmp);
	    s = system (cmd);
	    if (s != 0) {
		sprintf (errmsg, "Can not execute `%s' ", cmd);
		if (s < 0)
		    strcat (errmsg, syserrstr());
		fd = -1;
	    } else {
		tmp[strlen(tmp)-1] = 's';
		fd = openh (tmp, O_RDONLY);
		(void) unlink (tmp);	/* once open, remove the .fts copy */
		if (fd < 0)
		    sprintf (errmsg, "Can not decompress %s: %s", tmp,
							    syserrstr());
	    }
	}

	return (fd);
}

/* open and read a FITS file.
 * if all ok return 0, else return -1.
 */
static int
sf_readFile (name)
char *name;
{
	char buf[1024];
	FImage sfim, *fip = &sfim;
	double eq;
	char errmsg[1024];
	int fd;
	int s;

	/* init */
	initFImage (fip);

	/* open the fits file */
	fd = prepOpen (name, errmsg);
	if (fd < 0) {
	    (void) sprintf (buf, "%s: %s", name, errmsg);
	    xe_msg (buf, 1);
	    return(-1);
	}

	/* read in */
	s = readFITS (fd, fip, buf);
	close (fd);
	if (s < 0) {
	    char buf2[1024];
	    (void) sprintf (buf2, "%s: %s", name, buf);
	    xe_msg (buf2, 1);
	    return(-1);
	}

	/* we only support 16 bit integer images */
	if (fip->bitpix != 16) {
	    (void) sprintf (buf, "%s: must be BITPIX 16", name);
	    xe_msg (buf, 1);
	    resetFImage (fip);
	    return (-1);
	}

	/* EQUINOX is preferred, but we'll accept EPOCH in a pinch */
	if (getRealFITS (fip, "EQUINOX", &eq) < 0) {
	    if (getRealFITS (fip, "EPOCH", &eq) < 0) {
		setRealFITS (fip, "EQUINOX", 2000.0, 10, "Faked");
	    } else {
		setRealFITS (fip, "EQUINOX", eq, 10, "Copied from EPOCH");
	    }
	}

	/* ok!*/
	si_newfim (fip, name);	/* N.B. copies fip .. do not resetFImage */
	setObsDate ();
	return (0);
}

/* create, but do not manage, the FITS file dialog */
static void
sf_create()
{
	Widget tf_w, bf_w;
	Widget rc_w, rb_w;
	Widget go_w;
	Widget pw_w;
	Widget h_w;
	Widget w;
	Arg args[20];
	int n;

	/* create form */
	n = 0;
	XtSetArg (args[n], XmNautoUnmanage, False); n++;
	XtSetArg (args[n], XmNallowResize, True); n++;
	XtSetArg (args[n], XmNverticalSpacing, 5); n++;
	XtSetArg (args[n], XmNmarginWidth, 5); n++;
	XtSetArg (args[n], XmNcolormap, xe_cm); n++;
	sf_w = XmCreateFormDialog (svshell_w, "SkyFITS", args, n);
	set_something (sf_w, XmNcolormap, (XtArgVal)xe_cm);
	XtAddCallback (sf_w, XmNhelpCallback, sf_help_cb, NULL);

	/* set some stuff in the parent DialogShell.
	 * setting XmNdialogTitle in the Form didn't work..
	 */
	n = 0;
	XtSetArg (args[n], XmNtitle, "xephem Sky FITS"); n++;
	XtSetValues (XtParent(sf_w), args, n);

	/* top and bottom halves are in their own forms, then
	 * each form is in a paned window
	 */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	pw_w = XmCreatePanedWindow (sf_w, "FITSPW", args, n);
	XtManageChild (pw_w);

	/* the top form */

	n = 0;
	tf_w = XmCreateForm (pw_w, "TF", args, n);
	XtManageChild (tf_w);

	    /* controls to fetch networked images */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNtopOffset, 6); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	    go_w = XmCreatePushButton (tf_w, "Get", args, n);
	    wtip (go_w, "Retrieve image of Sky View center over Internet");
	    XtAddCallback (go_w, XmNactivateCallback, sf_go_cb, NULL);
	    XtManageChild (go_w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNtopOffset, 8); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNleftWidget, go_w); n++;
	    XtSetArg (args[n], XmNleftOffset, 4); n++;
	    XtSetArg (args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	    w = XmCreateLabel (tf_w, "GL", args, n);
	    set_xmstring (w,XmNlabelString,"Digitized Sky Survey image:");
	    XtManageChild (w);

	    /* institution selection */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, go_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 3); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 3); n++;
	    w = XmCreateLabel (tf_w, "From:", args, n);
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, go_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 1); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 25); n++;
	    XtSetArg (args[n], XmNmarginHeight, 0); n++;
	    XtSetArg (args[n], XmNpacking, XmPACK_TIGHT); n++;
	    XtSetArg (args[n], XmNspacing, 6); n++;
	    XtSetArg (args[n], XmNorientation, XmHORIZONTAL); n++;
	    rb_w = XmCreateRadioBox (tf_w, "GRB", args, n);
	    XtManageChild (rb_w);

		n = 0;
		XtSetArg (args[n], XmNspacing, 4); n++;
		stsci_w = XmCreateToggleButton (rb_w, "STScI", args, n);
		wtip (stsci_w, "Get image from Maryland USA");
		XtManageChild (stsci_w);
		sr_reg (stsci_w, NULL, skyfitscategory, 1);

		/* stsci sets logic */
		n = 0;
		XtSetArg (args[n], XmNspacing, 4); n++;
		XtSetArg(args[n],XmNset,!XmToggleButtonGetState(stsci_w)); n++;
		w = XmCreateToggleButton (rb_w, "ESO", args, n);
		wtip (stsci_w, "Get image from Germany");
		XtManageChild (w);

	    /* survey selection */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, rb_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 1); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 3); n++;
	    w = XmCreateLabel (tf_w, "Survey:", args, n);
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, rb_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 0); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 25); n++;
	    XtSetArg (args[n], XmNmarginHeight, 0); n++;
	    XtSetArg (args[n], XmNpacking, XmPACK_TIGHT); n++;
	    XtSetArg (args[n], XmNspacing, 6); n++;
	    XtSetArg (args[n], XmNorientation, XmHORIZONTAL); n++;
	    rb_w = XmCreateRadioBox (tf_w, "GRB", args, n);
	    XtManageChild (rb_w);

		n = 0;
		XtSetArg (args[n], XmNspacing, 4); n++;
		dss1_w = XmCreateToggleButton (rb_w, "DSS1", args, n);
		set_xmstring (dss1_w, XmNlabelString, "DSS 1");
		wtip (dss1_w, "Original DSS");
		XtManageChild (dss1_w);
		sr_reg (dss1_w, NULL, skyfitscategory, 1);

		n = 0;
		XtSetArg (args[n], XmNspacing, 4); n++;
		dss2r_w = XmCreateToggleButton (rb_w, "DSS2R", args, n);
		set_xmstring (dss2r_w, XmNlabelString, "DSS 2R");
		wtip (dss2r_w, "DSS 2, Red band (90% complete)");
		XtManageChild (dss2r_w);
		sr_reg (dss2r_w, NULL, skyfitscategory, 1);

		n = 0;
		XtSetArg (args[n], XmNspacing, 4); n++;
		dss2b_w = XmCreateToggleButton (rb_w, "DSS2B", args, n);
		set_xmstring (dss2b_w, XmNlabelString, "DSS 2B");
		wtip (dss2b_w, "DSS 2, Blue band (50% complete)");
		XtManageChild (dss2b_w);
		sr_reg (dss2b_w, NULL, skyfitscategory, 1);

	    /* header, with possible date */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, rb_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 10); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	    h_w = XmCreateLabel (tf_w, "Lab", args, n);
	    set_xmstring (h_w, XmNlabelString, "FITS Header:");
	    XtManageChild (h_w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, rb_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 10); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    setobsdate_w = XmCreatePushButton (tf_w, "SO", args, n);
	    set_xmstring (setobsdate_w, XmNlabelString, "Set time");
	    XtAddCallback (setobsdate_w, XmNactivateCallback, sf_setdate_cb, 0);
	    wtip(setobsdate_w,"Set main XEphem time to this Observation time");
	    XtManageChild (setobsdate_w);
	    XtSetSensitive (setobsdate_w, False); /* set true when have date */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, rb_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 10); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNleftWidget, h_w); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNrightWidget, setobsdate_w); n++;
	    XtSetArg (args[n], XmNalignment, XmALIGNMENT_CENTER); n++;
	    obsdate_w = XmCreateLabel (tf_w, "ObsDate", args, n);
	    set_xmstring (obsdate_w, XmNlabelString, " ");
	    wtip(obsdate_w, "Best-guess of time of Observation");
	    XtManageChild (obsdate_w);

	    /* scrolled text in which to display the header */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, setobsdate_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 2); n++;
	    XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNbottomOffset, 10); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNautoShowCursorPosition, False); n++;
	    XtSetArg (args[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
	    XtSetArg (args[n], XmNeditable, False); n++;
	    XtSetArg (args[n], XmNcursorPositionVisible, False); n++;
	    hdr_w = XmCreateScrolledText (tf_w, "Header", args, n);
	    wtip (hdr_w, "Scrolled text area containing FITS File header");
	    XtManageChild (hdr_w);

	/* the bottom form */

	n = 0;
	bf_w = XmCreateForm (pw_w, "BF", args, n);
	XtManageChild (bf_w);

	    /* auto listen */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNtopOffset, 16); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    w = XmCreateLabel (bf_w, "FFWL", args, n);
	    set_xmstring (w, XmNlabelString, "File watch:");
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNtopOffset, 15); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 45); n++;
	    fw_w = XmCreateToggleButton (bf_w, "Watch", args, n);
	    /* N.B. don't sr_reg because that can trigger before SV ever up */
	    XtAddCallback (fw_w, XmNvalueChangedCallback, fw_cb, NULL);
	    wtip (fw_w,
		    "Whether to watch this file for name of FITS file to load");
	    XtManageChild (fw_w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, fw_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 2); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    fwfn_w = XmCreateTextField (bf_w, "WatchFile", args, n);
	    defaultTextFN (fwfn_w, 0, getPrivateDir(), "watch.txt");
	    sr_reg (fwfn_w, NULL, skyfitscategory, 1);
	    wtip (fwfn_w,"Name of file to watch for name of FITS file to load");
	    XtManageChild (fwfn_w);

	    /* label, go PB, Auto name TB and TF for saving a file */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, fwfn_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 16); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    w = XmCreateLabel (bf_w, "Save", args, n);
	    set_xmstring (w, XmNlabelString, "Save as:");
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, fwfn_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 15); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	    XtSetArg (args[n], XmNleftPosition, 45); n++;
	    w = XmCreatePushButton (bf_w, "Save", args, n);
	    set_xmstring (w, XmNlabelString, "Save now");
	    XtAddCallback (w, XmNactivateCallback, sf_save_cb, NULL);
	    wtip (w, "Save the current image to the file named below");
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, fwfn_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 15); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    autoname_w = XmCreateToggleButton (bf_w, "AutoName", args, n);
	    set_xmstring (autoname_w, XmNlabelString, "Auto name");
	    XtManageChild (autoname_w);
	    wtip (autoname_w, "When on, automatically chooses a filename based on RA and Dec");
	    sr_reg (autoname_w, NULL, skyfitscategory, 1);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, autoname_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 2); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    savefn_w = XmCreateTextField (bf_w, "SaveFN", args, n);
	    defaultTextFN (savefn_w, 0, getPrivateDir(), "xxx.fts");
	    XtAddCallback (savefn_w, XmNactivateCallback, sf_save_cb, NULL);
	    wtip (savefn_w, "Enter name of file to write, then press Enter");
	    XtManageChild (savefn_w);

	    /* the Open FSB */

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, savefn_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 16); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	    w = XmCreateLabel (bf_w, "Lab", args, n);
	    set_xmstring (w, XmNlabelString, "Open:");
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, savefn_w); n++;
	    XtSetArg (args[n], XmNtopOffset, 14); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNorientation, XmHORIZONTAL); n++;
	    XtSetArg (args[n], XmNspacing, 5); n++;
	    rc_w = XmCreateRowColumn (bf_w, "USRB", args, n);
	    XtManageChild (rc_w);

	    n = 0;
	    XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	    XtSetArg (args[n], XmNtopWidget, rc_w); n++;
	    XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	    /* t00fri: keeps FILE scrolled list width correct */
            XtSetArg (args[n], XmNresizePolicy, XmRESIZE_NONE); n++;
	    fsb_w = XmCreateFileSelectionBox (bf_w, "FSB", args, n);
	    XtManageChild (fsb_w);
	    initFSB(fsb_w);
	    initPubShared (rc_w, fsb_w);
}

/* init the directory and pattern resources of the given FileSelectionBox.
 * we try to pull these from the basic program resources.
 */
static void
initFSB (fsb_w)
Widget fsb_w;
{
	Widget w;

	/* set default dir and pattern */
	set_xmstring (fsb_w, XmNdirectory, getPrivateDir());
	set_xmstring (fsb_w, XmNpattern, getXRes (fitsp, "*.f*t*"));

	/* change some button labels.
	 * N.B. can't add tips because these are really Gadgets.
	 */
	w = XmFileSelectionBoxGetChild (fsb_w, XmDIALOG_OK_BUTTON);
	set_xmstring (w, XmNlabelString, "Open");
	w = XmFileSelectionBoxGetChild (fsb_w, XmDIALOG_CANCEL_BUTTON);
	set_xmstring (w, XmNlabelString, "Close");

	/* some other tips */
	w = XmFileSelectionBoxGetChild (fsb_w, XmDIALOG_FILTER_TEXT);
	wtip (w, "Current directory and pattern; press `Filter' to rescan");
	w = XmFileSelectionBoxGetChild (fsb_w, XmDIALOG_TEXT);
	wtip (w, "FITS file name to be read if press `Open'");

	/* connect an Open handler */
	XtAddCallback (fsb_w, XmNokCallback, sf_open_cb, NULL);

	/* connect a Close handler */
	XtAddCallback (fsb_w, XmNcancelCallback, sf_close_cb, NULL);

	/* connect a Help handler */
	XtAddCallback (fsb_w, XmNhelpCallback, sf_help_cb, NULL);
}

/* callback from the Public dir PB */
/* ARGSUSED */
static void
sharedDirCB (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	Widget fsb_w = (Widget)client;
	char buf[1024];

	(void) sprintf (buf, "%s/fits", getShareDir());
	set_xmstring (fsb_w, XmNdirectory, expand_home(buf));
}

/* callback from the Private dir PB */
/* ARGSUSED */
static void
privateDirCB (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	Widget fsb_w = (Widget)client;

	set_xmstring (fsb_w, XmNdirectory, getPrivateDir());
}

/* build a set of PB in RC rc_w so that they
 * set the XmNdirectory resource in the FSB fsb_w and invoke the Filter.
 */
static void
initPubShared (rc_w, fsb_w)
Widget rc_w, fsb_w;
{
	Arg args[20];
	char tip[1024];
	Widget w;
	int n;

	n = 0;
	w = XmCreateLabel (rc_w, "Dir", args, n);
	set_xmstring (w, XmNlabelString, "Look in:");
	XtManageChild (w);

	n = 0;
	w = XmCreatePushButton (rc_w, "Private", args, n);
	XtAddCallback(w, XmNactivateCallback, privateDirCB, (XtPointer)fsb_w);
	sprintf (tip, "Set directory to %s", getPrivateDir());
	wtip (w, XtNewString(tip));
	XtManageChild (w);

	n = 0;
	w = XmCreatePushButton (rc_w, "Shared", args, n);
	XtAddCallback(w, XmNactivateCallback, sharedDirCB, (XtPointer)fsb_w);
	sprintf (tip, "Set directory to %s/fits", getShareDir());
	wtip (w, XtNewString(tip));
	XtManageChild (w);
}

/* called when Watch TB changes */
/* ARGSUSED */
void
fw_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	fw_on (XmToggleButtonGetState(w));
}

/* called when Get PB or toolbar PB is hit */
/* ARGSUSED */
void
sf_go_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	if (!sf_w) {
	    sf_create();
	    si_create();
	}

	if (read_iid) {
	    xe_msg ("DSS download is already in progress.", 1);
	    return;
	}

	if (XmToggleButtonGetState(stsci_w))
	    stsci_fits();
	else
	    eso_fits();
}

/* called when CR is hit in the Save text field or the Save PB is hit */
/* ARGSUSED */
static void
sf_save_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	char *fn;

	if (!si_getFImage ()) {
	    xe_msg ("No current FITS file to save", 1);
	    return;
	}

	fn = XmTextFieldGetString (savefn_w);
	if (!fn || (int)strlen(fn) < 1) {
	    xe_msg ("Please specify a filename", 1);
	    XtFree (fn);
	    return;
	}

	if (existsh (fn) == 0 && confirm()) {
	    char buf[1024];
	    (void) sprintf (buf, "%s exists: Ok to Overwrite?", fn);
	    query (toplevel_w, buf, "Yes -- Overwrite", "No -- Cancel",
						NULL, save_file, NULL, NULL);
	} else
	    save_file();

	XtFree (fn);
}

/* save to file named in savefn_w.
 * we already know everything is ok to just do it now.
 */
static void
save_file()
{
	FImage *fip;
	char buf[1024];
	char *fn;
	int fd;

	fn = XmTextFieldGetString (savefn_w);

	fd = openh (fn, O_CREAT|O_WRONLY, 0666);
	if (fd < 0) {
	    (void) sprintf (buf, "%s: %s", fn, syserrstr());
	    xe_msg (buf, 1);
	    XtFree (fn);
	    return;
	}

	fip = si_getFImage ();
	if (!fip) {
	    printf ("FImage disappeared in save_file\n");
	    exit(1);
	}
	si_setContrast(fip);
	if (writeFITS (fd, fip, buf, 1) < 0) {
	    char buf2[1024];
	    (void) sprintf (buf2, "%s: %s", fn, buf);
	    xe_msg (buf2, 1);
	} else {
	    (void) sprintf (buf, "%s:\nwritten successfully", fn);
	    xe_msg (buf, confirm());
	}

	(void) close (fd);
	XtFree (fn);
}

/* called when a file selected by the FSB is to be opened */
static void
/* ARGSUSED */
sf_open_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	XmFileSelectionBoxCallbackStruct *s =
				    (XmFileSelectionBoxCallbackStruct *)call;
	char *sp;

	if (s->reason != XmCR_OK) {
	    printf ("%s: Unknown reason = 0x%x\n", "sf_open_cb()", s->reason);
	    exit (1);
	}

	watch_cursor(1);

	XmStringGetLtoR (s->value, XmSTRING_DEFAULT_CHARSET, &sp);
	if (sf_readFile (sp) == 0) {
	    /* copy name to save buffer if autoname is on */
	    if (XmToggleButtonGetState(autoname_w)) {
		sf_setSaveName (bname(sp), 1);
	    }
	}

	XtFree (sp);

	watch_cursor(0);
}

/* ARGSUSED */
static void
sf_close_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	XtUnmanageChild (sf_w);
}

/* ARGSUSED */
static void
sf_help_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	static char *msg[] = {
"Read in local FITS files or read from Network.",
"Resulting image will be displayed in Sky View."
};

	hlp_dialog ("Sky FITS", msg, sizeof(msg)/sizeof(msg[0]));
}

/* callback to set main time to match FITS */
/* ARGSUSED */
static void
sf_setdate_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	double newmjd;

	if (fitsObs(&newmjd) == 0)
	    mm_newcaldate(newmjd);
}

/* set savefn_w to newfn.
 * preserve any existing directory.
 * preserve suffix too unless usemysuf.
 */
static void
sf_setSaveName(newfn, usemysuf)
char *newfn;
int usemysuf;
{
	char buf[1024];
	char *fn;

	fn = XmTextFieldGetString (savefn_w);
	(void) sprintf (buf, "%.*s%s", bname(fn)-fn, fn, newfn);
	if (!usemysuf)
	    sf_addSaveSuffix(buf);
	XtFree (fn);
	XmTextFieldSetString (savefn_w, buf);
}

/* return pointer to basename portion of filename fn.
 */
static char *
bname (fn)
char *fn;
{
	char *base;

	if (!(base = strrchr(fn,'/')) && !(base = strrchr(fn,'\\')))
	    base = fn;
	else
	    base++;		/* skip the / */

	return (base);
}

/* append existing suffic already in savefn_w, if any, to buf.
 * if none, make one up.
 */
static void
sf_addSaveSuffix(buf)
char *buf;
{
	char *fn = XmTextFieldGetString (savefn_w);
	char *sf = strrchr (fn, '.');

	strcat (buf, sf ? sf : ".fts");
	XtFree (fn);
}

/* return 0 if find the string str in buf, else -1 */
static int
chk4str (str, buf)
char str[];
char buf[];
{
	int l = strlen (str);

	while (*buf != '\0')
	    if (strncmp (str, buf++, l) == 0)
		return (0);
	return (-1);
}

/* return 0 if have gunzip else -1 */
static int
chk_gunzip()
{
#define	NOGZEXIT	88		/* any impossible gzip exit value */
	static int know = 1;		/* 0 or -1 when know for sure */
	int wstatus;
	int pid;

	/* only really test once */
	if (know <= 0)
	    return (know);

	/* fork/exec and see how it exits */
	pid = fork();
	if (pid < 0)
	    return (know = -1);
	if (pid == 0) {
	    /* new process: exec gunzip else exit crazy */
	    int nullfd = open ("/dev/null", O_RDWR);
	    if (nullfd < 0)
		_exit(NOGZEXIT);
	    dup2 (nullfd, 0);
	    dup2 (nullfd, 1);
	    dup2 (nullfd, 2);
	    execlp (gexe, gcmd, NULL);
	    /* does not return if works */
	    _exit(NOGZEXIT);
	}

	/* parent waits for exit status */
	know = (waitpid (pid, &wstatus, 0) == pid && WIFEXITED(wstatus)
				&& WEXITSTATUS(wstatus) != NOGZEXIT) ? 0 : -1;
	return (know);
}

/* return 1 if have/want to use gunzip, else 0 */
static int
use_gunzip()
{
	if (chk_gunzip() < 0) {
	    char msg[1024];
	    (void) sprintf (msg,
		    "Can not find %s.\nProceeding without compression", gcmd);
	    xe_msg (msg, 1);
	    return (0);
	}

	return (1);
}

/* setup the pipe between gunzip and xephem to decompress the data.
 * return pid if ok, else -1.
 */
static int
setup_gunzip_pipe(int sockfd)
{
	int gzfd[2];		/* file descriptors for gunzip pipe */
	int pid;
	int errfd;

	/* make the pipe to gunzip */
	if (pipe(gzfd) < 0) {
	    xe_msg ("Can not make pipe for gunzip", 1);
	    return (-1);
	}

	/* no zombies */
	signal (SIGCHLD, SIG_IGN);
  
	/* fork/exec gunzip */
	switch((pid = fork())) {
	case 0:			/* child: put gunzip between socket and us */
	    close (gzfd[0]);	/* do not need read side of pipe */
	    dup2 (sockfd, 0);	/* socket becomes gunzip's stdin */
	    close (sockfd);	/* do not need after dup */
	    dup2 (gzfd[1], 1);	/* write side of pipe becomes gunzip's stdout */
	    close (gzfd[1]);	/* do not need after dup */
	    errfd = open ("/dev/null", O_RDWR);
	    if (errfd >= 0) {
		dup2 (errfd, 2);/* dump gunzip's stderr */
		close (errfd);
	    }
	    execlp (gexe, gcmd, "-c", NULL);
	    _exit (1);		/* exit in case gunzip disappeared */
	    break;		/* :) */

	case -1:	/* fork failed */
	    xe_msg ("Can not fork for gunzip", 1);
	    return (-1);

	default:	/* parent */
	    break;
	}
	
	/* put gunzip between the socket and us */
	close (gzfd[1]);	/* do not need write side of pipe */
	dup2 (gzfd[0], sockfd);	/* read side of pipe masquarades as socket */
	close (gzfd[0]);	/* do not need after dup */

	/* return gunzip's pid */
	return (pid);
}

static Survey
whichSurvey()
{
	if (XmToggleButtonGetState(dss2r_w))
	    return (DSS_2R);
	if (XmToggleButtonGetState(dss2b_w))
	    return (DSS_2B);
	return (DSS_1);
}

/* start an input stream reading a FITS image from ESO */
static void
eso_fits()
{
	static char host[] = "archive.eso.org";
	static FImage sfim, *fip = &sfim;
	double fov, alt, az, ra, dec;
	char rastr[32], *rap, decstr[32], *decp;
	char buf[1024], msg[1024];
	char *survey;
	int gzpid;
	int sockfd;
	int aamode;
	int sawfits;

	/* do not turn off watch until completely finished */
	watch_cursor (1);

	/* let user abort */
	stopd_up();

	/* init fip (not reset because we copy the malloc'd fields to fim) */
	initFImage (fip);

	/* find current skyview center and size, in degrees */
	sv_getcenter (&aamode, &fov, &alt, &az, &ra, &dec);
	fov = 60*raddeg(fov);
	ra = radhr (ra);
	dec = raddeg (dec);

	if (fov > MAXDSSFOV)
	    fov = MAXDSSFOV;
	if (fov < MINDSSFOV)
	    fov = MINDSSFOV;

	/* get desired survey */
	switch (whichSurvey()) {
	default:
	case DSS_1:  survey = "DSS1"; break;
	case DSS_2R: survey = "DSS2-red"; break;
	case DSS_2B: survey = "DSS2-blue"; break;
	}

	/* format and send the request.
	 * N.B. ESO can't tolerate leading blanks in ra dec specs
	 */
	fs_sexa (rastr, ra, 2, 3600);
	for (rap = rastr; *rap == ' '; rap++)
	    continue;
	fs_sexa (decstr, dec, 3, 3600);
	for (decp = decstr; *decp == ' '; decp++)
	    continue;
	(void) sprintf (buf, "/dss/dss?ra=%s&dec=%s&equinox=J2000&Sky-Survey=%s&mime-type=%s&x=%.0f&y=%.0f HTTP/1.0\r\nUser-Agent: xephem/%s\r\n\r\n",
						rap, decp, survey,
			use_gunzip() ? "display/gz-fits" : "application/x-fits",
						fov, fov, PATCHLEVEL);
	(void) sprintf (msg, "Command to %s:", host);
	xe_msg (msg, 0);
	xe_msg (buf, 0);
	sockfd = httpGET (host, buf, msg);
	if (sockfd < 0) {
	    xe_msg (msg, 1);
	    stopd_down();
	    watch_cursor (0);
	    return;
	}

	/* read back the header -- ends with a blank line */
	sawfits = 0;
	while (recvline (sockfd, buf, sizeof(buf)) > 1) {
	    xe_msg (buf, 0);
	    if (chk4str ("application/x-fits", buf) == 0
					 || chk4str ("image/x-fits", buf) == 0)
		sawfits = 1;
	}

	/* if do not see a fits file, show what we do find */
	if (!sawfits) {
	    xe_msg (" ", 0);
	    xe_msg ("Message from server", 1);
	    xe_msg ("------------------", 0);
	    while (recvline (sockfd, buf, sizeof(buf)) > 0)
		xe_msg (buf, 0);
	    xe_msg ("------------------", 0);
	    xe_msg ("End of Message from server", 0);
	    msg_manage();
	    watch_cursor (0);
	    close (sockfd);
	    stopd_down();
	    return;
	}

	/* possibly connect via gunzip -- weird if have gunzip but can't */
	if (use_gunzip()) {
	    gzpid = setup_gunzip_pipe(sockfd);
	    if (gzpid < 0) {
		watch_cursor (0);
		close (sockfd);
		stopd_down();
		return;
	    }
	} else
	    gzpid = -1;
		

	/* read the FITS header portion */
	if (readFITSHeader (sockfd, fip, buf) < 0) {
	    watch_cursor (0);
	    xe_msg (buf, 1);
	    resetFImage (fip);
	    close (sockfd);
	    if (gzpid > 0)
		kill (gzpid, SIGTERM);
	    stopd_down();
	    return;
	}

	/* ok, start reading the pixels and give user a way to abort */
	pm_up();	/* for your viewing pleasure */
	read_iid = XtAppAddInput (xe_app, sockfd, (XtPointer)XtInputReadMask,
						fits_read_icb, (XtPointer)fip);
}

/* start an input stream reading a FITS image from STScI */
static void
stsci_fits()
{
	static char host[] = "archive.stsci.edu";
	static FImage sfim, *fip = &sfim;
	double fov, alt, az, ra, dec;
	char buf[1024], msg[1024];
	char *survey;
	int gzpid;
	int sockfd;
	int aamode;
	int sawfits;

	/* do not turn off watch until completely finished */
	watch_cursor (1);

	/* let user abort */
	stopd_up();

	/* init fip (not reset because we copy the malloc'd fields to fim) */
	initFImage (fip);

	/* find current skyview center and size, in degrees */
	sv_getcenter (&aamode, &fov, &alt, &az, &ra, &dec);
	fov = 60*raddeg(fov);
	ra = raddeg (ra);
	dec = raddeg (dec);

	if (fov > MAXDSSFOV)
	    fov = MAXDSSFOV;
	if (fov < MINDSSFOV)
	    fov = MINDSSFOV;

	/* get desired survey */
	switch (whichSurvey()) {
	default:
	case DSS_1:  survey = "1"; break;
	case DSS_2R: survey = "2r"; break;
	case DSS_2B: survey = "2b"; break;
	}

	/* format and send the request */
	(void) sprintf(buf,"/cgi-bin/dss_search?ra=%.5f&dec=%.5f&equinox=J2000&v=%s&height=%.0f&width=%.0f&format=FITS&compression=%s&version=3 HTTP/1.0\nUser-Agent: xephem/%s\r\n\r\n",
						ra, dec, survey, fov, fov,
						use_gunzip() ? "gz" : "NONE",
						PATCHLEVEL);
	(void) sprintf (msg, "Command to %s:", host);
	xe_msg (msg, 0);
	xe_msg (buf, 0);
	sockfd = httpGET (host, buf, msg);
	if (sockfd < 0) {
	    xe_msg (msg, 1);
	    stopd_down();
	    watch_cursor (0);
	    return;
	}

	/* read back the header -- ends with a blank line */
	sawfits = 0;
	while (recvline (sockfd, buf, sizeof(buf)) > 1) {
	    xe_msg (buf, 0);
	    if (chk4str ("image/x-fits", buf) == 0)
		sawfits = 1;
	}

	/* if do not see a fits file, show what we do find */
	if (!sawfits) {
	    xe_msg (" ", 0);
	    xe_msg ("Message from server", 1);
	    xe_msg ("------------------", 0);
	    while (recvline (sockfd, buf, sizeof(buf)) > 0)
		xe_msg (buf, 0);
	    xe_msg ("------------------", 0);
	    xe_msg ("End of Message from server", 0);
	    msg_manage();
	    watch_cursor (0);
	    close (sockfd);
	    stopd_down();
	    return;
	}


	/* possibly connect via gunzip -- weird if have gunzip but can't */
	if (use_gunzip()) {
	    gzpid = setup_gunzip_pipe(sockfd);
	    if (gzpid < 0) {
		watch_cursor (0);
		close (sockfd);
		stopd_down();
		return;
	    }
	} else
	    gzpid = -1;

	/* read the FITS header portion */
	if (readFITSHeader (sockfd, fip, buf) < 0) {
	    watch_cursor (0);
	    xe_msg (buf, 1);
	    resetFImage (fip);
	    close (sockfd);
	    if (gzpid > 0)
		kill (gzpid, SIGTERM);
	    stopd_down();
	    return;
	}

	/* ok, start reading the pixels and give user a way to abort */
	pm_up();	/* for your viewing pleasure */
	read_iid = XtAppAddInput (xe_app, sockfd, (XtPointer)XtInputReadMask,
						fits_read_icb, (XtPointer)fip);
}

/* called whenever there is more data available on the sockfd.
 * client is *FImage being accumulated.
 */
static void
fits_read_icb (client, fd, id)
XtPointer client;
int *fd;
XtInputId *id;
{
	FImage *fip = (FImage *)client;
	int sockfd = *fd;
	double ra, dec;
	char buf[1024];

	/* read another chunk */
	if (readIncFITS (sockfd, fip, buf) < 0) {
	    xe_msg (buf, 1);
	    resetFImage (fip);
	    close (sockfd);
	    XtRemoveInput (read_iid);
	    read_iid = (XtInputId)0;
	    stopd_down();
	    pm_down();
	    watch_cursor (0);
	    return;
	}

	/* report progress */
	pm_set (fip->nbytes * 100 / fip->totbytes);
	XmUpdateDisplay (toplevel_w);

	/* keep going if expecting more */
	if (fip->nbytes < fip->totbytes)
	    return;

	/* finished reading */
	stopd_down();
	pm_down();
	close (sockfd);
	XtRemoveInput (read_iid);
	read_iid = (XtInputId)0;

	/* YES! */

	/* give it a name */

	if (xy2RADec (fip, fip->sw/2.0, fip->sh/2.0, &ra, &dec) < 0) {
	    /* no headers! use time I guess */
	    struct tm *tp;
#if defined(__STDC__)
	    time_t t0;
#else
	    long t0;
#endif

	    time (&t0);
	    tp = gmtime (&t0);
	    if (!tp)
		localtime (&t0);

	    sprintf (buf, "%04d%02d%02dT%02d%02d%02d",
		tp->tm_year+1900, tp->tm_mon+1, tp->tm_mday, tp->tm_hour,
		tp->tm_min, tp->tm_sec);
	} else {
	    int dneg, rh, rm, dd, dm;
	    ra = radhr(ra);
	    dec = raddeg(dec);
	    if ((dneg = (dec < 0)))
		dec = -dec;
	    rh = (int)floor(ra);
	    rm = (int)floor((ra - rh)*60.0 + 0.5);
	    if (rm == 60) {
		if (++rh == 24)
		    rh = 0;
		rm = 0;
	    }
	    dd = (int)floor(dec);
	    dm = (int)floor((dec - dd)*60.0 + 0.5);
	    if (dm == 60) {
		dd++;
		dm = 0;
	    }
	    (void) sprintf (buf, "%02d%02d%c%02d%02d", rh, rm,
						dneg ? '-' : '+', dd, dm);

	}

	/* set save name from image center, if enabled */
	if (XmToggleButtonGetState(autoname_w))
	    sf_setSaveName (buf, 0);

	/* commit and display */
	sf_addSaveSuffix(buf);
	si_newfim (fip, buf);	/* N.B. copies fip .. do not resetFImage */
	setObsDate ();
	watch_cursor (0);
}

/* poke around in the headers and try to find the mjd of the observation.
 * return 0 if think we found something, else -1
 */
static int
fitsObs(mjdp)
double *mjdp;
{
	FImage *fip = si_getFImage();
	char buf[128];
	double x;

	if (!fip)
	    return (-1);

	if (getRealFITS (fip, "JD", &x) == 0) {
	    *mjdp = x - MJD0;
	    return (0);
	}

	if (getRealFITS (fip, "EPOCH", &x) == 0) {
	    year_mjd (x, mjdp);
	    return (0);
	}

	if (getStringFITS (fip, "DATE-OBS", buf) == 0) {
	    int dd, mm, yy;
	    if (sscanf (buf, "%d%*[/-]%d%*[/-]%d", &dd, &mm, &yy) == 3) {
		yy += (yy < 50 ? 2000 : 1900);
		cal_mjd (mm, (double)dd, yy, mjdp);
		return (0);
	    }
	}
	
	return (-1);
}

/* get and display the time of observation */
static void
setObsDate()
{
	double objsmjd;

	if (fitsObs(&objsmjd) == 0) {
	    int mm, yy, d, h, m, s;
	    double dd, dh, dm, ds;
	    char buf[128];

	    mjd_cal (objsmjd, &mm, &dd, &yy);
	    d = (int)dd;
	    dh = (dd - d)*24.;
	    h = (int)dh;
	    dm = (dh - h)*60.;
	    m = (int)dm;
	    ds = (dm - m)*60.;
	    if (ds > 59.5) {
		s = 0;
		if (++m == 60) {
		    m = 0;
		    h += 1; /* TODO: roll date if hits 24 */
		}
	    } else
		s = (int)ds;

	    sprintf (buf, "%d-%d-%d %02d:%02d:%02d", yy, mm, d, h, m, s);
	    set_xmstring (obsdate_w, XmNlabelString, buf);
	    XtSetSensitive (setobsdate_w, True);
	} else {
	    set_xmstring (obsdate_w, XmNlabelString, " ");
	    XtSetSensitive (setobsdate_w, False);
	}
}

/* turn on or off file watching.
 */
static void
fw_on (whether)
int whether;
{
	/* turn everything off */
	if (fw_tid) {
	    XtRemoveTimeOut (fw_tid);
	    fw_tid = 0;
	}
	if (fw_iid) {
	    close (fw_fd);
	    XtRemoveInput (fw_iid);
	    fw_iid = 0;
	}

	/* then maybe restart */
	if (whether) {
	    char *txt, wfn[1024];

	    /* clean scrubbed file name to watch */
	    txt = XmTextFieldGetString (fwfn_w);
	    strcpy (wfn, expand_home(txt));
	    XtFree (txt);

	    /* start timer or input depending on whether fifo */
	    if (fw_isFifo(wfn)) {
		fw_fd = open (wfn, O_RDWR);
		if (fw_fd < 0) {
		    char msg[1024];
		    sprintf (msg, "%s: %s", wfn, syserrstr());
		    XmToggleButtonSetState (fw_w, False, False);
		} else {
		    fw_iid = XtAppAddInput (xe_app, fw_fd,
				    (XtPointer)XtInputReadMask, fw_icb, NULL);
		}
	    } else {
		fw_tid = XtAppAddTimeOut (xe_app, 0, fw_to, 0);
	    }
	}
}

/* called periodically to check whether file in fwfn_w names a FITS file
 * to load. when it does, load the named file and delete the watch file
 * as a simple form of ACK.
 */
static void
fw_to (client, id)
XtPointer client;
XtIntervalId *id;
{
	char wfn[512], ffn[512];
	char *txt, *nl;
	int wfd, nr;

	/* try to open watch file */
	txt = XmTextFieldGetString (fwfn_w);
	strcpy (wfn, expand_home(txt));
	XtFree (txt);
	wfd = open (wfn, O_RDONLY|O_NONBLOCK);
	if (wfd < 0)
	    goto again;

	/* read it to get name of FITS file */
	nr = read (wfd, ffn, sizeof(ffn));
	close (wfd);
	if (nr <= 0)
	    goto again;
	ffn[nr] = '\0';
	nl = strchr (ffn, '\n');
	if (nl)
	    *nl = '\0';
	strcpy (ffn, expand_home(ffn));

	/* display and remove */
	sv_manage();
	sf_readFile (ffn);
	remove (wfn);

    again:

	fw_tid = XtAppAddTimeOut (xe_app, FWDT, fw_to, 0);
}

/* called whenever the FITS filename fifo might have something to read.
 */
static void
fw_icb (client, fdp, id)
XtPointer client;
int *fdp;
XtInputId *id;
{
	char *nl, ffn[1024];
	int nr;

	nr = read (fw_fd, ffn, sizeof(ffn));
	if (nr <= 0) {
	    if (nr < 0)
		sprintf (ffn, "FITS Watch fifo: %s", syserrstr());
	    else
		sprintf (ffn, "EOF from Watch fifo.");
	    strcat (ffn, "\nTurning FITS Watching off");
	    xe_msg (ffn, 1);
	    XmToggleButtonSetState (fw_w, False, True); /* let it clean up */
	}

	ffn[nr] = '\0';
	nl = strchr (ffn, '\n');
	if (nl)
	    *nl = '\0';
	strcpy (ffn, expand_home(ffn));

	/* display */
	sv_manage();
	sf_readFile (ffn);
}

/* return whether fn claims to be a fifo */
static int
fw_isFifo (fn)
char *fn;
{
	struct stat st;
	return (!stat (fn, &st) && (st.st_mode & S_IFIFO));
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: skyfits.c,v $ $Date: 2001/10/10 05:36:32 $ $Revision: 1.42 $ $Name:  $"};
