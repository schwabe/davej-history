/*
 * IPVS:        Weighted Least-Connection Scheduling module
 *
 * Version:     $Id: ip_vs_wlc.c,v 1.1.2.1 1999/08/13 18:25:44 davem Exp $
 *
 * Authors:     Wensong Zhang <wensong@iinchina.net>
 *              Peter Kese <peter.kese@ijs.si>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <net/ip_masq.h>
#ifdef CONFIG_IP_MASQUERADE_MOD
#include <net/ip_masq_mod.h>
#endif
#include <linux/sysctl.h>
#include <linux/ip_fw.h>
#include <net/ip_vs.h>


static int ip_vs_wlc_init_svc(struct ip_vs_service *svc)
{
        MOD_INC_USE_COUNT;
        return 0;
}


static int ip_vs_wlc_done_svc(struct ip_vs_service *svc)
{
        MOD_DEC_USE_COUNT;
        return 0;
}


/*
 *    Weighted Least Connection scheduling
 */
static struct ip_masq* ip_vs_wlc_schedule(struct ip_vs_service *svc, 
					 struct iphdr *iph)
{
	struct ip_masq *ms;
	struct ip_vs_dest *dest, *least;
	int loh, doh;
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);

	IP_VS_DBG("ip_vs_wlc_schedule(): Scheduling...\n");

	if (svc->destinations == NULL) return NULL;

	/*
         * The number of connections in TCP_FIN state is
         *                 dest->refcnt - dest->connections -1
         * We think the overhead of processing active connections is fifty
         * times than that of conncetions in TCP_FIN in average. (This fifty
         * times might be not accurate, we will change it later.) We use
         * the following formula to estimate the overhead:
         *                dest->connections*49 + dest->refcnt
         * and the load:
         *                (dest overhead) / dest->weight
         *
         * Remember -- no floats in kernel mode!!!
         * The comparison of h1*w2 > h2*w1 is equivalent to that of
         *                h1/w1 > h2/w2
         * if every weight is larger than zero.
         */

	least = svc->destinations;
	loh = atomic_read(&least->connections)*49 + atomic_read(&least->refcnt);
        
        /*
         *    Find the destination with the least load.
         */
	for (dest = least->next; dest; dest = dest->next) {
	        doh = atomic_read(&dest->connections)*49 + atomic_read(&dest->refcnt);
	        if (loh*dest->weight > doh*least->weight) {
		        least = dest;
			loh = doh;
		}
	}

        IP_VS_DBG("The selected server: connections %d refcnt %d weight %d "
                  "overhead %d\n", atomic_read(&least->connections),
                  atomic_read(&least->refcnt), least->weight, loh);

	/*
         *    Create a masquerading entry.
         */
        ms = ip_masq_new_vs(iph->protocol,
                            iph->daddr, portp[1],	
                            least->addr, least->port,
                            iph->saddr, portp[0],
                            0);
	if (ms == NULL) {
		IP_VS_ERR("ip_masq_new failed\n");
		return NULL;
	}

        /*
         *    Bind the masq entry with the vs dest.
         */
        ip_vs_bind_masq(ms, least);
        
        IP_VS_DBG("Masq fwd:%c s:%s c:%lX:%x v:%lX:%x d:%lX:%x flg:%X cnt:%d\n",
                  ip_vs_fwd_tag(ms), ip_masq_state_name(ms->state),
                  ntohl(ms->daddr),ntohs(ms->dport),
                  ntohl(ms->maddr),ntohs(ms->mport),
                  ntohl(ms->saddr),ntohs(ms->sport),
                  ms->flags, atomic_read(&ms->refcnt));

        return ms;
}


static struct ip_vs_scheduler ip_vs_wlc_scheduler = {
	NULL,			/* next */
	"wlc",			/* name */
	ATOMIC_INIT(0),		/* refcnt */
	ip_vs_wlc_init_svc,     /* service initializer */
	ip_vs_wlc_done_svc,     /* service done */
	ip_vs_wlc_schedule,     /* select a server and create new masq entry */
};


__initfunc(int ip_vs_wlc_init(void))
{
	IP_VS_INFO("Initializing WLC scheduling\n");
        return register_ip_vs_scheduler(&ip_vs_wlc_scheduler) ;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	/* module initialization by 'request_module' */
	if(register_ip_vs_scheduler(&ip_vs_wlc_scheduler) != 0)
	        return -EIO;

	IP_VS_INFO("WLC scheduling module loaded.\n");
	
        return 0;
}

void cleanup_module(void)
{
	/* module cleanup by 'release_module' */
	if(unregister_ip_vs_scheduler(&ip_vs_wlc_scheduler) != 0)
	        IP_VS_INFO("cannot remove WLC scheduling module\n");
	else
	        IP_VS_INFO("WLC scheduling module unloaded.\n");
}

#endif /* MODULE */
