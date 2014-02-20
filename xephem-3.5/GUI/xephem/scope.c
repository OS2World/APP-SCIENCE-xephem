/* stuff for telescope control
 * the real work is done in an auxilary process.
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>


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

extern Widget toplevel_w;
extern Colormap xe_cm;
extern Widget svshell_w;
extern XtAppContext xe_app;

extern Now *mm_get_now P_((void));
extern char *expand_home P_((char *path));
extern char *getPrivateDir P_((void));
extern char *getShareDir P_((void));
extern char *getXRes P_((char *name, char *def));
extern char *syserrstr P_((void));
extern int sv_ison P_((void));
extern void XCheck P_((XtAppContext app));
extern void defaultTextFN P_((Widget w, int setcols, char *x, char *y));
extern void get_xmstring P_((Widget w, char *resource, char **txtp));
extern void hlp_dialog P_((char *tag, char *deflt[], int ndeflt));
extern void prompt_map_cb P_((Widget w, XtPointer client, XtPointer call));
extern void set_something P_((Widget w, char *resource, XtArgVal value));
extern void set_xmstring P_((Widget w, char *resource, char *txtp));
extern void sr_reg P_((Widget w, char *res, char *cat, int autosav));
extern void sv_all P_((Now *np));
extern void sv_scopeMark P_((Obj *));
extern void wtip P_((Widget w, char *tip));
extern void xe_msg P_((char *msg, int show));

static int fifosOk P_((void));
static int fifosUp P_((void));
static int getPid P_((int verbose));
static int startRunning P_((void));
static void fifosDown P_((void));
static void sc_create_w P_((void));
static void sc_help_cb P_((Widget w, XtPointer client, XtPointer call));
static void sc_close_cb P_((Widget w, XtPointer client, XtPointer call));
static void sc_marker_cb P_((Widget w, XtPointer client, XtPointer call));
static void sc_running_cb P_((Widget w, XtPointer client, XtPointer call));
static void stopRunning P_((int verbose));
static void getToFifo P_((char path[]));
static void getInFifo P_((char path[]));
static void getPidFile P_((char proc[], char path[]));
static void getLogFile P_((char proc[], char path[]));
static void infifo_cb P_((XtPointer client, int *fdp, XtInputId *idp));

static Widget scope_w;		/* overall eyepiece dialog */
static Widget tty_w;		/* TF for path to serial device */
static Widget daemon_w;		/* TF for name of daemon to run */
static Widget marker_w;		/* TB for enabling marker */
static Widget goto_w;		/* TB for enabling goto commands */
static Widget nohw_w;		/* TB for enabling no-hardware test mode */
static Widget running_w;	/* TB for starting/stopping the daemon */

static char tdfifo[] = "xephem_loc_fifo";	/* fifo to daemon */
static char fdfifo[] = "xephem_in_fifo";	/* fifo to xephem */
static char fifodir[] = "fifos";		/* dir under Shared for these */
#define	FIFOMODE	0666			/* fifo creation mode */

static XtInputId infifo_iid;	/* infifo monitor */
static int infifo_fd = -1;	/* infifo fd when >= 0 */
static int gotofifo_fd = -1;	/* goto fifo fd when >= 0 */

static char scopecategory[] = "Telescope Configuration";

void 
sc_manage()
{
	if (!scope_w)
	    sc_create_w();
	XtManageChild (scope_w);
}

void 
sc_unmanage()
{
	if (scope_w)
	    XtUnmanageChild (scope_w);
}

/* control whether input from the process generates a sky view marker */
void
sc_marker(on)
int on;
{
	if (!scope_w)
	    sc_create_w();
	XmToggleButtonSetState (marker_w, on, True);
}

/* return whether we are sending goto commands */
int
sc_isGotoOn()
{
	if (!scope_w)
	    sc_create_w();
	return (gotofifo_fd >= 0 && XmToggleButtonGetState(goto_w));
}

/* send the given object to the loc_fifo if on and alt>0.
 */
