/*
 *  arch/s390/kernel/smp.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  based on other smp stuff by 
 *    (c) 1995 Alan Cox, CymruNET Ltd  <alan@cymru.net>
 *    (c) 1998 Ingo Molnar
 *
 * We work with logical cpu numbering everywhere we can. The only
 * functions using the real cpu address (got from STAP) are the sigp
 * functions. For all other functions we use the identity mapping.
 * That means that cpu_number_map[i] == i for every cpu. cpu_number_map is
 * used e.g. to find the idle task belonging to a logical cpu. Every array
 * in the kernel is sorted by the logical cpu number and not by the physical
 * one which is causing all the confusion with __cpu_logical_map and
 * cpu_number_map in other architectures.
 */

#include <asm/sigp.h>
#include <asm/timex.h>
#include <asm/init.h>
#include <asm/spinlock.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <asm/pgtable.h>
#include <asm/string.h>
#include <asm/s390_ext.h>

#include "cpcmd.h"
#include <asm/irq.h>

/* prototypes */
extern void update_one_process( struct task_struct *p,
                                unsigned long ticks, unsigned long user,
                                unsigned long system, int cpu);
extern int cpu_idle(void * unused);

extern __u16 boot_cpu_addr;
extern volatile int __cpu_logical_map[];

/*
 * An array with a pointer the lowcore of every CPU.
 */
static int       max_cpus = NR_CPUS;	  /* Setup configured maximum number of CPUs to activate	*/
int              smp_num_cpus;
struct _lowcore *lowcore_ptr[NR_CPUS];
unsigned int     prof_multiplier[NR_CPUS];
unsigned int     prof_counter[NR_CPUS];
volatile int     cpu_number_map[NR_CPUS];
cycles_t         cacheflush_time=0;
int              smp_threads_ready=0;      /* Set when the idlers are all forked. */
unsigned long    ipi_count=0;              /* Number of IPIs delivered. */
static atomic_t  smp_commenced = ATOMIC_INIT(0);

spinlock_t       kernel_flag = SPIN_LOCK_UNLOCKED;

/*
 *      Setup routine for controlling SMP activation
 *
 *      Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 *      activation entirely (the MPS table probe still happens, though).
 *
 *      Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 *      greater than 0, limits the maximum number of CPUs activated in
 *      SMP mode to <NUM>.
 */

void __init smp_setup(char *str, int *ints)
{
   if (ints && ints[0] > 0)
      max_cpus = ints[1];
   else
      max_cpus = 0;
}

/*
 * Reboot, halt and power_off routines for SMP.
 */
extern char vmhalt_cmd[];
extern char vmpoff_cmd[];
extern void reipl(int ipl_device);

void do_machine_restart(void)
{
        smp_send_stop();
	reipl(S390_lowcore.ipl_device); 
}

void machine_restart(char * __unused) 
{
        if (smp_processor_id() != 0) {
                smp_ext_call_async(0, ec_restart);
                for (;;);
        } else
                do_machine_restart();
}

void do_machine_halt(void)
{
        smp_send_stop();
        if (MACHINE_IS_VM && strlen(vmhalt_cmd) > 0) 
                cpcmd(vmhalt_cmd, NULL, 0);
        signal_processor(smp_processor_id(), sigp_stop_and_store_status);
}

void machine_halt(void)
{
        if (smp_processor_id() != 0) {
                smp_ext_call_async(0, ec_halt);
                for (;;);
        } else
                do_machine_halt();
}

void do_machine_power_off(void)
{
        smp_send_stop();
        if (MACHINE_IS_VM && strlen(vmpoff_cmd) > 0)
                cpcmd(vmpoff_cmd, NULL, 0);
        signal_processor(smp_processor_id(), sigp_stop_and_store_status);
}

void machine_power_off(void)
{
        if (smp_processor_id() != 0) {
                smp_ext_call_async(0, ec_power_off);
                for (;;);
        } else
                do_machine_power_off();
}

