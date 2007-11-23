/*
 * PowerPC atomic operations
 */

#ifndef _ASM_PPC_ATOMIC_H_ 
#define _ASM_PPC_ATOMIC_H_

#ifdef __SMP__
typedef struct { volatile int counter; } atomic_t;
#else
typedef struct { int counter; } atomic_t;
#endif

#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v,i)		(((v)->counter) = (i))

extern void atomic_add(int a, atomic_t *v);
extern int  atomic_add_return(int a, atomic_t *v);
extern void atomic_sub(int a, atomic_t *v);
extern void atomic_inc(atomic_t *v);
extern int  atomic_inc_return(atomic_t *v);
extern void atomic_dec(atomic_t *v);
extern int  atomic_dec_return(atomic_t *v);
extern int  atomic_dec_and_test(atomic_t *v);

extern void atomic_clear_mask(unsigned long mask, unsigned long *addr);
extern void atomic_set_mask(unsigned long mask, unsigned long *addr);

#endif /* _ASM_PPC_ATOMIC_H_ */
