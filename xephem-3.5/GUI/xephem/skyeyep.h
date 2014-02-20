/* include file to hook skyviewmenu.c and skyeyep.c together.
 */

typedef struct {
    double ra, dec, alt, az;	/* location when created */
    double eyepw, eyeph;	/* width and height, rads */
    int round;			/* true if want round, false if square */
    int solid;			/* true if want solid, else just border */
} EyePiece;

/* skyeyep.c */
extern void se_manage P_((void));
extern void se_unmanage P_((void));
extern void se_add P_((double ra, double dec, double alt, double az));
extern void se_del P_((double azra, double altdec, int isradec));
extern int se_getlist P_((EyePiece **ep));
extern int se_isOneHere P_((double azra, double altdeci, int isradec));

/* skyviewmenu.c */
extern Widget svshell_w;
extern void sv_all P_((Now *np));

/* For RCS Only -- Do Not Edit
 * @(#) $RCSfile: skyeyep.h,v $ $Date: 2001/10/06 21:09:35 $ $Revision: 1.3 $ $Name:  $
 */