/*
 * This is the main routine where commands issued by other
 * cpus are handled.
 */

void do_ext_call_interrupt(struct pt_regs *regs, __u16 source_cpu_addr)
{
        ec_ext_call *ec, *next;
        int bits;

        /*
         * handle bit signal external calls
         *
         * For the ec_schedule signal we have to do nothing. All the work
         * is done automatically when we return from the interrupt.
	 * For the ec_restart, ec_halt and ec_power_off we call the
         * appropriate routine.
         */
        do {
                bits = atomic_read(&S390_lowcore.ext_call_fast);
        } while (atomic_compare_and_swap(bits,0,&S390_lowcore.ext_call_fast));

        if (test_bit(ec_restart, &bits))
		do_machine_restart();
        if (test_bit(ec_halt, &bits))
		do_machine_halt();
        if (test_bit(ec_power_off, &bits))
		do_machine_power_off();

        /*
         * Handle external call commands with a parameter area
         */
        do {
                ec = (ec_ext_call *) atomic_read(&S390_lowcore.ext_call_queue);
        } while (atomic_compare_and_swap((int) ec, 0,
                                         &S390_lowcore.ext_call_queue));
        if (ec == NULL)
                return;   /* no command signals */

        /* Make a fifo out of the lifo */
        next = ec->next;
        ec->next = NULL;
        while (next != NULL) {
                ec_ext_call *tmp = next->next;
                next->next = ec;
                ec = next;
                next = tmp;
        }

        /* Execute every sigp command on the queue */
        while (ec != NULL) {
                switch (ec->cmd) {
                case ec_get_ctl: {
                        ec_creg_parms *pp;
                        pp = (ec_creg_parms *) ec->parms;
                        atomic_set(&ec->status,ec_executing);
                        asm volatile (
                                "   bras  1,0f\n"
                                "   stctl 0,0,0(%0)\n"
                                "0: ex    %1,0(1)\n"
                                : : "a" (pp->cregs+pp->start_ctl),
                                "a" ((pp->start_ctl<<4) + pp->end_ctl)
                                : "memory", "1" );
                        atomic_set(&ec->status,ec_done);
                        return;
                }
                case ec_set_ctl: {
                        ec_creg_parms *pp;
                        pp = (ec_creg_parms *) ec->parms;
                        atomic_set(&ec->status,ec_executing);
                        asm volatile (
                                "   bras  1,0f\n"
                                "   lctl 0,0,0(%0)\n"
                                "0: ex    %1,0(1)\n"
                                : : "a" (pp->cregs+pp->start_ctl),
                                "a" ((pp->start_ctl<<4) + pp->end_ctl)
                                : "memory", "1" );
                        atomic_set(&ec->status,ec_done);
                        return;
                }
                case ec_set_ctl_masked: {
                        ec_creg_mask_parms *pp;
                        u32 cregs[16];
                        int i;

                        pp = (ec_creg_mask_parms *) ec->parms;
                        atomic_set(&ec->status,ec_executing);
                        asm volatile (
                                "   bras  1,0f\n"
                                "   stctl 0,0,0(%0)\n"
                                "0: ex    %1,0(1)\n"
                                : : "a" (cregs+pp->start_ctl),
                                "a" ((pp->start_ctl<<4) + pp->end_ctl)
                                : "memory", "1" );
                        for (i = pp->start_ctl; i <= pp->end_ctl; i++)
                                cregs[i] = (cregs[i] & pp->andvals[i])
                                                     | pp->orvals[i];
                        asm volatile (
                                "   bras  1,0f\n"
                                "   lctl 0,0,0(%0)\n"
                                "0: ex    %1,0(1)\n"
                                : : "a" (cregs+pp->start_ctl),
                                "a" ((pp->start_ctl<<4) + pp->end_ctl)
                                : "memory", "1" );
                        atomic_set(&ec->status,ec_done);
                        return;
                }
		case ec_ptlb:
			atomic_set(&ec->status, ec_executing);
			__flush_tlb();
			atomic_set(&ec->status, ec_done);
		        return;
                case ec_callback: {
                        ec_callback_parms *cbp;
                        cbp = (ec_callback_parms *) ec->parms;
                        atomic_set(&ec->status,ec_executing);
                        (cbp->callback)(cbp->data);
                        atomic_set(&ec->status,ec_done);
                        return;
                }
                default:
                }
                ec = ec->next;
        }
}

