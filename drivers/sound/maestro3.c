/*****************************************************************************
 *
 *      ESS Maestro3/Allegro driver for Linux 2.2.x
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    (c) Copyright 2000 Zach Brown <zab@zabbo.net>
 *
 * I need to thank many people for helping make this driver happen.  
 * As always, Eric Brombaugh was a hacking machine and killed many bugs
 * that I was too dumb to notice.  Howard Kim at ESS provided reference boards 
 * and as much docs as he could.  Todd and Mick at Dell tested snapshots on 
 * an army of laptops.  msw and deviant at Red Hat also humoured me by hanging
 * their laptops every few hours in the name of science.
 * 
 * Shouts go out to Mike "DJ XPCom" Ang.
 *
 * History
 *  v0.51 - Dec 31 2000 - Zach Brown <zab@zabbo.net>
 *   fix up incredibly broken open/release resource management
 *   duh.  fix record format setting.
 *   add SMP locking and cleanup formatting here and there
 *  v0.50 - Dec 16 2000 - Zach Brown <zab@zabbo.net>
 *   use native ac97_codec
 *   pull out most SILLY_ stuff
 *   align instance allocation so record works
 *   fix up PCI IDs..
 *  v0.02 - Nov 04 2000 - Zach Brown <zab@zabbo.net>
 *   changed clocking setup for m3, slowdown fixed.
 *   codec reset is hopefully reliable now
 *   rudimentary apm/power management makes suspend/resume work
 *  v0.01 - Oct 31 2000 - Zach Brown <zab@zabbo.net>
 *   first release
 *  v0.00 - Sep 09 2000 - Zach Brown <zab@zabbo.net>
 *   first pass derivation from maestro.c
 *
 * TODO
 *  no beep on init (mute)
 *  resetup msrc data memory if freq changes?
 *  clean up driver in general, 2.2/2.3 junk, etc.
 *
 *  --- 
 *
 *  Allow me to ramble a bit about the m3 architecture.  The core of the
 *  chip is the 'assp', the custom ESS dsp that runs the show.  It has
 *  a small amount of code and data ram.  ESS drops binary dsp code images
 *  on our heads, but we don't get to see specs on the dsp.  
 *
 *  The constant piece of code on the dsp is the 'kernel'.  It also has a 
 *  chunk of the dsp memory that is statically set aside for its control
 *  info.  This is the KDATA defines in maestro3.h.  Part of its core
 *  data is a list of code addresses that point to the pieces of DSP code
 *  that it should walk through in its loop.  These other pieces of code
 *  do the real work.  The kernel presumably jumps into each of them in turn.
 *  These code images tend to have their own data area, and one can have
 *  multiple data areas representing different states for each of the 'client
 *  instance' code portions.  There is generaly a list in the kernel data
 *  that points to the data instances for a given piece of code.
 *
 *  We've only been given the binary image for the 'minisrc', mini sample 
 *  rate converter.  This is rather annoying because it limits the work
 *  we can do on the dsp, but it also greatly simplifies the job of managing
 *  dsp data memory for the code and data for our playing streams :).  We
 *  statically allocate the minisrc code into a region we 'know' to be free
 *  based on the map of the binary kernel image we're loading.  We also 
 *  statically allocate the data areas for the maximum number of pcm streams
 *  we can be dealing with.  This max is set by the length of the static list
 *  in the kernel data that records the number of minisrc data regions we
 *  can have.  Thats right, all software dsp mixing with static code list
 *  limits.  Rock.
 *
 *  How sound goes in and out is still a relative mystery.  It appears
 *  that the dsp has the ability to get input and output through various
 *  'connections'.  To do IO from or to a connection, you put the address
 *  of the minisrc client area in the static kernel data lists for that 
 *  input or output.  so for pcm -> dsp -> mixer, we put the minisrc data
 *  instance in the DMA list and also in the list for the mixer.  I guess
 *  it Just Knows which is in/out, and we give some dma control info that
 *  helps.  There are all sorts of cool inputs/outputs that it seems we can't
 *  use without dsp code images that know how to use them.
 *
 *  So at init time we preload all the memory allocation stuff and set some
 *  system wide parameters.  When we really get a sound to play we build
 *  up its minisrc header (stream parameters, buffer addresses, input/output
 *  settings).  Then we throw its header on the various lists.  We also
 *  tickle some KDATA settings that ask the assp to raise clock interrupts
 *  and do some amount of software mixing before handing data to the ac97.
 *
 *  Sorry for the vague details.  Feel free to ask Eric or myself if you
 *  happen to be trying to use this driver elsewhere.  Please accept my
 *  apologies for the quality of the OSS support code, its passed through
 *  too many hands now and desperately wants to be rethought.
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/malloc.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/reboot.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>
#include <asm/spinlock.h>

 /* 
  * apologies for old intra-2.2 and 
  * 2.2->2.4 compat cruft.. it will
  * die someday.
  */
#ifndef wait_queue_head_t 
    #define wait_queue_head_t struct wait_queue *
#endif
#ifndef DECLARE_WAITQUEUE
    #define DECLARE_WAITQUEUE(QUEUE,INIT) struct wait_queue QUEUE = {INIT, NULL}
#endif
#ifndef init_waitqueue_head
  #define init_waitqueue_head init_waitqueue
#endif

#define SILLY_PCI_BASE_ADDRESS(PCIDEV) (PCIDEV->base_address[0] & PCI_BASE_ADDRESS_IO_MASK)
#define SILLY_INIT_SEM(SEM) SEM=MUTEX;
#define SILLY_MAKE_INIT(FUNC) __initfunc(FUNC)
#define SILLY_OFFSET(VMA) ((VMA)->vm_offset)

#include <linux/ac97_codec.h>

#include "maestro3.h"

#define M_DEBUG 1

#define DRIVER_VERSION      "0.51"
#define PFX                 "maestro3: "

#ifndef PCI_VENDOR_ESS
#define PCI_VENDOR_ESS      0x125D
#endif

#define M3_STATE_MAGIC      0x734d724d
#define M3_CARD_MAGIC       0x646e6f50

#define ESS_FMT_STEREO      0x01
#define ESS_FMT_16BIT       0x02
#define ESS_FMT_MASK        0x03
#define ESS_DAC_SHIFT       0   
#define ESS_ADC_SHIFT       4

#define DAC_RUNNING         1
#define ADC_RUNNING         2

#define SND_DEV_DSP16       5 

   
#ifdef M_DEBUG
static int debug=0;
static int global_dsp_speed = 49;
#define DPMOD   1   /* per module load */
#define DPSTR   2   /* per 'stream' */
#define DPSYS   3   /* per syscall */
#define DPCRAP  4   /* stuff the user shouldn't see unless they're really debuggin */
#define DPINT   5   /* per interrupt, LOTS */
#define DPRINTK(DP, args...) {if (debug >= (DP)) printk(KERN_DEBUG PFX args);}
#else
#define DPRINTK(x)
#endif

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
static int m3_apm_callback(apm_event_t ae);
static int in_suspend=0;
wait_queue_head_t suspend_queue;
static void check_suspend(void);
#else
#define check_suspend(args...)
#define in_suspend 0
#endif

struct m3_list {
    int curlen;
    int mem_addr;
    int max;
};

int external_amp = 1;
static int maestro_notifier(struct notifier_block *nb, unsigned long event, void *buf);

struct notifier_block maestro_nb = {maestro_notifier, NULL, 0};

struct ess_state {
    unsigned int magic;
    struct ess_card *card;
    unsigned char fmt, enable;

    spinlock_t lock;

    int index;

    struct semaphore open_sem;
    wait_queue_head_t open_wait;
    mode_t open_mode;

    int dev_audio;

    struct assp_instance {
        u16 code, data;
    } dac_inst, adc_inst;

    /* should be in dmabuf */
    unsigned int rateadc, ratedac;

    struct dmabuf {
        void *rawbuf;
        unsigned buforder;
        unsigned numfrag;
        unsigned fragshift;
        unsigned hwptr, swptr;
        unsigned total_bytes;
        int count;
        unsigned error; /* over/underrun */
        wait_queue_head_t wait;
        /* redundant, but makes calculations easier */
        unsigned fragsize;
        unsigned dmasize;
        unsigned fragsamples;
        /* OSS stuff */
        unsigned mapped:1;
        unsigned ready:1;    
        unsigned endcleared:1;
        unsigned ossfragshift;
        int ossmaxfrags;
        unsigned subdivision;
        /* new in m3 */
        int mixer_index, dma_index, msrc_index, adc1_index;
        int in_lists;

    } dma_dac, dma_adc;
};
    
struct ess_card {
    unsigned int magic;

    struct ess_card *next;

    struct ac97_codec *ac97;
    spinlock_t ac97_lock;

    int card_type;

#define NR_DSPS 1
#define MAX_DSPS NR_DSPS
    struct ess_state channels[MAX_DSPS];

    spinlock_t lock;

    /* hardware resources */
    struct pci_dev *pcidev;
    u32 iobase;
    u32 irq;

    int dacs_active;

    int timer_users;

    struct m3_list  msrc_list,
                    mixer_list,
                    adc1_list,
                    dma_list;

    /* for storing reset state..*/
    u8 reset_state;

#ifdef CONFIG_APM
    u16 *suspend_dsp_mem;
#endif
};

/*
 * an arbitrary volume we set the internal
 * volume settings to so that the ac97 volume
 * range is a little less insane.  0x7fff is 
 * max.
 */
#define ARB_VOLUME ( 0x6800 )

static const unsigned sample_shift[] = { 0, 1, 1, 2 };

enum {
    ESS_ALLEGRO,
    ESS_MAESTRO3,
    /* hardware strapping */
    ESS_MAESTRO3HW
};

static struct card_type {
    int pci_id;
    int type;
    char *name;
} m3_card_types[] = {
    {0x1988, ESS_ALLEGRO, "Allegro"},
    {0x1998, ESS_MAESTRO3, "Maestro3(i)"},
    {0x199a, ESS_MAESTRO3, "Maestro3(i)hw"},
};
#define NUM_CARD_TYPES ( sizeof(m3_card_types) / sizeof(m3_card_types[0]) )

static unsigned 
ld2(unsigned int x)
{
    unsigned r = 0;
    
    if (x >= 0x10000) {
        x >>= 16;
        r += 16;
    }
    if (x >= 0x100) {
        x >>= 8;
        r += 8;
    }
    if (x >= 0x10) {
        x >>= 4;
        r += 4;
    }
    if (x >= 4) {
        x >>= 2;
        r += 2;
    }
    if (x >= 2)
        r++;
    return r;
}

static struct ess_card *devs = NULL;

static void m3_outw(struct ess_card *card,
        u16 value, unsigned long reg)
{
    check_suspend();
    outw(value, card->iobase + reg);
}

static u16 m3_inw(struct ess_card *card, unsigned long reg)
{
    check_suspend();
    return inw(card->iobase + reg);
}
static void m3_outb(struct ess_card *card, 
        u8 value, unsigned long reg)
{
    check_suspend();
    outb(value, card->iobase + reg);
}
static u8 m3_inb(struct ess_card *card, unsigned long reg)
{
    check_suspend();
    return inb(card->iobase + reg);
}

/*
 * access 16bit words to the code or data regions of the dsp's memory.
 * index addresses 16bit words.
 */
static u16 __m3_assp_read(struct ess_card *card, u16 region, u16 index)
{
    m3_outw(card, region & MEMTYPE_MASK, DSP_PORT_MEMORY_TYPE);
    m3_outw(card, index, DSP_PORT_MEMORY_INDEX);
    return m3_inw(card, DSP_PORT_MEMORY_DATA);
}
static u16 m3_assp_read(struct ess_card *card, u16 region, u16 index)
{
    unsigned long flags;
    u16 ret;

    spin_lock_irqsave(&(card->lock), flags);
    ret = __m3_assp_read(card, region, index);
    spin_unlock_irqrestore(&(card->lock), flags);

    return ret;
}
static void __m3_assp_write(struct ess_card *card, 
        u16 region, u16 index, u16 data)
{
    m3_outw(card, region & MEMTYPE_MASK, DSP_PORT_MEMORY_TYPE);
    m3_outw(card, index, DSP_PORT_MEMORY_INDEX);
    m3_outw(card, data, DSP_PORT_MEMORY_DATA);
}

