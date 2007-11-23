#ifndef _LINUX_PRCTL_H
#define _LINUX_PRCTL_H

/*  Values to pass as first argument to prctl()  */

#define PR_SET_PDEATHSIG  1  /*  Second arg is a signal  */

/* Get/set whether or not to drop capabilities on setuid() away from uid 0 */
#define PR_GET_KEEPCAPS   7
#define PR_SET_KEEPCAPS   8

#endif /* _LINUX_PRCTL_H */