/*
 * Send an external call sigp to another cpu and wait for its completion.
 */
sigp_ccode smp_ext_call_sync(int cpu, ec_cmd_sig cmd, void *parms)
{
        struct _lowcore *lowcore = &get_cpu_lowcore(cpu);
        sigp_ccode ccode;
        ec_ext_call ec;

        ec.cmd = cmd;
        atomic_set(&ec.status, ec_pending);
        ec.parms = parms;
        do {
                ec.next = (ec_ext_call*) atomic_read(&lowcore->ext_call_queue);
        } while (atomic_compare_and_swap((int) ec.next, (int)(&ec),
                                         &lowcore->ext_call_queue));
        /*
         * We try once to deliver the signal. There are four possible
         * return codes:
         * 0) Order code accepted - can't show up on an external call
         * 1) Status stored - fine, wait for completion.
         * 2) Busy - there is another signal pending. Thats fine too, because
         *    do_ext_call from the pending signal will execute all signals on
         *    the queue. We wait for completion.
         * 3) Not operational - something very bad has happened to the cpu.
         *    do not wait for completion.
         */
        ccode = signal_processor(cpu, sigp_external_call);

        if (ccode != sigp_not_operational)
                /* wait for completion, FIXME: possible seed of a deadlock */
                while (atomic_read(&ec.status) != ec_done);

        return ccode;
}

/*
 * Send an external call sigp to another cpu and return without waiting
 * for its completion. Currently we do not support parameters with
 * asynchronous sigps.
 */
sigp_ccode smp_ext_call_async(int cpu, ec_bit_sig sig)
{
        struct _lowcore *lowcore = &get_cpu_lowcore(cpu);
        sigp_ccode ccode;

        /*
         * Set signaling bit in lowcore of target cpu and kick it
         */
        atomic_set_mask(1<<sig, &lowcore->ext_call_fast);
        ccode = signal_processor(cpu, sigp_external_call);
        return ccode;
}

/*
 * Send an external call sigp to every other cpu in the system and
 * wait for the completion of the sigps.
 */
void smp_ext_call_sync_others(ec_cmd_sig cmd, void *parms)
{
        struct _lowcore *lowcore;
        ec_ext_call ec[NR_CPUS];
        sigp_ccode ccode;
        int i;

        for (i = 0; i < smp_num_cpus; i++) {
                if (smp_processor_id() == i)
                        continue;
                lowcore = &get_cpu_lowcore(i);
                ec[i].cmd = cmd;
                atomic_set(&ec[i].status, ec_pending);
                ec[i].parms = parms;
                do {
                        ec[i].next = (ec_ext_call *)
                                        atomic_read(&lowcore->ext_call_queue);
                } while (atomic_compare_and_swap((int) ec[i].next, (int)(ec+i),
                                                 &lowcore->ext_call_queue));
                ccode = signal_processor(i, sigp_external_call);
        }

        /* wait for completion, FIXME: possible seed of a deadlock */
        for (i = 0; i < smp_num_cpus; i++) {
                if (smp_processor_id() == i)
                        continue;
                while (atomic_read(&ec[i].status) != ec_done);
        }
}

/*
 * Send an external call sigp to every other cpu in the system and
 * return without waiting for the completion of the sigps. Currently
 * we do not support parameters with asynchronous sigps.
 */
