/* this file contains the code to display and search for help messages.
 * the messages come from a file or, if no file is found or there is no
 * help entry for the requested subject, a small default text is provided.
 *
 * help file format:
 *    @<tag>
 *	help for section labeled <tag> is from here to the next @
 *    +<tag>
 *	interpolate section for <tag> here then continue
 * the tags are referenced in the call to hlp_dialog().
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

#if defined(__STDC__)
#include <stdlib.h>
#endif

#include <X11/Xlib.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/RowColumn.h>
#include <Xm/Label.h>
#include <Xm/Text.h>
#include <Xm/List.h>
#include <Xm/TextF.h>

#include "P_.h"


extern Widget toplevel_w;
#define	XtD XtDisplay(toplevel_w)
extern Colormap xe_cm;

char helpcategory[] = "Help";			/* Save category */

extern FILE *fopenh P_((char *name, char *how));
extern char *getShareDir P_((void));
extern char *syserrstr P_((void));
extern void get_something P_((Widget w, char *resource, XtArgVal value));
extern void prompt_map_cb P_((Widget w, XtPointer client, XtPointer call));
extern void set_something P_((Widget w, char *resource, XtArgVal value));
extern void set_xmstring P_((Widget w, char *resource, char *txt));
extern void sr_reg P_((Widget w, char *res, char *cat, int autosav));
extern void wtip P_((Widget w, char *tip));
extern void xe_msg P_((char *msg, int app_modal));

static Widget hlp_w;		/* main help shell */
static Widget hlpt_w;		/* main help text field */

static void hlp_create P_((void));
static void hlp_close_cb P_((Widget w, XtPointer client, XtPointer call));
static FILE *hlp_openat P_((char *tag));
static int hlp_fillfromfile P_((char *tag, Widget txt_w, int l));
static void hlp_fillfromstrings P_((char *msg[], int nmsg, Widget txt_w));
static FILE *hlp_open P_((void));

static char *strnospace P_((char *str));
static void hlps_create P_((void));
static void hlps_search_cb P_((Widget w, XtPointer client, XtPointer call));
static void hlps_sel_cb P_((Widget w, XtPointer client, XtPointer call));
static void hlps_close_cb P_((Widget w, XtPointer client, XtPointer call));
static void hlps_next_cb P_((Widget w, XtPointer client, XtPointer call));
static void hlps_popdown_cb P_((Widget w, XtPointer client, XtPointer call));
static void hlps_dialog P_((char *tag, char *srch));
static void strtolower P_((char *str));
static void strtospace P_((char *str));
static void insertString P_((XmString xms));

static Widget hlps_w;		/* main shell for search dialog */
static Widget hlpstf_w;		/* search pattern text field */
static Widget hlpssl_w;		/* matching categories scrolled list */
static Widget mc_w;		/* TB: whether to match case */
static Widget ww_w;		/* TB: whether to require whole word only */
static Widget nxt_w;		/* PB: "Next" search position */

static XmTextPosition *mpos;	/* malloced array of search match positions */
static int nmpos;		/* number in mpos */
static int nxmpos;		/* index showing now */

#define	MAXLINE		128	/* longest allowable help file line */
#define	HLP_TAG		'@'	/* help file tag marker */
#define	HLP_NEST	'+'	/* help file nested tag marker */

/* put up a help dialog filled with text for the given tag.
 * if can't find any such tag, say so and use the deflt provided, if any.
 */
void
hlp_dialog (tag, deflt, ndeflt)
char *tag;	/* tag to look for in help file - also dialog title */
char *deflt[];	/* help text to use if tag not found */
int ndeflt;	/* number of strings in deflt[] */
{
	char title[MAXLINE];

	if (!hlp_w)
	    hlp_create ();

	if (hlp_fillfromfile(tag, hlpt_w, 0) < 0) {
	    if (!deflt || ndeflt == 0) {
		char buf[MAXLINE];
		(void) sprintf (buf, "No help for %s", tag);
		xe_msg (buf, 1);
		return;
	    } else
		hlp_fillfromstrings (deflt, ndeflt, hlpt_w);
	}

	XmTextShowPosition (hlpt_w, (XmTextPosition)0);
	(void) sprintf (title, "xephem Help on %s", tag);
	XtVaSetValues (hlp_w, XmNtitle, title, NULL);
	XtUnmanageChild (nxt_w);
	XtPopup (hlp_w, XtGrabNone);

	/* everything gets destroyed if shell is popped down */
}

/* put up a window that allows searching for text in all of helps */
void
hlps_manage()
{
	if (!hlps_w)
	    hlps_create();

	XtPopup (hlps_w, XtGrabNone);
	set_something (hlps_w, XmNiconic, (XtArgVal)False);
}