static void m3_assp_write(struct ess_card *card, 
        u16 region, u16 index, u16 data)
{
    unsigned long flags;
    spin_lock_irqsave(&(card->lock), flags);
    __m3_assp_write(card, region, index, data);
    spin_unlock_irqrestore(&(card->lock), flags);
}

static void m3_assp_halt(struct ess_card *card)
{
    card->reset_state = m3_inb(card, DSP_PORT_CONTROL_REG_B) & ~REGB_STOP_CLOCK;
    mdelay(10);
    m3_outb(card, card->reset_state & ~REGB_ENABLE_RESET, DSP_PORT_CONTROL_REG_B);
}

static void m3_assp_continue(struct ess_card *card)
{
    m3_outb(card, card->reset_state | REGB_ENABLE_RESET, DSP_PORT_CONTROL_REG_B);
}

/*
 * This makes me sad. the maestro3 has lists
 * internally that must be packed.. 0 terminates,
 * apparently, or maybe all unused entries have
 * to be 0, the lists have static lengths set
 * by the binary code images.
 */

static int m3_add_list(struct ess_card *card,
        struct m3_list *list, u16 val)
{
    DPRINTK(DPSTR, "adding val 0x%x to list 0x%p at pos %d\n",
            val, list, list->curlen);

    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
            list->mem_addr + list->curlen,
            val);

    return list->curlen++;

}

static void m3_remove_list(struct ess_card *card,
        struct m3_list *list, int index)
{
    u16  val;
    int lastindex = list->curlen - 1;

    DPRINTK(DPSTR, "removing ind %d from list 0x%p\n",
            index, list);

    if(index != lastindex) {
        val = m3_assp_read(card, MEMTYPE_INTERNAL_DATA,
                list->mem_addr + lastindex);
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
                list->mem_addr + index,
                val);
    }

    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
            list->mem_addr + lastindex,
            0);

    list->curlen--;
}

static void set_fmt(struct ess_state *s, unsigned char mask, unsigned char data)
{
    int tmp;

    s->fmt = (s->fmt & mask) | data;

    tmp = (s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_MASK;

    /* write to 'mono' word */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + SRC3_DIRECTION_OFFSET + 1, 
            (tmp & ESS_FMT_STEREO) ? 0 : 1);
    /* write to '8bit' word */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + SRC3_DIRECTION_OFFSET + 2, 
            (tmp & ESS_FMT_16BIT) ? 0 : 1);

    tmp = (s->fmt >> ESS_ADC_SHIFT) & ESS_FMT_MASK;

    /* write to 'mono' word */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + SRC3_DIRECTION_OFFSET + 1, 
            (tmp & ESS_FMT_STEREO) ? 0 : 1);
    /* write to '8bit' word */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + SRC3_DIRECTION_OFFSET + 2, 
            (tmp & ESS_FMT_16BIT) ? 0 : 1);
}

static void set_dac_rate(struct ess_state *s, unsigned int rate)
{
    u32 freq;

    if (rate > 48000)
        rate = 48000;
    if (rate < 8000)
        rate = 8000;

    s->ratedac = rate;

    freq = ((rate << 15) + 24000 ) / 48000;
    if(freq) 
        freq--;

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + CDATA_FREQUENCY,
            freq);
}

static void set_adc_rate(struct ess_state *s, unsigned int rate)
{
    u32 freq;

    if (rate > 48000)
        rate = 48000;
    if (rate < 8000)
        rate = 8000;

    s->rateadc = rate;

    freq = ((rate << 15) + 24000 ) / 48000;
    if(freq) 
        freq--;

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + CDATA_FREQUENCY,
            freq);
}

static void inc_timer_users(struct ess_card *card)
{
    unsigned long flags;

    spin_lock_irqsave(&card->lock, flags);
    card->timer_users++;
    DPRINTK(DPSYS, "inc timer users now %d\n",
            card->timer_users);
    if(card->timer_users != 1) 
        goto out;

    __m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_TIMER_COUNT_RELOAD,
         240 ) ;

    __m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_TIMER_COUNT_CURRENT,
         240 ) ;

    m3_outw(card,  
            m3_inw(card, HOST_INT_CTRL) | CLKRUN_GEN_ENABLE,
            HOST_INT_CTRL);

out:
    spin_unlock_irqrestore(&card->lock, flags);
}

static void dec_timer_users(struct ess_card *card)
{
    unsigned long flags;

    spin_lock_irqsave(&card->lock, flags);

    card->timer_users--;
    DPRINTK(DPSYS, "dec timer users now %d\n",
            card->timer_users);
    if(card->timer_users > 0 ) 
        goto out;

    __m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_TIMER_COUNT_RELOAD,
         0 ) ;

    __m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_TIMER_COUNT_CURRENT,
         0 ) ;

    m3_outw(card,  m3_inw(card, HOST_INT_CTRL) & ~CLKRUN_GEN_ENABLE,
            HOST_INT_CTRL);
out:
    spin_unlock_irqrestore(&card->lock, flags);
}

/*
 * {start,stop}_{adc,dac} should be called
 * while holding the 'state' lock and they
 * will try to grab the 'card' lock..
 */
static void stop_adc(struct ess_state *s)
{
    if (! (s->enable & ADC_RUNNING)) 
        return;

    s->enable &= ~ADC_RUNNING;
    dec_timer_users(s->card);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + CDATA_INSTANCE_READY, 0);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            KDATA_ADC1_REQUEST, 0);
}    

static void stop_dac(struct ess_state *s)
{
    if (! (s->enable & DAC_RUNNING)) 
        return;

    DPRINTK(DPSYS, "stop_dac()\n");

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + CDATA_INSTANCE_READY, 0);

    s->enable &= ~DAC_RUNNING;
    s->card->dacs_active--;
    dec_timer_users(s->card);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            KDATA_MIXER_TASK_NUMBER, 
            s->card->dacs_active ) ;
}    

static void start_dac(struct ess_state *s)
{
    if( (!s->dma_dac.mapped && s->dma_dac.count < 1) ||
            !s->dma_dac.ready ||
            (s->enable & DAC_RUNNING)) 
        return;

    DPRINTK(DPSYS, "start_dac()\n");

    s->enable |= DAC_RUNNING;
    s->card->dacs_active++;
    inc_timer_users(s->card);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + CDATA_INSTANCE_READY, 1);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            KDATA_MIXER_TASK_NUMBER, 
            s->card->dacs_active ) ;
}    

static void start_adc(struct ess_state *s)
{
    if ((! s->dma_adc.mapped &&
                s->dma_adc.count >= (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize)) 
        || !s->dma_adc.ready 
        || (s->enable & ADC_RUNNING) ) 
            return;

    DPRINTK(DPSYS, "start_adc()\n");

    s->enable |= ADC_RUNNING;
    inc_timer_users(s->card);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            KDATA_ADC1_REQUEST, 1);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + CDATA_INSTANCE_READY, 1);
}    

static struct play_vals {
    u16 addr, val;
} pv[] = {
    {CDATA_LEFT_VOLUME, ARB_VOLUME},
    {CDATA_RIGHT_VOLUME, ARB_VOLUME},
    {SRC3_DIRECTION_OFFSET, 0} ,
    /* +1, +2 are stereo/16 bit */
    {SRC3_DIRECTION_OFFSET + 3, 0X0000}, /* fraction? */
    {SRC3_DIRECTION_OFFSET + 4, 0}, /* first l */
    {SRC3_DIRECTION_OFFSET + 5, 0}, /* first r */
    {SRC3_DIRECTION_OFFSET + 6, 0}, /* second l */
    {SRC3_DIRECTION_OFFSET + 7, 0}, /* second r */
    {SRC3_DIRECTION_OFFSET + 8, 0}, /* delta l */
    {SRC3_DIRECTION_OFFSET + 9, 0}, /* delta r */
    {SRC3_DIRECTION_OFFSET + 10, 0X8000}, /* round */
    {SRC3_DIRECTION_OFFSET + 11, 0XFF00}, /* higher bute mark */
    {SRC3_DIRECTION_OFFSET + 13, 0}, /* temp0 */
    {SRC3_DIRECTION_OFFSET + 14, 0}, /* c fraction */
    {SRC3_DIRECTION_OFFSET + 15, 0}, /* counter */
    {SRC3_DIRECTION_OFFSET + 16, 8}, /* numin */
    {SRC3_DIRECTION_OFFSET + 17, 50*2}, /* numout */
    {SRC3_DIRECTION_OFFSET + 18, MINISRC_BIQUAD_STAGE - 1}, /* numstage */
    {SRC3_DIRECTION_OFFSET + 20, 0}, /* filtertap */
    {SRC3_DIRECTION_OFFSET + 21, 0} /* booster */
};

static void 
ess_play_setup(struct ess_state *s, int mode, u32 rate, void *buffer, int size)
{
    int dsp_in_size = MINISRC_IN_BUFFER_SIZE - (0x20 * 2);
    int dsp_out_size = MINISRC_OUT_BUFFER_SIZE - (0x20 * 2);
    int dsp_in_buffer = s->dac_inst.data + (MINISRC_TMP_BUFFER_SIZE / 2);
    int dsp_out_buffer = dsp_in_buffer + (dsp_in_size / 2) + 1;
    struct dmabuf *db = &s->dma_dac;
    int i;

    DPRINTK(DPSTR, "mode=%d rate=%d buf=%p len=%d.\n",
        mode, rate, buffer, size);

#define LO(x) ((x) & 0xffff)
#define HI(x) LO((x) >> 16)

    /* host dma buffer pointers */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_ADDRL,
        LO(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_ADDRH,
        HI(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_END_PLUS_1L,
        LO(virt_to_bus(buffer) + size));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_END_PLUS_1H,
        HI(virt_to_bus(buffer) + size));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_CURRENTL,
        LO(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_CURRENTH,
        HI(virt_to_bus(buffer)));
#undef LO
#undef HI

    /* dsp buffers */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_IN_BUF_BEGIN,
        dsp_in_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_IN_BUF_END_PLUS_1,
        dsp_in_buffer + (dsp_in_size / 2));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_IN_BUF_HEAD,
        dsp_in_buffer);
    
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_IN_BUF_TAIL,
        dsp_in_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_OUT_BUF_BEGIN,
        dsp_out_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_OUT_BUF_END_PLUS_1,
        dsp_out_buffer + (dsp_out_size / 2));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_OUT_BUF_HEAD,
        dsp_out_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_OUT_BUF_TAIL,
        dsp_out_buffer);

    /*
     * some per client initializers
     */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + SRC3_DIRECTION_OFFSET + 12,
        s->dac_inst.data + 40 + 8);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + SRC3_DIRECTION_OFFSET + 19,
        s->dac_inst.code + MINISRC_COEF_LOC);

    /* enable or disable low pass filter? */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + SRC3_DIRECTION_OFFSET + 22,
        s->ratedac > 45000 ? 0xff : 0 );
    
    /* tell it which way dma is going? */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_DMA_CONTROL,
        DMACONTROL_AUTOREPEAT + DMAC_PAGE3_SELECTOR + DMAC_BLOCKF_SELECTOR);

    /*
     * set an armload of static initializers
     */
    for(i = 0 ; i < (sizeof(pv) / sizeof(pv[0])) ; i++) 
        m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + pv[i].addr, pv[i].val);

    /* 
     * put us in the lists if we're not already there
     */

    if(db->in_lists == 0) {

        db->msrc_index = m3_add_list(s->card, &s->card->msrc_list, 
                s->dac_inst.data >> DP_SHIFT_COUNT);

        db->dma_index = m3_add_list(s->card, &s->card->dma_list, 
                s->dac_inst.data >> DP_SHIFT_COUNT);

        db->mixer_index = m3_add_list(s->card, &s->card->mixer_list, 
                s->dac_inst.data >> DP_SHIFT_COUNT);

        db->in_lists = 1;
    }

    set_dac_rate(s,rate);
    start_dac(s);
}