void smp_ext_call_async_others(ec_bit_sig sig)
{
        struct _lowcore *lowcore;
        sigp_ccode ccode;
        int i;

        for (i = 0; i < smp_num_cpus; i++) {
                if (smp_processor_id() == i)
                        continue;
                lowcore = &get_cpu_lowcore(i);
                /*
                 * Set signaling bit in lowcore of target cpu and kick it
                 */
                atomic_set_mask(1<<sig, &lowcore->ext_call_fast);
                ccode = signal_processor(i, sigp_external_call);
        }
}

/*
 * cycles through all the cpus,
 * returns early if info is not NULL & the processor has something
 * of intrest to report in the info structure.
 * it returns the next cpu to check if it returns early.
 * i.e. it should be used as follows if you wish to receive info.
 * next_cpu=0;
 * do
 * {
 *    info->cpu=next_cpu;
 *    next_cpu=smp_signal_others(order_code,parameter,TRUE,info);
 *    ... check info here
 * } while(next_cpu<=smp_num_cpus)
 *
 *  if you are lazy just use it like
 * smp_signal_others(order_code,parameter,0,TRUE,NULL);
 */
int smp_signal_others(sigp_order_code order_code, u32 parameter,
                      int spin, sigp_info *info)
{
        sigp_ccode   ccode;
        u32          dummy;
        u16          i;

        if (info)
                info->intresting = FALSE;
        for (i = (info ? info->cpu : 0); i < smp_num_cpus; i++) {
                if (smp_processor_id() != i) {
                        do {
                                ccode = signal_processor_ps(
                                        (info ? &info->status : &dummy),
                                        parameter, i, order_code);
                        } while(spin && ccode == sigp_busy);
                        if (info && ccode != sigp_order_code_accepted) {
                                info->intresting = TRUE;
                                info->cpu = i;
                                info->ccode = ccode;
                                i++;
                                break;
                        }
                }
        }
        return i;
}

/*
 * this function sends a 'stop' sigp to all other CPUs in the system.
 * it goes straight through.
 */

void smp_send_stop(void)
{
        int i;
        u32 dummy;
        unsigned long low_core_addr;

        /* write magic number to zero page (absolute 0) */

        get_cpu_lowcore(smp_processor_id()).panic_magic = __PANIC_MAGIC;

        /* stop all processors */

        smp_signal_others(sigp_stop, 0, TRUE, NULL);

        /* store status of all processors in their lowcores (real 0) */

        for (i =  0; i < smp_num_cpus; i++) {
                if (smp_processor_id() != i) {
                        int ccode;
                        low_core_addr = (unsigned long)&get_cpu_lowcore(i);
                        do {
                                ccode = signal_processor_ps(
                                   &dummy,
                                   low_core_addr,
                                   i,
                                   sigp_store_status_at_address);
                        } while(ccode == sigp_busy);
                }
        }
}

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */

void smp_send_reschedule(int cpu)
{
        smp_ext_call_async(cpu, ec_schedule);
}

/*
 * Set a bit in a control register of all cpus
 */
void smp_ctl_set_bit(int cr, int bit) {
        ec_creg_mask_parms parms;

        if (atomic_read(&smp_commenced) != 0) {
                parms.start_ctl = cr;
                parms.end_ctl = cr;
                parms.orvals[cr] = 1 << bit;
                parms.andvals[cr] = 0xFFFFFFFF;
                smp_ext_call_sync_others(ec_set_ctl_masked,&parms);
        }
        __ctl_set_bit(cr, bit);
}

/*
 * Clear a bit in a control register of all cpus
 */
void smp_ctl_clear_bit(int cr, int bit) {
        ec_creg_mask_parms parms;

        if (atomic_read(&smp_commenced) != 0) {
                parms.start_ctl = cr;
                parms.end_ctl = cr;
                parms.orvals[cr] = 0x00000000;
                parms.andvals[cr] = ~(1 << bit);
                smp_ext_call_sync_others(ec_set_ctl_masked,&parms);
        }
        __ctl_clear_bit(cr, bit);
}

/*
 * Execute a callback function on all cpus
 */
