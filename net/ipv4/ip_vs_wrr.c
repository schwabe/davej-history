/*
 * IPVS:        Weighted Round-Robin Scheduling module
 *
 * Version:     $Id: ip_vs_wrr.c,v 1.2 1999/07/09 12:13:16 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@iinchina.net>
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

/*
 * current destination pointer for weighted round-robin scheduling
 */
struct ip_vs_wrr_mark {
        struct ip_vs_dest *cdest;    /* current destination pointer */
        int cw;                      /* current weight */
};


static int ip_vs_wrr_init_svc(struct ip_vs_service *svc)
{
	/*
         *    Allocate the mark variable for WRR scheduling
         */
        svc->sched_data = kmalloc(sizeof(struct ip_vs_wrr_mark), GFP_ATOMIC);

        if (svc->sched_data == NULL) {
                IP_VS_ERR("ip_vs_wrr_init_svc(): no memory\n");
		return ENOMEM;
        }
        memset(svc->sched_data, 0, sizeof(struct ip_vs_wrr_mark));

        MOD_INC_USE_COUNT;
        return 0;
}


static int ip_vs_wrr_done_svc(struct ip_vs_service *svc)
{
        /*
         *    Release the mark variable
         */
        kfree_s(svc->sched_data, sizeof(struct ip_vs_wrr_mark));
        
        MOD_DEC_USE_COUNT;
        return 0;
}


int ip_vs_wrr_max_weight(struct ip_vs_dest *destinations)
{
        struct ip_vs_dest *dest;
        int weight = 0;

        for (dest=destinations; dest; dest=dest->next) {
                if (dest->weight > weight)
                        weight = dest->weight;
        }

        return weight;
}

        
/*
 *    Weighted Round-Robin Scheduling
 */
static struct ip_masq* ip_vs_wrr_schedule(struct ip_vs_service *svc, 
					 struct iphdr *iph)
{
	struct ip_masq *ms;
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
        struct ip_vs_wrr_mark *mark = svc->sched_data;
        struct ip_vs_dest *dest;

	IP_VS_DBG("ip_vs_wrr_schedule(): Scheduling...\n");

	if (svc->destinations == NULL) return NULL;

        /*
         * This loop will always terminate, because 0<mark->cw<max_weight,
         * and at least one server has its weight equal to max_weight.
         */
        while (1) {
                if (mark->cdest == NULL) {
                        mark->cdest = svc->destinations;
                        mark->cw--;
                        if (mark->cw <= 0) {
                                mark->cw = ip_vs_wrr_max_weight(svc->destinations);
                                /*
                                 * Still zero, which means no availabe servers.
                                 */
                                if (mark->cw == 0) {
                                        IP_VS_INFO("ip_vs_wrr_schedule(): no available servers\n");
                                        return NULL;
                                }
                        }
                }
                else mark->cdest = mark->cdest->next;

                if(mark->cdest && (mark->cdest->weight >= mark->cw))
                        break;
        }
        
	dest = mark->cdest;
        
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


static struct ip_vs_scheduler ip_vs_wrr_scheduler = {
	NULL,			/* next */
	"wrr",			/* name */
	ATOMIC_INIT(0),		/* refcnt */
	ip_vs_wrr_init_svc,     /* service initializer */
	ip_vs_wrr_done_svc,     /* service done */
	ip_vs_wrr_schedule,     /* select a server and create new masq entry */
};


__initfunc(int ip_vs_wrr_init(void))
{
	IP_VS_INFO("Initializing WRR scheduling\n");
	return register_ip_vs_scheduler(&ip_vs_wrr_scheduler) ;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	/* module initialization by 'request_module' */
	if(register_ip_vs_scheduler(&ip_vs_wrr_scheduler) != 0)
	        return -EIO;

	IP_VS_INFO("WRR scheduling module loaded.\n");
	
        return 0;
}

void cleanup_module(void)
{
	/* module cleanup by 'release_module' */
	if(unregister_ip_vs_scheduler(&ip_vs_wrr_scheduler) != 0)
	        IP_VS_INFO("cannot remove WRR scheduling module\n");
	else
	        IP_VS_INFO("WRR scheduling module unloaded.\n");
}

#endif /* MODULE */
