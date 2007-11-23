/*
 *      Read and write Memory Type Range Registers (MTRRs)
 *
 *      These machine specific registers contain information about
 *      caching of memory regions on Intel processors.
 *
 *      This code has been derived from pform_mod.c by M. Tisch"auser
 *      (email martin@ikcbarka.fzk.de). Special thanks to mingo for
 *      his hint.
 *
 *      (c) 1997 M. Ohlenroth <moh@informatik.tu-chemnitz.de>
 *      NO WARRANTY: use this code at your own risk!
 *
 *	This code is released under the GNU public license version 2 or
 *	later.
 *
 *      modified to have a /proc/mtrr interface by M. Fr"ohlich, Jan. 1998
 *      <frohlich@na.uni-tuebingen.de>
 *         the user Interface is partly taken form mtrr-patch-v1.5
 *      Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
 *        The postal address is:
 *      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.
 *
 */

#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/proc_fs.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/mtrr.h>

#define MTRR_CAP	0x0fe	/* MTRRcap register */
#define MTRR_VARIABLE	0x200	/* variable length registers */
#define MTRR_FIXED64K	0x250	/* fixed size registers 64k */
#define MTRR_FIXED16K	0x258	/* fixed size registers 16K */
#define	MTRR_FIXED4K	0x268	/* fixed size registers 4K */
#define MTRR_DEFTYPE	0x2ff	/* MTRRdefType register */

/*
 * data type for the MTRRcap register
 */
typedef struct {
	__u64   VCNT          :  8  __attribute__ ((packed)),
		FIX           :  1  __attribute__ ((packed)),
		__reserved_2  :  1  __attribute__ ((packed)),
		WC            :  1  __attribute__ ((packed)),
		__reserved_1  :  53 __attribute__ ((packed));
} MTRRcap_t __attribute__ ((packed));


/*
 * data type for the MTRRdefType register
 */
typedef struct {
	__u64   Type          :  8  __attribute__ ((packed)),
		__reserved_1  :  2  __attribute__ ((packed)),
		FE            :  1  __attribute__ ((packed)),
		E             :  1  __attribute__ ((packed)),
		__reserved_2  :  52 __attribute__ ((packed));
} MTRRdefType_t __attribute__ ((packed));

/* FIXME implement the entry struct */
typedef struct MTRRfix64K_t {
	__u64   raw;
} MTRRfix64K_t __attribute__ ((packed));

typedef struct MTRRfix16K_t {
	__u64   raw;
} MTRRfix16K_t __attribute__ ((packed));

typedef struct MTRRfix4K_t {
	__u64   raw;
} MTRRfix4K_t __attribute__ ((packed));

/*
 * data type for a pair of variable MTRR registers
 */
typedef struct {
	struct {
		__u64   Type          :  8  __attribute__ ((packed)),
			__reserved_1  :  4  __attribute__ ((packed)),
			PhysBase      :  24 __attribute__ ((packed)),
			__reserved_2  :  28 __attribute__ ((packed));
	} MTRRphysBase  __attribute__ ((packed));

	struct {
		__u64   __reserved_3  :  11 __attribute__ ((packed)),
			V             :  1  __attribute__ ((packed)),
			PhysMask      :  24 __attribute__ ((packed)),
			__reserved_4  :  28 __attribute__ ((packed));
	} MTRRphysMask __attribute__ ((packed));
} MTRRvar_t __attribute__ ((packed));

#define RAW_ACCESS64(data) (*(unsigned long long *)(&(data)))

/*
 * MTRR configuration struct
 */
struct mtrr_cntl_t {
	MTRRcap_t      MTRRcap;     /* MTRR capability register */
	MTRRdefType_t  MTRRdefType; /* MTRR default type register */
	MTRRfix64K_t   fixed64;     /* fixed length entries (raw data) */
	MTRRfix16K_t   fixed16[2];
	MTRRfix4K_t    fixed4[8];
	MTRRvar_t      variable[0]; /* variable type entries */
};

static struct mtrr_cntl_t *mtrrcntl      = NULL;

/*
 * Deletes the variable MTRR *MTRRvar
 */
static inline void MTRRvar_delete(MTRRvar_t *MTRRvar)
{
	RAW_ACCESS64(MTRRvar->MTRRphysBase) = 0;
	RAW_ACCESS64(MTRRvar->MTRRphysMask) = 0;
}


/*
 * Sets the variable MTRR *MTRRvar
 */
static inline void MTRRvar_set(MTRRvar_t *MTRRvar, unsigned int type,
			       unsigned long base, unsigned long size)
{
	unsigned long val;
	base >>= 12;
	size >>= 12;
	
	MTRRvar->MTRRphysBase.Type = type;
	MTRRvar->MTRRphysBase.PhysBase = base;
	
	MTRRvar->MTRRphysMask.V = 1;
	val = 1<<25;
	while (0 == (val & size)) val |= (val>>1);
	MTRRvar->MTRRphysMask.PhysMask = val;
}


