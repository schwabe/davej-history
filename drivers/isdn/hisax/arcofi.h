/* $Id: arcofi.h,v 1.1.2.3 1998/05/27 18:04:50 keil Exp $

 * arcofi.h   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: arcofi.h,v $
 * Revision 1.1.2.3  1998/05/27 18:04:50  keil
 * HiSax 3.0
 *
 * Revision 1.1.2.2  1998/04/11 18:45:14  keil
 * New interface
 *
 * Revision 1.1.2.1  1997/11/15 18:57:38  keil
 * ARCOFI 2165 support
 *
 *
 */
 
#define ARCOFI_USE	1

extern int send_arcofi(struct IsdnCardState *cs, const u_char *msg, int bc, int receive);
