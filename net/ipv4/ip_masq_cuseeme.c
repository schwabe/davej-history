/*
 *		IP_MASQ_FTP CUSeeMe masquerading module
 *
 *
 * Version:	@(#)ip_masq_cuseeme.c 0.02   07/23/96
 *
 * Author:	Richard Lynch
 *		
 *
 * Fixes:
 *	Richard Lynch     	:	Updated patch to conform to new module
 *					specifications
 *	Nigel Metheringham	:	Multiple port support
 *	
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 * Multiple Port Support
 *	The helper can be made to handle up to MAX_MASQ_APP_PORTS (normally 12)
 *	with the port numbers being defined at module load time.  The module
 *	uses the symbol "ports" to define a list of monitored ports, which can
 *	be specified on the insmod command line as
 *		ports=x1,x2,x3...
 *	where x[n] are integer port numbers.  This option can be put into
 *	/etc/conf.modules (or /etc/modules.conf depending on your config)
 *	where modload will pick it up should you use modload to load your
 *	modules.
 *	
 */

#include <linux/module.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <net/protocol.h>
#include <net/udp.h>
#include <net/ip_masq.h>

#ifndef DEBUG_CONFIG_IP_MASQ_CUSEEME
#define DEBUG_CONFIG_IP_MASQ_CUSEEME 0
#endif

/* 
 * List of ports (up to MAX_MASQ_APP_PORTS) to be handled by helper
 * First port is set to the default port.
 */
int ports[MAX_MASQ_APP_PORTS] = {7648}; /* I rely on the trailing items being set to zero */
struct ip_masq_app *masq_incarnations[MAX_MASQ_APP_PORTS];

static int
masq_cuseeme_init_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_INC_USE_COUNT;
        return 0;
}

static int
masq_cuseeme_done_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_DEC_USE_COUNT;
        return 0;
}

int
masq_cuseeme_out (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, struct device *dev)
{
	struct sk_buff *skb = *skb_p;
	struct iphdr *iph = skb->h.iph;
	struct udphdr *uh = (struct udphdr *)&(((char *)iph)[iph->ihl*4]);
	struct cu_header {
		char dest[8];
		short family;
		u_short port;
		u_long addr;
	} *cu_head;
	char *data=(char *)&uh[1];
	
	if (skb->len - ((unsigned char *) data - skb->h.raw) > 16)
	{
		cu_head         = (struct cu_header *) data;
/*		printk("CUSeeMe orig: %lX:%X\n",ntohl(cu_head->addr),ntohs(cu_head->port));*/
		cu_head->port   = ms->mport;
		cu_head->addr = (u_long) dev->pa_addr;
	}
	return 0;
}

struct ip_masq_app ip_masq_cuseeme = {
        NULL,			/* next */
        "cuseeme",
        0,                      /* type */
        0,                      /* n_attach */
        masq_cuseeme_init_1,	/* ip_masq_init_1 */
        masq_cuseeme_done_1,	/* ip_masq_done_1 */
        masq_cuseeme_out,	/* pkt_out */
        NULL                    /* pkt_in */
};


/*
 * 	ip_masq_cuseeme initialization
 */

int ip_masq_cuseeme_init(void)
{
	int i, j;

	for (i=0; (i<MAX_MASQ_APP_PORTS); i++) {
		if (ports[i]) {
			if ((masq_incarnations[i] = kmalloc(sizeof(struct ip_masq_app),
							    GFP_KERNEL)) == NULL)
				return -ENOMEM;
			memcpy(masq_incarnations[i], &ip_masq_cuseeme, sizeof(struct ip_masq_app));
			if ((j = register_ip_masq_app(masq_incarnations[i], 
						      IPPROTO_TCP, 
						      ports[i]))) {
				return j;
			}
#if DEBUG_CONFIG_IP_MASQ_CUSEEME
			printk("CuSeeMe: loaded support on port[%d] = %d\n",
			       i, ports[i]);
#endif
		} else {
			/* To be safe, force the incarnation table entry to NULL */
			masq_incarnations[i] = NULL;
		}
	}
	return 0;
}

/*
 * 	ip_masq_cuseeme fin.
 */

int ip_masq_cuseeme_done(void)
{
	int i, j, k;

	k=0;
	for (i=0; (i<MAX_MASQ_APP_PORTS); i++) {
		if (masq_incarnations[i]) {
			if ((j = unregister_ip_masq_app(masq_incarnations[i]))) {
				k = j;
			} else {
				kfree(masq_incarnations[i]);
				masq_incarnations[i] = NULL;
#if DEBUG_CONFIG_IP_MASQ_CUSEEME
				printk("CuSeeMe: unloaded support on port[%d] = %d\n",
				       i, ports[i]);
#endif
			}
		}
	}
	return k;
}

#ifdef MODULE

int init_module(void)
{
        if (ip_masq_cuseeme_init() != 0)
                return -EIO;
        register_symtab(0);
        return 0;
}

void cleanup_module(void)
{
        if (ip_masq_cuseeme_done() != 0)
                printk("ip_masq_cuseeme: can't remove module");
}

#endif /* MODULE */