void smp_do_callback_all(void (*callback)(void *), void *data) {
        ec_callback_parms parms;

        if (atomic_read(&smp_commenced) != 0) {
                parms.callback = callback;
                parms.data = data;
                smp_ext_call_sync_others(ec_callback, &parms);
        }
        (callback)(data);
}

/*
 * Lets check how many CPUs we have.
 */

void smp_count_cpus(void)
{
        int curr_cpu;

        current->processor = 0;
        smp_num_cpus = 1;
        for (curr_cpu = 0;
             curr_cpu <= 65535 && smp_num_cpus < max_cpus; curr_cpu++) {
                if ((__u16) curr_cpu == boot_cpu_addr)
                        continue;
                __cpu_logical_map[smp_num_cpus] = (__u16) curr_cpu;
                if (signal_processor(smp_num_cpus, sigp_sense) ==
                    sigp_not_operational)
                        continue;
                smp_num_cpus++;
        }
        printk("Detected %d CPU's\n",(int) smp_num_cpus);
        printk("Boot cpu address %2X\n", boot_cpu_addr);
}


/*
 *      Activate a secondary processor.
 */
extern void init_100hz_timer(void);
extern void cpu_init (void);

int __init start_secondary(void *cpuvoid)
{
        /* Setup the cpu */
        cpu_init();
        /* Print info about this processor */
        print_cpu_info(&safe_get_cpu_lowcore(smp_processor_id()).cpu_data);
        /* Wait for completion of smp startup */
        while (!atomic_read(&smp_commenced))
                /* nothing */ ;
        /* init per CPU 100 hz timer */
        init_100hz_timer();
        /* cpu_idle will call schedule for us */
        return cpu_idle(NULL);
}

/*
 * The restart interrupt handler jumps to start_secondary directly
 * without the detour over initialize_secondary. We defined it here
 * so that the linker doesn't complain.
 */
void __init initialize_secondary(void)
{
}

static void __init do_boot_cpu(int cpu)
{
        struct task_struct *idle;
        struct _lowcore    *cpu_lowcore;
        /*
         *	We need an idle process for each processor.
         */
        
        kernel_thread(start_secondary,(void *)cpu, CLONE_PID);
        idle = task[cpu];
        if (!idle)
                panic("No idle process for CPU %d",cpu);
        idle->processor = cpu;
        cpu_number_map[cpu] = cpu;
        cpu_lowcore=&get_cpu_lowcore(cpu);
        cpu_lowcore->kernel_stack=idle->tss.ksp;
        __asm__ __volatile__("stctl 0,15,%0\n\t"
                             "stam  0,15,%1"
                             : "=m" (cpu_lowcore->cregs_save_area[0]), 
                               "=m" (cpu_lowcore->access_regs_save_area[0])
                             : : "memory");
        
        eieio();
        signal_processor(cpu,sigp_restart);
}

/*
 *      Architecture specific routine called by the kernel just before init is
 *      fired off. This allows the BP to have everything in order [we hope].
 *      At the end of this all the APs will hit the system scheduling and off
 *      we go. Each AP will load the system gdt's and jump through the kernel
 *      init into idle(). At this point the scheduler will one day take over
 *      and give them jobs to do. smp_callin is a standard routine
 *      we use to track CPUs as they power up.
 */

void __init smp_commence(void)
{
        /*
         *      Lets the callins below out of their loop.
         */
        atomic_set(&smp_commenced,1);
}

/*
 *	Cycle through the processors sending APIC IPIs to boot each.
 */