void
sc_goto (op)
Obj *op;
{
	char msg[1024];
	char fn[1024];

	if (!scope_w)
	    sc_create_w();

	/* check whether enabled */
	if (gotofifo_fd < 0 || !XmToggleButtonGetState (goto_w))
	    return;

	/* assert */
	if (!op) {
	    printf ("sc_goto called with on op\n");
	    exit(1);
	}

	/* check whether above horizon */
	if (op->s_alt < 0) {
	    sprintf (msg, "%s is below the horizon", op->o_name);
	    xe_msg (msg, 1);
	    return;
	}

	/* send the formatted location string */
	db_write_line (op, msg);
	if (write (gotofifo_fd, msg, strlen(msg)) < 0) {
	    getToFifo (fn);
	    sprintf (msg, "%s:\n%s\nTurning system off", fn, syserrstr());
	    xe_msg (msg, 1);
	    XmToggleButtonSetState (running_w, False, True);
	}
}

/* called to put up or remove the watch cursor.  */
void
sc_cursor (c)
Cursor c;
{
	Window win;

	if (scope_w && (win = XtWindow(scope_w)) != 0) {
	    Display *dsp = XtDisplay(scope_w);
	    if (c)
		XDefineCursor (dsp, win, c);
	    else
		XUndefineCursor (dsp, win);
	}
}

/* create the main scope dialog.
 * init Running TB according to whether process is already running.
 */
static void
sc_create_w()
{
	Widget l_w, sep_w;
	Widget w;
	Arg args[20];
	int n;

	/* create form */

	n = 0;
	XtSetArg(args[n], XmNautoUnmanage, False); n++;
	XtSetArg (args[n], XmNcolormap, xe_cm); n++;
	XtSetArg (args[n], XmNverticalSpacing, 5); n++;
	XtSetArg (args[n], XmNmarginHeight, 10); n++;
	XtSetArg (args[n], XmNmarginWidth, 10); n++;
	scope_w = XmCreateFormDialog (svshell_w, "Telescope", args, n);
	set_something (scope_w, XmNcolormap, (XtArgVal)xe_cm);
	XtAddCallback (scope_w, XmNhelpCallback, sc_help_cb, 0);

	/* set some stuff in the parent DialogShell.
	 * setting XmNdialogTitle in the Form didn't work..
	 */
	n = 0;
	XtSetArg (args[n], XmNtitle, "xephem Telescope configuration"); n++;
	XtSetValues (XtParent(scope_w), args, n);

	/* daemon name */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	l_w = XmCreateLabel (scope_w, "SL", args, n);
	set_xmstring (l_w, XmNlabelString, "Control process:");
	XtManageChild (l_w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNleftWidget, l_w); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNcolumns, 25); n++;
	daemon_w = XmCreateTextField (scope_w, "Process", args, n);
	wtip (daemon_w, "Name of control process to operate telescope");
	sr_reg (daemon_w, NULL, scopecategory, 1);
	XtManageChild (daemon_w);

	/* tty name */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, daemon_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	w = XmCreateLabel (scope_w, "SL", args, n);
	set_xmstring (w, XmNlabelString, "Serial device:");
	XtManageChild (w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, daemon_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNleftWidget, l_w); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNcolumns, 25); n++;
	tty_w = XmCreateTextField (scope_w, "Serial", args, n);
	wtip(tty_w,"Operating system name of Serial device connected to scope");
	sr_reg (tty_w, NULL, scopecategory, 1);
	XtManageChild (tty_w);

	/* TBs */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, tty_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	marker_w = XmCreateToggleButton (scope_w, "Marker", args, n);
	XtAddCallback (marker_w, XmNvalueChangedCallback, sc_marker_cb, NULL);
	wtip (marker_w, "Whether to display scope location on Sky View");
	set_xmstring (marker_w, XmNlabelString, "Show Sky View marker");
	sr_reg (marker_w, NULL, scopecategory, 1);
	XtManageChild (marker_w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, marker_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	goto_w = XmCreateToggleButton (scope_w, "Goto", args, n);
	wtip (goto_w, "Whether to enable commands to position scope position");
	set_xmstring (goto_w, XmNlabelString, "Enable Telescope GoTo");
	sr_reg (goto_w, NULL, scopecategory, 1);
	XtManageChild (goto_w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, goto_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	nohw_w = XmCreateToggleButton (scope_w, "NoHW", args, n);
	set_xmstring (nohw_w, XmNlabelString,
			    "Use no-hardware test mode when next restarted");
	wtip(nohw_w,"Next time process is started it will echo back each GoTo");
	sr_reg (nohw_w, NULL, scopecategory, 1);
	XtManageChild (nohw_w);

	/* sep */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, nohw_w); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	sep_w = XmCreateSeparator (scope_w, "TSEp", args, n);
	XtManageChild (sep_w);

	/* bottom controls */

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, sep_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 10); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 30); n++;
	running_w = XmCreateToggleButton (scope_w, "Running", args, n);
	XtAddCallback (running_w, XmNvalueChangedCallback, sc_running_cb, NULL);
	wtip (running_w, "Start/stop daemon control process.");
	XtManageChild (running_w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, sep_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 40); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 60); n++;
	w = XmCreatePushButton (scope_w, "Close", args, n);
	XtAddCallback (w, XmNactivateCallback, sc_close_cb, NULL);
	XtManageChild (w);

	n = 0;
	XtSetArg (args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg (args[n], XmNtopWidget, sep_w); n++;
	XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNleftPosition, 70); n++;
	XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg (args[n], XmNrightPosition, 90); n++;
	w = XmCreatePushButton (scope_w, "Help", args, n);
	XtAddCallback (w, XmNactivateCallback, sc_help_cb, NULL);
	XtManageChild (w);

	/* if running already, get up to speed */
	if (getPid(0) > 0) {
	    XmToggleButtonSetState (running_w, 1, False);
	    if (fifosUp() < 0)
		stopRunning(1);
	}
}