/* create the help window (hlp_w) with a scrolled text area (hlpt_w).
 */
static void
hlp_create ()
{
	Widget f_w;
	Widget cb_w;
	Arg args[20];
	int n;

	/* make the help shell and form */

	n = 0;
	XtSetArg (args[n], XmNallowShellResize, True); n++;
	XtSetArg (args[n], XmNcolormap, xe_cm); n++;
	XtSetArg (args[n], XmNiconName, "Help"); n++;
	XtSetArg (args[n], XmNdeleteResponse, XmUNMAP); n++;
	hlp_w = XtCreatePopupShell ("HelpWindow", topLevelShellWidgetClass,
							toplevel_w, args, n);
	set_something (hlp_w, XmNcolormap, (XtArgVal)xe_cm);

	n = 0;
	XtSetArg (args[n], XmNmarginHeight, 10); n++;
	XtSetArg (args[n], XmNmarginWidth, 10); n++;
	XtSetArg (args[n], XmNverticalSpacing, 10); n++;
	XtSetArg (args[n], XmNfractionBase, 9); n++;
	f_w = XmCreateForm (hlp_w, "HelpF", args, n);
	XtManageChild (f_w);

	/* make the Close button */

	n = 0;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 2); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 3); n++;
	cb_w = XmCreatePushButton (f_w, "Close", args, n);
	wtip (cb_w, "Close this Help window");
	XtAddCallback (cb_w, XmNactivateCallback, hlp_close_cb, 0);
	XtManageChild (cb_w);

	/* make the Next button .. don't manage now */

	n = 0;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 6); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 7); n++;
	nxt_w = XmCreatePushButton (f_w, "Next", args, n);
	XtAddCallback (nxt_w, XmNactivateCallback, hlps_next_cb, 0);
	wtip (nxt_w, "Advance to next matching search text");

	/* make the scrolled text area to help the help text */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNbottomWidget, cb_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
	XtSetArg (args[n], XmNeditable, False); n++;
	XtSetArg (args[n], XmNcursorPositionVisible, False); n++;
	XtSetArg (args[n], XmNautoShowCursorPosition, False); n++;
	XtSetArg (args[n], XmNmarginHeight, 5); n++;
	XtSetArg (args[n], XmNmarginWidth, 5); n++;
	hlpt_w = XmCreateScrolledText (f_w, "HelpText", args, n);
	XtManageChild (hlpt_w);
}

/* called on Close */
/* ARGSUSED */
static void
hlp_close_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	XtPopdown (hlp_w);
}

/* open the help file and position at first line after we see "@tag\n".
 * if successfull return a FILE *, else return 0.
 */
static FILE *
hlp_openat (tag)
char *tag;
{
	char buf[MAXLINE];
	char tagline[MAXLINE];
	FILE *fp;

	fp = hlp_open();
	if (!fp)
	    return ((FILE *)0);

	(void) sprintf (tagline, "%c%s\n", HLP_TAG, tag);
	while (fgets (buf, sizeof(buf), fp))
	    if (strcmp (buf, tagline) == 0)
		return (fp);

	(void) fclose (fp);
	return ((FILE *)0);
}

/* search help file for tag entry, then copy that entry into txt_w.
 * l is the number of chars already in txt_w (forced if 0).
 * also recursively follow any NESTed entries found.
 * return new length of txt_w, else -1 if error.
 */
static int
hlp_fillfromfile(tag, txt_w, l)
char *tag;
Widget txt_w;
int l;
{
	FILE *fp;
	char buf[MAXLINE];
	
	fp = hlp_openat (tag);
	if (!fp)
	    return (-1);

	if (l == 0)
	    XmTextSetString (txt_w, "");

	while (fgets (buf, sizeof(buf), fp)) {
	    if (buf[0] == HLP_TAG)
		break;
	    else if (buf[0] == HLP_NEST) {
		int newl;
		buf[strlen(buf)-1] = '\0';	/* remove trailing \n */
		newl = hlp_fillfromfile (buf+1, txt_w, l);
		if (newl > l)
		    l = newl;
	    } else {
		char tabbuf[8*MAXLINE];
		int in, out;

		for (in = out = 0; buf[in] != '\0'; in++)
		    if (buf[in] == '\t')
			do
			    tabbuf[out++] = ' ';
			while (out%8);
		    else
			tabbuf[out++] = buf[in];
		tabbuf[out] = '\0';

		/* tabbuf will already include a trailing '\n' */
		XmTextReplace (txt_w, l, l, tabbuf);
		l += out;
	    }
	}

	(void) fclose (fp);
	return (l);
}

