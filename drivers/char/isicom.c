/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Original driver code supplied by Multi-Tech
 *
 *	Changes
 *	1/9/98	alan@redhat.com		Merge to 2.0.x kernel tree
 *					Obtain and use official major/minors
 *					Loader switched to a misc device
 *					(fixed range check bug as a side effect)
 *					Printk clean up
 *	9/12/98	alan@redhat.com		Rough port to 2.1.x
 *
 *	10/6/99 sameer			Merged the ISA and PCI drivers to
 *					a new unified driver.
 *
 *	3/9/99	sameer			Added support for ISI4616 cards.
 *
 *	16/9/99	sameer			We do not force RTS low anymore.
 *					This is to prevent the firmware 
 *					from getting confused.
 *
 *	26/10/99 sameer			Cosmetic changes:The driver now
 *					dumps the Port Count information
 *					along with I/O address and IRQ.
 *
 *	13/12/99 sameer			Fixed the problem with IRQ sharing.
 *
 *	10/5/00  sameer			Fixed isicom_shutdown_board()
 *					to not lower DTR on all the ports
 *					when the last port on the card is 
 *					closed.
 *
 *	10/5/00  sameer			Signal mask setup command added
 *					to  isicom_setup_port and 
 *					isicom_shutdown_port.
 *
 *	24/5/00 sameer			The driver is now SMP aware.
 *					
 *
 *	***********************************************************
 *
 *	To use this driver you also need the support package. You 
 *	can find this in RPM format on
 *		ftp://ftp.linux.org.uk/pub/linux/alan
 * 	
 *	You can find the original tools for this direct from Multitech
 *		ftp://ftp.multitech.com/ISI-Cards/
 *
 *	Having installed the cards the module options (/etc/conf.modules)
 *
 *	options isicom   io=card1,card2,card3,card4 irq=card1,card2,card3,card4
 *
 *	Omit those entries for boards you don't have installed.
 *
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/termios.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/serial.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/ioport.h>

#include <asm/segment.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/spinlock.h>

#include <linux/pci.h>

#include <linux/isicom.h>

static int device_id[] = {      0x2028,
				0x2051,
				0x2052,
				0x2053,
				0x2054,
				0x2055,
				0x2056,
				0x2057,
				0x2058
			};

#define	DEVID_COUNT	(sizeof(device_id) / sizeof(device_id[0]))

static int isicom_refcount = 0;
static int prev_card = 3;	/*	start servicing isi_card[0]	*/
/*static struct isi_board * irq_to_board[16] = { NULL, };*/
static struct tty_driver isicom_normal, isicom_callout;
static struct tty_struct * isicom_table[PORT_COUNT] = { NULL, };
static struct termios * isicom_termios[PORT_COUNT] = { NULL, };
static struct termios * isicom_termios_locked[PORT_COUNT] = { NULL, };

static struct isi_board isi_card[BOARD_COUNT];
static struct isi_port  isi_ports[PORT_COUNT];

DECLARE_TASK_QUEUE(tq_isicom);

static struct timer_list tx;
static char re_schedule = 1;
#ifdef ISICOM_DEBUG
static unsigned long tx_count = 0;
static unsigned long intr_count = 0;
#endif

static int ISILoad_open(struct inode *inode, struct file *filp);
static int ISILoad_release(struct inode *inode, struct file *filp);
static int ISILoad_ioctl(struct inode *inode, struct file *filp, unsigned  int cmd, unsigned long arg);

static void isicom_tx(unsigned long _data);
static void isicom_start(struct tty_struct * tty);

static unsigned char * tmp_buf = 0;
static struct semaphore tmp_buf_sem = MUTEX;

/*   baud index mappings from linux defns to isi */

static char linuxb_to_isib[] = {
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 13, 15, 16, 17,     
	18, 19
};

/* 
 *  Firmware loader driver specific routines
 *
 */

static struct file_operations ISILoad_fops = {
	NULL,	/*	lseek	*/
	NULL,	/*	read	*/
	NULL,	/*	write	*/
	NULL,	/*	readdir	*/
	NULL,	/*	select	*/
	ISILoad_ioctl,
	NULL,	/*	mmap	*/
	ISILoad_open,
	NULL,	/*	flush	*/
	ISILoad_release,
	NULL,	/*	fsync	*/
	NULL,	/*	fasync	*/
	NULL,	/*	check_media_change	*/
	NULL,	/*	revalidate	*/
};

struct miscdevice isiloader_device = {
	ISILOAD_MISC_MINOR, "isictl", &ISILoad_fops
};

 
extern inline int WaitTillCardIsFree(unsigned short base)
{
	unsigned long count=0;
	while( (!(inw(base+0xe) & 0x1)) && (count++ < 6000000));
	if (inw(base+0xe)&0x1)  
		return 0;
	else
		return 1;
}


extern inline unsigned char valid_irq(int irq)
{
	if	((irq != 2) &&
		(irq != 3) &&
		(irq != 4) &&
		(irq != 5) &&
		(irq != 7) &&
		(irq != 10) &&
		(irq != 11) &&
		(irq != 12) &&
		(irq != 15))
		return 0;
	else
		return 1;
}

static unsigned short reset_card(int base_addr, unsigned char read_portcount)
{
	unsigned int	i;
	unsigned short	sig, portcount;

	inw(base_addr + 0x8);
			
	for(i = jiffies+HZ/100; time_before(jiffies, i););
				
	outw(0, base_addr + 0x8); /* Reset */

	for(i = jiffies+HZ; time_before(jiffies, i););

	sig = (inw(base_addr + 0x4)) & 0xff;

	if ((sig!= 0xa5) &&
		(sig != 0xbb) &&
		(sig != 0xcc) &&
		(sig != 0xdd)) 
		return 0;

	if (read_portcount) {

		for(i = jiffies+HZ/100; time_before(jiffies, i););

		portcount = inw(base_addr + 0x2);
		if ((portcount != 0) && 
			(portcount != 4) && 
			(portcount != 8) &&
			(portcount != 16))
			return 0;
		else
			sig |= (portcount << 8);
	}

	if (!(inw(base_addr + 0xe) & 0x1)) {
#ifdef ISICOM_DEBUG				
		printk("\nbase+0xe=0x%x", inw(base_addr + 0xe));
#endif				
		printk("\nISILoad:ISA Card reset failure"
			"(Possible bad I/O Port Address 0x%x).\n",
			base_addr);
		return 0;
	}
	return sig;	
}

static int ISILoad_open(struct inode *inode, struct file *filp)
{
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISILoad:Firmware loader Opened!!!\n");
#endif	
	MOD_INC_USE_COUNT;
	return 0;
}

static int ISILoad_release(struct inode *inode, struct file *filp)
{
#ifdef ISICOM_DEBUG
	printk(KERN_DEBUG "ISILoad:Firmware loader Close(Release)d\n");
#endif	
	MOD_DEC_USE_COUNT;
	return 0;
}

