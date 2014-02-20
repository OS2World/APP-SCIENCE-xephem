/* Find median. Algorithm from N. Wirth's book, implementation by N. Devillard.
 * adapted for XEphem by Elwood Downey
 */

#include "ip.h"

/*---------------------------------------------------------------------------
   Function :   kth_smallest()
   In       :   array of elements, # of elements in the array, rank k
   Out      :   one element
   Job      :   find the kth smallest element in the array

                Reference:

                  Author: Wirth, Niklaus 
                   Title: Algorithms + data structures = programs 
               Publisher: Englewood Cliffs: Prentice-Hall, 1976 
    Physical description: 366 p. 
                  Series: Prentice-Hall Series in Automatic Computation 

 ---------------------------------------------------------------------------*/



static double
dkth_smallest(double a[], int n, int k)
{
    int i,j,l,m ;
    double t, x ;

    l=0 ; m=n-1 ;
    while (l<m) {
        x=a[k] ;
        i=l ;
        j=m ;
        do {
            while (a[i]<x) i++ ;
            while (x<a[j]) j-- ;
            if (i<=j) {
		t=a[i]; a[i]=a[j]; a[j]=t;
                i++ ; j-- ;
            }
        } while (i<=j) ;
        if (j<k) l=i ;
        if (k<i) m=j ;
    }
    return a[k] ;
}


/* find median of n double array.
 * N.B. array is rearranged IN PLACE.
 */
double
dmedian(double a[], int n)
{
	return (dkth_smallest (a, n, n/2));
}


static CamPix
ckth_smallest(CamPix a[], int n, int k)
{
    int i,j,l,m ;
    CamPix t, x ;

    l=0 ; m=n-1 ;
    while (l<m) {
        x=a[k] ;
        i=l ;
        j=m ;
        do {
            while (a[i]<x) i++ ;
            while (x<a[j]) j-- ;
            if (i<=j) {
		t=a[i]; a[i]=a[j]; a[j]=t;
                i++ ; j-- ;
            }
        } while (i<=j) ;
        if (j<k) l=i ;
        if (k<i) m=j ;
    }
    return a[k] ;
}


/* find median of n CamPix array.
 * N.B. array is rearranged IN PLACE.
 */
CamPix
cmedian(CamPix a[], int n)
{
	return (ckth_smallest (a, n, n/2));
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: median.c,v $ $Date: 2001/10/03 08:25:10 $ $Revision: 1.2 $ $Name:  $"};
