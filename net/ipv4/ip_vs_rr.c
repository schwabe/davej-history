/*
 * IPVS:        Round-Robin Scheduling module
 *
 * Version:     $Id: ip_vs_rr.c,v 1.2 1999/07/09 12:13:40 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@iinchina.net>
 *              Peter Kese <peter.kese@ijs.si>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Fixes/Changes:
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


static int ip_vs_rr_init_svc(struct ip_vs_service *svc)
{
        MOD_INC_USE_COUNT;
        return 0;
}


static int ip_vs_rr_done_svc(struct ip_vs_service *svc)
{
        MOD_DEC_USE_COUNT;
        return 0;
}


/*
 * Round-Robin Scheduling
 */
static struct ip_masq* ip_vs_rr_schedule(struct ip_vs_service *svc, 
					 struct iphdr *iph)
{
        struct ip_vs_dest *dest;
	struct ip_masq *ms;
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);

	IP_VS_DBG("ip_vs_rr_schedule(): Scheduling...\n");

        if (svc->sched_data != NULL) 
                svc->sched_data = ((struct ip_vs_dest*)svc->sched_data)->next;
        if (svc->sched_data == NULL) 
                svc->sched_data = svc->destinations;
        if (svc->sched_data == NULL)
                return NULL;

        dest = svc->sched_data;

	/*
         *    Create a masquerading entry.
         */
        ms = ip_masq_new_vs(iph->protocol,
                            iph->daddr, portp[1],	
                            dest->addr, dest->port,
                            iph->saddr, portp[0],
                            0);
	if (ms == NULL) {
		IP_VS_ERR("ip_masq_new failed\n");
		return NULL;
	}

        /*
         *    Bind the masq entry with the vs dest.
         */
        ip_vs_bind_masq(ms, dest);
        
        IP_VS_DBG("Masq fwd:%c s:%s c:%lX:%x v:%lX:%x d:%lX:%x flg:%X cnt:%d\n",
                  ip_vs_fwd_tag(ms), ip_masq_state_name(ms->state),
                  ntohl(ms->daddr),ntohs(ms->dport),
                  ntohl(ms->maddr),ntohs(ms->mport),
                  ntohl(ms->saddr),ntohs(ms->sport),
                  ms->flags, atomic_read(&ms->refcnt));

 	return ms;
}


static struct ip_vs_scheduler ip_vs_rr_scheduler = {
	NULL,			/* next */
	"rr",			/* name */
	ATOMIC_INIT(0),		/* refcnt */
	ip_vs_rr_init_svc,      /* service initializer */
	ip_vs_rr_done_svc,      /* service done */
	ip_vs_rr_schedule,      /* select a server and create new masq entry */
};


__initfunc(int ip_vs_rr_init(void))
{
	IP_VS_INFO("Initializing RR scheduling\n");
	return register_ip_vs_scheduler(&ip_vs_rr_scheduler) ;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	/* module initialization by 'request_module' */
	if(register_ip_vs_scheduler(&ip_vs_rr_scheduler) != 0)
	        return -EIO;

	IP_VS_INFO("RR scheduling module loaded.\n");
	
        return 0;
}

void cleanup_module(void)
{
	/* module cleanup by 'release_module' */
	if(unregister_ip_vs_scheduler(&ip_vs_rr_scheduler) != 0)
	        IP_VS_INFO("cannot remove RR scheduling module\n");
	else
	        IP_VS_INFO("RR scheduling module unloaded.\n");
}

#endif /* MODULE */