void __init smp_boot_cpus(void)
{
        struct _lowcore *curr_lowcore;
        sigp_ccode   ccode;
        int curr_cpu;
        int i;

        /* request the 0x1202 external interrupt */
        if (register_external_interrupt(0x1202, do_ext_call_interrupt) != 0)
                panic("Couldn't request external interrupt 0x1202");
        smp_count_cpus();
        memset(lowcore_ptr,0,sizeof(lowcore_ptr));  
        
        /*
         *      Initialize the logical to physical CPU number mapping
         *      and the per-CPU profiling counter/multiplier
         */
        
        for (i = 0; i < NR_CPUS; i++) {
                cpu_number_map[i] = -1;
                prof_counter[i] = 1;
                prof_multiplier[i] = 1;
        }

        cpu_number_map[0] = 0;
	print_cpu_info(&safe_get_cpu_lowcore(0).cpu_data);

        for(curr_cpu = 0; curr_cpu < smp_num_cpus; curr_cpu++)
        {
                curr_lowcore = (struct _lowcore *)
                                    __get_free_page(GFP_KERNEL|GFP_DMA);
                if (curr_lowcore == NULL) {
                        printk("smp_boot_cpus failed to allocate prefix memory\n"); 
                        break;
                }
                lowcore_ptr[curr_cpu] = curr_lowcore;
                memcpy(curr_lowcore, &S390_lowcore, sizeof(struct _lowcore));
                /*
                 * Most of the parameters are set up when the cpu is
                 * started up.
                 */
                if(smp_processor_id()==curr_cpu)
                        set_prefix((u32)curr_lowcore);
                else {
                        ccode=signal_processor_p((u32)(curr_lowcore),
                                                 curr_cpu,sigp_set_prefix);
                        if(ccode) {
                                /* if this gets troublesome I'll have to do 
                                 * something about it. */
                                printk("ccode %d for cpu %d  returned when "
                                       "setting prefix in smp_boot_cpus not good.\n",
                                       (int)ccode,(int)curr_cpu);
                        }
                        else
                                do_boot_cpu(curr_cpu);
                }
        }
}

/*
 * the frequency of the profiling timer can be changed
 * by writing a multiplier value into /proc/profile.
 *
 * usually you want to run this on all CPUs ;)
 */
int setup_profiling_timer(unsigned int multiplier)
{
        return 0;
}

/*
 * Local timer interrupt handler. It does both profiling and
 * process statistics/rescheduling.
 *
 * We do profiling in every local tick, statistics/rescheduling
 * happen only every 'profiling multiplier' ticks. The default
 * multiplier is 1 and it can be changed by writing the new multiplier
 * value into /proc/profile.
 */

void smp_local_timer_interrupt(struct pt_regs * regs)
{
        int cpu = smp_processor_id();

        /*
         * The profiling function is SMP safe. (nothing can mess
         * around with "current", and the profiling counters are
         * updated with atomic operations). This is especially
         * useful with a profiling multiplier != 1
         */
        if (!user_mode(regs))
                s390_do_profile(regs->psw.addr);

        if (!--prof_counter[cpu]) {
                int user=0,system=0;
                struct task_struct * p = current;

                /*
                 * After doing the above, we need to make like
                 * a normal interrupt - otherwise timer interrupts
                 * ignore the global interrupt lock, which is the
                 * WrongThing (tm) to do.
                 */

                if (user_mode(regs))
                        user=1;
                else
                        system=1;

                irq_enter(cpu, 0);
                update_one_process(p, 1, user, system, cpu);
                if (p->pid) {
                        p->counter -= 1;
                        if (p->counter < 0) {
                                p->counter = 0;
                                p->need_resched = 1;
                        }
                        if (p->priority < DEF_PRIORITY) {
                                kstat.cpu_nice += user;
                                kstat.per_cpu_nice[cpu] += user;
                        } else {
                                kstat.cpu_user += user;
                                kstat.per_cpu_user[cpu] += user;
                        }
                        kstat.cpu_system += system;
                        kstat.per_cpu_system[cpu] += system;

                }
                prof_counter[cpu]=prof_multiplier[cpu];
                irq_exit(cpu, 0);
        }
}

EXPORT_SYMBOL(kernel_flag);
EXPORT_SYMBOL(smp_ctl_set_bit);
EXPORT_SYMBOL(smp_ctl_clear_bit);
EXPORT_SYMBOL(smp_do_callback_all);