/*
 *    Native record driver 
 */
static struct rec_vals {
    u16 addr, val;
} rv[] = {
    {CDATA_LEFT_VOLUME, ARB_VOLUME},
    {CDATA_RIGHT_VOLUME, ARB_VOLUME},
    {SRC3_DIRECTION_OFFSET, 1} ,
    /* +1, +2 are stereo/16 bit */
    {SRC3_DIRECTION_OFFSET + 3, 0X0000}, /* fraction? */
    {SRC3_DIRECTION_OFFSET + 4, 0}, /* first l */
    {SRC3_DIRECTION_OFFSET + 5, 0}, /* first r */
    {SRC3_DIRECTION_OFFSET + 6, 0}, /* second l */
    {SRC3_DIRECTION_OFFSET + 7, 0}, /* second r */
    {SRC3_DIRECTION_OFFSET + 8, 0}, /* delta l */
    {SRC3_DIRECTION_OFFSET + 9, 0}, /* delta r */
    {SRC3_DIRECTION_OFFSET + 10, 0X8000}, /* round */
    {SRC3_DIRECTION_OFFSET + 11, 0XFF00}, /* higher bute mark */
    {SRC3_DIRECTION_OFFSET + 13, 0}, /* temp0 */
    {SRC3_DIRECTION_OFFSET + 14, 0}, /* c fraction */
    {SRC3_DIRECTION_OFFSET + 15, 0}, /* counter */
    {SRC3_DIRECTION_OFFSET + 16, 50},/* numin */
    {SRC3_DIRECTION_OFFSET + 17, 8}, /* numout */
    {SRC3_DIRECTION_OFFSET + 18, 0}, /* numstage */
    {SRC3_DIRECTION_OFFSET + 19, 0}, /* coef */
    {SRC3_DIRECTION_OFFSET + 20, 0}, /* filtertap */
    {SRC3_DIRECTION_OFFSET + 21, 0}, /* booster */
    {SRC3_DIRECTION_OFFSET + 22, 0xff} /* skip lpf */
};

/*
 * the buffer passed here must be 32bit aligned 
 */
static void 
ess_rec_setup(struct ess_state *s, int mode, u32 rate, void *buffer, int size)
{
    int dsp_in_size = MINISRC_IN_BUFFER_SIZE + (0x10 * 2);
    int dsp_out_size = MINISRC_OUT_BUFFER_SIZE - (0x10 * 2);
    int dsp_in_buffer = s->adc_inst.data + (MINISRC_TMP_BUFFER_SIZE / 2);
    int dsp_out_buffer = dsp_in_buffer + (dsp_in_size / 2) + 1;
    struct dmabuf *db = &s->dma_adc;
    int i;

    DPRINTK(DPSTR, "rec_setup mode=%d rate=%d buf=%p len=%d.\n",
        mode, rate, buffer, size);

#define LO(x) ((x) & 0xffff)
#define HI(x) LO((x) >> 16)

    /* host dma buffer pointers */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_ADDRL,
        LO(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_ADDRH,
        HI(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_END_PLUS_1L,
        LO(virt_to_bus(buffer) + size));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_END_PLUS_1H,
        HI(virt_to_bus(buffer) + size));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_CURRENTL,
        LO(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_CURRENTH,
        HI(virt_to_bus(buffer)));
#undef LO
#undef HI

    /* dsp buffers */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_IN_BUF_BEGIN,
        dsp_in_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_IN_BUF_END_PLUS_1,
        dsp_in_buffer + (dsp_in_size / 2));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_IN_BUF_HEAD,
        dsp_in_buffer);
    
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_IN_BUF_TAIL,
        dsp_in_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_OUT_BUF_BEGIN,
        dsp_out_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_OUT_BUF_END_PLUS_1,
        dsp_out_buffer + (dsp_out_size / 2));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_OUT_BUF_HEAD,
        dsp_out_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_OUT_BUF_TAIL,
        dsp_out_buffer);

    /*
     * some per client initializers
     */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + SRC3_DIRECTION_OFFSET + 12,
        s->adc_inst.data + 40 + 8);

    /* tell it which way dma is going? */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_DMA_CONTROL,
        DMACONTROL_DIRECTION + DMACONTROL_AUTOREPEAT + 
        DMAC_PAGE3_SELECTOR + DMAC_BLOCKF_SELECTOR);

    /*
     * set an armload of static initializers
     */
    for(i = 0 ; i < (sizeof(rv) / sizeof(rv[0])) ; i++) 
        m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + rv[i].addr, rv[i].val);

    /* 
     * put us in the lists if we're not already there
     */

    if(db->in_lists == 0) {

        db->adc1_index = m3_add_list(s->card, &s->card->adc1_list, 
                s->adc_inst.data >> DP_SHIFT_COUNT);

        db->dma_index = m3_add_list(s->card, &s->card->dma_list, 
                s->adc_inst.data >> DP_SHIFT_COUNT);

        db->msrc_index = m3_add_list(s->card, &s->card->msrc_list, 
                s->adc_inst.data >> DP_SHIFT_COUNT);

        db->in_lists = 1;
    }

    set_fmt(s, ~0, 0);
    set_adc_rate(s,rate);
    start_adc(s);
}
/* --------------------------------------------------------------------- */

static void set_dmaa(struct ess_state *s, unsigned int addr, unsigned int count)
{
    DPRINTK(DPINT,"set_dmaa??\n");
}

static void set_dmac(struct ess_state *s, unsigned int addr, unsigned int count)
{
    DPRINTK(DPINT,"set_dmac??\n");
}

u32 get_dma_pos(struct ess_card *card,
        int instance_addr)
{
    u16 hi = 0, lo = 0;
    int retry = 10;

    /*
     * try and get a valid answer
     */
    while(retry--) {
        hi =  m3_assp_read(card, MEMTYPE_INTERNAL_DATA,
                instance_addr + CDATA_HOST_SRC_CURRENTH);

        lo = m3_assp_read(card, MEMTYPE_INTERNAL_DATA,
                instance_addr + CDATA_HOST_SRC_CURRENTL);

        if(hi == m3_assp_read(card, MEMTYPE_INTERNAL_DATA,
                instance_addr + CDATA_HOST_SRC_CURRENTH))
            break;
    }
    return lo | (hi<<16);
}

u32 get_dmaa(struct ess_state *s)
{
    u32 offset;

    offset = get_dma_pos(s->card, s->dac_inst.data) - 
        virt_to_bus(s->dma_dac.rawbuf);

    DPRINTK(DPINT,"get_dmaa: 0x%08x\n",offset);

    return offset;
}

u32 get_dmac(struct ess_state *s)
{
    u32 offset;

    offset = get_dma_pos(s->card, s->adc_inst.data) -
        virt_to_bus(s->dma_adc.rawbuf);

    DPRINTK(DPINT,"get_dmac: 0x%08x\n",offset);

    return offset;

}

static void ess_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static int 
prog_dmabuf(struct ess_state *s, unsigned rec)
{
    struct dmabuf *db = rec ? &s->dma_adc : &s->dma_dac;
    unsigned rate = rec ? s->rateadc : s->ratedac;
    unsigned bytepersec;
    unsigned bufs;
    unsigned char fmt;
    unsigned long flags;

    spin_lock_irqsave(&s->lock, flags);

    fmt = s->fmt;
    if (rec) {
        stop_adc(s);
        fmt >>= ESS_ADC_SHIFT;
    } else {
        stop_dac(s);
        fmt >>= ESS_DAC_SHIFT;
    }
    fmt &= ESS_FMT_MASK;

    db->hwptr = db->swptr = db->total_bytes = db->count = db->error = db->endcleared = 0;

    bytepersec = rate << sample_shift[fmt];
    bufs = PAGE_SIZE << db->buforder;
    if (db->ossfragshift) {
        if ((1000 << db->ossfragshift) < bytepersec)
            db->fragshift = ld2(bytepersec/1000);
        else
            db->fragshift = db->ossfragshift;
    } else {
        db->fragshift = ld2(bytepersec/100/(db->subdivision ? db->subdivision : 1));
        if (db->fragshift < 3)
            db->fragshift = 3; 
    }
    db->numfrag = bufs >> db->fragshift;
    while (db->numfrag < 4 && db->fragshift > 3) {
        db->fragshift--;
        db->numfrag = bufs >> db->fragshift;
    }
    db->fragsize = 1 << db->fragshift;
    if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
        db->numfrag = db->ossmaxfrags;
    db->fragsamples = db->fragsize >> sample_shift[fmt];
    db->dmasize = db->numfrag << db->fragshift;

    DPRINTK(DPSTR,"prog_dmabuf: numfrag: %d fragsize: %d dmasize: %d\n",db->numfrag,db->fragsize,db->dmasize);

    memset(db->rawbuf, (fmt & ESS_FMT_16BIT) ? 0 : 0x80, db->dmasize);

    if (rec) 
        ess_rec_setup(s, fmt, s->rateadc, db->rawbuf, db->dmasize);
    else 
        ess_play_setup(s, fmt, s->ratedac, db->rawbuf, db->dmasize);

    db->ready = 1;

    spin_unlock_irqrestore(&s->lock, flags);

    return 0;
}

static void clear_advance(struct ess_state *s)
{
    unsigned char c = ((s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_16BIT) ? 0 : 0x80;
    
    unsigned char *buf = s->dma_dac.rawbuf;
    unsigned bsize = s->dma_dac.dmasize;
    unsigned bptr = s->dma_dac.swptr;
    unsigned len = s->dma_dac.fragsize;
    
    if (bptr + len > bsize) {
        unsigned x = bsize - bptr;
        memset(buf + bptr, c, x);
        /* account for wrapping? */
        bptr = 0;
        len -= x;
    }
    memset(buf + bptr, c, len);
}

static void ess_update_ptr(struct ess_state *s)
{
    unsigned hwptr;
    int diff;

    /* update ADC pointer */
    if (s->dma_adc.ready) {
        hwptr = get_dmac(s) % s->dma_adc.dmasize;
        diff = (s->dma_adc.dmasize + hwptr - s->dma_adc.hwptr) % s->dma_adc.dmasize;
        s->dma_adc.hwptr = hwptr;
        s->dma_adc.total_bytes += diff;
        s->dma_adc.count += diff;
        if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
            wake_up(&s->dma_adc.wait);
        if (!s->dma_adc.mapped) {
            if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
                stop_adc(s); 
                /* brute force everyone back in sync, sigh */
                s->dma_adc.count = 0;
                s->dma_adc.swptr = 0;
                s->dma_adc.hwptr = 0;
                s->dma_adc.error++;
            }
        }
    }
    /* update DAC pointer */
    if (s->dma_dac.ready) {
        hwptr = get_dmaa(s) % s->dma_dac.dmasize; 
        diff = (s->dma_dac.dmasize + hwptr - s->dma_dac.hwptr) % s->dma_dac.dmasize;

        DPRINTK(DPINT,"updating dac: hwptr: %6d diff: %6d count: %6d\n",
                hwptr,diff,s->dma_dac.count);

        s->dma_dac.hwptr = hwptr;
        s->dma_dac.total_bytes += diff;

        if (s->dma_dac.mapped) {
            
            s->dma_dac.count += diff;
            if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) {
                wake_up(&s->dma_dac.wait);
            }
        } else {

            s->dma_dac.count -= diff;
            
            if (s->dma_dac.count <= 0) {
                DPRINTK(DPCRAP,"underflow! diff: %d (0x%x) count: %d (0x%x) hw: %d (0x%x) sw: %d (0x%x)\n", 
                        diff, diff, 
                        s->dma_dac.count, 
                        s->dma_dac.count, 
                    hwptr, hwptr,
                    s->dma_dac.swptr,
                    s->dma_dac.swptr);
                stop_dac(s);
                /* brute force everyone back in sync, sigh */
                s->dma_dac.count = 0; 
                s->dma_dac.swptr = hwptr; 
                s->dma_dac.error++;
            } else if (s->dma_dac.count <= (signed)s->dma_dac.fragsize && !s->dma_dac.endcleared) {
                clear_advance(s);
                s->dma_dac.endcleared = 1;
            }
            if (s->dma_dac.count + (signed)s->dma_dac.fragsize <= (signed)s->dma_dac.dmasize) {
                wake_up(&s->dma_dac.wait);
                DPRINTK(DPINT,"waking up DAC count: %d sw: %d hw: %d\n",
                        s->dma_dac.count, s->dma_dac.swptr, hwptr);
            }
        }
    }
}