static int ISILoad_ioctl(struct inode *inode, struct file *filp,
		         unsigned int cmd, unsigned long arg)
{
	unsigned int card, i, signature, status;
	unsigned short word_count, base;
	bin_frame frame;
	/* exec_record exec_rec; */
	
	if(get_user(card, (int *)arg))
		return -EFAULT;
		
	if(card < 0 || card >= BOARD_COUNT)
		return -ENXIO;
		
	base=isi_card[card].base;
	
	if(base==0)
		return -ENXIO;	/* disabled or not used */
	
	switch(cmd) {
		case MIOCTL_RESET_CARD:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			printk(KERN_DEBUG "ISILoad:Resetting Card%d at 0x%x ",card+1,base);
							
			signature = reset_card(base, 
					(isi_card[card].isa ? 0 : 1));

			switch(signature & 0xff) {
			case	0xa5:
			case	0xbb:
			case	0xdd:	
					if (isi_card[card].isa) 
						isi_card[card].port_count = 8;
					else {
						if ((signature & 0xff00) == 0x0400)
							isi_card[card].port_count = 4;
						else
							isi_card[card].port_count = 8;
					}	
				     	isi_card[card].shift_count = 12;
				     	break;
				        
			case	0xcc:	/*	ISI616EM	*/
			case	0xee:	/*	ISI4616		*/
					isi_card[card].port_count = 16;
					isi_card[card].shift_count = 11;
					break;  			
					
			default: printk("\nISILoad:Card%d reset failure (Possible bad I/O Port Address 0x%x).\n",card+1,base);
#ifdef ISICOM_DEBUG			
				 printk("Sig=0x%x\n", signature);
#endif				 
				 return -EIO;
			}
			printk("-Done\n");
			signature &= 0xff;
			return put_user(signature,(unsigned int*)arg);
						
	case	MIOCTL_LOAD_FIRMWARE:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
				
			if(copy_from_user(&frame, (void *) arg, sizeof(bin_frame)))
				return -EFAULT;
			
			if (WaitTillCardIsFree(base))
				return -EIO;
			
			outw(0xf0,base);	/* start upload sequence */ 
			outw(0x00,base);
			outw((frame.addr), base);/*      lsb of adderess    */
			
			word_count=(frame.count >> 1) + frame.count % 2;
			outw(word_count, base);
			InterruptTheCard(base);
			
			for(i=0;i<=0x2f;i++);	/* a wee bit of delay */
			
			if (WaitTillCardIsFree(base)) 
				return -EIO;
				
			if ((status=inw(base+0x4))!=0) {
				printk(KERN_WARNING "ISILoad:"
					"Card%d rejected load header:"
					"\nAddress:0x%x \nCount:0x%x"
					" \nStatus:0x%x \n", 
					card+1, 
					frame.addr, 
					frame.count, 
					status);
				return -EIO;
			}
			outsw(base, (void *) frame.bin_data, word_count);
			
			InterruptTheCard(base);
			
			for(i=0;i<=0x0f;i++);	/* another wee bit of delay */ 
			
			if (WaitTillCardIsFree(base)) 
				return -EIO;
				
			if ((status=inw(base+0x4))!=0) {
				printk(KERN_ERR "ISILoad:Card%d"
				" got out of sync.Card Status:0x%x\n",
				card+1, 
				status);
				return -EIO;
			}	
			return 0;
						
	case	MIOCTL_READ_FIRMWARE:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
				
			if(copy_from_user(&frame, (void *) arg, sizeof(bin_header)))
				return -EFAULT;
			
			if (WaitTillCardIsFree(base))
				return -EIO;
			
			outw(0xf1,base);	/* start download sequence */ 
			outw(0x00,base);
			outw((frame.addr), base);/*      lsb of adderess    */
			
			word_count=(frame.count >> 1) + frame.count % 2;
			outw(word_count+1, base);
			InterruptTheCard(base);
			
			for(i=0;i<=0xf;i++);	/* a wee bit of delay */
			
			if (WaitTillCardIsFree(base)) 
				return -EIO;
				
			if ((status=inw(base+0x4))!=0) {
				printk(KERN_WARNING "ISILoad:Card%d rejected verify header:\nAddress:0x%x \nCount:0x%x \nStatus:0x%x \n", 
				card+1, frame.addr, frame.count, status);
				return -EIO;
			}
			
			inw(base);
			insw(base, frame.bin_data, word_count);
			InterruptTheCard(base);
			
			for(i=0;i<=0x0f;i++);	/* another wee bit of delay */ 
			
			if (WaitTillCardIsFree(base)) 
				return -EIO;
				
			if ((status=inw(base+0x4))!=0) {
				printk(KERN_ERR "ISILoad:Card%d verify got out of sync.Card Status:0x%x\n",card+1, status);
				return -EIO;
			}	
			
			if(copy_to_user((void *) arg, &frame, sizeof(bin_frame)))
				return -EFAULT;
			return 0;
	
	case	MIOCTL_XFER_CTRL:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			if (WaitTillCardIsFree(base)) 
				return -EIO;
					
			outw(0xf2, base);
			outw(0x800, base);
			outw(0x0, base);
			outw(0x0, base);
			InterruptTheCard(base);
			outw(0x0, base+0x4);    /* for ISI4608 cards */
							
			isi_card[card].status |= FIRMWARE_LOADED;
			printk(KERN_INFO "ISICOM: Card%d with %d ports at 0x%x using irq %d.\n", 
				card+1,isi_card[card].port_count, 
				isi_card[card].base, isi_card[card].irq); 
			return 0;	
			
	default:
#ifdef ISICOM_DEBUG	
		printk(KERN_DEBUG "ISILoad: Received Ioctl cmd 0x%x.\n", cmd); 
#endif
		return -ENOIOCTLCMD;
	
	}
	
}
		        	

/*
 *	ISICOM Driver specific routines ...
 *
 */
#ifndef ISIOCM_DEBUG 
static 
#endif
inline int isicom_paranoia_check(struct isi_port const * port, kdev_t dev, 
					const char * routine)
{
#ifdef ISICOM_DEBUG 
	static const char * badmagic = 
			KERN_WARNING "ISICOM: Warning: bad isicom magic for dev %s in %s.\n";
	static const char * badport = 
			KERN_WARNING "ISICOM: Warning: NULL isicom port for dev %s in %s.\n";		
	if (!port) {
		printk(badport, kdevname(dev), routine);
		return 1;
	}
	if (port->magic != ISICOM_MAGIC) {
		printk(badmagic, kdevname(dev), routine);
		return 1;
	}	
#endif	
	return 0;
}
			
extern inline void schedule_bh(struct isi_port * port)
{
	queue_task(&port->bh_tqueue, &tq_isicom);
	mark_bh(ISICOM_BH);
} 

/*	Transmitter	*/
#ifndef ISICOM_DEBUG
static 
#endif
void isicom_tx(unsigned long _data)
{
	short count = (BOARD_COUNT-1), card, base;
	short txcount, wrd, residue, word_count, cnt;
	struct isi_port * port;
	struct tty_struct * tty;
	
#ifdef ISICOM_DEBUG
	++tx_count;
#endif	
	
	/*	find next active board	*/
	card = (prev_card + 1) & 0x0003;
	while(count-- > 0) {
		if (isi_card[card].status & BOARD_ACTIVE) 
			break;
		card = (card + 1) & 0x0003;	
	}
	if (!(isi_card[card].status & BOARD_ACTIVE))
		goto sched_again;
		
	prev_card = card;
	
	
	port = isi_card[card].ports;
	base = isi_card[card].base;
	for (count = isi_card[card].port_count; count > 0; count--, port++) {
		
		if (!lock_card_at_interrupt(&isi_card[card])) {
#ifdef ISICOM_DEBUG
			printk(KERN_DEBUG "ISICOM: isicom_tx:"
				"Card(0x%x) found busy.\n",
				card);
#endif
			continue;
		}

		/* port not active or tx disabled to force flow control */
		if (!(port->flags & ASYNC_INITIALIZED) ||
			!(port->status & ISI_TXOK)) {
			unlock_card(&isi_card[card]);
			continue;
		}
		
		tty = port->tty;

		txcount = MIN(TX_SIZE, port->xmit_cnt);
		if ((txcount <= 0) || tty->stopped || tty->hw_stopped) {
			unlock_card(&isi_card[card]);
			continue;
		}

		if (!(inw(base + 0x02) & (1 << port->channel))) {
			unlock_card(&isi_card[card]);
#ifdef ISICOM_DEBUG					
			printk(KERN_DEBUG "ISICOM: isicom_tx:"
					"Card(0x%x) cannot accept data"
					" for channel %d!\n",
					base,
					port->channel + 1);
#endif					
			continue;		
		}
#ifdef ISICOM_DEBUG
		printk(KERN_DEBUG "ISICOM: txing %d bytes, port%d.\n", 
				txcount,
				port->channel+1); 
#endif	
		outw((port->channel << isi_card[card].shift_count) | txcount,
			base);
		residue = NO;
		wrd = 0;
		while (1) {
			cnt = MIN(txcount, (SERIAL_XMIT_SIZE - port->xmit_tail));
			if (residue == YES) {
				residue = NO;
				if (cnt > 0) {
					wrd |= (port->xmit_buf[port->xmit_tail]
						 << 8);
					port->xmit_tail = (port->xmit_tail + 1)
						 & (SERIAL_XMIT_SIZE - 1);
					port->xmit_cnt--;
					txcount--;
					cnt--;
					outw(wrd, base);			
				}
				else {
					outw(wrd, base);
					break;
				}
			}		
			if (cnt <= 0) break;
			word_count = cnt >> 1;
			outsw(base, port->xmit_buf+port->xmit_tail, word_count);
			port->xmit_tail = (port->xmit_tail + (word_count << 1))
				& (SERIAL_XMIT_SIZE - 1);
			txcount -= (word_count << 1);
			port->xmit_cnt -= (word_count << 1);
			if (cnt & 0x0001) {
				residue = YES;
				wrd = port->xmit_buf[port->xmit_tail];
				port->xmit_tail = (port->xmit_tail + 1) &
						(SERIAL_XMIT_SIZE - 1);
				port->xmit_cnt--;
				txcount--;
			}
		}

		InterruptTheCard(base);
		if (port->xmit_cnt <= 0)
			port->status &= ~ISI_TXOK;
		if (port->xmit_cnt <= WAKEUP_CHARS)
			schedule_bh(port);
		unlock_card(&isi_card[card]);
	}	

		/*	schedule another tx for hopefully in about 10ms	*/	
sched_again:	
	if (!re_schedule)	
		return;
	init_timer(&tx);
	tx.expires = jiffies + HZ/100;
	tx.data = 0;
	tx.function = isicom_tx;
	add_timer(&tx);
	
	return;	
}		
 
