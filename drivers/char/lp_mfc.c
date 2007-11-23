/*
 * lp driver for the parallel port of a Multiface Card III
 * 6.11.95 Joerg Dorchain (dorchain@mpi-sb.mpg.de)
 *
 * ported to 2.0.18 and modularised
 * 25.9.96 Joerg Dorchain
 *
 * ported to 2.1.42
 * 24.6.97 Joerg Dorchain (jdorchain@i-con.de)
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/lp_m68k.h>
#include <linux/lp_mfc.h>
#include <linux/mc6821.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/amigaints.h>
#include "multiface.h"

static void lp_mfc_out(int,int);
static int lp_mfc_busy(int);
static int lp_mfc_pout(int);
static int lp_mfc_online(int);
static void lp_mfc_interrupt(int, void *, struct pt_regs *);

static inline struct pia *pia(int);

static volatile int dummy; /* for trigger reads */
static int minor[MAX_LP] = { -1, -1, -1, -1, -1 };
MODULE_PARM(minor,"1-" __MODULE_STRING(MAX_LP) "i");
#ifdef MODULE
static unsigned int board_key[MAX_LP];
#endif

static void lp_mfc_out(int c, int dev)
{
int wait = 0;

while (wait != lp_table[dev]->wait) wait++;
dummy = pia(dev)->pprb; /* trigger read clears irq bit*/
pia(dev)->pprb = c;  /* strobe goes down by hardware */
}

static int lp_mfc_busy(int dev)
{
return pia(dev)->ppra&1;
}

static int lp_mfc_pout(int dev)
{
return pia(dev)->ppra&2;
}

static int lp_mfc_online(int dev)
{
return pia(dev)->ppra&4;
}

static void lp_mfc_interrupt(int irq,void *data,struct pt_regs *fp)
{
int i;

for( i=0; i<MAX_LP; i++)  /* test all lp's */
	if (minor[i] != -1)  /* one of ours ? */
		if (pia(minor[i])->crb&128) { /* has an irq? */
			dummy = pia(minor[i])->pprb; /* clear bit */
			lp_interrupt(minor[i]);
		}
}

static inline struct pia *pia(int dev)
{
return lp_table[dev]->base;
}

static int lp_mfc_open(int dev)
{
MOD_INC_USE_COUNT;
return 0;
}

static void lp_mfc_release(int dev)
{
MOD_DEC_USE_COUNT;
}

static struct lp_struct tab[MAX_LP] = {{0,},};

__initfunc(int lp_mfc_init(void))
{
int pias;
struct pia *pp;
unsigned int key = 0;
const struct ConfigDev *cd;

if (!MACH_IS_AMIGA)
  return -ENODEV;

pias = 0;
while((key = zorro_find(ZORRO_PROD_BSC_MULTIFACE_III, 0 , key))) {
  cd = zorro_get_board( key );
  pp = (struct pia *)ZTWO_VADDR((((u_char *)cd->cd_BoardAddr)+PIABASE));
  if (pias < MAX_LP) {
	pp->crb = 0;
	pp->pddrb = 255;    /* all pins output */
	pp->crb = PIA_DDR|32|8;
	dummy = pp->pprb;
	pp->crb |=(lp_irq!=0)?PIA_C1_ENABLE_IRQ:0; 
	pp->cra = 0;
	pp->pddra = 0xe0;   /* /RESET,  /DIR ,/AUTO-FEED output */
	pp->cra = PIA_DDR;
	pp->ppra = 0;    /* reset printer */
	udelay(5);
	pp->ppra |= 128;
	tab[pias].name="Multiface III LP";
	tab[pias].lp_out=lp_mfc_out;
	tab[pias].lp_is_busy=lp_mfc_busy;
	tab[pias].lp_has_pout=lp_mfc_pout;
	tab[pias].lp_is_online=lp_mfc_online;
	tab[pias].lp_ioctl=NULL;
	tab[pias].lp_open=lp_mfc_open;
	tab[pias].lp_release=lp_mfc_release;
	tab[pias].flags=LP_EXIST;
	tab[pias].chars=LP_INIT_CHAR;
	tab[pias].time=LP_INIT_TIME;
	tab[pias].wait=LP_INIT_WAIT;
	tab[pias].lp_wait_q=NULL;
	tab[pias].base=pp;
	tab[pias].type=LP_MFC;
	if ((minor[pias] = register_parallel(tab + pias, minor[pias] )) >= 0) {
	  zorro_config_board( key, 0 );
#ifdef MODULE
	  board_key[minor[pias]] = key;
#endif
	  pias++;
	}
	else
	  printk("mfc_init: cant get a minor for pia at 0x%08lx\n",(long)pp);
  }
}
if ((pias != 0) && (lp_irq != 0))
  request_irq(IRQ_AMIGA_PORTS, lp_mfc_interrupt, 0,
    "Multiface III printer", lp_mfc_interrupt);

return (pias==0)?-ENODEV:0;
}

#ifdef MODULE
int init_module(void)
{
return lp_mfc_init();
}

void cleanup_module(void)
{
int i;

if (lp_irq)
  free_irq(IRQ_AMIGA_PORTS, lp_mfc_interrupt);
for(i = 0; i < MAX_LP; i++)
  if ((lp_table[i] != NULL) && (lp_table[i]->type == LP_MFC)) {
    unregister_parallel(i);
    zorro_unconfig_board(board_key[i], 0);
  }
}
#endif