static void 
ess_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    struct ess_card *c = (struct ess_card *)dev_id;
    struct ess_state *s = &c->channels[0];
    u8 status;

    status = inb(c->iobase + 0x1A);

    if(status == 0xff) 
        return;
   
    /* presumably acking the ints? */
    outw(status, c->iobase + 0x1A); 

    if(in_suspend)
        return;

    /*
     * ack an assp int if its running
     * and has an int pending
     */
    if( status & ASSP_INT_PENDING) {
        u8 ctl = inb(c->iobase + ASSP_CONTROL_B);
        if( !(ctl & STOP_ASSP_CLOCK)) {
            ctl = inb(c->iobase + ASSP_HOST_INT_STATUS );
            if(ctl & DSP2HOST_REQ_TIMER) {
                outb( DSP2HOST_REQ_TIMER, c->iobase + ASSP_HOST_INT_STATUS);

                /* update adc/dac info if it was a timer int */
                spin_lock(&s->lock);
                ess_update_ptr(s);
                spin_unlock(&s->lock);
            }
        }
    }

    /* XXX is this needed? */
    if(status & 0x40) 
        outb(0x40, c->iobase+0x1A);
}


/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT PFX "invalid magic value in %s\n";

#define VALIDATE_MAGIC(FOO,MAG)                         \
({                                                \
    if (!(FOO) || (FOO)->magic != MAG) { \
        printk(invalid_magic,__FUNCTION__);            \
        return -ENXIO;                    \
    }                                         \
})

#define VALIDATE_STATE(a) VALIDATE_MAGIC(a,M3_STATE_MAGIC)
#define VALIDATE_CARD(a) VALIDATE_MAGIC(a,M3_CARD_MAGIC)

/* --------------------------------------------------------------------- */

static loff_t m3_llseek(struct file *file, loff_t offset, int origin)
{
    return -ESPIPE;
}

/* --------------------------------------------------------------------- */

static int drain_dac(struct ess_state *s, int nonblock)
{
    DECLARE_WAITQUEUE(wait,current);
    unsigned long flags;
    int count;
    signed long tmo;

    if (s->dma_dac.mapped || !s->dma_dac.ready)
        return 0;
    current->state = TASK_INTERRUPTIBLE;
    add_wait_queue(&s->dma_dac.wait, &wait);
    for (;;) {
        spin_lock_irqsave(&s->lock, flags);
        count = s->dma_dac.count;
        spin_unlock_irqrestore(&s->lock, flags);
        if (count <= 0)
            break;
        if (signal_pending(current))
            break;
        if (nonblock) {
            remove_wait_queue(&s->dma_dac.wait, &wait);
            current->state = TASK_RUNNING;
            return -EBUSY;
        }
        tmo = (count * HZ) / s->ratedac;
        tmo >>= sample_shift[(s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_MASK];
        /* XXX this is just broken.  someone is waking us up alot, or schedule_timeout is broken.
            or something.  who cares. - zach */
        if (!schedule_timeout(tmo ? tmo : 1) && tmo)
            DPRINTK(DPCRAP,"dma timed out?? %ld\n",jiffies);
    }
    remove_wait_queue(&s->dma_dac.wait, &wait);
    current->state = TASK_RUNNING;
    if (signal_pending(current))
            return -ERESTARTSYS;
    return 0;
}

static ssize_t 
ess_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
    struct ess_state *s = (struct ess_state *)file->private_data;
    ssize_t ret;
    unsigned long flags;
    unsigned swptr;
    int cnt;
    
    VALIDATE_STATE(s);
    if (ppos != &file->f_pos)
        return -ESPIPE;
    if (s->dma_adc.mapped)
        return -ENXIO;
    if (!s->dma_adc.ready && (ret = prog_dmabuf(s, 1)))
        return ret;
    if (!access_ok(VERIFY_WRITE, buffer, count))
        return -EFAULT;
    ret = 0;

    spin_lock_irqsave(&s->lock, flags);

    while (count > 0) {
        int timed_out;

        swptr = s->dma_adc.swptr;
        cnt = s->dma_adc.dmasize-swptr;
        if (s->dma_adc.count < cnt)
            cnt = s->dma_adc.count;

        if (cnt > count)
            cnt = count;

        if (cnt <= 0) {
            start_adc(s);
            if (file->f_flags & O_NONBLOCK) {
                ret = ret ? ret : -EAGAIN;
                goto out;
            }

            spin_unlock_irqrestore(&s->lock, flags);

            timed_out = interruptible_sleep_on_timeout(&s->dma_adc.wait, HZ) == 0;

            spin_lock_irqsave(&s->lock, flags);
                
            if(timed_out) {
                printk("read: chip lockup? dmasz %d fragsz %d count %d hwptr %d swptr %d\n",
                       s->dma_adc.dmasize, s->dma_adc.fragsize, s->dma_adc.count, 
                       s->dma_adc.hwptr, s->dma_adc.swptr);
                stop_adc(s);
                set_dmac(s, virt_to_bus(s->dma_adc.rawbuf), s->dma_adc.numfrag << s->dma_adc.fragshift);
                s->dma_adc.count = s->dma_adc.hwptr = s->dma_adc.swptr = 0;
            }

            if (signal_pending(current)) {
                ret = ret ? ret : -ERESTARTSYS;
                goto out;
            }
            continue;
        }
    
        spin_unlock_irqrestore(&s->lock, flags);
        if (copy_to_user(buffer, s->dma_adc.rawbuf + swptr, cnt)) {
            ret = ret ? ret : -EFAULT;
            return ret;
        }
        spin_lock_irqsave(&s->lock, flags);

        swptr = (swptr + cnt) % s->dma_adc.dmasize;
        s->dma_adc.swptr = swptr;
        s->dma_adc.count -= cnt;
        count -= cnt;
        buffer += cnt;
        ret += cnt;
        start_adc(s);
    }

out:
    spin_unlock_irqrestore(&s->lock, flags);
    return ret;
}

static ssize_t 
ess_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
    struct ess_state *s = (struct ess_state *)file->private_data;
    ssize_t ret;
    unsigned long flags;
    unsigned swptr;
    int cnt;
    
    VALIDATE_STATE(s);
    if (ppos != &file->f_pos)
        return -ESPIPE;
    if (s->dma_dac.mapped)
        return -ENXIO;
    if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
        return ret;
    if (!access_ok(VERIFY_READ, buffer, count))
        return -EFAULT;
    ret = 0;

    spin_lock_irqsave(&s->lock, flags);

    while (count > 0) {
        int timed_out;

        if (s->dma_dac.count < 0) {
            s->dma_dac.count = 0;
            s->dma_dac.swptr = s->dma_dac.hwptr;
        }
        swptr = s->dma_dac.swptr;

        cnt = s->dma_dac.dmasize-swptr;

        if (s->dma_dac.count + cnt > s->dma_dac.dmasize)
            cnt = s->dma_dac.dmasize - s->dma_dac.count;

        if (cnt > count)
            cnt = count;

        if (cnt <= 0) {
            start_dac(s);
            if (file->f_flags & O_NONBLOCK) {
                if(!ret) ret = -EAGAIN;
                goto out;
            }

            spin_unlock_irqrestore(&s->lock, flags);
            timed_out = interruptible_sleep_on_timeout(&s->dma_dac.wait, HZ) == 0;
            spin_lock_irqsave(&s->lock, flags);

            if(timed_out) {
                DPRINTK(DPCRAP,"write: chip lockup? dmasz %d fragsz %d count %d hwptr %d swptr %d\n",
                       s->dma_dac.dmasize, s->dma_dac.fragsize, s->dma_dac.count, 
                       s->dma_dac.hwptr, s->dma_dac.swptr);
                stop_dac(s);
                set_dmaa(s, virt_to_bus(s->dma_dac.rawbuf), s->dma_dac.numfrag << s->dma_dac.fragshift);
                s->dma_dac.count = s->dma_dac.hwptr = s->dma_dac.swptr = 0;
            }
            if (signal_pending(current)) {
                if (!ret) ret = -ERESTARTSYS;
                goto out;
            }
            continue;
        }
        spin_unlock_irqrestore(&s->lock, flags);
        if (copy_from_user(s->dma_dac.rawbuf + swptr, buffer, cnt)) {
            if (!ret) ret = -EFAULT;
            return ret;
        }
        spin_lock_irqsave(&s->lock, flags);
        DPRINTK(DPSYS,"wrote %6d bytes at sw: %6d cnt: %6d while hw: %6d\n",
                cnt, swptr, s->dma_dac.count, s->dma_dac.hwptr);
        
        swptr = (swptr + cnt) % s->dma_dac.dmasize;

        s->dma_dac.swptr = swptr;
        s->dma_dac.count += cnt;
        s->dma_dac.endcleared = 0;
        count -= cnt;
        buffer += cnt;
        ret += cnt;
        start_dac(s);
    }
out:
    spin_unlock_irqrestore(&s->lock, flags);
    return ret;
}

static unsigned int ess_poll(struct file *file, struct poll_table_struct *wait)
{
    struct ess_state *s = (struct ess_state *)file->private_data;
    unsigned long flags;
    unsigned int mask = 0;

    VALIDATE_STATE(s);
    if (file->f_mode & FMODE_WRITE)
        poll_wait(file, &s->dma_dac.wait, wait);
    if (file->f_mode & FMODE_READ)
        poll_wait(file, &s->dma_adc.wait, wait);

    spin_lock_irqsave(&s->lock, flags);
    ess_update_ptr(s);

    if (file->f_mode & FMODE_READ) {
        if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
            mask |= POLLIN | POLLRDNORM;
    }
    if (file->f_mode & FMODE_WRITE) {
        if (s->dma_dac.mapped) {
            if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
                mask |= POLLOUT | POLLWRNORM;
        } else {
            if ((signed)s->dma_dac.dmasize >= s->dma_dac.count + (signed)s->dma_dac.fragsize)
                mask |= POLLOUT | POLLWRNORM;
        }
    }

    spin_unlock_irqrestore(&s->lock, flags);
    return mask;
}

static int ess_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct ess_state *s = (struct ess_state *)file->private_data;
    struct dmabuf *db;
    int ret;
    unsigned long size;

    VALIDATE_STATE(s);
    if (vma->vm_flags & VM_WRITE) {
        if ((ret = prog_dmabuf(s, 1)) != 0)
            return ret;
        db = &s->dma_dac;
    } else 
    if (vma->vm_flags & VM_READ) {
        if ((ret = prog_dmabuf(s, 0)) != 0)
            return ret;
        db = &s->dma_adc;
    } else  
        return -EINVAL;
    if (SILLY_OFFSET(vma) != 0)
        return -EINVAL;
    size = vma->vm_end - vma->vm_start;
    if (size > (PAGE_SIZE << db->buforder))
        return -EINVAL;
    if (remap_page_range(vma->vm_start, virt_to_phys(db->rawbuf), size, vma->vm_page_prot))
        return -EAGAIN;
    db->mapped = 1;
    return 0;
}

/*
 * god, what an absolute mess..
 * not all the paths through here are
 * properly locked.  
 *   *sob*
 */