/* 	Interrupt handlers 	*/

static void do_isicom_bh(void)
{
	run_task_queue(&tq_isicom);
}


 
static void isicom_bottomhalf(void * data)
{
	struct isi_port * port = (struct isi_port *) data;
	struct tty_struct * tty = port->tty;
	
	if (!tty)
		return;
	
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
	wake_up_interruptible(&tty->write_wait);
} 		
 		
/* main interrupt handler routine */ 		
#ifndef	ISICOM_DEBUG
static 
#endif
void isicom_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
	struct isi_board * card;
	struct isi_port * port;
	struct tty_struct * tty;
	unsigned short base, header, word_count, count;
	unsigned char channel;
	short byte_count;
	
#ifdef	ISICOM_DEBUG
	++intr_count;
#endif
	
	card = (struct isi_board *) dev_id;

	if (!card || !(card->status & FIRMWARE_LOADED)) {
#ifdef ISICOM_DEBUG 
		printk(KERN_DEBUG "ISICOM: interrupt:"
				" not handling irq%d!.\n", 
				irq);
#endif
		return;
	}

	base = card->base;

	/*
	 *	If this is a PCI card, did it really interrupt us ?
	 */
	if ((card->isa == NO) && (!inw(base+0x0e) & 0x02))
		return;

	spin_lock(&card->card_lock);
#ifdef ISICOM_DEBUG 
	printk(KERN_DEBUG "ISICOM: isicom_interrupt: acquired card spinlock\n");

#endif	
	if (card->isa == NO) {
	/*
	 *      disable any interrupts from the PCI card and lower the
	 *      interrupt line
	 */
		outw(0x8000, base+0x04);
		ClearInterrupt(base);
	}
	
	inw(base);		/* get the dummy word out */
	header = inw(base);
	channel = (header & 0x7800) >> card->shift_count;
	byte_count = header & 0xff;
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM:Intr:(0x%x:%d).\n", base, channel+1);
#endif	
	if ((channel+1) > card->port_count) {
		printk(KERN_WARNING "ISICOM: isicom_interrupt(0x%x):"
					" %d(channel) > port_count.\n",
					base, channel+1);
		if (card->isa)
			ClearInterrupt(base);
		else
			outw(0x0000, base+0x04); /* enable interrupts */		
		spin_unlock(&card->card_lock);
		return;			
	}
	port = card->ports + channel;
	if (!(port->flags & ASYNC_INITIALIZED)) {
		if (card->isa)
			ClearInterrupt(base);
		else
			outw(0x0000, base+0x04); /* enable interrupts */
		spin_unlock(&card->card_lock);
		return;
	}	
		
	tty = port->tty;
	
	if (header & 0x8000) {		/* Status Packet */
		header = inw(base);
		switch(header & 0xff) {
			case 0:	/* Change in EIA signals */
				

				if (port->flags & ASYNC_CHECK_CD) {
					if (port->status & ISI_DCD) {
						if (!(header & ISI_DCD)) {
						/* Carrier has been lost  */
#ifdef ISICOM_DEBUG						
							printk(KERN_DEBUG "ISICOM: interrupt: DCD->low.\n");
#endif							
							if (!((port->flags & ASYNC_CALLOUT_ACTIVE) &&
								(port->flags & ASYNC_CALLOUT_NOHUP)))
								queue_task(&port->hangup_tq,
									&tq_scheduler);
						}
					}
					else {
						if (header & ISI_DCD) {
						/* Carrier has been detected */
#ifdef ISICOM_DEBUG
							printk(KERN_DEBUG "ISICOM: interrupt: DCD->high.\n");
#endif							
							wake_up_interruptible(&port->open_wait);
						}
					}
				}
				
				if (header & ISI_DCD) 
					port->status |= ISI_DCD;
				else
					port->status &= ~ISI_DCD;

				if (port->flags & ASYNC_CTS_FLOW) {
					if (port->tty->hw_stopped) {
						if (header & ISI_CTS) {
							port->tty->hw_stopped = 0;
							/* start tx ing 
							port->status |= (ISI_TXOK | ISI_CTS);*/
							schedule_bh(port);
						}
					}
					else {
						if (!(header & ISI_CTS)) {
						/*	port->tty->hw_stopped = 1;*/
							/* stop tx ing 
							port->status &= ~(ISI_TXOK | ISI_CTS);*/
						}
					}
				}
				
				if (header & ISI_CTS) 
					port->status |= ISI_CTS;
				else
					port->status &= ~ISI_CTS;

				if (header & ISI_DSR) 
					port->status |= ISI_DSR;
				else
					port->status &= ~ISI_DSR;
				
				if (header & ISI_RI) 
					port->status |= ISI_RI;
				else
					port->status &= ~ISI_RI;						
				
				break;
				
			case 1:	/* Received Break !!!	 */
				if (tty->flip.count >= TTY_FLIPBUF_SIZE)
					break;
				*tty->flip.flag_buf_ptr++ = TTY_BREAK;
				/* dunno if this is right */	
				*tty->flip.char_buf_ptr++ = 0;
				tty->flip.count++;
				if (port->flags & ASYNC_SAK)
					do_SAK(tty);
				queue_task(&tty->flip.tqueue, &tq_timer);
				break;
				
			case 2:	/* Statistics		 */
				printk(KERN_DEBUG "ISICOM: isicom_interrupt:"
					" stats!!!.\n");
				break;
				
			default:
				printk(KERN_WARNING "ISICOM: Intr: Unknown code in status packet.\n");
				break;
		}	 
	}
	else {				/* Data   Packet */
		count = MIN(byte_count, (TTY_FLIPBUF_SIZE - tty->flip.count));
#ifdef ISICOM_DEBUG
		printk(KERN_DEBUG "ISICOM: Intr: Can rx %d of %d bytes.\n", 
					count, byte_count);
#endif			
		word_count = count >> 1;
		insw(base, tty->flip.char_buf_ptr, word_count);
		tty->flip.char_buf_ptr += (word_count << 1);		
		byte_count -= (word_count << 1);
		if (count & 0x0001) {
			*tty->flip.char_buf_ptr++ = (char)(inw(base) & 0xff);
			byte_count -= 2;
		}	
		memset(tty->flip.flag_buf_ptr, 0, count);
		tty->flip.flag_buf_ptr += count;
		tty->flip.count += count;
		
		if (byte_count > 0) {
			printk(KERN_DEBUG "ISICOM: Intr(0x%x:%d):"
				" Flip buffer overflow! dropping bytes...\n",
				base, channel+1);
			while(byte_count > 0) { /* drain out unread xtra data */
				inw(base);
				byte_count -= 2;
			}
		}
		queue_task(&tty->flip.tqueue, &tq_timer);
	}
	if (card->isa == YES)
		ClearInterrupt(base);
	else
		outw(0x0000, base+0x04); /* enable interrupts */
	spin_unlock(&card->card_lock);
	return;
} 
#ifndef ISICOM_DEBUG
static 
#endif
void isicom_config_port(struct isi_port * port)
{
	struct isi_board * card = port->card;
	struct tty_struct * tty;
	unsigned long baud;
	unsigned short channel_setup, base = card->base;
	unsigned short channel = port->channel, shift_count = card->shift_count;
	unsigned char flow_ctrl;
	
#ifdef	ISICOM_DEBUG
	printk(KERN_DEBUG "ISICOM:isicom_config_port:start\n");
#endif
	if (!(tty = port->tty) || !tty->termios)
		return;
	baud = C_BAUD(tty);
	if (baud & CBAUDEX) {
		baud &= ~CBAUDEX;
		
		/*  if CBAUDEX bit is on and the baud is set to either 50 or 75
		 *  then the card is programmed for 57.6Kbps or 115Kbps
		 *  respectively.
		 */   
		 
		if (baud < 1 || baud > 2)
			port->tty->termios->c_cflag &= ~CBAUDEX;
		else
			baud += 15;
	}	
	if (baud == 15) {
	
		/*  the ASYNC_SPD_HI and ASYNC_SPD_VHI options are set 
		 *  by the set_serial_info ioctl ... this is done by
		 *  the 'setserial' utility.
		 */  
			
		if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			baud++;     /*  57.6 Kbps */
		if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			baud +=2;   /*  115  Kbps */	 
	}
	if (linuxb_to_isib[baud] == -1) {
		/* hang up */
	 	drop_dtr(port);
	 	return;
	}	
	else  
		raise_dtr(port);
		
	if (lock_card(card)) {
		outw(0x8000 | (channel << shift_count) |0x03, base);
		outw(linuxb_to_isib[baud] << 8 | 0x03, base);
		channel_setup = 0;
		switch(C_CSIZE(tty)) {
			case CS5:
				channel_setup |= ISICOM_CS5;
				break;
			case CS6:
				channel_setup |= ISICOM_CS6;
				break;
			case CS7:
				channel_setup |= ISICOM_CS7;
				break;
			case CS8:
				channel_setup |= ISICOM_CS8;
				break;
		}
		
		if (C_CSTOPB(tty))
			channel_setup |= ISICOM_2SB;
	
		if (C_PARENB(tty)) {
			if (C_PARODD(tty))
				channel_setup |= ISICOM_ODPAR;
			else
				channel_setup |= ISICOM_EVPAR;
		}
		outw(channel_setup, base);	
		InterruptTheCard(base);

#ifdef	ISICOM_DEBUG
		printk(KERN_DEBUG "ISICOM:isicom_config_port:channel setup\n");
#endif
		unlock_card(card);	
	}
	else 
		printk(KERN_WARNING "ISICOM:isicom_config_port:"
				"Card (0x%x) busy for Channel Setup\n",
				card->base);

	if (C_CLOCAL(tty))
		port->flags &= ~ASYNC_CHECK_CD;
	else
		port->flags |= ASYNC_CHECK_CD;	
	
	/* flow control settings ...*/
	flow_ctrl = 0;
	port->flags &= ~ASYNC_CTS_FLOW;
	if (C_CRTSCTS(tty)) {
		port->flags |= ASYNC_CTS_FLOW;
		flow_ctrl |= ISICOM_CTSRTS;
	}	
	if (I_IXON(tty))	
		flow_ctrl |= ISICOM_RESPOND_XONXOFF;
	if (I_IXOFF(tty))
		flow_ctrl |= ISICOM_INITIATE_XONXOFF;	
		
	if (lock_card(card)) {
		outw(0x8000 | (channel << shift_count) |0x04, base);
		outw(flow_ctrl << 8 | 0x05, base);
		outw((STOP_CHAR(tty)) << 8 | (START_CHAR(tty)), base);
		InterruptTheCard(base);

#ifdef	ISICOM_DEBUG
		printk(KERN_DEBUG "ISICOM:isicom_config_port:flow setup\n");
#endif

		unlock_card(card);
	}
	else
		printk(KERN_WARNING "ISICOM:isicom_config_port:"
				"Card (0x%x) busy for Flow Ctrl Setup\n",
				card->base);
	
			 
	/*	rx enabled -> enable port for rx on the card	*/
	if (C_CREAD(tty)) {
		card->port_status |= (1 << channel);
		outw(card->port_status, base + 0x02);
	}
		
#ifdef	ISICOM_DEBUG
	printk(KERN_DEBUG "ISICOM:isicom_config_port:stop\n");
#endif
}
 
