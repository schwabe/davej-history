/* spinlock.h -- dummy version for PARIDE-2.0.34 */

#define spin_lock_irqsave(a,b) 		{ save_flags(b); cli(); }
#define spin_unlock_irqrestore(a,b) 	restore_flags(b);