/* callback from the close PB.
 */
/* ARGSUSED */
static void
sc_close_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	XtUnmanageChild (scope_w);
}

/* called when the help button is hit */
/* ARGSUSED */
static void
sc_help_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	static char *msg[] = {
	    "Control telescope."
	};

	hlp_dialog ("Telescope", msg, sizeof(msg)/sizeof(msg[0]));

}

/* called when marker TB is changed */
/* ARGSUSED */
static void
sc_marker_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	if (!XmToggleButtonGetState(w))
	    sv_all(NULL);		/* erase */
}

/* TB to Start or Stop the daemon process */
/* ARGSUSED */
static void
sc_running_cb (w, client, call)
Widget w;
XtPointer client;
XtPointer call;
{
	int wanton = XmToggleButtonGetState(w);

	/* always stop, then maybe restart */
	stopRunning(!wanton);
	fifosDown();

	if (XmToggleButtonGetState(marker_w))
	    sv_all(NULL);	/* erase */

	if (wanton)
	    if (fifosOk() < 0 || startRunning() < 0)
		XmToggleButtonSetState(w, False, False);
}

/* start the daemon and associated fifo connections.
 * N.B. we assume we know as best we can it is not already running.
 * return 0 if ok, else -1 and xe_msg
 */
static int
startRunning()
{
	char *proc = XmTextFieldGetString (daemon_w);
	char logfn[256], buf[256], msg[256];
	int logfd, pidfd;
	int pid;

	/* create the lock and log files */
	getLogFile (proc, logfn);
	logfd = open (logfn, O_WRONLY|O_CREAT, 0666);
	if (logfd < 0) {
	    sprintf (msg, "%s log file: %s", logfn, syserrstr());
	    xe_msg(msg,1);
	    XtFree(proc);
	    return (-1);
	}
	getPidFile (proc, buf);
	pidfd = open (buf, O_WRONLY|O_CREAT, 0666);
	if (pidfd < 0) {
	    sprintf (msg, "%s lock file: %s", buf, syserrstr());
	    xe_msg(msg,1);
	    XtFree(proc);
	    close (logfd);
	    return (-1);
	}

	/* create process -- no zombies */
	signal (SIGCHLD, SIG_IGN);
	pid = fork();
	if (pid < 0) {
	    /* trouble */
	    sprintf (msg, "Spawning %s: %s", proc, syserrstr());
	    xe_msg (msg, 1);
	    XtFree(proc);
	    return (-1);
	} else if (pid == 0) {
	    /* child */
	    char tofifo[256], frfifo[256];
	    char *tty = XmTextFieldGetString (tty_w);
	    int nohw = XmToggleButtonGetState (nohw_w);
	    char *args[20], **av = args;
	    int fd;
	    
	    /* do some basic isolation */
	    setsid();
	    dup2 (open ("/dev/null", O_RDONLY), 0);
	    dup2 (logfd, 1);
	    dup2 (logfd, 2);
	    for (fd = 3; fd < 100; fd++)
		close(fd);

	    /* build command */
	    getToFifo (tofifo);
	    getInFifo (frfifo);
	    *av++ = proc;
	    *av++ = "-m";
	    *av++ = frfifo;
	    *av++ = "-g";
	    *av++ = tofifo;

	    /* emulate or use real hardware */
	    if (nohw) {
		*av++ = "-e";
	    } else {
		*av++ = "-t";
		*av++ = tty;
	    }

	    /* end arg list */
	    *av++ = NULL;

	    /* echo args to log */
	    for (av = args; *av; av++)
		printf ("%s ", *av);
	    printf ("\n");

	    /* go */
	    execvp (proc, args);
	    exit(1);
	}

	/* parent continues */

	/* log not needed here */
	close (logfd);

	/* save pid */
	write (pidfd, buf, sprintf (buf, "%d\n", pid));
	close (pidfd);

	/* wait a sec and check for blatant failure */
	sleep (1);
	if (waitpid (pid, NULL, WNOHANG) != 0) {
	    /* 0 means ok because it means return was due to NOHANG.
	     * because we ignore sigchld, we never get a useable
	     *   pid+wstatus back.
	     */
	    sprintf (msg, "%s: unexpected exit.\nCheck %s", proc, logfn);
	    xe_msg (msg, 1);
	    XtFree (proc);
	    return (-1);
	} else {
	    /* return is because of NOHANG .. that's what we want */
	    sprintf (msg, "%s: Running", proc);
	    XtFree (proc);
	    xe_msg (msg, 1);
	}

	/* connect our fifo ends -- must work or else */
	if (fifosUp() < 0) {
	    stopRunning(1);
	    return (-1);
	}

	/* whew! */
	return (0);
}