/* open et all */ 

extern inline void isicom_setup_board(struct isi_board * bp)
{
	int channel;
	struct isi_port * port;
	unsigned long flags;
	
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM: setup_board:start\n");
#endif

	spin_lock_irqsave(&bp->card_lock, flags);
		if (bp->status & BOARD_ACTIVE) {
#ifdef ISICOM_DEBUG	
			printk(KERN_DEBUG "ISICOM: setup_board:"
					" already active\n");
#endif
			spin_unlock_irqrestore(&bp->card_lock, flags);
			return;
		}
		port = bp->ports;
		bp->status |= BOARD_ACTIVE;
		MOD_INC_USE_COUNT;
	spin_unlock_irqrestore(&bp->card_lock, flags);

	for(channel = 0; channel < bp->port_count; channel++, port++)
		drop_dtr(port);
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM: setup_board:stop\n");
#endif
	return;
}

#ifndef	ISICOM_DEBUG 
static 
#endif
int isicom_setup_port(struct isi_port * port)
{
	struct isi_board * card = port->card;
	unsigned long flags;
	
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM: setup_port:start\n");
#endif
	spin_lock_irqsave(&card->card_lock, flags);
		if (port->flags & ASYNC_INITIALIZED) {
			spin_unlock_irqrestore(&card->card_lock, flags);
			return 0;
		}

		if (!port->xmit_buf) {
			unsigned long page;
		
			if (!(page = get_free_page(GFP_KERNEL))) {
				spin_unlock_irqrestore(&card->card_lock, flags);
				return -ENOMEM;
			}
		
			if (port->xmit_buf) {
				free_page(page);
				spin_unlock_irqrestore(&card->card_lock, flags);
				return -ERESTARTSYS;
			}
			port->xmit_buf = (unsigned char *) page;	
		}

		if (port->tty)
			clear_bit(TTY_IO_ERROR, &port->tty->flags);
		if (port->count == 1)
			card->count++;
		
		port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;

		port->flags |= ASYNC_INITIALIZED;
	spin_unlock_irqrestore(&card->card_lock, flags);
	
	/*	discard any residual data	*/
	kill_queue(port, ISICOM_KILLTX | ISICOM_KILLRX);
	
#ifdef ISICOM_DEBUG		
	printk(KERN_DEBUG "ISICOM: setup_port: isicom_config_port...\n");	
#endif	
	isicom_config_port(port);
	
	set_signal_mask(port, 0xff); /* enable EIA status interrupt reporting */
	
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM: setup_port:stop\n");
#endif
	return 0;		
}
#ifndef	ISICOM_DEBUG
static 
#endif
int block_til_ready(struct tty_struct * tty, struct file * filp, 
				struct isi_port * port)
{
	int do_clocal = 0, retval;
	struct wait_queue wait = { current, NULL };
	struct isi_board *card = port->card;

	/* block if port is in the process of being closed */