/* copy each msg[] into txt_w */
static void
hlp_fillfromstrings(msg, nmsg, txt_w)
char *msg[];
int nmsg;
Widget txt_w;
{
	static char nohelpwarn[] = 
	    "No HELP file found. Set XEphem.HELPFILE to point at xephem.hlp.\n\nMinimal Help only:\n\n";
	int i, l;

	l = 0;

	XmTextReplace (txt_w, l, l, nohelpwarn);
	l += strlen (nohelpwarn);

	for (i = 0; i < nmsg; i++) {
	    XmTextReplace (txt_w, l, l, msg[i]);
	    l += strlen (msg[i]);
	    XmTextReplace (txt_w, l, l, "\n");
	    l += 1;
	}
}

/* open help file */
static FILE *
hlp_open()
{
	char fn[1024];
	FILE *fp;

	(void) sprintf (fn, "%s/auxil/xephem.hlp",  getShareDir());
	fp = fopenh (fn, "r");
	if (!fp) {
	    char buf[1024];
	    sprintf (buf, "%s:\n%s", fn, syserrstr());
	    xe_msg (buf, 1);
	}
	return (fp);
}

/* create the help search window */
static void
hlps_create()
{
	Widget rc_w, f_w, s_w, w;
	Arg args[20];
	int n;

	/* create shell and form */

	n = 0;
	XtSetArg (args[n], XmNallowShellResize, True); n++;
	XtSetArg (args[n], XmNcolormap, xe_cm); n++;
	XtSetArg (args[n], XmNtitle, "xephem Help search"); n++;
	XtSetArg (args[n], XmNiconName, "Search"); n++;
	XtSetArg (args[n], XmNdeleteResponse, XmUNMAP); n++;
	hlps_w = XtCreatePopupShell ("HelpSearch", topLevelShellWidgetClass,
							toplevel_w, args, n);
	set_something (hlps_w, XmNcolormap, (XtArgVal)xe_cm);
        XtAddCallback (hlps_w, XmNpopdownCallback, hlps_popdown_cb, 0);
	sr_reg (hlps_w, "XEphem*HelpSearch.width", helpcategory, 0);
	sr_reg (hlps_w, "XEphem*HelpSearch.height", helpcategory, 0);
	sr_reg (hlps_w, "XEphem*HelpSearch.x", helpcategory, 0);
	sr_reg (hlps_w, "XEphem*HelpSearch.y", helpcategory, 0);

	n = 0;
	XtSetArg (args[n], XmNmarginHeight, 10); n++;
	XtSetArg (args[n], XmNmarginWidth, 10); n++;
	XtSetArg (args[n], XmNverticalSpacing, 10); n++;
	f_w = XmCreateForm (hlps_w, "HelpForm", args, n);
	XtManageChild (f_w);


	/* controls at bottom */

	n = 0;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 20); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 40); n++;
	s_w = XmCreatePushButton (f_w, "Search", args, n);
	XtAddCallback (s_w, XmNactivateCallback, hlps_search_cb, 0);
	wtip (s_w, "Click to find string (same as RETURN in search field)");
	XtManageChild (s_w);

	n = 0;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 60); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 80); n++;
	w = XmCreatePushButton (f_w, "Close", args, n);
	XtAddCallback (w, XmNactivateCallback, hlps_close_cb, 0);
	wtip (w, "Dismiss this window");
	XtManageChild (w);

	/* most everything else goes in a RC */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNmarginWidth, 0); n++;
	XtSetArg (args[n], XmNadjustMargin, False); n++;
	rc_w = XmCreateRowColumn (f_w, "HSRC", args, n);
	XtManageChild (rc_w);

	    /* search label and field */

	    n = 0;
	    XtSetArg (args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	    w = XmCreateLabel (rc_w, "S", args, n);
	    set_xmstring (w, XmNlabelString, "Search string:");
	    XtManageChild (w);

	    n = 0;
	    XtSetArg (args[n], XmNcolormap, xe_cm); n++;
	    hlpstf_w = XmCreateTextField (rc_w, "HelpText", args, n);
	    XtAddCallback (hlpstf_w, XmNactivateCallback, hlps_search_cb, 0);
	    wtip (hlpstf_w, "Type string to find, then RETURN or click Search");
	    XtManageChild (hlpstf_w);

	    /* options */

	    n = 0;
	    XtSetArg (args[n], XmNmarginWidth, 10); n++;
	    XtSetArg (args[n], XmNspacing, 5); n++;
	    mc_w = XmCreateToggleButton (rc_w, "MatchCase", args, n);
	    set_xmstring (mc_w, XmNlabelString, "Match case");
	    sr_reg (mc_w, "XEphem*HelpSearch*MatchCase.set", helpcategory, 1);
	    wtip (mc_w, "Whether to ignore case while searching");
	    XtManageChild (mc_w);

	    n = 0;
	    XtSetArg (args[n], XmNmarginWidth, 10); n++;
	    XtSetArg (args[n], XmNspacing, 5); n++;
	    ww_w = XmCreateToggleButton (rc_w, "WholeWordsOnly", args, n);
	    set_xmstring (ww_w, XmNlabelString, "Whole words only");
	    sr_reg (ww_w,"XEphem*HelpSearch*WholeWordsOnly.set",helpcategory,1);
	    wtip (ww_w, "Whether search string must be surrounded by blanks");
	    XtManageChild (ww_w);

	/* scrolled list -- attached such that it grows when user resizes */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, rc_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNbottomWidget, s_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNselectionPolicy, XmSINGLE_SELECT); n++;
	hlpssl_w = XmCreateScrolledList (f_w, "HelpList", args, n);
	XtAddCallback (hlpssl_w, XmNdefaultActionCallback, hlps_sel_cb, 0);
	wtip (hlpssl_w,"Double-click to see help on these matching categories");
	XtManageChild (hlpssl_w);
}