static int ess_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
    struct ess_state *s = (struct ess_state *)file->private_data;
    unsigned long flags;
    audio_buf_info abinfo;
    count_info cinfo;
    int val, mapped, ret;
    unsigned char fmtm, fmtd;

    VALIDATE_STATE(s);

    mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
        ((file->f_mode & FMODE_READ) && s->dma_adc.mapped);

    DPRINTK(DPSYS,"ess_ioctl: cmd %d\n", cmd);

    switch (cmd) {
    case OSS_GETVERSION:
        return put_user(SOUND_VERSION, (int *)arg);

    case SNDCTL_DSP_SYNC:
        if (file->f_mode & FMODE_WRITE)
            return drain_dac(s, file->f_flags & O_NONBLOCK);
        return 0;
        
    case SNDCTL_DSP_SETDUPLEX:
        /* XXX fix */
        return 0;

    case SNDCTL_DSP_GETCAPS:
        return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP, (int *)arg);
        
    case SNDCTL_DSP_RESET:
        spin_lock_irqsave(&s->lock, flags);
        if (file->f_mode & FMODE_WRITE) {
            stop_dac(s);
            synchronize_irq();
            s->dma_dac.swptr = s->dma_dac.hwptr = s->dma_dac.count = s->dma_dac.total_bytes = 0;
        }
        if (file->f_mode & FMODE_READ) {
            stop_adc(s);
            synchronize_irq();
            s->dma_adc.swptr = s->dma_adc.hwptr = s->dma_adc.count = s->dma_adc.total_bytes = 0;
        }
        spin_unlock_irqrestore(&s->lock, flags);
        return 0;

    case SNDCTL_DSP_SPEED:
        get_user_ret(val, (int *)arg, -EFAULT);
        spin_lock_irqsave(&s->lock, flags);
        if (val >= 0) {
            if (file->f_mode & FMODE_READ) {
                stop_adc(s);
                s->dma_adc.ready = 0;
                set_adc_rate(s, val);
            }
            if (file->f_mode & FMODE_WRITE) {
                stop_dac(s);
                s->dma_dac.ready = 0;
                set_dac_rate(s, val);
            }
        }
        spin_unlock_irqrestore(&s->lock, flags);
        return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);
        
    case SNDCTL_DSP_STEREO:
        get_user_ret(val, (int *)arg, -EFAULT);
        spin_lock_irqsave(&s->lock, flags);
        fmtd = 0;
        fmtm = ~0;
        if (file->f_mode & FMODE_READ) {
            stop_adc(s);
            s->dma_adc.ready = 0;
            if (val)
                fmtd |= ESS_FMT_STEREO << ESS_ADC_SHIFT;
            else
                fmtm &= ~(ESS_FMT_STEREO << ESS_ADC_SHIFT);
        }
        if (file->f_mode & FMODE_WRITE) {
            stop_dac(s);
            s->dma_dac.ready = 0;
            if (val)
                fmtd |= ESS_FMT_STEREO << ESS_DAC_SHIFT;
            else
                fmtm &= ~(ESS_FMT_STEREO << ESS_DAC_SHIFT);
        }
        set_fmt(s, fmtm, fmtd);
        spin_unlock_irqrestore(&s->lock, flags);
        return 0;

    case SNDCTL_DSP_CHANNELS:
        get_user_ret(val, (int *)arg, -EFAULT);
        spin_lock_irqsave(&s->lock, flags);
        if (val != 0) {
            fmtd = 0;
            fmtm = ~0;
            if (file->f_mode & FMODE_READ) {
                stop_adc(s);
                s->dma_adc.ready = 0;
                if (val >= 2)
                    fmtd |= ESS_FMT_STEREO << ESS_ADC_SHIFT;
                else
                    fmtm &= ~(ESS_FMT_STEREO << ESS_ADC_SHIFT);
            }
            if (file->f_mode & FMODE_WRITE) {
                stop_dac(s);
                s->dma_dac.ready = 0;
                if (val >= 2)
                    fmtd |= ESS_FMT_STEREO << ESS_DAC_SHIFT;
                else
                    fmtm &= ~(ESS_FMT_STEREO << ESS_DAC_SHIFT);
            }
            set_fmt(s, fmtm, fmtd);
        }
        spin_unlock_irqrestore(&s->lock, flags);
        return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_FMT_STEREO << ESS_ADC_SHIFT) 
                       : (ESS_FMT_STEREO << ESS_DAC_SHIFT))) ? 2 : 1, (int *)arg);
        
    case SNDCTL_DSP_GETFMTS: /* Returns a mask */
        return put_user(AFMT_U8|AFMT_S16_LE, (int *)arg);
        
    case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
        get_user_ret(val, (int *)arg, -EFAULT);
        spin_lock_irqsave(&s->lock, flags);
        if (val != AFMT_QUERY) {
            fmtd = 0;
            fmtm = ~0;
            if (file->f_mode & FMODE_READ) {
                stop_adc(s);
                s->dma_adc.ready = 0;
                if (val == AFMT_S16_LE)
                    fmtd |= ESS_FMT_16BIT << ESS_ADC_SHIFT;
                else
                    fmtm &= ~(ESS_FMT_16BIT << ESS_ADC_SHIFT);
            }
            if (file->f_mode & FMODE_WRITE) {
                stop_dac(s);
                s->dma_dac.ready = 0;
                if (val == AFMT_S16_LE)
                    fmtd |= ESS_FMT_16BIT << ESS_DAC_SHIFT;
                else
                    fmtm &= ~(ESS_FMT_16BIT << ESS_DAC_SHIFT);
            }
            set_fmt(s, fmtm, fmtd);
        }
        spin_unlock_irqrestore(&s->lock, flags);
        return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? 
            (ESS_FMT_16BIT << ESS_ADC_SHIFT) 
            : (ESS_FMT_16BIT << ESS_DAC_SHIFT))) ? 
                AFMT_S16_LE : 
                AFMT_U8, 
            (int *)arg);
        
    case SNDCTL_DSP_POST:
        return 0;

    case SNDCTL_DSP_GETTRIGGER:
        val = 0;
        if ((file->f_mode & FMODE_READ) && (s->enable & ADC_RUNNING))
            val |= PCM_ENABLE_INPUT;
        if ((file->f_mode & FMODE_WRITE) && (s->enable & DAC_RUNNING)) 
            val |= PCM_ENABLE_OUTPUT;
        return put_user(val, (int *)arg);
        
    case SNDCTL_DSP_SETTRIGGER:
        get_user_ret(val, (int *)arg, -EFAULT);
        if (file->f_mode & FMODE_READ) {
            if (val & PCM_ENABLE_INPUT) {
                if (!s->dma_adc.ready && (ret =  prog_dmabuf(s, 1)))
                    return ret;
                start_adc(s);
            } else
                stop_adc(s);
        }
        if (file->f_mode & FMODE_WRITE) {
            if (val & PCM_ENABLE_OUTPUT) {
                if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
                    return ret;
                start_dac(s);
            } else
                stop_dac(s);
        }
        return 0;

    case SNDCTL_DSP_GETOSPACE:
        if (!(file->f_mode & FMODE_WRITE))
            return -EINVAL;
        if (!(s->enable & DAC_RUNNING) && (val = prog_dmabuf(s, 0)) != 0)
            return val;
        spin_lock_irqsave(&s->lock, flags);
        ess_update_ptr(s);
        abinfo.fragsize = s->dma_dac.fragsize;
        abinfo.bytes = s->dma_dac.dmasize - s->dma_dac.count;
        abinfo.fragstotal = s->dma_dac.numfrag;
        abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;      
        spin_unlock_irqrestore(&s->lock, flags);
        return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

    case SNDCTL_DSP_GETISPACE:
        if (!(file->f_mode & FMODE_READ))
            return -EINVAL;
        if (!(s->enable & ADC_RUNNING) && (val = prog_dmabuf(s, 1)) != 0)
            return val;
        spin_lock_irqsave(&s->lock, flags);
        ess_update_ptr(s);
        abinfo.fragsize = s->dma_adc.fragsize;
        abinfo.bytes = s->dma_adc.count;
        abinfo.fragstotal = s->dma_adc.numfrag;
        abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;      
        spin_unlock_irqrestore(&s->lock, flags);
        return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
        
    case SNDCTL_DSP_NONBLOCK:
        file->f_flags |= O_NONBLOCK;
        return 0;

    case SNDCTL_DSP_GETODELAY:
        if (!(file->f_mode & FMODE_WRITE))
            return -EINVAL;
        spin_lock_irqsave(&s->lock, flags);
        ess_update_ptr(s);
        val = s->dma_dac.count;
        spin_unlock_irqrestore(&s->lock, flags);
        return put_user(val, (int *)arg);

    case SNDCTL_DSP_GETIPTR:
        if (!(file->f_mode & FMODE_READ))
            return -EINVAL;
        spin_lock_irqsave(&s->lock, flags);
        ess_update_ptr(s);
        cinfo.bytes = s->dma_adc.total_bytes;
        cinfo.blocks = s->dma_adc.count >> s->dma_adc.fragshift;
        cinfo.ptr = s->dma_adc.hwptr;
        if (s->dma_adc.mapped)
            s->dma_adc.count &= s->dma_adc.fragsize-1;
        spin_unlock_irqrestore(&s->lock, flags);
        return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

    case SNDCTL_DSP_GETOPTR:
        if (!(file->f_mode & FMODE_WRITE))
            return -EINVAL;
        spin_lock_irqsave(&s->lock, flags);
        ess_update_ptr(s);
        cinfo.bytes = s->dma_dac.total_bytes;
        cinfo.blocks = s->dma_dac.count >> s->dma_dac.fragshift;
        cinfo.ptr = s->dma_dac.hwptr;
        if (s->dma_dac.mapped)
            s->dma_dac.count &= s->dma_dac.fragsize-1;
        spin_unlock_irqrestore(&s->lock, flags);
        return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

    case SNDCTL_DSP_GETBLKSIZE:
        if (file->f_mode & FMODE_WRITE) {
            if ((val = prog_dmabuf(s, 0)))
                return val;
            return put_user(s->dma_dac.fragsize, (int *)arg);
        }
        if ((val = prog_dmabuf(s, 1)))
            return val;
        return put_user(s->dma_adc.fragsize, (int *)arg);

    case SNDCTL_DSP_SETFRAGMENT:
        get_user_ret(val, (int *)arg, -EFAULT);
        spin_lock_irqsave(&s->lock, flags);
        if (file->f_mode & FMODE_READ) {
            s->dma_adc.ossfragshift = val & 0xffff;
            s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
            if (s->dma_adc.ossfragshift < 4)
                s->dma_adc.ossfragshift = 4;
            if (s->dma_adc.ossfragshift > 15)
                s->dma_adc.ossfragshift = 15;
            if (s->dma_adc.ossmaxfrags < 4)
                s->dma_adc.ossmaxfrags = 4;
        }
        if (file->f_mode & FMODE_WRITE) {
            s->dma_dac.ossfragshift = val & 0xffff;
            s->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
            if (s->dma_dac.ossfragshift < 4)
                s->dma_dac.ossfragshift = 4;
            if (s->dma_dac.ossfragshift > 15)
                s->dma_dac.ossfragshift = 15;
            if (s->dma_dac.ossmaxfrags < 4)
                s->dma_dac.ossmaxfrags = 4;
        }
        spin_unlock_irqrestore(&s->lock, flags);
        return 0;

    case SNDCTL_DSP_SUBDIVIDE:
        if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
            (file->f_mode & FMODE_WRITE && s->dma_dac.subdivision))
            return -EINVAL;

        get_user_ret(val, (int *)arg, -EFAULT);

        if (val != 1 && val != 2 && val != 4)
            return -EINVAL;
        if (file->f_mode & FMODE_READ)
            s->dma_adc.subdivision = val;
        if (file->f_mode & FMODE_WRITE)
            s->dma_dac.subdivision = val;
        return 0;

    case SOUND_PCM_READ_RATE:
        return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);

    case SOUND_PCM_READ_CHANNELS:
        return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_FMT_STEREO << ESS_ADC_SHIFT) 
                       : (ESS_FMT_STEREO << ESS_DAC_SHIFT))) ? 2 : 1, (int *)arg);

    case SOUND_PCM_READ_BITS:
        return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_FMT_16BIT << ESS_ADC_SHIFT) 
                       : (ESS_FMT_16BIT << ESS_DAC_SHIFT))) ? 16 : 8, (int *)arg);

    case SOUND_PCM_WRITE_FILTER:
    case SNDCTL_DSP_SETSYNCRO:
    case SOUND_PCM_READ_FILTER:
        return -EINVAL;
    }
    return -EINVAL;
}