/* stop the daemon */
static void
stopRunning(verbose)
int verbose;
{
	char *proc = XmTextFieldGetString (daemon_w);
	char msg[1024];
	int pid;

	/* get pid */
	pid = getPid (verbose);
	if (pid < 0)
	    return;

	/* send TERM signal */
	if (kill (pid, SIGTERM) < 0) {
	    sprintf (msg, "Could not stop %s:\n%s", proc, syserrstr());
	    xe_msg(msg, 1);
	    XtFree(proc);
	    return;
	}

	sprintf (msg, "%s is now stopped", proc);
	xe_msg (msg, 1);
	XtFree(proc);
}

/* if process is running now, return pid, else remove pid file and retunn -1.
 * also generate xe_msg if verbose.
 */
static int
getPid(verbose)
int verbose;
{
	char *proc = XmTextFieldGetString (daemon_w);
	char pidfn[256], buf[32], msg[256];
	FILE *fp;
	int pid;

	/* open file */
	getPidFile (proc, pidfn);
	fp = fopen (pidfn, "r");
	if (!fp) {
	    if (verbose) {
		sprintf (msg, "Lock file %s:\n%s.\n%s presumed not running.",
						pidfn, syserrstr(), proc);
		xe_msg (msg, 1);
	    }
	    XtFree(proc);
	    remove (pidfn);
	    return (-1);
	}
	if (!fgets (buf, sizeof(buf), fp)) {
	    if (verbose) {
		sprintf (msg, "Lock file %s:\n%s.\n%s presumed not running",
				pidfn, feof(fp) ? "Empty" : syserrstr(), proc);
		xe_msg (msg, 1);
	    }
	    fclose (fp);
	    XtFree(proc);
	    remove (pidfn);
	    return(-1);
	}
	fclose (fp);

	/* dig out pid */
	pid = atoi (buf);
	if (pid <= 1) {
	    if (verbose) {
		sprintf (msg, "Lock file %s:\nContains preposterous PID: %d.\n%s presumed not running", pidfn, pid, proc);
		xe_msg(msg,1);
	    }
	    XtFree(proc);
	    remove (pidfn);
	    return (-1);
	}

	/* see whether really alive */
	if (kill (pid, 0) < 0) {
	    if (verbose) {
		sprintf (msg, "%s: not running", proc);
		xe_msg (msg, 1);
	    }
	    XtFree(proc);
	    remove (pidfn);
	    return (-1);
	}

	/* yes, really running */
	return (pid);
}