/* callback from either Activate in the search text field or Search PB.
 * N.B. therefore, do not use call.
 */
/* ARGSUSED */
static void
hlps_search_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	int mc = XmToggleButtonGetState (mc_w);
	int ww = XmToggleButtonGetState (ww_w);
	char *wwneedle = NULL;
	char buf[MAXLINE];
	char tag[MAXLINE];
	char *tf, *needle;
	FILE *fp;

	/* get search string */
	needle = tf = XmTextFieldGetString (hlpstf_w);
	needle = strnospace (tf);
	if (strlen(needle) == 0) {
	    xe_msg ("Please enter a search string", 1);
	    XtFree (tf);
	    return;
	}

	/* open help text */
	fp = hlp_open();
	if (!fp) {
	    XtFree (tf);
	    return;	/* already issued xe_msg */
	}

	/* scan for lines with needle, keeping track of current tag */
	XtUnmanageChild (hlpssl_w);	/* quieter */
	XmListDeleteAllItems (hlpssl_w);
	if (!mc)
	    strtolower (needle);
	if (ww) {
	    sprintf (wwneedle = XtMalloc (strlen(needle) + 3), " %s ", needle);
	    needle = wwneedle;
	}
	buf[0] = ' ';			/* in case of whole-word only */
	tag[0] = '\0';			/* just look for tags */
	while (fgets (buf+1, sizeof(buf)-1, fp)) {
	    if (buf[1] == HLP_TAG) {
		buf[strlen(buf)-1] = '\0';
		strcpy(tag, buf+2);	/* skip ' ' and HLP_TAG */
	    } else if (tag[0]) {
		char *bufstart = &buf[1];
		if (ww) {
		    bufstart = buf;
		    strtospace (bufstart);
		}
		if (!mc)
		    strtolower (bufstart);
		if (strstr (bufstart, needle)) {
		    XmString xms = XmStringCreateSimple (tag);
		    insertString (xms);
		    XmStringFree (xms);
		    tag[0] = '\0';	/* skip any more until next tag */
		}
	    }
	}

	/* finished */
	XtManageChild (hlpssl_w);
	(void) fclose (fp);
	XtFree (tf);
	if (wwneedle)
	    XtFree (wwneedle);
}

/* callback when an item in the list is double-clicked */
/* ARGSUSED */
static void
hlps_sel_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	XmListCallbackStruct *lp = (XmListCallbackStruct *)call;
	char *srch = XmTextFieldGetString (hlpstf_w);
	char *tag;

	XmStringGetLtoR (lp->item, XmFONTLIST_DEFAULT_TAG, &tag);
	hlps_dialog (tag, srch);
	XtFree (tag);
	XtFree (srch);
}

/* callback from the Next button */
/* ARGSUSED */
static void
hlps_next_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	nxmpos = (nxmpos+1) % nmpos;
	XmTextShowPosition (hlpt_w, mpos[nxmpos]);
}

/* callback from the search Close button */
/* ARGSUSED */
static void
hlps_close_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	/* let popdown do all the work */
	XtPopdown (hlps_w);
}

/* callback from closing the search shell */
/* ARGSUSED */
static void
hlps_popdown_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	XtUnmanageChild (hlps_w);
}