static int
allocate_dmabuf(struct dmabuf *db)
{
    int order;
    unsigned long mapend,map;
    void *rawbuf = NULL;

    DPRINTK(DPSTR,"allocating for dmabuf %p\n", db);

    /* 
     * alloc as big a chunk as we can, start with 
     * 64k 'cause we're insane.
     */
    for (order = 16-PAGE_SHIFT; order >= 1; order--)
        /* XXX might be able to get rid of gfp_dma */
        if((rawbuf = (void *)__get_free_pages(GFP_KERNEL|GFP_DMA, order)))
            break;

    if (!rawbuf)
        return 1;

    /*
     * can't cross a 64k boundry..
     */
    if( ((virt_to_bus(rawbuf) & 0xffff) + ((PAGE_SIZE << order) - 1)) > ~0xffff) {
        printk(KERN_ERR PFX "DMA buffer crosses 64k: busaddr 0x%lx  size %ld\n",
            virt_to_bus(rawbuf), PAGE_SIZE << order);
        kfree(rawbuf);
        free_pages((unsigned long)db->rawbuf,db->buforder);
        return 1;
    }

    DPRINTK(DPSTR,"allocated %ld (%d) bytes at %p\n",
            PAGE_SIZE<<order, order, rawbuf);

    /* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
    mapend = MAP_NR(rawbuf + (PAGE_SIZE << order) - 1);
    for (map = MAP_NR(rawbuf); map <= mapend; map++) {
        set_bit(PG_reserved, &mem_map[map].flags);
    }

    db->rawbuf = rawbuf;
    db->buforder = order;
    db->ready = 0;
    db->mapped = 0;

    return 0;
}

static void
nuke_lists(struct ess_card *card, struct dmabuf *db)
{
    m3_remove_list(card, &(card->dma_list), db->dma_index);
    m3_remove_list(card, &(card->msrc_list), db->msrc_index);
    db->in_lists = 0;
}

static void
free_dmabuf(struct dmabuf *db)
{
    unsigned long map, mapend;

    DPRINTK(DPSTR,"freeing %p from dmabuf %p\n",db->rawbuf, db);

    mapend = MAP_NR(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
    for (map = MAP_NR(db->rawbuf); map <= mapend; map++)
        clear_bit(PG_reserved, &mem_map[map].flags);    

    free_pages((unsigned long)db->rawbuf,db->buforder);

    db->rawbuf = NULL;
    db->buforder = 0;
    db->mapped = 0;
    db->ready = 0;
}

static int 
ess_open(struct inode *inode, struct file *file)
{
    int minor = MINOR(inode->i_rdev);
    struct ess_card *c;
    struct ess_state *s = NULL;
    int i;
    int ret = 0;
    unsigned char fmtm = ~0, fmts = 0;
    unsigned long flags;

    /*
     *    Scan the cards and find the channel. We only
     *    do this at open time so it is ok
     */
    for(c = devs ; c != NULL ; c = c->next) {

        for(i=0;i<NR_DSPS;i++) {

            if(c->channels[i].dev_audio < 0)
                continue;
            if((c->channels[i].dev_audio ^ minor) & ~0xf)
                continue;

            s = &c->channels[i];
            break;
        }
    }
        
    if (!s)
        return -ENODEV;
        
    VALIDATE_STATE(s);

    file->private_data = s;

    /* wait for device to become free */
    down(&s->open_sem);
    while (s->open_mode & file->f_mode) {
        if (file->f_flags & O_NONBLOCK) {
            up(&s->open_sem);
            return -EWOULDBLOCK;
        }
        up(&s->open_sem);
        interruptible_sleep_on(&s->open_wait);
        if (signal_pending(current))
            return -ERESTARTSYS;
        down(&s->open_sem);
    }

    spin_lock_irqsave(&s->lock, flags);

    if (file->f_mode & FMODE_READ) {
        if(allocate_dmabuf(&(s->dma_adc)))  {
            ret = -ENOMEM;
            goto out;
        }
        fmtm &= ~((ESS_FMT_STEREO | ESS_FMT_16BIT) << ESS_ADC_SHIFT);
        if ((minor & 0xf) == SND_DEV_DSP16)
            fmts |= ESS_FMT_16BIT << ESS_ADC_SHIFT; 

        s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
        set_adc_rate(s, 8000);
    }
    if (file->f_mode & FMODE_WRITE) {
        if(allocate_dmabuf(&(s->dma_dac)))  {
            ret = -ENOMEM;
            goto out;
        }

        fmtm &= ~((ESS_FMT_STEREO | ESS_FMT_16BIT) << ESS_DAC_SHIFT);
        if ((minor & 0xf) == SND_DEV_DSP16)
            fmts |= ESS_FMT_16BIT << ESS_DAC_SHIFT;

        s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags = s->dma_dac.subdivision = 0;
        set_dac_rate(s, 8000);
    }
    set_fmt(s, fmtm, fmts);
    s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);

    MOD_INC_USE_COUNT;
out:
    spin_unlock_irqrestore(&s->lock, flags);
    up(&s->open_sem);
    return ret;
}

static int 
ess_release(struct inode *inode, struct file *file)
{
    struct ess_state *s = (struct ess_state *)file->private_data;
    unsigned long flags;

    VALIDATE_STATE(s);
    if (file->f_mode & FMODE_WRITE)
        drain_dac(s, file->f_flags & O_NONBLOCK);

    down(&s->open_sem);
    spin_lock_irqsave(&s->lock, flags);

    if (file->f_mode & FMODE_WRITE) {
        stop_dac(s);
        if(s->dma_dac.in_lists) {
            m3_remove_list(s->card, &(s->card->mixer_list), s->dma_dac.mixer_index);
            nuke_lists(s->card, &(s->dma_dac));
        }
        free_dmabuf(&(s->dma_dac));
    }
    if (file->f_mode & FMODE_READ) {
        stop_adc(s);
        if(s->dma_adc.in_lists) {
            m3_remove_list(s->card, &(s->card->adc1_list), s->dma_adc.adc1_index);
            nuke_lists(s->card, &(s->dma_adc));
        }
        free_dmabuf(&(s->dma_adc));
    }
        
    s->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);

    spin_unlock_irqrestore(&s->lock, flags);
    up(&s->open_sem);
    wake_up(&s->open_wait);

    MOD_DEC_USE_COUNT;
    return 0;
}

/*
 * Wait for the ac97 serial bus to be free.
 * return nonzero if the bus is still busy.
 */
static int m3_ac97_wait(struct ess_card *card)
{
    int i = 10000;

    while( (m3_inb(card, 0x30) & 1) && i--) ;

    return i == 0;
}

u16 m3_ac97_read(struct ac97_codec *codec, u8 reg)
{
    u16 ret = 0;
    struct ess_card *card = codec->private_data;

    spin_lock(&card->ac97_lock);

    if(m3_ac97_wait(card)) {
        printk(KERN_ERR PFX "serial bus busy reading reg 0x%x\n",reg);
        goto out;
    }

    m3_outb(card, 0x80 | (reg & 0x7f), 0x30);

    if(m3_ac97_wait(card)) {
        printk(KERN_ERR PFX "serial bus busy finishing read reg 0x%x\n",reg);
        goto out;
    }

    ret =  m3_inw(card, 0x32);
    DPRINTK(DPCRAP,"reading 0x%04x from 0x%02x\n",ret, reg);

out:
    spin_unlock(&card->ac97_lock);
    return ret;
}

void m3_ac97_write(struct ac97_codec *codec, u8 reg, u16 val)
{
    struct ess_card *card = codec->private_data;

    spin_lock(&card->ac97_lock);

    if(m3_ac97_wait(card)) {
        printk(KERN_ERR PFX "serial bus busy writing 0x%x to 0x%x\n",val, reg);
        goto out;
    }
    DPRINTK(DPCRAP,"writing 0x%04x  to  0x%02x\n", val, reg);

    m3_outw(card, val, 0x32);
    m3_outb(card, reg & 0x7f, 0x30);
out:
    spin_unlock(&card->ac97_lock);
}
/* OSS /dev/mixer file operation methods */
static int m3_open_mixdev(struct inode *inode, struct file *file)
{
    int minor = MINOR(inode->i_rdev);
    struct ess_card *card = devs;

    MOD_INC_USE_COUNT;
    for (card = devs; card != NULL; card = card->next) {
        if((card->ac97 != NULL) && (card->ac97->dev_mixer == minor))
                break;
    }

    if (!card) {
        MOD_DEC_USE_COUNT;
        return -ENODEV;
    }

    file->private_data = card->ac97;

    return 0;
}

static int m3_release_mixdev(struct inode *inode, struct file *file)
{
    MOD_DEC_USE_COUNT;
    return 0;
}

static int m3_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
                                    unsigned long arg)
{
    struct ac97_codec *codec = (struct ac97_codec *)file->private_data;

    return codec->mixer_ioctl(codec, cmd, arg);
}

static /*const*/ struct file_operations m3_mixer_fops = {
llseek:         m3_llseek,
ioctl:          m3_ioctl_mixdev,
open:           m3_open_mixdev,
release:        m3_release_mixdev,
};

void remote_codec_config(int io, int isremote)
{
    isremote = isremote ? 1 : 0;

    outw(  (inw(io + RING_BUS_CTRL_B) & ~SECOND_CODEC_ID_MASK) | isremote,
            io + RING_BUS_CTRL_B);
    outw(  (inw(io + SDO_OUT_DEST_CTRL) & ~COMMAND_ADDR_OUT) | isremote,
            io + SDO_OUT_DEST_CTRL);
    outw(  (inw(io + SDO_IN_DEST_CTRL) & ~STATUS_ADDR_IN) | isremote,
            io + SDO_IN_DEST_CTRL);
}

/* 
 * hack, returns non zero on err 
 */
static int try_read_vendor(struct ess_card *card)
{
    u16 ret;

    if(m3_ac97_wait(card)) 
        return 1;

    m3_outb(card, 0x80 | (AC97_VENDOR_ID1 & 0x7f), 0x30);

    if(m3_ac97_wait(card)) 
        return 1;

    ret =  m3_inw(card, 0x32);

    return (ret == 0) || (ret == 0xffff);
}

static void m3_codec_reset(struct ess_card *card, int busywait)
{
    u16 dir;
    int delay1 = 0, delay2 = 0, i;
    int io = card->iobase;

    switch (card->card_type) {
        /*
         * the onboard codec on the allegro seems 
         * to want to wait a very long time before
         * coming back to life 
         */
        case ESS_ALLEGRO:
            delay1 = 50;
            delay2 = 800;
        break;
        case ESS_MAESTRO3:
            delay1 = 20;
            delay2 = 500;
        break;
    }

    for(i = 0; i < 5; i ++) {
        dir = inw(io + GPIO_DIRECTION);
        dir |= 0x10; /* assuming pci bus master? */

        remote_codec_config(io, 0);

        outw(IO_SRAM_ENABLE, io + RING_BUS_CTRL_A);
        udelay(20);

        outw(dir & ~GPO_PRIMARY_AC97 , io + GPIO_DIRECTION);
        outw(~GPO_PRIMARY_AC97 , io + GPIO_MASK);
        outw(0, io + GPIO_DATA);
        outw(dir | GPO_PRIMARY_AC97, io + GPIO_DIRECTION);

        if(busywait)  {
            mdelay(delay1);
        } else {
            current->state = TASK_UNINTERRUPTIBLE;
            schedule_timeout((delay1 * HZ) / 1000);
        }

        outw(GPO_PRIMARY_AC97, io + GPIO_DATA);
        udelay(5);
        /* ok, bring back the ac-link */
        outw(IO_SRAM_ENABLE | SERIAL_AC_LINK_ENABLE, io + RING_BUS_CTRL_A);
        outw(~0, io + GPIO_MASK);

        if(busywait) {
            mdelay(delay2);
        } else {
            current->state = TASK_UNINTERRUPTIBLE;
            schedule_timeout((delay2 * HZ) / 1000);
        }
        if(! try_read_vendor(card))
            break;

        delay1 += 10;
        delay2 += 100;

        DPRINTK(DPMOD, "retrying codec reset with delays of %d and %d ms\n",
                delay1, delay2);
    }

#if 0
    /* more gung-ho reset that doesn't
     * seem to work anywhere :)
     */
    tmp = inw(io + RING_BUS_CTRL_A);
    outw(RAC_SDFS_ENABLE|LAC_SDFS_ENABLE, io + RING_BUS_CTRL_A);
    mdelay(20);
    outw(tmp, io + RING_BUS_CTRL_A);
    mdelay(50);
#endif
}