/* make sure the two fifos exist.
 * return 0 if ok, else -1.
 */
static int
fifosOk()
{
	char fpath[1024];
	char msg[1024];
	int fd;

	/* fifo towards daemon */
	getToFifo (fpath);
	fd = open (fpath, O_RDWR|O_NONBLOCK);
	if (fd < 0 && errno == ENOENT && mkfifo (fpath, FIFOMODE) < 0) {
	    /* can't open and can't create */
	    sprintf (msg, "Creating %s:\n%s", fpath, syserrstr());
	    xe_msg (msg, 1);
	    return (-1);
	}
	close (fd);

	/* now try again */
	fd = open (fpath, O_RDWR|O_NONBLOCK);
	if (fd < 0) {
	    sprintf (msg, "%s:\n%s", fpath, syserrstr());
	    xe_msg (msg, 1);
	    return (-1);
	}
	close (fd);

	/* fifo back to us */
	getInFifo (fpath);
	fd = open (fpath, O_RDWR|O_NONBLOCK);
	if (fd < 0 && errno == ENOENT && mkfifo (fpath, FIFOMODE) < 0) {
	    /* can't open and can't create */
	    sprintf (msg, "Creating %s:\n%s", fpath, syserrstr());
	    xe_msg (msg, 1);
	    return (-1);
	}
	close (fd);

	/* now try again */
	fd = open (fpath, O_RDWR|O_NONBLOCK);
	if (fd < 0) {
	    sprintf (msg, "%s:\n%s", fpath, syserrstr());
	    xe_msg (msg, 1);
	    return (-1);
	}
	close (fd);

	/* ok */
	return (0);
}

/* connect fifos and start our local processing.
 * this assumes the daemon is already running.
 * return 0 if ok, else -1 and xe_msg
 */
static int
fifosUp()
{
	char fpath[1024];
	char msg[1024];

	/* make sure they exist but are closed */
	if (fifosOk() < 0)
	    return (-1);
	fifosDown();

	/* output fifo */
	getToFifo (fpath);
	gotofifo_fd = open (fpath, O_WRONLY|O_NONBLOCK);
	if (gotofifo_fd < 0) {
	    sprintf (msg, "Opening %s:\n%s", fpath, syserrstr());
	    return (-1);
	}

	/* input fifo */
	getInFifo (fpath);
	infifo_fd = open (fpath, O_RDONLY|O_NONBLOCK);
	if (infifo_fd < 0) {
	    sprintf (msg, "Opening %s:\n%s", fpath, syserrstr());
	    return (-1);
	}

	/* connect input monitor */
	infifo_iid = XtAppAddInput (xe_app, infifo_fd,
				(XtPointer)XtInputReadMask, infifo_cb, NULL);

	/* ok */
	return (0);
}

