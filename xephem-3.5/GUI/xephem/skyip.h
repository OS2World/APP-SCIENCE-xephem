/* zoom support */
typedef struct {
    int x0, y0, x1, y1;		/* aoi corners */
    double ad;			/* alt/dec */
    double ar;			/* az/ra */
    double fov;			/* fov */
} ZM_Undo;			/* info about each undo level */

/* For RCS Only -- Do Not Edit
 * @(#) $RCSfile$ $Date$ $Revision$ $Name$
 */