/* just like hlp_dialog() but highlights each occurance of the search string
 * and builds table of each in nxtpos.
 */
static void
hlps_dialog (tag, srch)
char *tag;	/* tag to look for in help file - also dialog title */
char *srch;	/* string to highlight in text */
{
	int mc = XmToggleButtonGetState (mc_w);
	int ww = XmToggleButtonGetState (ww_w);
	char *wwsrch = NULL;
	char *txt, *ntxt, *sp;
	char title[MAXLINE];
	int srchl;

	/* create main window if first time */
	if (!hlp_w)
	    hlp_create ();

	/* get tag section into hlpt_w */
	if (hlp_fillfromfile(tag, hlpt_w, 0) < 0) {
	    xe_msg ("Help topic disappeared!", 1);
	    return;
	}

	/* prepare for options */
	srch = strnospace (srch);
	srchl = strlen (srch);
	txt = XmTextGetString (hlpt_w);
	if (!mc) {
	    strtolower (txt);
	    strtolower (srch);
	}
	if (ww) {
	    sprintf (wwsrch = XtMalloc (strlen(srch) + 3), " %s ", srch);
	    srch = wwsrch;
	    strtospace (txt);
	}

	/* prepare for new set of matching positions */
	if (mpos) {
	    XtFree ((char *)mpos);
	    mpos = NULL;
	}
	nmpos = 0;
	
	/* scan for each matching position */
	for (ntxt = txt; (sp = strstr (ntxt, srch)) != 0; ntxt = sp+1) {
	    XmTextPosition pos;

	    /* find match position, skip space if whole-word mode */
	    if (ww)
		sp++;
	    pos = sp - txt;

	    /* grow position list */
	    mpos = (XmTextPosition *) XtRealloc ((char *)mpos,
					    (nmpos+1)*sizeof(XmTextPosition));
	    mpos[nmpos++] = pos;
	    XmTextSetHighlight (hlpt_w, pos, pos+srchl, XmHIGHLIGHT_SELECTED);
	}
	XtFree (txt);
	if (wwsrch)
	    XtFree (wwsrch);
	if (nmpos == 0) {
	    xe_msg ("Help entry disappeared!", 1);
	    return;
	}

	/* show first match */
	XmTextShowPosition (hlpt_w, mpos[nxmpos = 0]);
	(void) sprintf (title, "xephem Help on %s", tag);
	XtVaSetValues (hlp_w, XmNtitle, title, NULL);
	if (nmpos > 1)
	    XtManageChild (nxt_w);
	else
	    XtUnmanageChild (nxt_w);
	XtPopup (hlp_w, XtGrabNone);

	/* everything gets destroyed if shell is popped down */
}

/* convert string to all lower cas IN PLACE */
static void
strtolower (str)
char *str;
{
	for (; *str; str++)
	    if (isupper(*str))
		*str = tolower(*str);
}

/* convert all isspace() and ispunct() chars to ' ' IN PLACE
 */
static void
strtospace (str)
char *str;
{
	for (; *str; str++)
	    if (isspace(*str) || ispunct(*str))
		*str = ' ';
}

/* return ptr to first non-space char in str.
 * also trim off all trailing spaces IN PLACE
 */
static char *
strnospace (str)
char *str;
{
	char *firstgood, *lastgood;

	while (isspace(*str))
	    str++;
	firstgood = str;
	for (lastgood = str; *str; str++)
	    if (!isspace(*lastgood))
		lastgood = str;
	if (*lastgood)
	    lastgood[1] = '\0';
	return (firstgood);
}

/* insert xms into hlpssl_w in nice sorted order */
static void
insertString (xms)
XmString xms;
{
	XmStringTable xmt;
	char *cxms, *cxmt;
	int nxmt;
	int i, cmp;

	get_something (hlpssl_w, XmNitems, (XtArgVal)&xmt); /* DO NOT FREE */
	get_something (hlpssl_w, XmNitemCount, (XtArgVal)&nxmt);
	XmStringGetLtoR (xms, XmFONTLIST_DEFAULT_TAG, &cxms);

	for (i = 0; i < nxmt; i++) {
	    XmStringGetLtoR (xmt[i], XmFONTLIST_DEFAULT_TAG, &cxmt);
	    cmp = strcmp (cxms, cxmt);
	    XtFree (cxmt);
	    if (cmp <= 0)
		break;
	}
	XtFree (cxms);

	XmListAddItem (hlpssl_w, xms, i+1);	/* 1-based */
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: helpmenu.c,v $ $Date: 2001/10/13 03:36:24 $ $Revision: 1.10 $ $Name:  $"};