/*
 * returns 1 if the variable MTRR entry *MTRRvar is valid, 0 otherwise
 */
static inline int MTRRvar_is_valid(const MTRRvar_t *MTRRvar)
{
	return MTRRvar->MTRRphysMask.V;
}

/*
 * returns the type of the variable MTRR entry *MTRRvar
 */
static inline int MTRRvar_get_type(const MTRRvar_t *MTRRvar)
{
	return MTRRvar->MTRRphysBase.Type;
}

/*
 * returns the base of the variable MTRR entry *MTRRvar
 */
static inline unsigned long long MTRRvar_get_base(const MTRRvar_t *MTRRvar)
{
	return ((unsigned long long)MTRRvar->MTRRphysBase.PhysBase) << 12;
}

/*
 * returns the size of the variable MTRR entry *MTRRvar
 */
static inline unsigned long long MTRRvar_get_size(const MTRRvar_t *MTRRvar)
{
	if (MTRRvar->MTRRphysMask.PhysMask == 0) {
		return 0;
	} else {
		unsigned long size = 1;
		const unsigned long Mask = MTRRvar->MTRRphysMask.PhysMask;
		while (0 == (Mask & size)) size <<= 1;
		return ((unsigned long long)size) << 12;
	}
}

/*
 *  returns the eflags register
 */
static inline int read_eflags(void)
{
	int ret;
	asm volatile (
		"pushfl\n\t"
		"popl %%eax\n\t"
		:"=a" (ret)
		:
		);
	return ret;
}

/*
 *  writes the eflags register
 */
static inline void write_eflags(int flag)
{
	asm volatile (
		"pushl %%eax\n\t"
		"popfl\n\t"
		:
		:"a" (flag)
		);
}

/*
 * returns 1 if the mtrr's are supported by the current processor, 0 otherwise
 */
static inline int mtrr_detect(void) {
	unsigned long flags;
	int eflags;
	int val;
	
#define MSR_MASK 0x20
#define MTRR_MASK 0x1000
#define CPUID_MASK 0x200000
	/* this function may be called before the cpu_data array has
		been initialized */
	save_flags(flags); sti();
	eflags = read_eflags();
	write_eflags(eflags ^ CPUID_MASK);
	if (!((eflags ^ read_eflags()) & CPUID_MASK)) {
		write_eflags(eflags);
		restore_flags(flags);
		return 0;
	}
	write_eflags(eflags);
	restore_flags(flags);

	/* get the cpuid level */
	asm volatile (
		"xorl %%eax,%%eax\n\t"
		"cpuid"
		:"=a"(val)::"ebx","ecx","edx"
	);
	if (val < 1) return 0;
	/* get the x86_capability value */
	asm volatile (
		"movl $1,%%eax\n\t"
		"cpuid"
		:"=d"(val)::"ebx","ecx","eax"
	);
	if (!(val & MSR_MASK)) return 0;
	if (!(val & MTRR_MASK)) return 0;

	return 1;
#undef MSR_MASK
#undef MTRR_MASK
#undef CPUID_MASK
}


/*
 * reads the mtrr configuration of the actual processor and returns
 * this configuration on sucess. returns NULL if an error occured or 
 * if mtrr's are not supported.
 */
static struct mtrr_cntl_t *read_mtrr_configuration (void) {
	struct mtrr_cntl_t *mtrrcntl;
	int i;
	size_t size;
	MTRRcap_t MTRRcap;

	if (!mtrr_detect()) {
		printk("/proc/mtrr: MTRR's are NOT supported\n");
		return NULL;
	}

	RAW_ACCESS64(MTRRcap) = rdmsr(MTRR_CAP);

/* #define DUMP_MTRR */
#ifdef DUMP_MTRR
	{
		/* Written for a bugreport to Gigabyte ... */
		inline void print_msr(int num) {
			unsigned long long tmp = rdmsr(num);
			printk("MSR #%#06x: 0x%08lx%08lx\n",
			       num , (unsigned long)(tmp >> 32),
			       (unsigned long)tmp);
		}
		
		print_msr(MTRR_CAP);
		/* all variable type */
		for (i=0;i < MTRRcap.VCNT;i++) {
			print_msr(MTRR_VARIABLE+2*i);
			print_msr(MTRR_VARIABLE+2*i+1);
		}
		/* all fixed type */
		print_msr(MTRR_FIXED64K);
		print_msr(MTRR_FIXED16K);
		print_msr(MTRR_FIXED16K+1);
		for (i=0;i<8;i++)
			print_msr(MTRR_FIXED4K+i);
		print_msr(MTRR_DEFTYPE);
	}
#endif
	