	if (tty_hung_up_p(filp) || port->flags & ASYNC_CLOSING) {
#ifdef ISICOM_DEBUG	
		printk(KERN_DEBUG "ISICOM: block_til_ready:"
				" close in progress.\n");
#endif		
		interruptible_sleep_on(&port->close_wait);
		if (port->flags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
	}
	
	/* trying to open a callout device... check for constraints */
	
	if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
#ifdef ISICOM_DEBUG
		printk(KERN_DEBUG "ISICOM: bl_ti_rdy: callout open.\n");	
#endif		
		if (port->flags & ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((port->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (port->flags & ASYNC_SESSION_LOCKOUT) &&
		    (port->session != current->session))
			return -EBUSY;
			
		if ((port->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (port->flags & ASYNC_PGRP_LOCKOUT) &&
		    (port->pgrp != current->pgrp))
			return -EBUSY;
		port->flags |= ASYNC_CALLOUT_ACTIVE;
		raise_dtr_rts(port);
		return 0;
	}
	
	/* if non-blocking mode is set ... */
	
	if ((filp->f_flags & O_NONBLOCK) || 
		(tty->flags & (1 << TTY_IO_ERROR))) {
#ifdef ISICOM_DEBUG	
		printk(KERN_DEBUG "ISICOM: block_til_ready: non-block mode.\n");
#endif		
		if (port->flags & ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		port->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;	
	}	
	
	if (port->flags & ASYNC_CALLOUT_ACTIVE) {
		if (port->normal_termios.c_cflag & CLOCAL)
			do_clocal = 1; 
	} else {
		if (C_CLOCAL(tty))
			do_clocal = 1;
	}
#ifdef ISICOM_DEBUG	
	if (do_clocal)
		printk(KERN_DEBUG "ISICOM: block_til_ready: CLOCAL set.\n");
#endif 		
	
	/* block waiting for DCD to be asserted, and while 
						callout dev is busy */
	retval = 0;
	add_wait_queue(&port->open_wait, &wait);

	spin_lock_irqsave(&card->card_lock, card->flags);
		if (!tty_hung_up_p(filp))
			port->count--;
	spin_unlock_irqrestore(&card->card_lock, card->flags);

	port->blocked_open++;
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM: block_til_ready: waiting for DCD...\n");
#endif	
	while (1) {
		if (!(port->flags & ASYNC_CALLOUT_ACTIVE)) 
			raise_dtr_rts(port);
		
		current->state = TASK_INTERRUPTIBLE;
		if (tty_hung_up_p(filp) || !(port->flags & ASYNC_INITIALIZED)) { 	
			if (port->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#ifdef ISICOM_DEBUG				
			printk(KERN_DEBUG "ISICOM: block_til_ready:"
					" tty_hung_up_p || not init.\n"); 
#endif			
			break;
		}	
		if (!(port->flags & ASYNC_CALLOUT_ACTIVE) &&
		    !(port->flags & ASYNC_CLOSING) &&
		    (do_clocal || (port->status & ISI_DCD))) {
#ifdef ISICOM_DEBUG		    
		 	printk(KERN_DEBUG "ISICOM: block_til_ready:"
					" do_clocal || DCD.\n");   
#endif		 	
			break;
		}	
		if (signal_pending(current)) {
#ifdef ISICOM_DEBUG		
			printk(KERN_DEBUG "ISICOM: block_til_ready:"
					" sig blocked.\n");
#endif			
			retval = -ERESTARTSYS;
			break;
		}
		schedule();		
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&port->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		port->count++;
	port->blocked_open--;
	if (retval)
		return retval;
	port->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

#ifndef	ISICOM_DEBUG 
static 
#endif
int isicom_open(struct tty_struct * tty, struct file * filp)
{
	struct isi_port * port;
	struct isi_board * card;
	unsigned int line, board;
	int error;

#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM: open start!!!.\n");
#endif	
	line = MINOR(tty->device) - tty->driver.minor_start;
	
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "line = %d.\n", line);
#endif	
	
	if ((line < 0) || (line > (PORT_COUNT-1)))
		return -ENODEV;
	board = BOARD(line);
	
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "board = %d.\n", board);
#endif	
	
	card = &isi_card[board];
	if (!(card->status & FIRMWARE_LOADED)) {
#ifdef ISICOM_DEBUG	
		printk(KERN_DEBUG"ISICOM: Firmware not loaded"
				" to card%d.\n", board);
#endif		
		return -ENODEV;
	}
	
	/*  open on a port greater than the port count for the card !!! */
	if (line > ((board * 16) + card->port_count - 1)) {
		printk(KERN_ERR "ISICOM: Open on a port which exceeds"
				" the port_count of the card!\n");
		return -ENODEV;
	}	
	port = &isi_ports[line];	
	if (isicom_paranoia_check(port, tty->device, "isicom_open"))
		return -ENODEV;
		
#ifdef ISICOM_DEBUG		
	printk(KERN_DEBUG "ISICOM: isicom_setup_board ...\n");		
#endif	
	isicom_setup_board(card);
	
	port->count++;
	tty->driver_data = port;
	port->tty = tty;

#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM: isicom_setup_port ...\n");
#endif	
	if ((error = isicom_setup_port(port))!=0)
		return error;

#ifdef ISICOM_DEBUG		
	printk(KERN_DEBUG "ISICOM: block_til_ready ...\n");	
#endif	
	if ((error = block_til_ready(tty, filp, port))!=0)
		return error;
		
	if ((port->count == 1) && (port->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = port->normal_termios;
		else 
			*tty->termios = port->callout_termios;
		isicom_config_port(port);
	}	
	
	port->session = current->session;	
	port->pgrp = current->pgrp;
#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM: open end!!!.\n");
#endif	
	return 0;      		
}
 
/* close et all */

extern inline void isicom_shutdown_board(struct isi_board * bp)
{
	unsigned long flags;

	spin_lock_irqsave(&bp->card_lock, flags);
		if (!(bp->status & BOARD_ACTIVE)) {
			spin_unlock_irqrestore(&bp->card_lock, flags);
			return;
		}
		bp->status &= ~BOARD_ACTIVE;
		MOD_DEC_USE_COUNT;
	spin_unlock_irqrestore(&bp->card_lock, flags);
}

#ifndef	ISICOM_DEBUG
static 
#endif
void isicom_shutdown_port(struct isi_port * port)
{
	struct isi_board * card = port->card;
	struct tty_struct * tty;
	unsigned long flags;

	spin_lock_irqsave(&card->card_lock, flags);	
		if (!(port->flags & ASYNC_INITIALIZED)){
			spin_unlock_irqrestore(&card->card_lock, flags);
			return;
		}
		if (port->xmit_buf) {
			free_page((unsigned long) port->xmit_buf);
			port->xmit_buf = NULL;
		}	
		port->flags &= ~ASYNC_INITIALIZED;
	spin_unlock_irqrestore(&card->card_lock, flags);

	tty = port->tty;
	if (!(tty = port->tty) || C_HUPCL(tty)) 
		/* drop dtr on this port */
		drop_dtr(port);
		
	/* any other port uninits  */ 
	
	if (tty)
		set_bit(TTY_IO_ERROR, &tty->flags);
	
	if (--card->count < 0) {
		printk(KERN_DEBUG "ISICOM: isicom_shutdown_port:"
				" bad board(0x%x) count %d.\n",
				card->base, card->count);
		card->count = 0;	
	}
	
	set_signal_mask(port, 0x00); /*disable EIA status interrupt reporting */

	/* last port was closed , shutdown that boad too */
	if (!card->count)
		isicom_shutdown_board(card);
}

#ifndef	ISICOM_DEBUG
static 
#endif
void isicom_close(struct tty_struct * tty, struct file * filp)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	struct isi_board * card;
	unsigned long flags;
	
#ifdef ISICOM_DEBUG		
	printk(KERN_DEBUG "ISICOM: Close start!!!.\n");
#endif	
	if (!port)
		return;

	card = port->card;

	if (isicom_paranoia_check(port, tty->device, "isicom_close"))
		return;
	
	spin_lock_irqsave(&card->card_lock, flags);
	if (tty_hung_up_p(filp)) {
		spin_unlock_irqrestore(&card->card_lock, flags);
		return;
	}
	
	if ((tty->count == 1) && (port->count != 1)) {
		printk(KERN_WARNING "ISICOM:(0x%x) isicom_close: bad port count"
			"tty->count = 1	port count = %d.\n",
			card->base, port->count);
		port->count = 1;
	}
	if (--port->count < 0) {
		printk(KERN_WARNING "ISICOM:(0x%x) isicom_close:"
			" bad port count for channel%d = %d", 
			card->base, 
			port->channel, 
			port->count);
		port->count = 0;	
	}
	
	if (port->count) {
		spin_unlock_irqrestore(&card->card_lock, flags);
		return;
	} 	
	port->flags |= ASYNC_CLOSING;
	/* 
	 * save termios struct since callout and dialin termios may be 
	 * different.
	 */	
	if (port->flags & ASYNC_NORMAL_ACTIVE)
		port->normal_termios = *tty->termios;
	if (port->flags & ASYNC_CALLOUT_ACTIVE)
		port->callout_termios = *tty->termios;
	
	tty->closing = 1;

	spin_unlock_irqrestore(&card->card_lock, flags);
	if (port->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, port->closing_wait);
	spin_lock_irqsave(&card->card_lock, flags);

	/* 
	 *	indicate to the card that no more data can be received 
	 *	on this port
	 */
	if (port->flags & ASYNC_INITIALIZED) {   
		card->port_status &= ~(1 << port->channel);
		outw(card->port_status, card->base + 0x02);
	}

	spin_unlock_irqrestore(&card->card_lock, flags);

	isicom_shutdown_port(port);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);

	spin_lock_irqsave(&card->card_lock, flags);

	tty->closing = 0;
	port->tty = 0;
	if (port->blocked_open) {
		spin_unlock_irqrestore(&card->card_lock, flags);
		if (port->close_delay) {
			current->state = TASK_INTERRUPTIBLE;
#ifdef ISICOM_DEBUG			
			printk(KERN_DEBUG "ISICOM: scheduling until time out.\n");
#endif			
			schedule_timeout(port->close_delay);
		}
		wake_up_interruptible(&port->open_wait);
		spin_lock_irqsave(&card->card_lock, flags);
	}	
	port->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CALLOUT_ACTIVE | 
			ASYNC_CLOSING);
	port->status &= ~ISI_TXOK;
	wake_up_interruptible(&port->close_wait);

	spin_unlock_irqrestore(&card->card_lock, flags);

#ifdef ISICOM_DEBUG	
	printk(KERN_DEBUG "ISICOM: Close end!!!.\n");
#endif	
}

/* write et all */
#ifndef	ISICOM_DEBUG
static 
#endif
int isicom_write(struct tty_struct * tty, int from_user,
			const unsigned char * buf, int count)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	struct isi_board * card = port->card;
	int cnt, total = 0;
#ifdef ISICOM_DEBUG
/*	printk(KERN_DEBUG "ISICOM: isicom_write for port%d: %d bytes.\n",
			port->channel+1, count);*/
#endif	  	
	if (isicom_paranoia_check(port, tty->device, "isicom_write"))
		return 0;
	
	spin_lock_irqsave(&card->card_lock, card->flags);

	if (!tty || !port->xmit_buf || !tmp_buf)
		return 0;
	if (from_user)
		down(&tmp_buf_sem); /* acquire xclusive access to tmp_buf */
		
	
	while(1) {	

/*		spin_lock_irqsave(&card->card_lock, card->flags);*/
		cnt = MIN(count, MIN(SERIAL_XMIT_SIZE - port->xmit_cnt - 1,
			SERIAL_XMIT_SIZE - port->xmit_head));
		if (cnt <= 0)  {
/*			spin_unlock_irqrestore(&card->card_lock, card->flags);*/
			break;
		}
		
		if (from_user) {
			/*	
			 *	the following may block for paging... hence 
			 *	enabling interrupts but tx routine may have 
			 *	created more space in xmit_buf when the ctrl 
			 *	gets back here
			 */

			spin_unlock_irqrestore(&card->card_lock, card->flags);

			copy_from_user(tmp_buf, buf, cnt);

			spin_lock_irqsave(&card->card_lock, card->flags);

			cnt = MIN(cnt,
				MIN(SERIAL_XMIT_SIZE - port->xmit_cnt - 1,
				SERIAL_XMIT_SIZE - port->xmit_head));
			memcpy(port->xmit_buf + port->xmit_head, tmp_buf, cnt);
		}	
		else
			memcpy(port->xmit_buf + port->xmit_head, buf, cnt);
		port->xmit_head = (port->xmit_head + cnt) &
				(SERIAL_XMIT_SIZE - 1);
		port->xmit_cnt += cnt;

/*		spin_unlock_irqrestore(&card->card_lock, card->flags);*/
		
		buf += cnt;
		count -= cnt;
		total += cnt;
	}
	if (from_user)
		up(&tmp_buf_sem);

	if (port->xmit_cnt && !tty->stopped && !tty->hw_stopped)
		port->status |= ISI_TXOK;

	spin_unlock_irqrestore(&card->card_lock, card->flags);

#ifdef ISICOM_DEBUG
/*	printk(KERN_DEBUG "ISICOM: isicom_write %d bytes written.\n", total);*/
#endif		
	return total;
}

/* put_char et all */
#ifndef	ISICOM_DEBUG
static 
#endif
void isicom_put_char(struct tty_struct * tty, unsigned char ch)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	struct isi_board * card = port->card;
	
#ifdef ISICOM_DEBUG
	printk(KERN_DEBUG "ISICOM: put_char, port %d, char %c.\n", port->channel+1, ch);
#endif			
	if (isicom_paranoia_check(port, tty->device, "isicom_put_char"))
		return;
	
	if (!tty || !port->xmit_buf)
		return;

	spin_lock_irqsave(&card->card_lock, card->flags);
	
		if (port->xmit_cnt >= (SERIAL_XMIT_SIZE - 1)) {
			spin_unlock_irqrestore(&card->card_lock, card->flags);
			return;
		}
	
		port->xmit_buf[port->xmit_head++] = ch;
		port->xmit_head &= (SERIAL_XMIT_SIZE - 1);
		port->xmit_cnt++;

	spin_unlock_irqrestore(&card->card_lock, card->flags);
}

/* flush_chars et all */
static void isicom_flush_chars(struct tty_struct * tty)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	
	if (isicom_paranoia_check(port, tty->device, "isicom_flush_chars"))
		return;
	
