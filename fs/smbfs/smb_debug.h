/*
 * Defines some debug macros for smbfs.
 */

/*
 * safety checks that should never happen ??? 
 * these are normally enabled.
 */
#ifdef SMBFS_PARANOIA
#define PARANOIA(x...) printk(KERN_NOTICE ## x);
#else
#define PARANOIA(x...) do { ; } while(0)
#endif

/* lots of debug messages */
#ifdef SMBFS_DEBUG_VERBOSE
#define VERBOSE(x...) printk(KERN_DEBUG ## x);
#else
#define VERBOSE(x...) do { ; } while(0)
#endif

/*
 * "normal" debug messages, but not with a normal DEBUG define ... way
 * too common name.
 */
#ifdef SMBFS_DEBUG
#define DEBUG1(x...) printk(KERN_DEBUG ## x);
#else
#define DEBUG1(x...) do { ; } while(0)
#endif