static int __init m3_codec_install(struct ess_card *card)
{
    struct ac97_codec *codec;

    if ((codec = kmalloc(sizeof(struct ac97_codec), GFP_KERNEL)) == NULL)
        return -ENOMEM;
    memset(codec, 0, sizeof(struct ac97_codec));

    codec->private_data = card;
    codec->codec_read = m3_ac97_read;
    codec->codec_write = m3_ac97_write;

    if (ac97_probe_codec(codec) == 0) {
        printk(KERN_ERR PFX "codec probe failed\n");
        kfree(codec);
        return -1;
    }

    if ((codec->dev_mixer = register_sound_mixer(&m3_mixer_fops, -1)) < 0) {
        printk(KERN_ERR PFX "couldn't register mixer!\n");
        kfree(codec);
        return -1;
    }

    card->ac97 = codec;

    return 0;
}


#define MINISRC_LPF_LEN 10
static u16 minisrc_lpf[MINISRC_LPF_LEN] = {
    0X0743, 0X1104, 0X0A4C, 0XF88D, 0X242C,
    0X1023, 0X1AA9, 0X0B60, 0XEFDD, 0X186F
};
static void m3_assp_init(struct ess_card *card)
{
    int i;

    /* zero kernel data */
    for(i = 0 ; i < (REV_B_DATA_MEMORY_UNIT_LENGTH * NUM_UNITS_KERNEL_DATA) / 2; i++)
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
                KDATA_BASE_ADDR + i, 0);

    /* zero mixer data? */
    for(i = 0 ; i < (REV_B_DATA_MEMORY_UNIT_LENGTH * NUM_UNITS_KERNEL_DATA) / 2; i++)
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
                KDATA_BASE_ADDR2 + i, 0);

    /* init dma pointer */
    m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
            KDATA_CURRENT_DMA, 
            KDATA_DMA_XFER0);

    /* write kernel into code memory.. */
    for(i = 0 ; i < sizeof(assp_kernel_image) / 2; i++) {
        m3_assp_write(card, MEMTYPE_INTERNAL_CODE, 
                REV_B_CODE_MEMORY_BEGIN + i, 
                assp_kernel_image[i]);
    }

    /*
     * We only have this one client and we know that 0x400
     * is free in our kernel's mem map, so lets just
     * drop it there.  It seems that the minisrc doesn't
     * need vectors, so we won't bother with them..
     */
    for(i = 0 ; i < sizeof(assp_minisrc_image) / 2; i++) {
        m3_assp_write(card, MEMTYPE_INTERNAL_CODE, 
                0x400 + i, 
                assp_minisrc_image[i]);
    }

    /*
     * write the coefficients for the low pass filter?
     */
    for(i = 0; i < MINISRC_LPF_LEN ; i++) {
        m3_assp_write(card, MEMTYPE_INTERNAL_CODE,
            0x400 + MINISRC_COEF_LOC + i,
            minisrc_lpf[i]);
    }

    m3_assp_write(card, MEMTYPE_INTERNAL_CODE,
        0x400 + MINISRC_COEF_LOC + MINISRC_LPF_LEN,
        0x8000);

    /*
     * the minisrc is the only thing on
     * our task list..
     */
    m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
            KDATA_TASK0, 
            0x400);

    /*
     * init the mixer number..
     */

    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
            KDATA_MIXER_TASK_NUMBER,0);

    /*
     * EXTREME KERNEL MASTER VOLUME
     */
    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_DAC_LEFT_VOLUME, ARB_VOLUME);
    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_DAC_RIGHT_VOLUME, ARB_VOLUME);

    card->mixer_list.mem_addr = KDATA_MIXER_XFER0;
    card->mixer_list.max = MAX_VIRTUAL_MIXER_CHANNELS;
    card->adc1_list.mem_addr = KDATA_ADC1_XFER0;
    card->adc1_list.max = MAX_VIRTUAL_ADC1_CHANNELS;
    card->dma_list.mem_addr = KDATA_DMA_XFER0;
    card->dma_list.max = MAX_VIRTUAL_DMA_CHANNELS;
    card->msrc_list.mem_addr = KDATA_INSTANCE0_MINISRC;
    card->msrc_list.max = MAX_INSTANCE_MINISRC;
}

static int setup_msrc(struct ess_card *card,
        struct assp_instance *inst, int index)
{
    int data_bytes = 2 * ( MINISRC_TMP_BUFFER_SIZE / 2 + 
            MINISRC_IN_BUFFER_SIZE / 2 +
            1 + MINISRC_OUT_BUFFER_SIZE / 2 + 1 );
    int address, i;

    /*
     * the revb memory map has 0x1100 through 0x1c00
     * free.  
     */

    /*
     * align instance mem so that the shifted list
     * addresses are aligned.
     */
    data_bytes = (data_bytes + 255) & ~255;
    address = 0x1100 + ((data_bytes/2) * index);

    if((address + (data_bytes/2)) >= 0x1c00) {
        printk(KERN_ERR PFX "no memory for %d bytes at ind %d (addr 0x%x)\n",
                data_bytes, index, address);
        return -1;
    }

    for(i = 0; i < data_bytes/2 ; i++) 
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
                address + i, 0);

    inst->code = 0x400;
    inst->data = address;

    return 0;
}

static int m3_assp_client_init(struct ess_state *s)
{
    setup_msrc(s->card, &(s->dac_inst), s->index * 2);
    setup_msrc(s->card, &(s->adc_inst), (s->index * 2) + 1);

    return 0;
}

static void
m3_amp_enable(struct ess_card *card, int enable)
{
    /* 
     * this works for the reference board, have to find
     * out about others
     *
     * this needs more magic for 4 speaker, but..
     */
    int io = card->iobase;
    u16 gpo, polarity_port, polarity;

    if(!external_amp)
        return;

    switch (card->card_type) {
        case ESS_ALLEGRO:
            polarity_port = 0x1800;
            break;
        default:
            /* presumably this is for all 'maestro3's.. */
            polarity_port = 0x1100;
            break;
    }

    gpo = (polarity_port >> 8) & 0x0F;
    polarity = polarity_port >> 12;
    if ( enable )
        polarity = !polarity;
    polarity = polarity << gpo;
    gpo = 1 << gpo;

    outw(~gpo , io + GPIO_MASK);

    outw( inw(io + GPIO_DIRECTION) | gpo ,
            io + GPIO_DIRECTION);

    outw( (GPO_SECONDARY_AC97 | GPO_PRIMARY_AC97 | polarity) ,
            io + GPIO_DATA);

    outw(0xffff , io + GPIO_MASK);
}

static int
maestro_config(struct ess_card *card) 
{
    struct pci_dev *pcidev = card->pcidev;
    u32 n;
    u8  t; /* makes as much sense as 'n', no? */

    pci_read_config_dword(pcidev, PCI_ALLEGRO_CONFIG, &n);
    n &= REDUCED_DEBOUNCE;
    n |= PM_CTRL_ENABLE | CLK_DIV_BY_49 | USE_PCI_TIMING;
    pci_write_config_dword(pcidev, PCI_ALLEGRO_CONFIG, n);

    outb(RESET_ASSP, card->iobase + ASSP_CONTROL_B);
    pci_read_config_dword(pcidev, PCI_ALLEGRO_CONFIG, &n);
    n &= ~INT_CLK_SELECT;
    if(card->card_type == ESS_MAESTRO3)  {
        n &= ~INT_CLK_MULT_ENABLE; 
        n |= INT_CLK_SRC_NOT_PCI;
    }
    n &=  ~( CLK_MULT_MODE_SELECT | CLK_MULT_MODE_SELECT_2 );
    pci_write_config_dword(pcidev, PCI_ALLEGRO_CONFIG, n);

    if(card->card_type <= ESS_ALLEGRO) {
        pci_read_config_dword(pcidev, PCI_USER_CONFIG, &n);
        n |= IN_CLK_12MHZ_SELECT;
        pci_write_config_dword(pcidev, PCI_USER_CONFIG, n);
    }

    t = inb(card->iobase + ASSP_CONTROL_A);
    t &= ~( DSP_CLK_36MHZ_SELECT  | ASSP_CLK_49MHZ_SELECT);
    switch(global_dsp_speed) {
        case 33:
            break;
        case 36:
            t |= DSP_CLK_36MHZ_SELECT;
            break;
        case 49:
            t |= ASSP_CLK_49MHZ_SELECT;
            break;
    }
    t |= ASSP_0_WS_ENABLE; 
    outb(t, card->iobase + ASSP_CONTROL_A);

    outb(RUN_ASSP, card->iobase + ASSP_CONTROL_B); 

    return 0;
} 

static void
m3_enable_ints(struct ess_card *card)
{
    unsigned long io = card->iobase;

    outw(ASSP_INT_ENABLE, io + HOST_INT_CTRL);
    outb(inb(io + ASSP_CONTROL_C) | ASSP_HOST_INT_ENABLE,
            io + ASSP_CONTROL_C);
}

static struct file_operations ess_audio_fops = {
    &m3_llseek,
    &ess_read,
    &ess_write,
    NULL,  /* readdir */
    &ess_poll,
    &ess_ioctl,
    &ess_mmap,
    &ess_open,
    NULL,    /* flush */
    &ess_release,
    NULL,  /* fsync */
    NULL,  /* fasync */
    NULL,  /* check_media_change */
    NULL,  /* revalidate */
    NULL,  /* lock */
};

#ifdef CONFIG_APM
int alloc_dsp_savemem(struct ess_card *card)
{
    int len = sizeof(u16) * (REV_B_CODE_MEMORY_LENGTH + REV_B_DATA_MEMORY_LENGTH);

    if( (card->suspend_dsp_mem = vmalloc(len)) == NULL)
        return 1;

    return 0;
}
void free_dsp_savemem(struct ess_card *card)
{
   if(card->suspend_dsp_mem)
       vfree(card->suspend_dsp_mem);
}

#else
#define alloc_dsp_savemem(args...) 0
#define free_dsp_savemem(args...) 
#endif

/*
 * great day!  this function is ugly as hell.
 */