	if (port->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !port->xmit_buf)
		return;
		
	/* this tells the transmitter to consider this port for
	   data output to the card ... that's the best we can do. */
	port->status |= ISI_TXOK;	
}

/* write_room et all */
static int isicom_write_room(struct tty_struct * tty)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	int free;
	if (isicom_paranoia_check(port, tty->device, "isicom_write_room"))
		return 0;
	
	free = SERIAL_XMIT_SIZE - port->xmit_cnt - 1;
	if (free < 0)
		free = 0;
	return free;
}

/* chars_in_buffer et all */
static int isicom_chars_in_buffer(struct tty_struct * tty)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	if (isicom_paranoia_check(port, tty->device, "isicom_chars_in_buffer"))
		return 0;
	return port->xmit_cnt;
}

/* ioctl et all */
extern inline void isicom_send_break(struct isi_port * port, unsigned long length)
{
	struct isi_board * card = port->card;
	unsigned short base = card->base;	
	
	if (!lock_card(card)) {
		printk(KERN_DEBUG "ISICOM: Card found busy in"
			" isicom_send_break.\n");
		return;
	}

	outw(0x8000 | ((port->channel) << (card->shift_count)) | 0x3, base);
	outw((length & 0xff) << 8 | 0x00, base);
	outw((length & 0xff00), base);
	InterruptTheCard(base);

	unlock_card(card);
}

static int isicom_get_modem_info(struct isi_port * port, unsigned int * value)
{
	/* just send the port status */
	unsigned int info;
	unsigned short status = port->status;
	
	info =  ((status & ISI_RTS) ? TIOCM_RTS : 0) |
		((status & ISI_DTR) ? TIOCM_DTR : 0) |
		((status & ISI_DCD) ? TIOCM_CAR : 0) |
		((status & ISI_DSR) ? TIOCM_DSR : 0) |
		((status & ISI_CTS) ? TIOCM_CTS : 0) |
		((status & ISI_RI ) ? TIOCM_RI  : 0);
	put_user(info, (unsigned long *) value);
	return 0;	
}

static int isicom_set_modem_info(struct isi_port * port, unsigned int cmd,
					unsigned int * value)
{
	unsigned int arg;
	
	if(get_user(arg, value))
		return -EFAULT;
	
	
	switch(cmd) {
		case TIOCMBIS:
			if (arg & TIOCM_RTS) 
				raise_rts(port);
			if (arg & TIOCM_DTR) 
				raise_dtr(port);
			break;
		
		case TIOCMBIC:
			if (arg & TIOCM_RTS)
				drop_rts(port);
			if (arg & TIOCM_DTR)
				drop_dtr(port);	
			break;
			
		case TIOCMSET:
			if (arg & TIOCM_RTS)
				raise_rts(port);
			else
				drop_rts(port);
			
			if (arg & TIOCM_DTR)
				raise_dtr(port);
			else
				drop_dtr(port);
			break;
		
		default:
			return -EINVAL;		 	
	}
	return 0;
}			

static int isicom_set_serial_info(struct isi_port * port,
					struct serial_struct * info)
{
	struct serial_struct newinfo;
	int reconfig_port;

	if(copy_from_user(&newinfo, info, sizeof(newinfo)))
		return -EFAULT;
		
	reconfig_port = ((port->flags & ASYNC_SPD_MASK) != 
			 (newinfo.flags & ASYNC_SPD_MASK));
	
	if (!suser()) {
		if ((newinfo.close_delay != port->close_delay) ||
		    (newinfo.closing_wait != port->closing_wait) ||
		    ((newinfo.flags & ~ASYNC_USR_MASK) != 
		     (port->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		port->flags = ((port->flags & ~ ASYNC_USR_MASK) |
				(newinfo.flags & ASYNC_USR_MASK));
	}	
	else {
		port->close_delay = newinfo.close_delay;
		port->closing_wait = newinfo.closing_wait; 
		port->flags = ((port->flags & ~ASYNC_FLAGS) | 
				(newinfo.flags & ASYNC_FLAGS));
	}
	if (reconfig_port) {
		isicom_config_port(port);
	}
	return 0;		 
}		

static int isicom_get_serial_info(struct isi_port * port, 
					struct serial_struct * info)
{
	struct serial_struct out_info;
	
	memset(&out_info, 0, sizeof(out_info));
/*	out_info.type = ? */
	out_info.line = port - isi_ports;
	out_info.port = port->card->base;
	out_info.irq = port->card->irq;
	out_info.flags = port->flags;
/*	out_info.baud_base = ? */
	out_info.close_delay = port->close_delay;
	out_info.closing_wait = port->closing_wait;
	if(copy_to_user(info, &out_info, sizeof(out_info)))
		return -EFAULT;
	return 0;
}					

static int isicom_ioctl(struct tty_struct * tty, struct file * filp,
			unsigned int cmd, unsigned long arg) 
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	int retval;

	if (isicom_paranoia_check(port, tty->device, "isicom_ioctl"))
		return -ENODEV;

	switch(cmd) {
		case TCSBRK:
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (!arg)
				isicom_send_break(port, HZ/4);
			return 0;
			
		case TCSBRKP:	
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			isicom_send_break(port, arg ? arg * (HZ/10) : HZ/4);
			return 0;
			
		case TIOCGSOFTCAR:
			return put_user(C_CLOCAL(tty) ? 1 : 0, (unsigned long *) arg);
			
		case TIOCSSOFTCAR:
			if(get_user(arg, (unsigned long *) arg))
				return -EFAULT;
			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				(arg ? CLOCAL : 0));
			return 0;	
			
		case TIOCMGET:
			return isicom_get_modem_info(port, (unsigned int*) arg);
			
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET: 	
			return isicom_set_modem_info(port, cmd, 
					(unsigned int *) arg);
		
		case TIOCGSERIAL:
			return isicom_get_serial_info(port, 
					(struct serial_struct *) arg);
		
		case TIOCSSERIAL:
			return isicom_set_serial_info(port,
					(struct serial_struct *) arg);
					
		default:
			return -ENOIOCTLCMD;						
	}
	return 0;
}

/* set_termios et all */
static void isicom_set_termios(struct tty_struct * tty, struct termios * old_termios)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	
	if (isicom_paranoia_check(port, tty->device, "isicom_set_termios"))
		return;
	