/* close fifos and stop monitoring */
static void
fifosDown()
{
	/* close output */
	if (gotofifo_fd >= 0) {
	    close (gotofifo_fd);
	    gotofifo_fd = -1;
	}

	/* close input and stop monitoring */
	if (infifo_fd >= 0) {
	    close (infifo_fd);
	    infifo_fd = -1;
	}
	if (infifo_iid) {
	    XtRemoveInput (infifo_iid);
	    infifo_iid = 0;
	}
}

/* get pathname of fifo sending messages to deamon */
static void
getToFifo (path)
char path[];
{
	sprintf (path, "%s/%s/%s", getShareDir(), fifodir, tdfifo);
}

/* get pathname of fifo receiving messages from deamon */
static void
getInFifo (path)
char path[];
{
	sprintf (path, "%s/%s/%s", getShareDir(), fifodir, fdfifo);
}

/* get path to daemon pid/lock file */
static void
getPidFile (proc, path)
char *proc, *path;
{
	sprintf (path, "%s/%s.pid", getPrivateDir(), proc);
}

/* get path to daemon log file */
static void
getLogFile (proc, path)
char *proc, *path;
{
	sprintf (path, "%s/%s.log", getPrivateDir(), proc);
}

/* called whenever there is input waiting from the xephem_in_fifo.
 * we gulp a lot and use last whole line we see that looks reasonable.
 * N.B. do EXACTLY ONE read -- don't know that more won't block.
 */
/* ARGSUSED */
static void
infifo_cb (client, fdp, idp)
XtPointer client;       /* file name */
int *fdp;               /* pointer to file descriptor */
XtInputId *idp;         /* pointer to input id */
{
	static char fmt[] = "RA:%lf Dec:%lf Epoch:%lf";
	double ra, dec, y;
	char buf[1025];
	char msg[1024];
	char *bp;
	int nr;

	/* one read -- with room for EOS later */
	nr = read (infifo_fd, buf, sizeof(buf)-1);
	if (nr <= 0) {
	    getInFifo(buf);
	    (void) sprintf (msg, "%s: %s\nStopping process", buf, 
				nr == 0 ? "Unexpected EOF" : syserrstr());
	    xe_msg (msg, 1);
	    XmToggleButtonSetState (running_w, False, True);
	    return;
	}

	/* beware locking up if getting flooded.
	 * N.B. can change enmarker_w
	 */
	XCheck(xe_app);

	/* if not up or not wanted, oh well, suffice to have done the read */
	if (!XmToggleButtonGetState(marker_w) || !sv_ison())
	    return;

	/* look backwards for newest good line */
	buf[nr] = '\0';
	for (bp = &buf[nr]; bp >= buf; --bp) {
	    if (*bp == 'R') {
		/* worth a careful look */
		int ns = sscanf (bp, fmt, &ra, &dec, &y);
		if (ns>=2 && ra>=0 && ra<2*PI && dec<=PI/2 && dec>=-PI/2) {
		    /* ok, and let 'em get away without a year */
		    if (ns == 2)
			y = 2000.0;
		    break;
		}
	    }
	}

	/* display if found one */
	if (bp >= buf) {
	    Now *np = mm_get_now();
	    double m;
	    Obj obj;

	    /* mark the scope location -- be simple since likely more soon */
	    zero_mem ((void *)&obj, sizeof(obj));
	    (void) strcpy (obj.o_name, "ScopeMark");
	    obj.o_type = FIXED;
	    obj.f_RA = (float)ra;
	    obj.f_dec = (float)dec;
	    year_mjd (y, &m);
	    obj.f_epoch = (float)m;
	    if (obj_cir (np, &obj) == 0)
		sv_scopeMark (&obj);
	} else {
	    (void) sprintf (msg, "Bogus infifo cmd:\n  %s", buf);
	    xe_msg (msg, 0);
	}
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: scope.c,v $ $Date: 2001/10/10 05:10:04 $ $Revision: 1.2 $ $Name:  $"};