static int 
maestro_install(struct pci_dev *pcidev, struct card_type *ct)
{
    u32 n;
    int i;
    int iobase;
    struct ess_card *card = NULL;
    int num = 0;

    /* don't pick up modems */
    if(((pcidev->class >> 8) & 0xffff) != PCI_CLASS_MULTIMEDIA_AUDIO)
        return 1;

    DPRINTK(DPMOD, "in maestro_install\n");

    iobase = SILLY_PCI_BASE_ADDRESS(pcidev); 

    if(check_region(iobase, 256))
    {
        printk(KERN_WARNING PFX "can't allocate 256 bytes I/O at 0x%4.4x\n", iobase);
        return 1;
    }
    /* stake our claim on the iospace */
    request_region(iobase, 256, ct->name);

    /* this was tripping up some machines */
    if(pcidev->irq == 0) 
        printk(KERN_WARNING PFX "pci subsystem reports irq 0, this might not be correct.\n");

    /* just to be sure */
    pci_set_master(pcidev);

    card = kmalloc(sizeof(struct ess_card), GFP_KERNEL);
    if(card == NULL)
    {
        printk(KERN_WARNING PFX "out of memory\n");
        release_region(card->iobase, 256);        
        return 1;
    }

    memset(card, 0, sizeof(*card));
    card->pcidev = pcidev;

    if(alloc_dsp_savemem(card)) {
        printk(KERN_WARNING PFX "couldn't alloc %d bytes for saving dsp state on suspend\n",
                REV_B_CODE_MEMORY_LENGTH + REV_B_DATA_MEMORY_LENGTH);
        release_region(card->iobase, 256);
        free_dsp_savemem(card);
        kfree(card);
        return 1;
    }

    if (register_reboot_notifier(&maestro_nb)) {
        printk(KERN_WARNING PFX "reboot notifier registration failed\n");
        release_region(card->iobase, 256);        
        free_dsp_savemem(card);
        kfree(card);
        return 1;
    }

    card->iobase = iobase;
    card->card_type = ct->type;
    card->irq = pcidev->irq;
    card->next = devs;
    card->magic = M3_CARD_MAGIC;
    spin_lock_init(&card->lock);
    spin_lock_init(&card->ac97_lock);
    devs = card;

    printk(KERN_INFO PFX "Configuring %s found at IO 0x%04X IRQ %d\n", 
        ct->name,iobase,card->irq);
    pci_read_config_dword(pcidev, PCI_SUBSYSTEM_VENDOR_ID, &n);
    printk(KERN_INFO PFX " subvendor id: 0x%08x\n",n); 

    maestro_config(card);
    m3_assp_halt(card);

    m3_codec_reset(card, 0);

    if(m3_codec_install(card))  {
        unregister_reboot_notifier(&maestro_nb);
        release_region(card->iobase, 256);        
        free_dsp_savemem(card);
        kfree(card);
        return 1;
    }

    m3_assp_init(card);
    m3_amp_enable(card, 1);
    
    for(i=0;i<NR_DSPS;i++)
    {
        struct ess_state *s=&card->channels[i];

        s->index = i;

        s->card = card;
        init_waitqueue_head(&s->dma_adc.wait);
        init_waitqueue_head(&s->dma_dac.wait);
        init_waitqueue_head(&s->open_wait);
        spin_lock_init(&s->lock);
        SILLY_INIT_SEM(s->open_sem);
        s->magic = M3_STATE_MAGIC;

        m3_assp_client_init(s);
        
        if(s->dma_adc.ready || s->dma_dac.ready || s->dma_adc.rawbuf)
            printk("maestro: BOTCH!\n");
        /* register devices */
        if ((s->dev_audio = register_sound_dsp(&ess_audio_fops, -1)) < 0)
            break;
    }
    
    num = i;
    
    /* clear the rest if we ran out of slots to register */
    for(;i<NR_DSPS;i++)
    {
        struct ess_state *s=&card->channels[i];
        s->dev_audio = -1;
    }

    
    if(request_irq(card->irq, ess_interrupt, SA_SHIRQ, ct->name, card))
    {
        printk(KERN_ERR PFX "unable to allocate irq %d,\n", card->irq);
        for(i=0;i<NR_DSPS;i++)
        {
            struct ess_state *s = &card->channels[i];
            if(s->dev_audio != -1)
                unregister_sound_dsp(s->dev_audio);
        }
        unregister_reboot_notifier(&maestro_nb);
        unregister_sound_mixer(card->ac97->dev_mixer);
        kfree(card->ac97);
        release_region(card->iobase, 256);        
        free_dsp_savemem(card);
        kfree(card);
        return 1;
    }

#ifdef CONFIG_APM
    if (apm_register_callback(m3_apm_callback)) {
        printk(KERN_WARNING PFX "couldn't register apm callback, suspend/resume might not work.\n");
    }
#endif

    m3_enable_ints(card);
    m3_assp_continue(card);

    printk(KERN_INFO PFX "%d channels configured.\n", num);

    return 0; 
}

#ifdef MODULE
int init_module(void)
#else
SILLY_MAKE_INIT(int init_maestro3(void))
#endif
{
    struct pci_dev *pcidev = NULL;
    int found = 0;
    int i;

    if (!pci_present())   /* No PCI bus in this machine! */
        return -ENODEV;

    printk(KERN_INFO PFX "version " DRIVER_VERSION " built at " __TIME__ " " __DATE__ "\n");

    switch(global_dsp_speed) {
        case 33: case 36: case 49:
            break;
        default:
            printk(KERN_INFO PFX "invalid global_dsp_speed: %d, must be 33, 36, or 49.\n",
                    global_dsp_speed);
            return -EINVAL;
            break;
    }

#ifdef CONFIG_APM
    init_waitqueue_head(&suspend_queue);
#endif

    for(i = 0; i < NUM_CARD_TYPES; i++) {
        struct card_type *ct = &m3_card_types[i];

        pcidev = NULL;
        while( (pcidev = pci_find_device(PCI_VENDOR_ESS, ct->pci_id, pcidev)) != NULL ) {
            if (! maestro_install(pcidev, ct))
                found++;
        }
    }

    printk(KERN_INFO PFX "%d maestros installed.\n",found);

    if(found == 0 ) 
        return -ENODEV;

    return 0;
}

void nuke_maestros(void)
{
    struct ess_card *card;

    unregister_reboot_notifier(&maestro_nb);
#ifdef CONFIG_APM
    apm_unregister_callback(m3_apm_callback);
#endif

    while ((card = devs)) {
        int i;
        devs = devs->next;
    
        free_irq(card->irq, card);
        unregister_sound_mixer(card->ac97->dev_mixer);
        kfree(card->ac97);

        for(i=0;i<NR_DSPS;i++)
        {
            struct ess_state *ess = &card->channels[i];
            if(ess->dev_audio != -1)
                unregister_sound_dsp(ess->dev_audio);
        }

        release_region(card->iobase, 256);
        free_dsp_savemem(card);
        kfree(card);
    }
    devs = NULL;
}

static int maestro_notifier(struct notifier_block *nb, unsigned long event, void *buf)
{
    /* this notifier is called when the kernel is really shut down. */
    DPRINTK(DPMOD,"shutting down\n");
    nuke_maestros();
    return NOTIFY_OK;
}

/* --------------------------------------------------------------------- */

#ifdef MODULE
MODULE_AUTHOR("Zach Brown <zab@zabbo.net>");
MODULE_DESCRIPTION("ESS Maestro3/Allegro Driver");
#ifdef M_DEBUG
MODULE_PARM(debug,"i");
MODULE_PARM(global_dsp_speed,"i");
#endif
MODULE_PARM(external_amp,"i");

void cleanup_module(void) {
    DPRINTK(DPMOD,"unloading\n");
    nuke_maestros();
}

#endif /* MODULE */

#ifdef CONFIG_APM
void
check_suspend(void)
{
    DECLARE_WAITQUEUE(wait, current);

    if(!in_suspend) 
        return;

    in_suspend++;
    add_wait_queue(&suspend_queue, &wait);
    current->state = TASK_UNINTERRUPTIBLE;
    schedule();
    remove_wait_queue(&suspend_queue, &wait);
    current->state = TASK_RUNNING;
}

static int 
m3_suspend(void)
{
    struct ess_card *card;
    unsigned long flags;
    int index;

    save_flags(flags);
    cli();

    for (card = devs; card ; card = card->next) {
        int i;

        DPRINTK(DPMOD, "apm in dev %p\n",card);

        for(i=0;i<NR_DSPS;i++) {
            struct ess_state *s = &card->channels[i];

            if(s->dev_audio == -1)
                continue;

            DPRINTK(DPMOD, "stop_adc/dac() device %d\n",i);
            stop_dac(s);
            stop_adc(s);
        }

        mdelay(10); /* give the assp a chance to idle.. */

        m3_assp_halt(card);

        index = 0;
        DPRINTK(DPMOD, "saving code\n");
        for(i = REV_B_CODE_MEMORY_BEGIN ; i <= REV_B_CODE_MEMORY_END; i++)
            card->suspend_dsp_mem[index++] = 
                m3_assp_read(card, MEMTYPE_INTERNAL_CODE, i);
        DPRINTK(DPMOD, "saving data\n");
        for(i = REV_B_DATA_MEMORY_BEGIN ; i <= REV_B_DATA_MEMORY_END; i++)
            card->suspend_dsp_mem[index++] = 
                m3_assp_read(card, MEMTYPE_INTERNAL_DATA, i);

        DPRINTK(DPMOD, "powering down apci regs\n");
        m3_outw(card, 0xffff, 0x54);
        m3_outw(card, 0xffff, 0x56);
    }
    in_suspend=1;

    restore_flags(flags);

    return 0;
}

static int 
m3_resume(void)
{
    struct ess_card *card;
    unsigned long flags;
    int index;

    save_flags(flags); /* paranoia */
    cli();
    in_suspend=0;

    DPRINTK(DPMOD, "resuming\n");

    /* first lets just bring everything back. .*/
    for (card = devs; card ; card = card->next) {
        int i;

        DPRINTK(DPMOD, "bringing power back on card 0x%p\n",card);
        m3_outw(card, 0, 0x54);
        m3_outw(card, 0, 0x56);

        DPRINTK(DPMOD, "restoring pci configs and reseting codec\n");
        maestro_config(card);
        m3_assp_halt(card);
        m3_codec_reset(card, 1);

        DPRINTK(DPMOD, "restoring dsp code\n");
        index = 0;
        for(i = REV_B_CODE_MEMORY_BEGIN ; i <= REV_B_CODE_MEMORY_END; i++)
            m3_assp_write(card, MEMTYPE_INTERNAL_CODE, i, 
                card->suspend_dsp_mem[index++]);
        for(i = REV_B_DATA_MEMORY_BEGIN ; i <= REV_B_DATA_MEMORY_END; i++)
            m3_assp_write(card, MEMTYPE_INTERNAL_DATA, i, 
                card->suspend_dsp_mem[index++]);

         /* tell the dma engine to restart itself */
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
                KDATA_DMA_ACTIVE, 0);

        DPRINTK(DPMOD, "resuming dsp\n");
        m3_assp_continue(card);

        DPRINTK(DPMOD, "enabling ints\n");
        m3_enable_ints(card);

        /* bring back the old school flavor */
        for(i = 0; i < SOUND_MIXER_NRDEVICES ; i++) {
            int state = card->ac97->mixer_state[i];
            if (!supported_mixer(card->ac97, i)) 
                continue;

            card->ac97->write_mixer(card->ac97, i, 
                    state & 0xff, (state >> 8) & 0xff);
        }

        m3_amp_enable(card, 1);
    }

    /* 
     * now we flip on the music 
     */
    for (card = devs; card ; card = card->next) {
        int i;


        for(i=0;i<NR_DSPS;i++) {
            struct ess_state *s = &card->channels[i];
            if(s->dev_audio == -1)
                continue;
            /*
             * db->ready makes it so these guys can be
             * called unconditionally..
             */
            DPRINTK(DPMOD, "turning on dacs ind %d\n",i);
            start_dac(s);    
            start_adc(s);    
        }
    }

    restore_flags(flags);

    /* 
     * all right, we think things are ready, 
     * wake up people who were using the device 
     * when we suspended
     */
    wake_up(&suspend_queue);

    return 0;
}

int 
m3_apm_callback(apm_event_t ae) {

    DPRINTK(DPMOD, "APM event received: 0x%x\n",ae);

    switch(ae) {
        case APM_SYS_SUSPEND: 
        case APM_CRITICAL_SUSPEND: 
        case APM_USER_SUSPEND: 

            m3_suspend();
            break;

        case APM_NORMAL_RESUME: 
        case APM_CRITICAL_RESUME: 
        case APM_STANDBY_RESUME: 

            m3_resume();
            break;

        default: 
            break;
    }

    return 0;
}
#endif