	if (tty->termios->c_cflag == old_termios->c_cflag &&
	    tty->termios->c_iflag == old_termios->c_iflag)
		return;
		
	isicom_config_port(port);
	
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {	
		tty->hw_stopped = 0;
		isicom_start(tty);   
	}    
}

/* throttle et all */
static void isicom_throttle(struct tty_struct * tty)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	struct isi_board * card = port->card;
	
	if (isicom_paranoia_check(port, tty->device, "isicom_throttle"))
		return;
	
	/* tell the card that this port cannot handle any more data for now */
	card->port_status &= ~(1 << port->channel);
	outw(card->port_status, card->base + 0x02);
}

/* unthrottle et all */
static void isicom_unthrottle(struct tty_struct * tty)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	struct isi_board * card = port->card;
	
	if (isicom_paranoia_check(port, tty->device, "isicom_unthrottle"))
		return;
	
	/* tell the card that this port is ready to accept more data */
	card->port_status |= (1 << port->channel);
	outw(card->port_status, card->base + 0x02);
}

/* stop et all */
static void isicom_stop(struct tty_struct * tty)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;

	if (isicom_paranoia_check(port, tty->device, "isicom_stop"))
		return;
	
	/* this tells the transmitter not to consider this port for
	   data output to the card. */
	port->status &= ~ISI_TXOK;
}

/* start et all */
static void isicom_start(struct tty_struct * tty)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	
	if (isicom_paranoia_check(port, tty->device, "isicom_start"))
		return;
	
	/* this tells the transmitter to consider this port for
	   data output to the card. */
	port->status |= ISI_TXOK;
}

/* hangup et all */
static void do_isicom_hangup(void * data)
{
	struct isi_port * port = (struct isi_port *) data;
	struct tty_struct * tty;
	
	tty = port->tty;
	if (!tty)
		return;
		
	tty_hangup(tty);	
}

static void isicom_hangup(struct tty_struct * tty)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	
	if (isicom_paranoia_check(port, tty->device, "isicom_hangup"))
		return;
	
	isicom_shutdown_port(port);
	port->count = 0;
	port->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CALLOUT_ACTIVE);
	port->tty = 0;
	wake_up_interruptible(&port->open_wait);
}

/* flush_buffer et all */
static void isicom_flush_buffer(struct tty_struct * tty)
{
	struct isi_port * port = (struct isi_port *) tty->driver_data;
	unsigned long flags;
#ifdef	__SMP__
	struct isi_board *card = port->card;
#endif
	
	if (isicom_paranoia_check(port, tty->device, "isicom_flush_buffer"))
		return;
	
	spin_lock_irqsave(&card->card_lock, flags);

	port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;

	spin_unlock_irqrestore(&card->card_lock, flags);
	
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}


static int register_ioregion(void)
{
	int count, done=0;
	for (count=0; count < BOARD_COUNT; count++ ) {
		if (isi_card[count].base) {
			if (check_region(isi_card[count].base,16)) {
				printk(KERN_DEBUG "ISICOM: I/O Region"
					" 0x%x-0x%x is busy."
					" Card%d will be disabled.\n",
					isi_card[count].base,
					isi_card[count].base+15,
					count+1);
				isi_card[count].base=0;
			}
			else {
				request_region(isi_card[count].base,16,
						ISICOM_NAME);
#ifdef ISICOM_DEBUG				
				printk(KERN_DEBUG "ISICOM: I/O Region"
					" 0x%x-0x%x requested for Card%d.\n",
					isi_card[count].base,
					isi_card[count].base+15,
					count+1);
#endif				
				done++;
			}
		}	
	}
	return done;
}

static void unregister_ioregion(void)
{
	int count;
	for (count=0; count < BOARD_COUNT; count++ ) 
		if (isi_card[count].base) {
			release_region(isi_card[count].base,16);
#ifdef ISICOM_DEBUG			
			printk(KERN_DEBUG "ISICOM: I/O Region 0x%x-0x%x"
				" released for Card%d.\n",
				isi_card[count].base,
				isi_card[count].base+15,
				count+1);
#endif			
		}
}

static int register_drivers(void)
{
	int error;

	/* tty driver structure initialization */
	memset(&isicom_normal, 0, sizeof(struct tty_driver));
	isicom_normal.magic		= TTY_DRIVER_MAGIC;
	isicom_normal.name 		= "ttyM";
	isicom_normal.major		= ISICOM_NMAJOR;
	isicom_normal.minor_start	= 0;
	isicom_normal.num		= PORT_COUNT;
	isicom_normal.type		= TTY_DRIVER_TYPE_SERIAL;
	isicom_normal.subtype		= SERIAL_TYPE_NORMAL;
	isicom_normal.init_termios	= tty_std_termios;
	isicom_normal.init_termios.c_cflag	= 
				B9600 | CS8 | CREAD | HUPCL |CLOCAL;
	isicom_normal.flags		= TTY_DRIVER_REAL_RAW;
	isicom_normal.refcount		= &isicom_refcount;
	
	isicom_normal.table		= isicom_table;
	isicom_normal.termios		= isicom_termios;
	isicom_normal.termios_locked	= isicom_termios_locked;
	
	isicom_normal.open		= isicom_open;
	isicom_normal.close		= isicom_close;
	isicom_normal.write		= isicom_write;
	isicom_normal.put_char		= isicom_put_char;
	isicom_normal.flush_chars	= isicom_flush_chars;
	isicom_normal.write_room	= isicom_write_room;
	isicom_normal.chars_in_buffer	= isicom_chars_in_buffer;
	isicom_normal.ioctl		= isicom_ioctl;
	isicom_normal.set_termios	= isicom_set_termios;
	isicom_normal.throttle		= isicom_throttle;
	isicom_normal.unthrottle	= isicom_unthrottle;
	isicom_normal.stop		= isicom_stop;
	isicom_normal.start		= isicom_start;
	isicom_normal.hangup		= isicom_hangup;
	isicom_normal.flush_buffer	= isicom_flush_buffer;
	
	/*	callout device	*/
	
	isicom_callout			= isicom_normal;
	isicom_callout.name		= "cum"; 
	isicom_callout.major		= ISICOM_CMAJOR;
	isicom_callout.subtype		= SERIAL_TYPE_CALLOUT;
	
	if ((error=tty_register_driver(&isicom_normal))!=0) {
		printk(KERN_DEBUG "ISICOM: Couldn't register the"
				" dialin driver, error=%d\n",
				error);
		return error;
	}
	if ((error=tty_register_driver(&isicom_callout))!=0) {
		tty_unregister_driver(&isicom_normal);
		printk(KERN_DEBUG "ISICOM: Couldn't register the"
				" callout driver, error=%d\n",
				error);
		return error;	
	}
	return 0;
}

static void unregister_drivers(void)
{
	int error;
	if ((error=tty_unregister_driver(&isicom_callout))!=0)
		printk(KERN_DEBUG "ISICOM: couldn't unregister"
				" callout driver error=%d.\n",error);
	if (tty_unregister_driver(&isicom_normal))
		printk(KERN_DEBUG "ISICOM: couldn't unregister"
				" normal driver error=%d.\n",error);
}

#if 0
static int register_isr(void)
{
	int count, done=0, card;
	unsigned char request;
	for (count=0; count < BOARD_COUNT; count++ ) {
		if (isi_card[count].base) {
		/*
		 * verify if the required irq has already been requested for
		 * another ISI Card, if so we already have it, else request it
		 */
			request = YES;
			for(card = 0; card < count; card++)
			if ((isi_card[card].base) && 
				(isi_card[card].irq == isi_card[count].irq)) {
				request = NO;
				if ((isi_card[count].isa == NO) 
					&& (isi_card[card].isa == NO))
					break;
				/*
				 * ISA cards cannot share interrupts with other
				 * PCI or ISA devices hence disable this card.
				 */
				release_region(isi_card[count].base,16);
				isi_card[count].base = 0;
				break;
			}
			if (request == YES) { 
				if (request_irq(isi_card[count].irq, 
						isicom_interrupt, 
						SA_INTERRUPT | SA_SHIRQ, 
						ISICOM_NAME, NULL)) {

					printk(KERN_WARNING "ISICOM: Could not"
						" install handler at Irq %d."
						" Card%d will be disabled.\n",
						isi_card[count].irq, count+1);

					release_region(isi_card[count].base,16);
					isi_card[count].base=0;
				}
				else {
					
/*					irq_to_board[isi_card[count].irq]=
								&isi_card[count];*/
					done++;
				}
			}
		}	
	}
	return done;
}
#endif