	size = sizeof(struct mtrr_cntl_t) + sizeof(MTRRvar_t)*MTRRcap.VCNT;
	if (NULL == (mtrrcntl = kmalloc(size, GFP_KERNEL))) return NULL;
	memset(mtrrcntl, 0, size);

	/* read MTRRcap register */
	mtrrcntl->MTRRcap = MTRRcap;

	/* read MTRRdefType register */
	RAW_ACCESS64(mtrrcntl->MTRRdefType) = rdmsr(MTRR_DEFTYPE);

	/* read fixed length entries */
	if (mtrrcntl->MTRRdefType.E && mtrrcntl->MTRRdefType.FE) {
		mtrrcntl->fixed64.raw = rdmsr(MTRR_FIXED64K);
		mtrrcntl->fixed16[0].raw = rdmsr(MTRR_FIXED16K);
		mtrrcntl->fixed16[1].raw = rdmsr(MTRR_FIXED16K+1);
		for (i=0;i<8;i++)
			mtrrcntl->fixed4[i].raw = rdmsr(MTRR_FIXED4K+i);
	}

	/* read variable length entries */
	if (mtrrcntl->MTRRdefType.E) {
		const int vcnt = mtrrcntl->MTRRcap.VCNT;
		for (i = 0 ; i < vcnt ; i++) {
			RAW_ACCESS64(mtrrcntl->variable[i].MTRRphysBase) =
				rdmsr(MTRR_VARIABLE + 2*i);
			RAW_ACCESS64(mtrrcntl->variable[i].MTRRphysMask) =
				rdmsr(MTRR_VARIABLE + 2*i + 1);
		}
	}

	return mtrrcntl;
}

/*
 * initializes the global mtrr configuration
 */
/*__init_function(void init_mtrr_config(void)) FIXME*/
void init_mtrr_config(void)
{
	mtrrcntl = read_mtrr_configuration();
}


/* write back and invalidate cache */
static inline void wbinvd(void)
{
	asm volatile("wbinvd");
}

/* flush tlb's */
static inline void flush__tlb(void)
{
	asm volatile (
		"movl  %%cr3, %%eax\n\t"
		"movl  %%eax, %%cr3\n\t"
		:
		:
		: "memory", "eax");
}

/* clear page global enable and return previous value */
static inline unsigned long clear_pge(void)
{
	unsigned long ret;
	asm volatile (
		"movl  %%cr4, %%eax\n\t"
		"movl  %%eax, %%edx\n\t"
		"andl  $0x7f, %%edx\n\t"
		"movl  %%edx, %%cr4\n\t"
		: "=a" (ret)
		:
		: "memory", "cc", "eax", "edx");
	return ret;
}

/* restores page global enable bit */
static inline void restore_pge(unsigned long cr4)
{
	asm volatile (
		"movl  %0, %%cr4\n\t"
		:
		: "r" (cr4)
		: "memory");
}

/* ... */
static inline void disable_cache(void)
{
	asm volatile (
		"movl  %%cr0, %%eax\n\t"
		"orl   $0x40000000, %%eax\n\t"
		"movl  %%eax, %%cr0\n\t"
		:
		:
		:"memory", "cc", "eax");
}

/* ... */
static inline void enable_cache(void)
{
	asm volatile (
		"movl  %%cr0, %%eax\n\t"
		"andl  $0xbfffffff, %%eax\n\t"
		"movl  %%eax, %%cr0"
		:
		:
		:"memory", "cc", "eax");
}

/* clear the MTRRdefType.E and MTRRdefType.FE flag to disable these MTRR's */
static inline void disable_mtrr(void)
{
	MTRRdefType_t MTRRdefType;

	RAW_ACCESS64(MTRRdefType) = rdmsr(MTRR_DEFTYPE);
	MTRRdefType.E = 0;
	MTRRdefType.FE = 0;
	wrmsr(MTRR_DEFTYPE, RAW_ACCESS64(MTRRdefType));
}

/*
 * written from pseudocode from intel
 *  (PentiumPro Family Developers manual Volume 3, P 322)
 *  
 */
static inline unsigned long pre_mtrr_change(void)
{
	unsigned long cr4;

	cr4 = clear_pge();

	wbinvd();

	disable_cache();

	wbinvd();

	flush__tlb();
	
	disable_mtrr();

	return cr4;
}

/*
 * written from pseudocode from intel
 *  (PentiumPro Family Developers manual Volume 3, P 322)
 */
static inline void post_mtrr_change(MTRRdefType_t MTRRdefType,unsigned long cr4)
{
	wbinvd();

	flush__tlb();

	wrmsr(MTRR_DEFTYPE, RAW_ACCESS64(MTRRdefType));

	enable_cache();

	restore_pge(cr4);
}