static int register_isr(void)
{
	int count, done = 0;
	unsigned long irqflags;

	for (count=0; count < BOARD_COUNT; count++ ) {
		if (isi_card[count].base) {

			irqflags = (isi_card[count].isa == YES) ? 
					SA_INTERRUPT : 
					(SA_INTERRUPT | SA_SHIRQ);

			if (request_irq(isi_card[count].irq, 
					isicom_interrupt, 
					irqflags, 
					ISICOM_NAME, &isi_card[count])) {

				printk(KERN_WARNING "ISICOM: Could not"
					" install handler at Irq %d."
					" Card%d will be disabled.\n",
					isi_card[count].irq, count+1);

				release_region(isi_card[count].base,16);
				isi_card[count].base=0;
			}
			else
				done++;
		}
	}
	return done;
}

#if 0
static void unregister_isr(void)
{
	int count, card;
	unsigned char freeirq;
	for (count=0; count < BOARD_COUNT; count++ ) {
		if (isi_card[count].base) {
			freeirq = YES;
			for(card = 0; card < count; card++)
				if ((isi_card[card].base) && 
					(isi_card[card].irq == 
						isi_card[count].irq)) {
					freeirq = NO;
					break;
				}
			if (freeirq == YES) {
				free_irq(isi_card[count].irq, NULL);
#ifdef ISICOM_DEBUG			
				printk(KERN_DEBUG "ISICOM: Irq %d released"
					" for Card%d.\n",
					isi_card[count].irq, 
					count+1);
#endif	
			}		
		}
	}
}
#endif

static void unregister_isr(void)
{
	int count;

	for (count=0; count < BOARD_COUNT; count++ ) {
		if (isi_card[count].base) {
			free_irq(isi_card[count].irq, &isi_card[count]);
#ifdef ISICOM_DEBUG			
			printk(KERN_DEBUG "ISICOM: Irq %d released"
				" for Card%d.\n",
				isi_card[count].irq, 
				count+1);
#endif	
		}
	}
}

static int isicom_init(void)
{
	int card, channel, base;
	struct isi_port * port;
	unsigned long page;
	
	if (!tmp_buf) { 
		page = get_free_page(GFP_KERNEL);
	      	if (!page) {
#ifdef ISICOM_DEBUG	      	
	      		printk(KERN_DEBUG "ISICOM: Couldn't allocate page"
				" for tmp_buf.\n");
#else
			printk(KERN_ERR "ISICOM: Not enough memory...\n");
#endif	      
	      		return 0;
	      	}	
	      	tmp_buf = (unsigned char *) page;
	}
	
	if (!register_ioregion()) 
	{
		printk(KERN_ERR "ISICOM: All required I/O space found busy.\n");
		free_page((unsigned long)tmp_buf);
		return 0;
	}
	if (register_drivers()) 
	{
		unregister_ioregion();
		free_page((unsigned long)tmp_buf);
		return 0;
	}

	if (!register_isr()) 
	{
		unregister_drivers();
		/*  ioports already uregistered in register_isr */
		free_page((unsigned long)tmp_buf);
		return 0;		
	}

	/* initialize bottom half  */
	init_bh(ISICOM_BH, do_isicom_bh);


	memset(isi_ports, 0, sizeof(isi_ports));
	for (card = 0; card < BOARD_COUNT; card++) {
		port = &isi_ports[card * 16];
		isi_card[card].ports = port;
		/* 11/5/00 -sameer SMPCHG*/
		isi_card[card].card_lock = SPIN_LOCK_UNLOCKED;

		base = isi_card[card].base;
		for (channel = 0; channel < 16; channel++, port++) {
			port->magic = ISICOM_MAGIC;
			port->card = &isi_card[card];
			port->channel = channel;		
			port->normal_termios = isicom_normal.init_termios;
			port->callout_termios = isicom_callout.init_termios;
		 	port->close_delay = 50 * HZ/100;
		 	port->closing_wait = 3000 * HZ/100;
			port->hangup_tq.routine = do_isicom_hangup;
		 	port->hangup_tq.data = port;
		 	port->bh_tqueue.routine = isicom_bottomhalf;
		 	port->bh_tqueue.data = port;
		 	port->status = 0;
		 				
			/*  . . .  */
 		}
	} 
	
	return 1;	
}

/*
 *	Insmod can set static symbols so keep these static
 */
 
static int io[4];
static int irq[4];

MODULE_AUTHOR("MultiTech");
MODULE_DESCRIPTION("Driver for the ISI series of cards by MultiTech");
MODULE_PARM(io, "1-4i");
MODULE_PARM_DESC(io, "I/O ports for the cards");
MODULE_PARM(irq, "1-4i");
MODULE_PARM_DESC(irq, "Interrupts for the cards");

int init_module(void)
{
	struct pci_dev *dev = NULL;
	int retval, card, idx, count;
	unsigned char pciirq;
	unsigned int ioaddr;

	for (idx = 0; idx < BOARD_COUNT; idx++) {
		isi_card[idx].base = 0;
		isi_card[idx].irq = 0;
	}

	card = 0;
	for(idx=0; idx < BOARD_COUNT; idx++) {	
		if (io[idx] && reset_card(io[idx], 0) && valid_irq(irq[idx])) {
			isi_card[card].base=io[idx];
			isi_card[card].irq=irq[idx];
			isi_card[card].isa=YES;
			card++;
		}
	}
	
	if (pci_present() && (card < BOARD_COUNT)) {
		for (idx=0; idx < DEVID_COUNT; idx++) {
			dev = NULL;
			for (;;){
				if (!(dev = pci_find_device(VENDOR_ID, 
							device_id[idx], dev)))
					break;
				if (card >= BOARD_COUNT)
					break;
					
				/* found a PCI ISI card! */

				/*
				 *	16 bytes address space base at 
				 *	offset 0x1c in the PCI configuration 
				 *	register space
				 */
				ioaddr = dev->base_address[3]; 
				ioaddr &= PCI_BASE_ADDRESS_IO_MASK;
				pciirq = dev->irq;
				printk(KERN_INFO "ISI PCI "
					"Card(Device ID 0x%x)\n", 
					device_id[idx]);
				/*
				 * allot the first empty slot in the array
				 */				
				for (count=0; count < BOARD_COUNT; count++) {				
					if (isi_card[count].base == 0) {
						isi_card[count].base = ioaddr;
						isi_card[count].irq = pciirq;
						isi_card[count].isa = NO;
						card++;
						break;
					}
				}
			}				
			if (card >= BOARD_COUNT) break;
		}
	}
	
	if (!(isi_card[0].base || isi_card[1].base ||
		isi_card[2].base || isi_card[3].base)) {

		printk(KERN_ERR "ISICOM: No valid card configuration."
				" Driver cannot be initialized...\n"); 
		return -EIO;
	}		
	retval=misc_register(&isiloader_device);
	if (retval<0) {
		printk(KERN_ERR "ISICOM: Unable to register"
				" firmware loader driver.\n");
		return -EIO;
	}
	
	if (!isicom_init()) {
		if (misc_deregister(&isiloader_device)) 
			printk(KERN_ERR "ISICOM: Unable to unregister"
					" Firmware Loader driver\n");
		return -EIO;
	}

	init_timer(&tx);
	tx.expires = jiffies + 1;
	tx.data = 0;
	tx.function = isicom_tx;
	re_schedule = 1;
	add_timer(&tx);

	return 0;
}

void cleanup_module(void)
{
	re_schedule = 0;
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ);
	disable_bh(ISICOM_BH);
	
#ifdef ISICOM_DEBUG	
	printk("ISICOM: isicom_tx tx_count = %ld.\n", tx_count);
	printk("ISICOM: isicom_interrupt intr_count = %ld.\n", intr_count);
#endif	

#ifdef ISICOM_DEBUG
	printk("ISICOM: unregistering isr ...\n");
#endif	
	unregister_isr();

#ifdef ISICOM_DEBUG	
	printk("ISICOM: unregistering drivers ...\n");
#endif
	unregister_drivers();
	
#ifdef ISICOM_DEBUG	
	printk("ISICOM: unregistering ioregion ...\n");
#endif	
	unregister_ioregion();	
	
#ifdef ISICOM_DEBUG	
	printk("ISICOM: freeing tmp_buf ...\n");
#endif	
	free_page((unsigned long)tmp_buf);
	
#ifdef ISICOM_DEBUG		
	printk("ISICOM: unregistering firmware loader ...\n");	
#endif
	if (misc_deregister(&isiloader_device))
		printk(KERN_ERR "ISICOM: Unable to unregister"
				" Firmware Loader driver\n");
}