/*
 * writes all fixed mtrr's
 */
static inline void set_mtrr_fixed(void) {
	int i;

	wrmsr(MTRR_FIXED64K,mtrrcntl->fixed64.raw);
	wrmsr(MTRR_FIXED16K+0,mtrrcntl->fixed16[0].raw);
	wrmsr(MTRR_FIXED16K+1,mtrrcntl->fixed16[1].raw);
	for (i=0;i<8;i++)
		wrmsr(MTRR_FIXED4K+i,mtrrcntl->fixed4[i].raw);
}

/*
 * writes all variable mtrr's
 */
static inline void set_mtrr_variable(void) {
	int i;
	const int vcnt = mtrrcntl->MTRRcap.VCNT;

	for (i = 0 ; i < vcnt ; i++ ) {
		wrmsr(MTRR_VARIABLE +2*i,
		      RAW_ACCESS64(mtrrcntl->variable[i].MTRRphysBase));
		wrmsr(MTRR_VARIABLE +2*i+1,
		      RAW_ACCESS64(mtrrcntl->variable[i].MTRRphysMask));
	}
}


/*
 * compares the mtrr_cntl_t structure second with that set by
 * the boot processor,
 * returns 0 if equal,
 *        -1 if the structs are not initialized,
 *         1 if they are different
 *
 * the *second struct is assumed to be local, it is not locked!
 */
static inline int compare_mtrr_configuration(struct mtrr_cntl_t *second) {
	int i, result = 0;

	if (NULL == mtrrcntl) { result = -1; goto end; }
	if (NULL == second) { result = -1; goto end; }
	if (RAW_ACCESS64(mtrrcntl->MTRRcap)
	    != RAW_ACCESS64(second->MTRRcap)) {
		result = 1; goto end;
	}

	if (RAW_ACCESS64(mtrrcntl->MTRRdefType)
	    != RAW_ACCESS64(second->MTRRdefType)) {
		result = 1; goto end;
	}

	if (mtrrcntl->fixed64.raw != second->fixed64.raw) {
		result = 1; goto end;
	}
	if (mtrrcntl->fixed16[0].raw != second->fixed16[0].raw) {
		result = 1; goto end;
	}
	if (mtrrcntl->fixed16[1].raw != second->fixed16[1].raw) {
		result = 1; goto end;
	}

	for (i=0;i<8;i++)
		if (mtrrcntl->fixed4[i].raw != second->fixed4[i].raw) {
			result = 1; goto end;
		}
	{
		const int vcnt = mtrrcntl->MTRRcap.VCNT;
		for (i = 0; i < vcnt; i++) {
			if (RAW_ACCESS64(mtrrcntl->variable[i].MTRRphysBase) !=
			    RAW_ACCESS64(second->variable[i].MTRRphysBase)) {
				result = 1; goto end;
			}
			if (RAW_ACCESS64(mtrrcntl->variable[i].MTRRphysMask) !=
			    RAW_ACCESS64(second->variable[i].MTRRphysMask)) {
				result = 1; goto end;
			}
		}
	}

	end:

	return result;
}

/*
 * compares the mtrr configuration of the current processor with the
 * main configuration and overwrites the mtrr's in the processor if they
 * differ. (fixes a bug in the GA-686DX mainboard BIOS)
 */
void check_mtrr_config(void) {
	unsigned long cr4;
	unsigned long flags;
	struct mtrr_cntl_t *this_cpu_setting;
	int result;

	save_flags(flags); sti();

	/* if global struct is not initialized return */
	if (mtrrcntl == NULL) {
		restore_flags(flags);
		return;
	}

	/* disable MTRR feature if this_cpu_setting == NULL */
	/* read mtrr configuration of this cpu */
	this_cpu_setting = read_mtrr_configuration();
	if (this_cpu_setting == NULL) {
		printk("/proc/mtrr: MTRR's are NOT supported by cpu %i.\n",
		       smp_processor_id());
		restore_flags(flags);

		return;
	}
	/* compare mtrr configuration */
	result = compare_mtrr_configuration(this_cpu_setting);
	kfree(this_cpu_setting);
 	/* return if mtrr setting is correct */
	if (0 >= result) {
		restore_flags(flags);
		return;
	}
	
	/* prepare cpu's for setting mtrr's */
	cr4 = pre_mtrr_change();

	/* set all mtrr's */
 	set_mtrr_fixed();
 	set_mtrr_variable();

	/* prepare cpu's for running */
	post_mtrr_change(mtrrcntl->MTRRdefType, cr4);

	restore_flags(flags);

	printk("\nBIOS bug workaround: MTRR configuration changed on cpu "
	       "%i.\n", smp_processor_id());
}

